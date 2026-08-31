#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/ScopedValue.h>
#include <Caracal/Semantic/ArgumentBinder.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Semantic/OperatorRegistration.h>
#include <Caracal/Semantic/SliceParameterPromotion.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Syntax/UnaryExpression.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Caracal
{
    static bool ShouldIgnoreUnusedVariableWarning(std::string_view name)
    {
        return name == "_" || name == ImplicitThisName;
    }

    [[nodiscard]] static SourceLocation TypeNameSpan(const CompilationContext& context, const CompilationUnit& unit, u32 index)
    {
        return NodeSpans::typeNameSpan(unit.nodeBuffer, context.tokenBuffer(unit.fileId), index);
    }

    [[nodiscard]] static SourceLocation ParameterSpan(const CompilationContext& context, const CompilationUnit& unit, u32 parameterIndex)
    {
        return NodeSpans::parameterSpan(unit.nodeBuffer, context.tokenBuffer(unit.fileId), parameterIndex);
    }

    [[nodiscard]] static SourceLocation ParametersNodeSpan(const CompilationContext& context, const CompilationUnit& unit, u32 parametersIndex)
    {
        return NodeSpans::parametersNodeSpan(unit.nodeBuffer, context.tokenBuffer(unit.fileId), parametersIndex);
    }

    [[nodiscard]] static SourceLocation ExpressionSourceLocation(const CompilationContext& context, const CompilationUnit& unit, u32 index)
    {
        return NodeSpans::expressionSourceLocation(unit.nodeBuffer, context.tokenBuffer(unit.fileId), index);
    }

    static bool IsInitConstantAssignmentSite(std::string_view functionName)
    {
        return functionName == EntryPointFunctionName || functionName == UserMainFunctionName;
    }

    [[nodiscard]] static TokenKind TokenKindAt(const TokenBuffer& tokens, u32 tokenIndex) noexcept
    {
        return tokens.getToken(static_cast<i32>(tokenIndex)).kind;
    }

    [[nodiscard]] static const SourceLocation& TokenLocationAt(const TokenBuffer& tokens, u32 tokenIndex) noexcept
    {
        return tokens.getSourceLocation(tokens.getToken(static_cast<i32>(tokenIndex)));
    }

    [[nodiscard]] static bool ContainsRuntimeCall(
        SemanticContext& module,
        const CompilationUnit& unit,
        const TokenBuffer& tokens,
        u32 index)
    {
        switch (unit.nodeBuffer.kind(index))
        {
            case NodeKind::FunctionCallExpression:
            {
                return true;
            }
            case NodeKind::GroupingExpression:
            case NodeKind::UnaryExpression:
            case NodeKind::MemberAccessExpression:
            {
                return ContainsRuntimeCall(module, unit, tokens, NodeLayout::onlyChild(index));
            }
            case NodeKind::BinaryExpression:
            {
                const auto binaryLayout = NodeLayout::binary(unit.nodeBuffer, index);
                const auto isMemberCall = unit.nodeBuffer.isMemberAccessBinary(tokens, index)
                    && unit.nodeBuffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression;
                if (!isMemberCall)
                {
                    return ContainsRuntimeCall(module, unit, tokens, binaryLayout.left)
                        || ContainsRuntimeCall(module, unit, tokens, binaryLayout.right);
                }

                if (ContainsRuntimeCall(module, unit, tokens, binaryLayout.left))
                {
                    return true;
                }

                const auto methodType = module.typeOfNode(NodeIndex{ unit.fileId, NodeLayout::subtreeBegin(unit.nodeBuffer, binaryLayout.right) });
                if (methodType != Type::Undefined())
                {
                    const auto* definition = module.tryGetFunctionDefinition(methodType);
                    if (definition != nullptr && isBitwiseIntrinsicKind(definition->intrinsicKind()))
                    {
                        // foldable builtins are fine but arguments can force this into a runtime call
                        std::vector<u32> arguments;
                        NodeLayout::collectChildren(unit.nodeBuffer, binaryLayout.right, arguments);
                        for (size_t argumentIndex = 1; argumentIndex < arguments.size(); ++argumentIndex)
                        {
                            auto valueIndex = arguments[argumentIndex];
                            if (unit.nodeBuffer.kind(valueIndex) == NodeKind::NamedArgument)
                            {
                                valueIndex = NodeLayout::namedArgumentValue(valueIndex);
                            }

                            if (ContainsRuntimeCall(module, unit, tokens, valueIndex))
                            {
                                return true;
                            }
                        }

                        return false;
                    }
                }

                return true;
            }
            case NodeKind::ArrayLiteral:
            {
                std::vector<u32> elements;
                NodeLayout::collectChildren(unit.nodeBuffer, index, elements);
                for (const auto element : elements)
                {
                    if (ContainsRuntimeCall(module, unit, tokens, element))
                    {
                        return true;
                    }
                }

                return false;
            }
            default:
            {
                return false;
            }
        }
    }

    [[nodiscard]] static bool IsConstructorCallExpression(
        SemanticContext& module,
        const CompilationUnit& unit,
        const TokenBuffer& tokens,
        u32 index)
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, index);
        if (unit.nodeBuffer.kind(stripped) != NodeKind::BinaryExpression)
        {
            return false;
        }

        const auto binaryLayout = NodeLayout::binary(unit.nodeBuffer, stripped);
        if (!unit.nodeBuffer.isMemberAccessBinary(tokens, stripped)
            || unit.nodeBuffer.kind(binaryLayout.right) != NodeKind::FunctionCallExpression)
        {
            return false;
        }

        const auto receiverType = module.typeOfNode(NodeIndex{ unit.fileId, binaryLayout.left });
        if (receiverType.kind() != TypeKind::Type)
        {
            return false;
        }

        if (module.typeOfNode(NodeIndex{ unit.fileId, binaryLayout.right }) == Type::Undefined())
        {
            return false;
        }

        const auto methodType = module.typeOfNode(NodeIndex{ unit.fileId, NodeLayout::subtreeBegin(unit.nodeBuffer, binaryLayout.right) });
        const auto* definition = module.tryGetFunctionDefinition(methodType);
        return definition != nullptr && definition->functionType() == FunctionType::SynthesizedConstructor;
    }

    static bool ConditionIsLiteralTrue(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 conditionIndex)
    {
        const auto condition = NodeLayout::stripGroupings(buffer, conditionIndex);
        return buffer.kind(condition) == NodeKind::BoolLiteral
            && TokenKindAt(tokens, buffer.mainToken(condition)) == TokenKind::TrueKeyword;
    }

    static bool ContainsLoopBreak(const NodeBuffer& buffer, u32 index)
    {
        switch (buffer.kind(index))
        {
            case NodeKind::BreakStatement:
            {
                return true;
            }
            case NodeKind::BlockNode:
            {
                std::vector<u32> children;
                NodeLayout::collectChildren(buffer, index, children);
                for (const auto inner : children)
                {
                    if (ContainsLoopBreak(buffer, inner))
                    {
                        return true;
                    }
                }

                return false;
            }
            case NodeKind::IfStatement:
            {
                const auto ifLayout = NodeLayout::ifStatement(buffer, index);
                if (ContainsLoopBreak(buffer, ifLayout.trueStatement))
                {
                    return true;
                }

                return ifLayout.hasFalse && ContainsLoopBreak(buffer, ifLayout.falseStatement);
            }
            case NodeKind::TrailingIfStatement:
            {
                return ContainsLoopBreak(buffer, NodeLayout::trailingIf(buffer, index).statement);
            }
            default:
            {
                return false;
            }
        }
    }

    static bool StatementGuaranteesReturn(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index)
    {
        switch (buffer.kind(index))
        {
            case NodeKind::ReturnStatement:
            {
                return true;
            }
            case NodeKind::BlockNode:
            {
                std::vector<u32> children;
                NodeLayout::collectChildren(buffer, index, children);
                for (const auto inner : children)
                {
                    if (StatementGuaranteesReturn(buffer, tokens, inner))
                    {
                        return true;
                    }
                }

                return false;
            }
            case NodeKind::IfStatement:
            {
                const auto ifLayout = NodeLayout::ifStatement(buffer, index);
                return ifLayout.hasFalse
                    && StatementGuaranteesReturn(buffer, tokens, ifLayout.trueStatement)
                    && StatementGuaranteesReturn(buffer, tokens, ifLayout.falseStatement);
            }
            case NodeKind::WhileStatement:
            {
                const auto whileLayout = NodeLayout::whileStatement(buffer, index);
                return ConditionIsLiteralTrue(buffer, tokens, whileLayout.condition)
                    && !ContainsLoopBreak(buffer, whileLayout.body);
            }
            default:
            {
                return false;
            }
        }
    }

    static bool IsTerminatingStatement(const NodeBuffer& buffer, u32 index)
    {
        switch (buffer.kind(index))
        {
            case NodeKind::ReturnStatement:
            case NodeKind::BreakStatement:
            case NodeKind::SkipStatement:
            {
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    [[nodiscard]] static std::optional<SourceLocation> TypeFieldLocation(
        const CompilationContext& compilationContext,
        const TypeDefinition& typeDefinition,
        const FieldDefinition& fieldDefinition)
    {
        const auto definitionNode = typeDefinition.definitionNode();
        if (definitionNode.index == InvalidNodeIndex)
        {
            return std::nullopt;
        }

        const auto& buffer = compilationContext.nodeBuffer(definitionNode.fileId);
        const auto typeLayout = NodeLayout::typeDefinition(buffer, definitionNode.index);
        std::vector<u32> bodyStatements;
        NodeLayout::collectChildren(buffer, typeLayout.body, bodyStatements);

        auto remaining = fieldDefinition.index();
        for (const auto statement : bodyStatements)
        {
            if (buffer.kind(statement) == NodeKind::AnnotationNode)
            {
                continue;
            }

            if (remaining > 0)
            {
                --remaining;
                continue;
            }

            if (buffer.kind(statement) != NodeKind::TypeFieldDeclaration)
            {
                return std::nullopt;
            }

            const auto declarationLayout = NodeLayout::declaration(buffer, statement);
            return compilationContext.sourceLocation(definitionNode.fileId, buffer.mainToken(declarationLayout.left));
        }

        return std::nullopt;
    }

    [[nodiscard]] static std::optional<i32> TryConvertEnumFieldLiteralValue(
        const SemanticContext& module,
        std::string_view lexeme,
        Type literalType)
    {
        const auto* description = module.tryGetBuiltinTypeDescription(literalType);
        if (description == nullptr
            || (description->kind != BuiltinTypeKind::Int && description->kind != BuiltinTypeKind::Float))
        {
            return std::nullopt;
        }

        const auto parsedValue = tryParseNumberLiteralValue(lexeme, literalType, module);
        if (!parsedValue.has_value())
        {
            return std::nullopt;
        }

        return std::visit([](const auto value) -> std::optional<i32>
            {
                using Payload = std::decay_t<decltype(value)>;
                if constexpr (std::is_integral_v<Payload> && !std::is_same_v<Payload, bool>)
                {
                    // the EnumDefinition stores field values as i32
                    if (static_cast<i64>(value) < std::numeric_limits<i32>::min()
                        || static_cast<i64>(value) > std::numeric_limits<i32>::max())
                    {
                        return std::nullopt;
                    }

                    return static_cast<i32>(value);
                }
                else
                {
                    return std::nullopt;
                }
            }, parsedValue.value());
    }

    [[nodiscard]] static UnaryOperatorKind ConvertToUnaryOperator(TokenKind kind) noexcept
    {
        switch (kind)
        {
            case TokenKind::Bang:
            {
                return UnaryOperatorKind::LogicalNegation;
            }
            case TokenKind::Minus:
            {
                return UnaryOperatorKind::ValueNegation;
            }
            case TokenKind::RefKeyword:
            {
                return UnaryOperatorKind::ReferenceOf;
            }
            default:
            {
                return UnaryOperatorKind::Invalid;
            }
        }
    }

    [[nodiscard]] static BinaryOperatorKind ConvertToBinaryOperator(TokenKind kind) noexcept
    {
        switch (kind)
        {
            case TokenKind::Dot:
            {
                return BinaryOperatorKind::MemberAccess;
            }
            case TokenKind::PercentPlus:
            {
                return BinaryOperatorKind::AdditionWrapping;
            }
            case TokenKind::PercentMinus:
            {
                return BinaryOperatorKind::SubtractionWrapping;
            }
            case TokenKind::PercentStar:
            {
                return BinaryOperatorKind::MultiplicationWrapping;
            }
            case TokenKind::Slash:
            {
                return BinaryOperatorKind::Division;
            }
            case TokenKind::EqualEqual:
            {
                return BinaryOperatorKind::Equal;
            }
            case TokenKind::BangEqual:
            {
                return BinaryOperatorKind::NotEqual;
            }
            case TokenKind::LessThan:
            {
                return BinaryOperatorKind::LessThan;
            }
            case TokenKind::LessThanEqual:
            {
                return BinaryOperatorKind::LessOrEqual;
            }
            case TokenKind::GreaterThan:
            {
                return BinaryOperatorKind::GreaterThan;
            }
            case TokenKind::GreaterThanEqual:
            {
                return BinaryOperatorKind::GreaterOrEqual;
            }
            case TokenKind::AndKeyword:
            {
                return BinaryOperatorKind::LogicalAnd;
            }
            case TokenKind::OrKeyword:
            {
                return BinaryOperatorKind::LogicalOr;
            }
            default:
            {
                return BinaryOperatorKind::Invalid;
            }
        }
    }

    [[nodiscard]] static SourceLocation CallArgumentsLocation(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 callIndex,
        std::span<const u32> argumentNodes)
    {
        return NodeSpans::callArgumentsLocation(unit.nodeBuffer, context.tokenBuffer(unit.fileId), callIndex, argumentNodes);
    }

    TypeChecker::TypeChecker(
        CompilationContext& compilationContext,
        bool isPreludePass,
        DiagnosticsBag& diagnostics)
        : m_compilationContext{ compilationContext }
        , m_isPreludePass{ isPreludePass }
        , m_module{ compilationContext.semanticContext() }
        , m_options{ compilationContext.options() }
        , m_diagnostics{ diagnostics }
    {
        for (auto& unit : compilationContext.units())
        {
            const auto isPreludeUnit = unit.origin == UnitOrigin::Prelude;
            if (isPreludeUnit == m_isPreludePass)
            {
                m_units.push_back(&unit);
            }
        }

        m_scopeFrames.push_back(ScopeFrame{ 0, ScopeKind::Global });
    }

    bool TypeChecker::typeCheck()
    {
        if (m_diagnostics.hasErrors())
        {
            return false;
        }

        for (const auto* unit : m_units)
        {
            m_module.createUnitSemantics(unit->fileId, unit->nodeBuffer.nodeCount());
            internUnitIdentifiers(*unit);
        }

        collectDeclarations();
        resolveWellKnownTypes();
        collectMethodDeclarations();

        typeCheckFunctionSignatures();
        typeCheckTypeSignatures();
        typeCheckEnumDefinitions();
        typeCheckGlobalConstants();
        typeCheckTypeFieldDefinitions();
        typeCheckFunctionDefinitions();
        typeCheckTypeMethodDefinitions();

        checkUninitializedInitConstants();

        return !m_diagnostics.hasErrors();
    }

    void TypeChecker::collectDeclarations()
    {
        for (auto* unit : m_units)
        {
            collectUnitDeclarations(*unit);
        }
    }

    void TypeChecker::collectUnitDeclarations(CompilationUnit& unit)
    {
        m_pendingAnnotations.clear();
        for (const auto index : collectRootStatements(unit.nodeBuffer))
        {
            const auto kind = unit.nodeBuffer.kind(index);
            if (kind == NodeKind::AnnotationNode)
            {
                m_pendingAnnotations.push_back(index);
                continue;
            }

            switch (kind)
            {
                case NodeKind::ConstantDeclaration:
                case NodeKind::InitConstantDeclaration:
                {
                    m_globalConstantDeclarations.push_back(NodeIndex{ unit.fileId, index });
                    break;
                }
                case NodeKind::VariableDeclaration:
                {
                    for (const auto annotation : m_pendingAnnotations)
                    {
                        m_diagnostics.addUnexpectedAnnotationTargetError(
                            unit.source,
                            annotationLocation(unit, annotation));
                    }
                    break;
                }
                case NodeKind::EnumDefinitionStatement:
                {
                    collectEnumDeclaration(unit, index);
                    break;
                }
                case NodeKind::TypeDefinitionStatement:
                {
                    collectTypeDeclaration(unit, index);
                    break;
                }
                case NodeKind::FunctionDefinitionStatement:
                {
                    collectFunctionDeclaration(unit, index);
                    break;
                }
                default:
                {
                    break;
                }
            }

            m_pendingAnnotations.clear();
        }
    }

    void TypeChecker::collectFunctionDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto functionName = nameOf(unit, index);
        const auto existingFunctionType = m_module.tryGetFunctionTypeByName(functionName);
        if (existingFunctionType != Type::Undefined())
        {
            const auto& existingDefinition = m_module.getFunctionDefinition(existingFunctionType);
            auto otherSource = SourceTextSharedPtr{};
            auto otherLocation = std::optional<SourceLocation>{};
            const auto* otherStatement = static_cast<const FunctionDefinitionStatement*>(existingDefinition.statement());
            auto otherNameToken = std::optional<Token>{};
            if (otherStatement != nullptr)
            {
                otherNameToken = otherStatement->nameToken();
            }
            locateDefinitionName(
                existingDefinition.definitionNode(),
                otherNameToken,
                otherSource,
                otherLocation);

            m_diagnostics.addDuplicateFunctionDeclarationError(
                unit.source,
                nameLocation(unit, index),
                std::string(functionName),
                otherSource,
                otherLocation);
            return;
        }

        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);
        auto parameters = collectParameterNames(unit, functionLayout.parameters);
        auto& functionDefinition = m_module.createFunction(functionName, std::move(parameters), std::vector<Type>(), nullptr);
        functionDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, functionDefinition.type());
        m_functionDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    void TypeChecker::reportDuplicateTypeDeclaration(const CompilationUnit& unit, u32 index, std::string_view name, Type existingType)
    {
        auto otherSource = SourceTextSharedPtr{};
        auto otherLocation = std::optional<SourceLocation>{};
        auto otherNameToken = std::optional<Token>{};
        auto otherDefinitionNode = NodeIndex{};
        if (existingType.kind() == TypeKind::Enum)
        {
            const auto& other = m_module.getEnumDefinition(existingType);
            otherDefinitionNode = other.definitionNode();
            if (other.statement() != nullptr)
            {
                otherNameToken = other.statement()->nameToken();
            }
        }
        else if (existingType.kind() == TypeKind::Type)
        {
            const auto& other = m_module.getTypeDefinition(existingType);
            otherDefinitionNode = other.definitionNode();
            if (other.statement() != nullptr)
            {
                otherNameToken = other.statement()->nameToken();
            }
        }
        locateDefinitionName(
            otherDefinitionNode,
            otherNameToken,
            otherSource,
            otherLocation);

        m_diagnostics.addDuplicateTypeDeclarationError(
            unit.source,
            nameLocation(unit, index),
            std::string(name),
            otherSource,
            otherLocation);
    }

    void TypeChecker::collectEnumDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto enumName = nameOf(unit, index);
        const auto existingType = m_module.tryGetTypeByName(enumName);
        if (existingType != Type::Undefined())
        {
            reportDuplicateTypeDeclaration(unit, index, enumName, existingType);
            return;
        }

        auto& enumDefinition = m_module.createEnum(enumName, nullptr);
        enumDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, enumDefinition.type());
        m_enumDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    BuiltinTypeDescription TypeChecker::extractPendingBuiltinTypeDescription(const CompilationUnit& unit) const
    {
        BuiltinTypeDescription description{};
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        std::vector<u32> arguments;
        for (const auto annotationIndex : m_pendingAnnotations)
        {
            if (nameOf(unit, annotationIndex) != BuiltinAnnotationName)
            {
                continue;
            }

            NodeLayout::collectChildren(buffer, annotationIndex, arguments);
            for (const auto argumentIndex : arguments)
            {
                if (buffer.kind(argumentIndex) != NodeKind::NamedArgument)
                {
                    continue;
                }

                const auto argumentName = nameOf(unit, argumentIndex);
                const auto valueIndex = NodeLayout::namedArgumentValue(argumentIndex);
                if (argumentName == KindAnnotationArgumentName && buffer.kind(valueIndex) == NodeKind::NameExpression)
                {
                    const auto kindName = nameOf(unit, valueIndex);
                    if (kindName == BuiltinKindFloatName)
                    {
                        description.kind = BuiltinTypeKind::Float;
                    }
                    else if (kindName == BuiltinKindBoolName)
                    {
                        description.kind = BuiltinTypeKind::Bool;
                    }
                    else if (kindName == BuiltinKindPointerName)
                    {
                        description.kind = BuiltinTypeKind::Pointer;
                    }
                }
                else if (argumentName == BitsAnnotationArgumentName && buffer.kind(valueIndex) == NodeKind::NumberLiteral)
                {
                    const auto parsed = tryParseI32Literal(nameOf(unit, valueIndex));
                    if (parsed.has_value())
                    {
                        description.bits = parsed.value();
                    }
                }
                else if (argumentName == SignedAnnotationArgumentName && buffer.kind(valueIndex) == NodeKind::BoolLiteral)
                {
                    description.isSigned = tokens.getToken(static_cast<i32>(buffer.mainToken(valueIndex))).kind == TokenKind::TrueKeyword;
                }
            }
        }

        return description;
    }

    void TypeChecker::collectBuiltinTypeDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto typeName = nameOf(unit, index);
        const auto builtinType = m_module.tryGetTypeByName(typeName);
        if (builtinType == Type::Undefined())
        {
            const auto description = extractPendingBuiltinTypeDescription(unit);
            auto& typeDefinition = m_module.createBuiltinTypeFromDescription(typeName, description, nullptr);
            typeDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
            m_module.setNodeType(NodeIndex{ unit.fileId, index }, typeDefinition.type());
            m_typeDeclarations.push_back(NodeIndex{ unit.fileId, index });
            return;
        }

        if (builtinType.kind() != TypeKind::Builtin || builtinType.id() < 0)
        {
            m_diagnostics.addNotABuiltinTypeError(
                unit.source,
                nameLocation(unit, index),
                std::string(typeName));
            return;
        }

        const auto& existingDefinition = m_module.getTypeDefinition(builtinType);
        if (existingDefinition.type() != Type::Undefined())
        {
            auto otherSource = SourceTextSharedPtr{};
            auto otherLocation = std::optional<SourceLocation>{};
            auto otherNameToken = std::optional<Token>{};
            if (existingDefinition.statement() != nullptr)
            {
                otherNameToken = existingDefinition.statement()->nameToken();
            }
            locateDefinitionName(
                existingDefinition.definitionNode(),
                otherNameToken,
                otherSource,
                otherLocation);

            m_diagnostics.addDuplicateBuiltinTypeBindingError(
                unit.source,
                nameLocation(unit, index),
                std::string(typeName),
                otherLocation);
            return;
        }

        auto& typeDefinition = m_module.bindBuiltinTypeDefinition(builtinType, nullptr);
        typeDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, builtinType);
        m_typeDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    void TypeChecker::collectTypeDeclaration(const CompilationUnit& unit, u32 index)
    {
        if (pendingAnnotationsContain(unit, BuiltinAnnotationName))
        {
            collectBuiltinTypeDeclaration(unit, index);
            return;
        }

        const auto typeName = nameOf(unit, index);
        const auto existingType = m_module.tryGetTypeByName(typeName);
        if (existingType != Type::Undefined())
        {
            reportDuplicateTypeDeclaration(unit, index, typeName, existingType);
            return;
        }

        auto& typeDefinition = m_module.createType(typeName, nullptr);
        typeDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, typeDefinition.type());
        m_typeDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    void TypeChecker::resolveWellKnownTypes()
    {
        m_module.refreshWellKnownTypes();
        m_defaultIntegerType = m_module.tryGetTypeByName(m_options.defaultIntegerType);
        m_defaultFloatingType = m_module.tryGetTypeByName(m_options.defaultFloatingType);
        m_defaultEnumBaseType = m_module.tryGetTypeByName(m_options.defaultEnumBaseType);
    }

    std::vector<u32> TypeChecker::collectRootStatements(const NodeBuffer& buffer) const
    {
        std::vector<u32> roots;
        auto index = buffer.nodeCount();
        while (index > 0)
        {
            const auto root = index - 1;
            roots.push_back(root);
            index -= buffer.subtreeSize(root);
        }

        std::reverse(roots.begin(), roots.end());
        return roots;
    }

    std::vector<Parameter> TypeChecker::collectParameterNames(const CompilationUnit& unit, u32 parametersNode)
    {
        std::vector<Parameter> parameters;
        NodeLayout::collectChildren(unit.nodeBuffer, parametersNode, m_childrenScratch);
        for (const auto parameterIndex : m_childrenScratch)
        {
            if (NodeLayout::isVariadicParameter(unit.nodeBuffer, parameterIndex))
            {
                parameters.emplace_back("varargs", Type::Undefined());
            }
            else
            {
                parameters.emplace_back(nameOf(unit, parameterIndex), Type::Undefined());
            }
        }

        return parameters;
    }

    Token TypeChecker::tokenAt(u16 fileId, u32 tokenIndex) const noexcept
    {
        return Token{ .kind = TokenKind::Identifier, .fileId = fileId, .index = static_cast<i32>(tokenIndex) };
    }

    std::string_view TypeChecker::nameOf(const CompilationUnit& unit, u32 index) const noexcept
    {
        return m_compilationContext.getLexeme(unit.fileId, unit.nodeBuffer.mainToken(index));
    }

    const SourceLocation& TypeChecker::nameLocation(const CompilationUnit& unit, u32 index) const noexcept
    {
        return m_compilationContext.sourceLocation(unit.fileId, unit.nodeBuffer.mainToken(index));
    }

    SourceLocation TypeChecker::annotationLocation(const CompilationUnit& unit, u32 index) const noexcept
    {
        const auto nameToken = unit.nodeBuffer.mainToken(index);
        const auto& hashLocation = m_compilationContext.sourceLocation(unit.fileId, nameToken - 1);
        const auto& nameLocation = m_compilationContext.sourceLocation(unit.fileId, nameToken);
        return SourceLocation{ hashLocation.startIndex, nameLocation.endIndex };
    }

    void TypeChecker::locateDefinitionName(
        NodeIndex definitionNode,
        const std::optional<Token>& statementNameToken,
        SourceTextSharedPtr& source,
        std::optional<SourceLocation>& location) const noexcept
    {
        if (definitionNode.index != InvalidNodeIndex)
        {
            const auto& buffer = m_compilationContext.nodeBuffer(definitionNode.fileId);
            const auto nameToken = tokenAt(definitionNode.fileId, buffer.mainToken(definitionNode.index));
            source = m_compilationContext.source(nameToken);
            location = m_compilationContext.sourceLocation(nameToken);
            return;
        }

        if (statementNameToken.has_value())
        {
            source = m_compilationContext.source(statementNameToken.value());
            location = m_compilationContext.sourceLocation(statementNameToken.value());
        }
    }

    void TypeChecker::collectMethodAnnotationsBefore(const CompilationUnit& unit, u32 methodIndex, const TypeDefinition& typeDefinition, std::vector<u32>& annotations) const
    {
        NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, methodIndex, annotations);
        if (typeDefinition.definitionNode().index == InvalidNodeIndex)
        {
            return;
        }

        const auto typeLayout = NodeLayout::typeDefinition(unit.nodeBuffer, typeDefinition.definitionNode().index);
        const auto bodyBegin = NodeLayout::subtreeBegin(unit.nodeBuffer, typeLayout.body);
        std::erase_if(annotations, [bodyBegin](u32 annotation) { return annotation < bodyBegin; });
    }

    CompilationUnit& TypeChecker::unitFor(NodeIndex index) const noexcept
    {
        return m_compilationContext.units()[index.fileId];
    }

    void TypeChecker::checkUninitializedInitConstants()
    {
        std::vector<std::pair<std::string_view, const InitConstantRecord*>> uninitialized;
        for (const auto& [name, declaration] : m_initConstants)
        {
            if (m_initConstantAssignments.contains(name))
                continue;

            uninitialized.emplace_back(name, &declaration);
        }

        // sort by source position so the errors emit in declaration order
        std::sort(uninitialized.begin(), uninitialized.end(), [](const auto& left, const auto& right)
        {
            return left.second->location.startIndex < right.second->location.startIndex;
        });

        for (const auto& [name, declaration] : uninitialized)
        {
            m_diagnostics.addUninitializedInitConstantError(declaration->source, declaration->location, std::string(name));
        }
    }

    bool TypeChecker::pendingAnnotationsContain(const CompilationUnit& unit, std::string_view name) const noexcept
    {
        for (const auto annotation : m_pendingAnnotations)
        {
            if (nameOf(unit, annotation) == name)
            {
                return true;
            }
        }

        return false;
    }

    void TypeChecker::collectMethodDeclarations()
    {
        for (const auto typeDeclaration : m_typeDeclarations)
        {
            const auto& unit = unitFor(typeDeclaration);
            const auto typeIndex = typeDeclaration.index;
            const auto typeType = m_module.tryGetTypeByName(nameOf(unit, typeIndex));
            if (typeType == Type::Undefined())
            {
                TODO("This shouldn't happen");
            }

            auto& typeDefinition = m_module.getTypeDefinition(typeType);
            const auto typeLayout = NodeLayout::typeDefinition(unit.nodeBuffer, typeIndex);
            const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
            auto bodyStatements = std::vector<u32>{};
            NodeLayout::collectChildren(unit.nodeBuffer, typeLayout.body, bodyStatements);
            for (const auto bodyStatement : bodyStatements)
            {
                if (unit.nodeBuffer.kind(bodyStatement) != NodeKind::MethodDefinitionStatement)
                {
                    continue;
                }

                const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, bodyStatement);
                const auto modifier = unit.nodeBuffer.methodModifier(tokens, methodLayout.methodName);
                const auto methodName = nameOf(unit, methodLayout.methodName);
                if (unit.nodeBuffer.methodNameIsNew(tokens, methodLayout.methodName))
                {
                    auto constructorLocation = nameLocation(unit, typeIndex);
                    if (typeLayout.hasConstructorParameters)
                    {
                        constructorLocation = ParametersNodeSpan(m_compilationContext, unit, typeLayout.constructorParameters);
                    }

                    m_diagnostics.addTypeDotNewDeclarationError(
                        unit.source,
                        nameLocation(unit, methodLayout.methodName),
                        constructorLocation,
                        std::string(nameOf(unit, typeIndex)));
                    continue;
                }

                std::vector<Parameter> declarationParameters{};
                if (modifier != MethodModifier::Static)
                {
                    declarationParameters.emplace_back(ImplicitThisName, typeDefinition.type().toReference());
                }

                const auto parameterNames = collectParameterNames(unit, methodLayout.parameters);
                declarationParameters.insert(declarationParameters.end(), parameterNames.begin(), parameterNames.end());

                auto methodType = typeDefinition.tryGetMethodTypeByName(methodName);
                if (methodType != Type::Undefined())
                {
                    auto otherLocation = std::optional<SourceLocation>{};
                    const auto& existingDefinition = m_module.getFunctionDefinition(methodType);
                    if (existingDefinition.definitionNode().index != InvalidNodeIndex)
                    {
                        const auto otherNode = existingDefinition.definitionNode();
                        const auto& otherBuffer = m_compilationContext.nodeBuffer(otherNode.fileId);
                        const auto otherLayout = NodeLayout::methodDefinition(otherBuffer, otherNode.index);
                        otherLocation = m_compilationContext.sourceLocation(otherNode.fileId, otherBuffer.mainToken(otherLayout.methodName));
                    }

                    m_diagnostics.addDuplicateMethodDeclarationError(
                        unit.source,
                        nameLocation(unit, methodLayout.methodName),
                        std::string(methodName),
                        typeDefinition.name(),
                        otherLocation);
                    continue;
                }

                auto& methodDefinition = m_module.createMethod(typeDefinition, modifier, std::string(methodName), std::move(declarationParameters), std::vector<Type>(), nullptr);
                methodDefinition.setDefinitionNode(NodeIndex{ unit.fileId, bodyStatement });
                m_module.setNodeType(NodeIndex{ unit.fileId, bodyStatement }, methodDefinition.type());
            }

            auto annotations = std::vector<u32>{};
            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, typeIndex, annotations);
            if (NodeLayout::annotationsContain(unit.nodeBuffer, tokens, annotations, BuiltinAnnotationName))
            {
                // builtin types are not constructible, they get no synthesized constructor
                continue;
            }

            std::vector<Parameter> constructorParameters{};
            constructorParameters.emplace_back(ImplicitThisName, typeType.toReference());

            if (typeLayout.hasConstructorParameters)
            {
                const auto parameterNames = collectParameterNames(unit, typeLayout.constructorParameters);
                constructorParameters.insert(constructorParameters.end(), parameterNames.begin(), parameterNames.end());
            }

            static_cast<void>(m_module.createConstructor(typeDefinition, std::move(constructorParameters)));
        }
    }

    void TypeChecker::typeCheckFunctionSignatures()
    {
        for (const auto functionDeclaration : m_functionDeclarations)
        {
            const auto& unit = unitFor(functionDeclaration);
            typeCheckFunctionSignature(unit, functionDeclaration.index);
        }
    }

    void TypeChecker::typeCheckTypeSignatures()
    {
        for (const auto typeDeclaration : m_typeDeclarations)
        {
            const auto& unit = unitFor(typeDeclaration);
            typeCheckTypeSignature(unit, typeDeclaration.index);
        }

        promoteReadOnlySliceParameters(m_compilationContext, m_module, m_functionDeclarations, m_typeDeclarations);
    }

    void TypeChecker::typeCheckFunctionSignature(const CompilationUnit& unit, u32 index)
    {
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);

        pushScope(ScopeKind::Function);
        auto parameters = typeCheckParametersNode(unit, functionLayout.parameters);
        auto returns = typeCheckReturnTypesNode(unit, functionLayout.returnTypes);
        popScope(false);

        const auto functionName = nameOf(unit, index);
        auto functionType = m_module.typeOfNode(NodeIndex{ unit.fileId, index });
        if (functionType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& functionDefinition = m_module.getFunctionDefinition(functionType);
        auto annotations = std::vector<u32>{};
        NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
        std::optional<std::string> symbolName;
        auto isRequiredExtern = false;
        const auto isExtern = validateCallableAnnotations(unit, annotations, symbolName, isRequiredExtern);
        if (isExtern && isRequiredExtern)
        {
            m_module.markExternRequired(functionType);
        }

        auto parameterNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, functionLayout.parameters, parameterNodes);
        const auto isVariadic = !parameterNodes.empty() && NodeLayout::isVariadicParameter(unit.nodeBuffer, parameterNodes.back());
        if (isVariadic && !isExtern)
        {
            m_diagnostics.addNonExternVariadicFunctionError(
                unit.source,
                ParameterSpan(m_compilationContext, unit, parameterNodes.back()),
                std::string(functionName));
        }
        functionDefinition.setParameters(std::move(parameters));
        functionDefinition.setReturnTypes(std::move(returns));
        functionDefinition.setIsVariadic(isVariadic);
        functionDefinition.setSymbolName(std::move(symbolName));
    }

    void TypeChecker::typeCheckTypeSignature(const CompilationUnit& unit, u32 index)
    {
        const auto typeType = m_module.tryGetTypeByName(nameOf(unit, index));
        if (typeType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto annotations = std::vector<u32>{};
        NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
        validateTypeAnnotation(unit, index, annotations);

        auto& typeDefinition = m_module.getTypeDefinition(typeType);
        const auto typeLayout = NodeLayout::typeDefinition(unit.nodeBuffer, index);
        const auto isBuiltin = NodeLayout::annotationsContain(unit.nodeBuffer, m_compilationContext.tokenBuffer(unit.fileId), annotations, BuiltinAnnotationName);
        if (isBuiltin)
        {
            if (typeLayout.hasConstructorParameters)
            {
                m_diagnostics.addBuiltinTypeConstructorIgnoredWarning(
                    unit.source,
                    ParametersNodeSpan(m_compilationContext, unit, typeLayout.constructorParameters),
                    std::string(nameOf(unit, index)));
            }
        }
        else
        {
            typeCheckConstructorSignature(unit, index, typeDefinition, typeType);
        }

        auto bodyStatements = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, typeLayout.body, bodyStatements);
        for (const auto bodyStatement : bodyStatements)
        {
            if (unit.nodeBuffer.kind(bodyStatement) != NodeKind::MethodDefinitionStatement)
            {
                continue;
            }

            const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, bodyStatement);
            if (unit.nodeBuffer.methodNameIsNew(m_compilationContext.tokenBuffer(unit.fileId), methodLayout.methodName))
            {
                continue;
            }

            if (m_module.typeOfNode(NodeIndex{ unit.fileId, bodyStatement }) == Type::Undefined())
            {
                continue;
            }

            typeCheckMethodSignature(unit, bodyStatement, typeDefinition, typeType);

            if (isBuiltin)
            {
                validateBuiltinMethod(m_compilationContext, unit, bodyStatement, typeDefinition, typeType, m_module, m_diagnostics);
            }
            else
            {
                validateStaticMethodTypeName(unit, bodyStatement, typeDefinition);
                registerOperatorMethod(m_compilationContext, unit, bodyStatement, typeDefinition, typeType, m_module);
            }
        }
    }

    void TypeChecker::typeCheckMethodSignature(const CompilationUnit& unit, u32 methodIndex, TypeDefinition& typeDefinition, Type typeType)
    {
        const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, methodIndex);
        const auto methodName = nameOf(unit, methodLayout.methodName);

        pushScope(ScopeKind::Method);
        auto parameters = typeCheckParametersNode(unit, methodLayout.parameters);
        auto returns = typeCheckReturnTypesNode(unit, methodLayout.returnTypes);
        popScope(false);

        auto methodType = typeDefinition.tryGetMethodTypeByName(methodName);
        if (methodType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& methodDefinition = m_module.getFunctionDefinition(methodType);

        auto annotations = std::vector<u32>{};
        collectMethodAnnotationsBefore(unit, methodIndex, typeDefinition, annotations);

        std::optional<std::string> symbolName;
        auto isRequiredExtern = false;
        const auto isExtern = validateCallableAnnotations(unit, annotations, symbolName, isRequiredExtern);
        if (isExtern && isRequiredExtern)
        {
            m_module.markExternRequired(methodType);
        }
        if (isExtern && !symbolName.has_value())
        {
            // methods need to specify the extern symbol name
            for (const auto annotation : annotations)
            {
                if (nameOf(unit, annotation) == ExternAnnotationName)
                {
                    m_diagnostics.addExternMethodRequiresSymbolError(unit.source, annotationLocation(unit, annotation), methodDefinition.fullName());
                    break;
                }
            }
        }

        auto parameterNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, methodLayout.parameters, parameterNodes);
        const auto isVariadic = !parameterNodes.empty() && NodeLayout::isVariadicParameter(unit.nodeBuffer, parameterNodes.back());
        if (isVariadic && !isExtern)
        {
            m_diagnostics.addNonExternVariadicFunctionError(
                unit.source,
                ParameterSpan(m_compilationContext, unit, parameterNodes.back()),
                std::string(methodName));
        }

        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        if (unit.nodeBuffer.methodModifier(tokens, methodLayout.methodName) != MethodModifier::Static)
        {
            parameters.insert(parameters.begin(), Parameter{ ImplicitThisName, typeType.toReference() });
        }
        methodDefinition.setParameters(std::move(parameters));
        methodDefinition.setReturnTypes(std::move(returns));
        methodDefinition.setIsVariadic(isVariadic);
        methodDefinition.setSymbolName(std::move(symbolName));
    }

    void TypeChecker::validateStaticMethodTypeName(const CompilationUnit& unit, u32 methodIndex, TypeDefinition& typeDefinition)
    {
        const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, methodIndex);
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        if (!unit.nodeBuffer.methodHasTypeName(tokens, methodLayout.methodName))
        {
            return;
        }

        const auto typeNameToken = unit.nodeBuffer.methodTypeNameToken(tokens, methodLayout.methodName);
        const auto typeName = m_compilationContext.getLexeme(unit.fileId, typeNameToken);
        if (typeName == typeDefinition.name())
        {
            return;
        }

        m_diagnostics.addStaticMethodTypeNameMismatchError(
            unit.source,
            m_compilationContext.sourceLocation(unit.fileId, typeNameToken),
            std::string(typeName),
            typeDefinition.name(),
            std::string(nameOf(unit, methodLayout.methodName)));
    }

    void TypeChecker::typeCheckConstructorSignature(const CompilationUnit& unit, u32 typeIndex, TypeDefinition& typeDefinition, Type typeType)
    {
        auto constructorType = typeDefinition.tryGetMethodTypeByName("new");
        if (constructorType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        std::vector<Parameter> constructorParameters{};
        constructorParameters.emplace_back(ImplicitThisName, typeType.toReference());

        const auto typeLayout = NodeLayout::typeDefinition(unit.nodeBuffer, typeIndex);
        if (typeLayout.hasConstructorParameters)
        {
            pushScope(ScopeKind::Type);
            auto declaredConstructorParameters = typeCheckParametersNode(unit, typeLayout.constructorParameters);
            popScope(false);

            constructorParameters.insert(
                constructorParameters.end(),
                declaredConstructorParameters.begin(),
                declaredConstructorParameters.end());
        }

        auto& constructorDefinition = m_module.getFunctionDefinition(constructorType);
        constructorDefinition.setParameters(std::move(constructorParameters));
        constructorDefinition.setReturnTypes(std::vector<Type>{});
    }

    Type TypeChecker::typeCheckTypeNameNode(const CompilationUnit& unit, u32 index)
    {
        if (unit.nodeBuffer.kind(index) == NodeKind::ArrayTypeNameNode)
        {
            return typeCheckArrayTypeNameNode(unit, index);
        }

        const auto name = nameOf(unit, index);
        auto type = m_module.tryGetTypeByName(name);
        if (type != Type::Undefined())
        {
            if (!m_isPreludePass)
            {
                m_module.markPreludeTypeRequired(type);
            }

            const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
            if (unit.nodeBuffer.isReference(tokens, index))
            {
                if (type.isReference())
                {
                    TODO("Add error diagnostics for already being a reference");
                    return Type::Undefined();
                }
                type = type.toReference();
            }
            // TODO nullable handling
            return noteType(unit, index, type);
        }
        else
        {
            m_diagnostics.addUnknownTypeError(unit.source, nameLocation(unit, index), std::string(name));
            return Type::Undefined();
        }
    }

    Type TypeChecker::typeCheckArrayTypeNameNode(const CompilationUnit& unit, u32 index)
    {
        const auto arrayTypeLayout = NodeLayout::arrayTypeName(unit.nodeBuffer, index);
        auto elementType = typeCheckTypeNameNode(unit, arrayTypeLayout.elementType);
        if (elementType == Type::Undefined())
        {
            return Type::Undefined();
        }

        // ownership rule: arrays cant store references or slices
        if (elementType.isReference() || elementType.kind() == TypeKind::Slice)
        {
            m_diagnostics.addReferenceOrSliceArrayElementError(
                unit.source,
                TypeNameSpan(m_compilationContext, unit, arrayTypeLayout.elementType));
        }
        else if (elementType.kind() == TypeKind::DynamicArray)
        {
            m_diagnostics.addDynamicArrayElementError(
                unit.source,
                TypeNameSpan(m_compilationContext, unit, arrayTypeLayout.elementType));
        }

        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        auto arrayType = Type::Undefined();
        switch (unit.nodeBuffer.arrayKind(tokens, index))
        {
            case ArrayTypeKind::Slice:
            {
                arrayType = m_module.getOrCreateArrayType(TypeKind::Slice, elementType, 0);
                break;
            }
            case ArrayTypeKind::Fixed:
            {
                if (!arrayTypeLayout.hasLength)
                {
                    return Type::Undefined();
                }

                const auto length = convertToI32(unit, arrayTypeLayout.lengthLiteral);
                arrayType = m_module.getOrCreateArrayType(TypeKind::FixedArray, elementType, length);
                break;
            }
            case ArrayTypeKind::Dynamic:
            {
                arrayType = m_module.getOrCreateArrayType(TypeKind::DynamicArray, elementType, 0);
                break;
            }
        }

        if (unit.nodeBuffer.isReference(tokens, index))
        {
            arrayType = arrayType.toReference();
        }

        return noteType(unit, index, arrayType);
    }

    std::vector<Parameter> TypeChecker::typeCheckParametersNode(const CompilationUnit& unit, u32 index)
    {
        std::vector<Parameter> parameters{};
        auto seenDefault = false;

        auto parameterNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, index, parameterNodes);

        // first loop doesnt put the parameter names into the scope, so that default values cannot reference other parameters
        for (const auto parameterIndex : parameterNodes)
        {
            const auto parameterLayout = NodeLayout::parameter(unit.nodeBuffer, parameterIndex);
            const auto isVariadic = NodeLayout::isVariadicParameter(unit.nodeBuffer, parameterIndex);
            auto parameterName = std::string_view{};
            if (isVariadic)
            {
                parameterName = "varargs";
            }
            else
            {
                parameterName = nameOf(unit, parameterIndex);
            }

            auto parameterType = typeCheckTypeNameNode(unit, parameterLayout.typeName);
            if (parameterType == Type::Void())
            {
                m_diagnostics.addVoidParameterTypeError(
                    unit.source,
                    ParameterSpan(m_compilationContext, unit, parameterIndex),
                    std::string(parameterName));

                parameterType = Type::Undefined();
            }
            m_module.setNodeType(NodeIndex{ unit.fileId, parameterIndex }, parameterType);

            if (parameterLayout.hasDefaultValue)
            {
                auto defaultContext = ExpressionContext{};
                defaultContext.contextualNumberType = parameterType;
                const auto defaultType = typeCheckExpression(unit, parameterLayout.defaultValue, defaultContext);
                if (defaultType != Type::Undefined() && !isAssignableTo(defaultType, parameterType))
                {
                    const auto location = ExpressionSourceLocation(m_compilationContext, unit, parameterLayout.defaultValue);
                    if (!tryAddArrayLengthMismatchError(unit, location, parameterType, defaultType))
                    {
                        m_diagnostics.addDefaultParameterTypeMismatchError(
                            unit.source,
                            location,
                            formatTypeName(m_module, parameterType),
                            formatTypeName(m_module, defaultType));
                    }
                }
                seenDefault = true;
            }
            else if (seenDefault && !isVariadic)
            {
                // once a parameter with a default value is seen, all following parameters must have defaults
                m_diagnostics.addNonTrailingDefaultParameterError(
                    unit.source,
                    ParameterSpan(m_compilationContext, unit, parameterIndex),
                    std::string(parameterName));
            }

            auto parameter = Parameter{ parameterName, parameterType, nullptr };
            if (parameterLayout.hasDefaultValue)
            {
                parameter.setDefaultValueNode(NodeIndex{ unit.fileId, parameterLayout.defaultValue });
            }
            parameters.push_back(std::move(parameter));
        }

        // finally add parameter names into the current scope
        for (const auto parameterIndex : parameterNodes)
        {
            auto nameId = 0u;
            if (NodeLayout::isVariadicParameter(unit.nodeBuffer, parameterIndex))
            {
                nameId = internName("varargs");
            }
            else
            {
                nameId = nameIdOf(unit, parameterIndex);
            }

            if (const auto* existing = tryGetBinding(nameId); existing == nullptr)
            {
                addVariableBinding(nameId, m_module.typeOfNode(NodeIndex{ unit.fileId, parameterIndex }), nameLocation(unit, parameterIndex), unit.source, VariableBindingKind::Parameter);
            }
            else
            {
                m_diagnostics.addDuplicateParameterDeclarationError(
                    unit.source,
                    nameLocation(unit, parameterIndex),
                    std::string(m_nameLexemes[nameId]),
                    existing->source,
                    existing->location);
            }
        }

        return parameters;
    }

    std::vector<Type> TypeChecker::typeCheckReturnTypesNode(const CompilationUnit& unit, u32 index)
    {
        std::vector<Type> types{};
        auto returnTypeNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, index, returnTypeNodes);
        for (const auto returnTypeNode : returnTypeNodes)
        {
            auto returnType = typeCheckTypeNameNode(unit, returnTypeNode);
            // ownership rule: returning references or slices is not allowed because they could outlive the values
            if (returnType.isReference() || returnType.kind() == TypeKind::Slice)
            {
                m_diagnostics.addReferenceOrSliceReturnError(
                    unit.source,
                    TypeNameSpan(m_compilationContext, unit, returnTypeNode));
            }
            types.push_back(returnType);
        }
        return types;
    }

    i32 TypeChecker::convertToI32(const CompilationUnit& unit, u32 numberLiteralIndex)
    {
        const auto literalType = typeCheckNumberLiteral(unit, numberLiteralIndex, ExpressionContext{});
        if (literalType == Type::Undefined() || literalType != m_module.wellKnown().i32)
        {
            return 0;
        }

        const auto parsedValue = tryParseNumberLiteralValue(nameOf(unit, numberLiteralIndex), literalType, m_module);
        if (!parsedValue.has_value())
        {
            m_diagnostics.addNumberLiteralOutOfRangeError(
                unit.source,
                nameLocation(unit, numberLiteralIndex),
                std::string(nameOf(unit, numberLiteralIndex)),
                formatTypeName(m_module, m_module.wellKnown().i32));
            return 0;
        }

        return std::get<i32>(parsedValue.value());
    }

    u32 TypeChecker::internName(std::string_view lexeme)
    {
        const auto [entry, inserted] = m_nameIds.try_emplace(lexeme, static_cast<u32>(m_nameLexemes.size()));
        if (inserted)
        {
            m_nameLexemes.push_back(lexeme);
        }
        return entry->second;
    }

    void TypeChecker::internUnitIdentifiers(const CompilationUnit& unit)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto tokenCount = static_cast<i32>(tokens.size());
        if (m_tokenNameIds.size() <= unit.fileId)
        {
            m_tokenNameIds.resize(static_cast<size_t>(unit.fileId) + 1);
        }
        auto& ids = m_tokenNameIds[unit.fileId];
        ids.assign(tokenCount, InvalidNameId);
        for (i32 position = 0; position < tokenCount; ++position)
        {
            const auto token = tokens.getToken(position);
            if (isIdentifierKind(token.kind))
            {
                ids[position] = internName(tokens.getLexeme(token));
            }
        }
    }

    u32 TypeChecker::nameIdOf(const CompilationUnit& unit, u32 index) const noexcept
    {
        return m_tokenNameIds[unit.fileId][unit.nodeBuffer.mainToken(index)];
    }

    void TypeChecker::addVariableBinding(u32 nameId, Type type, std::optional<SourceLocation> location, const SourceTextSharedPtr& source, VariableBindingKind kind, bool referencesConstant)
    {
        if (m_bindingsByName.size() <= nameId)
        {
            m_bindingsByName.resize(static_cast<size_t>(nameId) + 1);
        }

        auto& bindings = m_bindingsByName[nameId];
        const auto scopeIndex = static_cast<u32>(m_scopeFrames.size() - 1);
        if (!bindings.empty() && bindings.back().scopeIndex == scopeIndex)
        {
            // the first binding in a scope wins
            return;
        }

        bindings.push_back(LexicalBinding{ type, source, location, kind, false, referencesConstant, scopeIndex });
        m_boundNames.push_back(nameId);
    }

    const TypeChecker::LexicalBinding* TypeChecker::tryGetBinding(u32 nameId) const noexcept
    {
        if (nameId >= m_bindingsByName.size() || m_bindingsByName[nameId].empty())
        {
            return nullptr;
        }
        return &m_bindingsByName[nameId].back();
    }

    TypeChecker::LexicalBinding* TypeChecker::tryGetBindingMutable(u32 nameId) noexcept
    {
        if (nameId >= m_bindingsByName.size() || m_bindingsByName[nameId].empty())
        {
            return nullptr;
        }
        return &m_bindingsByName[nameId].back();
    }

    void TypeChecker::pushScope(ScopeKind kind)
    {
        m_scopeFrames.push_back(ScopeFrame{ static_cast<u32>(m_boundNames.size()), kind });
    }

    void TypeChecker::popScope(bool emitUnusedWarnings)
    {
        const auto boundNamesStart = m_scopeFrames.back().boundNamesStart;
        if (emitUnusedWarnings)
        {
            emitUnusedVariableWarnings(m_scopeFrames.back());
        }

        for (auto position = static_cast<size_t>(boundNamesStart); position < m_boundNames.size(); ++position)
        {
            m_bindingsByName[m_boundNames[position]].pop_back();
        }
        m_boundNames.resize(boundNamesStart);
        m_scopeFrames.pop_back();
        if (m_scopeFrames.size() == 0)
        {
            TODO("Popped too many scopes");
        }
    }

    ScopeKind TypeChecker::currentScopeKind() const noexcept
    {
        return m_scopeFrames.back().kind;
    }

    bool TypeChecker::currentScopeIsGlobal() const noexcept
    {
        return m_scopeFrames.size() == 1;
    }

    void TypeChecker::emitUnusedVariableWarnings(const ScopeFrame& frame)
    {
        if (m_diagnostics.hasErrors())
        {
            return;
        }

        for (auto position = static_cast<size_t>(frame.boundNamesStart); position < m_boundNames.size(); ++position)
        {
            const auto nameId = m_boundNames[position];
            const auto& binding = m_bindingsByName[nameId].back();
            const auto name = m_nameLexemes[nameId];
            if (binding.wasRead || ShouldIgnoreUnusedVariableWarning(name) || !binding.location.has_value())
            {
                continue;
            }

            if (binding.kind != VariableBindingKind::LocalVariable && binding.kind != VariableBindingKind::LocalConstant && binding.kind != VariableBindingKind::Parameter)
            {
                continue;
            }

            if (binding.kind == VariableBindingKind::Parameter)
            {
                m_diagnostics.addUnusedParameterWarning(binding.source, binding.location.value(), std::string(name));
            }
            else if (binding.kind == VariableBindingKind::LocalConstant)
            {
                m_diagnostics.addUnusedLocalConstantWarning(binding.source, binding.location.value(), std::string(name));
            }
            else
            {
                m_diagnostics.addUnusedLocalVariableWarning(binding.source, binding.location.value(), std::string(name));
            }
        }
    }

    void TypeChecker::typeCheckGlobalConstants()
    {
        for (const auto declaration : m_globalConstantDeclarations)
        {
            const auto& unit = unitFor(declaration);
            if (unit.nodeBuffer.kind(declaration.index) == NodeKind::InitConstantDeclaration)
            {
                typeCheckInitConstantDeclaration(unit, declaration.index);
            }
            else
            {
                typeCheckConstantDeclaration(unit, declaration.index);
            }
        }
    }

    void TypeChecker::typeCheckFunctionDefinitions()
    {
        for (const auto declaration : m_functionDeclarations)
        {
            const auto& unit = unitFor(declaration);
            typeCheckFunctionDefinitionStatement(unit, declaration.index);
        }
    }

    void TypeChecker::typeCheckEnumDefinitions()
    {
        for (const auto declaration : m_enumDeclarations)
        {
            const auto& unit = unitFor(declaration);
            typeCheckEnumDefinitionStatement(unit, declaration.index);
        }
    }

    void TypeChecker::typeCheckTypeFieldDefinitions()
    {
        for (const auto declaration : m_typeDeclarations)
        {
            const auto& unit = unitFor(declaration);
            const auto index = declaration.index;

            const auto typeType = m_module.tryGetTypeByName(nameOf(unit, index));
            if (typeType == Type::Undefined())
            {
                TODO("This shouldn't happen");
            }

            std::vector<u32> annotations;
            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
            auto isBuiltin = false;
            for (const auto annotation : annotations)
            {
                if (nameOf(unit, annotation) == BuiltinAnnotationName)
                {
                    isBuiltin = true;
                    break;
                }
            }

            if (isBuiltin)
            {
                const auto typeLayout = NodeLayout::typeDefinition(unit.nodeBuffer, index);
                std::vector<u32> bodyStatements;
                NodeLayout::collectChildren(unit.nodeBuffer, typeLayout.body, bodyStatements);
                for (const auto bodyStatement : bodyStatements)
                {
                    if (unit.nodeBuffer.kind(bodyStatement) != NodeKind::TypeFieldDeclaration)
                    {
                        continue;
                    }

                    const auto fieldLayout = NodeLayout::declaration(unit.nodeBuffer, bodyStatement);
                    m_diagnostics.addBuiltinTypeFieldIgnoredWarning(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, fieldLayout.left),
                        std::string(nameOf(unit, fieldLayout.left)));
                }

                continue;
            }

            pushScope(ScopeKind::Type);

            const auto typeLayout = NodeLayout::typeDefinition(unit.nodeBuffer, index);
            if (typeLayout.hasConstructorParameters)
            {
                static_cast<void>(typeCheckParametersNode(unit, typeLayout.constructorParameters));
            }

            auto& typeDefinition = m_module.getTypeDefinition(typeType);
            auto fieldIndex = 0;
            std::vector<u32> bodyStatements;
            NodeLayout::collectChildren(unit.nodeBuffer, typeLayout.body, bodyStatements);
            for (const auto bodyStatement : bodyStatements)
            {
                if (unit.nodeBuffer.kind(bodyStatement) != NodeKind::TypeFieldDeclaration)
                {
                    continue;
                }

                typeCheckTypeFieldDeclaration(unit, bodyStatement, typeDefinition, fieldIndex);
                ++fieldIndex;
            }

            popScope(true);
        }
    }

    void TypeChecker::typeCheckTypeMethodDefinitions()
    {
        for (const auto declaration : m_typeDeclarations)
        {
            const auto& unit = unitFor(declaration);
            const auto index = declaration.index;

            const auto typeType = m_module.tryGetTypeByName(nameOf(unit, index));
            if (typeType == Type::Undefined())
            {
                TODO("This shouldn't happen");
            }

            std::vector<u32> annotations;
            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
            auto isBuiltin = false;
            for (const auto annotation : annotations)
            {
                if (nameOf(unit, annotation) == BuiltinAnnotationName)
                {
                    isBuiltin = true;
                    break;
                }
            }

            if (isBuiltin)
            {
                const auto typeLayout = NodeLayout::typeDefinition(unit.nodeBuffer, index);
                std::vector<u32> bodyStatements;
                NodeLayout::collectChildren(unit.nodeBuffer, typeLayout.body, bodyStatements);
                for (const auto bodyStatement : bodyStatements)
                {
                    if (unit.nodeBuffer.kind(bodyStatement) != NodeKind::MethodDefinitionStatement)
                    {
                        continue;
                    }

                    const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, bodyStatement);
                    if (unit.nodeBuffer.subtreeSize(methodLayout.body) > 1)
                    {
                        m_diagnostics.addBuiltinMethodBodyIgnoredWarning(
                            unit.source,
                            nameLocation(unit, methodLayout.methodName),
                            std::string(nameOf(unit, methodLayout.methodName)));
                    }
                }

                continue;
            }

            const ScopedValue<Type> currentTypeScope{ m_currentType, typeType };
            pushScope(ScopeKind::Type);

            auto& typeDefinition = m_module.getTypeDefinition(typeType);
            for (const auto& fieldDefinition : typeDefinition.fields())
            {
                addVariableBinding(
                    internName(fieldDefinition.name()),
                    fieldDefinition.type(),
                    TypeFieldLocation(m_compilationContext, typeDefinition, fieldDefinition),
                    unit.source,
                    VariableBindingKind::Field);
            }

            const auto typeLayout = NodeLayout::typeDefinition(unit.nodeBuffer, index);
            std::vector<u32> bodyStatements;
            NodeLayout::collectChildren(unit.nodeBuffer, typeLayout.body, bodyStatements);
            for (const auto bodyStatement : bodyStatements)
            {
                if (unit.nodeBuffer.kind(bodyStatement) != NodeKind::MethodDefinitionStatement)
                {
                    continue;
                }

                const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, bodyStatement);
                if (unit.nodeBuffer.methodNameIsNew(m_compilationContext.tokenBuffer(unit.fileId), methodLayout.methodName))
                {
                    continue;
                }

                const auto methodType = typeDefinition.tryGetMethodTypeByName(nameOf(unit, methodLayout.methodName));
                if (methodType == Type::Undefined())
                {
                    continue;
                }

                if (!(m_module.getFunctionDefinition(methodType).definitionNode() == NodeIndex{ unit.fileId, bodyStatement }))
                {
                    continue;
                }

                typeCheckMethodDefinitionStatement(unit, bodyStatement);
            }

            popScope(false);
        }
    }

    void TypeChecker::typeCheckStatement(const CompilationUnit& unit, u32 index)
    {
        switch (unit.nodeBuffer.kind(index))
        {
            case NodeKind::ConstantDeclaration:
            {
                typeCheckConstantDeclaration(unit, index);
                break;
            }
            case NodeKind::InitConstantDeclaration:
            {
                typeCheckInitConstantDeclaration(unit, index);
                break;
            }
            case NodeKind::VariableDeclaration:
            {
                typeCheckVariableDeclaration(unit, index);
                break;
            }
            case NodeKind::ExpressionStatement:
            {
                typeCheckExpressionStatement(unit, index);
                break;
            }
            case NodeKind::AssignmentStatement:
            {
                typeCheckAssignmentStatement(unit, index);
                break;
            }
            case NodeKind::FunctionDefinitionStatement:
            {
                typeCheckFunctionDefinitionStatement(unit, index);
                break;
            }
            case NodeKind::EnumDefinitionStatement:
            {
                typeCheckEnumDefinitionStatement(unit, index);
                break;
            }
            case NodeKind::IfStatement:
            {
                typeCheckIfStatement(unit, index);
                break;
            }
            case NodeKind::TrailingIfStatement:
            {
                typeCheckTrailingIfStatement(unit, index);
                break;
            }
            case NodeKind::WhileStatement:
            {
                typeCheckWhileStatement(unit, index);
                break;
            }
            case NodeKind::ReturnStatement:
            {
                typeCheckReturnStatement(unit, index);
                break;
            }
            case NodeKind::BlockNode:
            {
                typeCheckBlockNode(unit, index);
                break;
            }
            default:
            {
                break;
            }
        }
    }

    void TypeChecker::typeCheckConstantDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto isGlobalConstant = currentScopeIsGlobal();

        if (isGlobalConstant)
        {
            std::vector<u32> annotations;
            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
            for (const auto annotation : annotations)
            {
                m_diagnostics.addUnexpectedAnnotationTargetError(
                    unit.source,
                    annotationLocation(unit, annotation));
            }
        }

        const auto declarationLayout = NodeLayout::declaration(unit.nodeBuffer, index);
        auto declaredType = Type::Undefined();
        if (declarationLayout.hasExplicitType)
        {
            declaredType = typeCheckTypeNameNode(unit, declarationLayout.explicitType);
        }

        auto rightType = Type::Undefined();
        {
            auto context = ExpressionContext{};
            // use the explicit type for the initializer
            context.contextualNumberType = contextualTypeForExpectedType(declaredType);
            rightType = typeCheckExpression(unit, declarationLayout.right, context);
        }

        // the declared type wins for the binding, the initializer only has to be assignable to it
        auto bindingType = rightType;
        if (declaredType != Type::Undefined())
        {
            bindingType = declaredType;
        }

        const auto leftKind = unit.nodeBuffer.kind(declarationLayout.left);
        if (leftKind == NodeKind::NameExpression)
        {
            const auto name = nameOf(unit, declarationLayout.left);
            if (bindingType == Type::Void())
            {
                m_diagnostics.addCallReturnsNoValueError(unit.source, nameLocation(unit, declarationLayout.left), std::string(name));
                noteType(unit, declarationLayout.left, Type::Undefined());
                noteType(unit, index, Type::Undefined());
                return;
            }

            if (isGlobalConstant
                && ContainsRuntimeCall(m_module, unit, tokens, declarationLayout.right)
                && !IsConstructorCallExpression(m_module, unit, tokens, declarationLayout.right))
            {
                m_diagnostics.addGlobalConstantWithCallError(
                    unit.source,
                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right),
                    std::string(name));
            }

            const auto nameId = nameIdOf(unit, declarationLayout.left);
            if (const auto* existing = tryGetBinding(nameId); existing == nullptr)
            {
                addVariableBinding(nameId, bindingType, nameLocation(unit, declarationLayout.left), unit.source, VariableBindingKind::LocalConstant);

                if (isGlobalConstant)
                {
                    static_cast<void>(m_module.createConstant(name, rightType, NodeIndex{ unit.fileId, declarationLayout.right }));
                }
            }
            else
            {
                m_diagnostics.addDuplicateConstantDeclarationError(
                    unit.source,
                    nameLocation(unit, declarationLayout.left),
                    std::string(name),
                    existing->source,
                    existing->location);
            }
            noteType(unit, declarationLayout.left, bindingType);
        }
        else if (isGlobalConstant && leftKind == NodeKind::DiscardLiteral)
        {
            // a discarded global still needs to evaluate for potential side effects
            m_module.addGlobalDiscardExpression(nullptr);
        }

        if (declaredType != Type::Undefined() && rightType != Type::Undefined() && !isAssignableTo(rightType, declaredType))
        {
            const auto location = NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right);
            if (!tryAddArrayLengthMismatchError(unit, location, declaredType, rightType))
            {
                m_diagnostics.addExplicitConstantTypeMismatchError(
                    unit.source,
                    location,
                    formatTypeName(m_module, declaredType),
                    formatTypeName(m_module, rightType));
            }
        }

        noteType(unit, index, bindingType);
    }

    void TypeChecker::typeCheckInitConstantDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto isGlobalConstant = currentScopeIsGlobal();

        if (isGlobalConstant)
        {
            std::vector<u32> annotations;
            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
            for (const auto annotation : annotations)
            {
                m_diagnostics.addUnexpectedAnnotationTargetError(
                    unit.source,
                    annotationLocation(unit, annotation));
            }
        }

        const auto initLayout = NodeLayout::initConstant(unit.nodeBuffer, index);
        const auto declaredType = typeCheckTypeNameNode(unit, initLayout.initType);

        const auto isValidInitConstant = isGlobalConstant;
        if (unit.nodeBuffer.kind(initLayout.left) == NodeKind::NameExpression)
        {
            const auto name = nameOf(unit, initLayout.left);

            if (!isValidInitConstant)
            {
                m_diagnostics.addNonGlobalInitConstantError(
                    unit.source,
                    nameLocation(unit, initLayout.left),
                    std::string(name));
            }

            const auto nameId = nameIdOf(unit, initLayout.left);
            if (const auto* existing = tryGetBinding(nameId); existing == nullptr)
            {
                addVariableBinding(nameId, declaredType, nameLocation(unit, initLayout.left), unit.source, VariableBindingKind::LocalConstant);

                if (isValidInitConstant)
                {
                    static_cast<void>(m_module.createInitConstant(name, declaredType));
                    m_initConstants.insert_or_assign(std::string(name), InitConstantRecord{ nameLocation(unit, initLayout.left), unit.source });
                }
            }
            else
            {
                m_diagnostics.addDuplicateConstantDeclarationError(
                    unit.source,
                    nameLocation(unit, initLayout.left),
                    std::string(name),
                    existing->source,
                    existing->location);
            }
            noteType(unit, initLayout.left, declaredType);
        }

        noteType(unit, index, declaredType);
    }

    void TypeChecker::typeCheckVariableDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto declarationLayout = NodeLayout::declaration(unit.nodeBuffer, index);

        auto declaredType = Type::Undefined();
        if (declarationLayout.hasExplicitType)
        {
            declaredType = typeCheckTypeNameNode(unit, declarationLayout.explicitType);
        }

        auto rightType = Type::Undefined();
        {
            auto context = ExpressionContext{};
            // use the explicit type for the initializer
            context.contextualNumberType = contextualTypeForExpectedType(declaredType);
            rightType = typeCheckExpression(unit, declarationLayout.right, context);
        }

        // the declared type wins for the binding, the initializer only has to be assignable to it
        auto bindingType = rightType;
        if (declaredType != Type::Undefined())
        {
            bindingType = declaredType;
        }

        auto referencesConstant = false;
        const auto strippedRight = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
        const auto strippedRightKind = unit.nodeBuffer.kind(strippedRight);
        if (strippedRightKind == NodeKind::UnaryExpression)
        {
            // taking a ref of a constant is allowed, but assigning through it is not
            const auto isReferenceOf = TokenKindAt(tokens, unit.nodeBuffer.mainToken(strippedRight)) == TokenKind::RefKeyword;
            if (isReferenceOf && m_module.typeOfNode(NodeIndex{ unit.fileId, strippedRight }).isReference())
            {
                const auto operand = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(strippedRight));
                if (unit.nodeBuffer.kind(operand) == NodeKind::NameExpression)
                {
                    const auto* operandBinding = tryGetBinding(nameIdOf(unit, operand));
                    referencesConstant = operandBinding != nullptr && operandBinding->kind == VariableBindingKind::LocalConstant;
                }
                else
                {
                    auto rootName = std::string{};
                    auto rootReferencesConstant = false;
                    referencesConstant = receiverChainIsConstant(unit, operand, rootName, rootReferencesConstant);
                }
            }
        }
        else if (strippedRightKind == NodeKind::NameExpression)
        {
            const auto* rightBinding = tryGetBinding(nameIdOf(unit, strippedRight));
            referencesConstant = rightBinding != nullptr && rightBinding->referencesConstant;
        }
        else if (strippedRightKind == NodeKind::BinaryExpression)
        {
            // a slice() view borrows its receiver, a view of a constant stays constant
            const auto binaryLayout = NodeLayout::binary(unit.nodeBuffer, strippedRight);
            if (unit.nodeBuffer.isMemberAccessBinary(tokens, strippedRight)
                && unit.nodeBuffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression
                && unit.nodeBuffer.kind(binaryLayout.left) == NodeKind::NameExpression)
            {
                const auto methodType = m_module.typeOfNode(NodeIndex{ unit.fileId, NodeLayout::subtreeBegin(unit.nodeBuffer, binaryLayout.right) });
                if (methodType != Type::Undefined()
                    && m_module.getFunctionDefinition(methodType).intrinsicKind() == IntrinsicKind::ArraySlice)
                {
                    const auto* receiverBinding = tryGetBinding(nameIdOf(unit, binaryLayout.left));
                    referencesConstant = receiverBinding != nullptr
                        && (receiverBinding->kind == VariableBindingKind::LocalConstant || receiverBinding->referencesConstant);
                }
            }
        }

        if (unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::NameExpression)
        {
            const auto name = nameOf(unit, declarationLayout.left);
            if (bindingType == Type::Void())
            {
                m_diagnostics.addCallReturnsNoValueError(unit.source, nameLocation(unit, declarationLayout.left), std::string(name));
                noteType(unit, declarationLayout.left, Type::Undefined());
                noteType(unit, index, Type::Undefined());
                return;
            }

            const auto nameId = nameIdOf(unit, declarationLayout.left);
            if (const auto* existing = tryGetBinding(nameId); existing == nullptr)
            {
                addVariableBinding(nameId, bindingType, nameLocation(unit, declarationLayout.left), unit.source, VariableBindingKind::LocalVariable, referencesConstant);
            }
            else
            {
                m_diagnostics.addDuplicateVariableDeclarationError(
                    unit.source,
                    nameLocation(unit, declarationLayout.left),
                    std::string(name),
                    existing->source,
                    existing->location);
            }
            noteType(unit, declarationLayout.left, bindingType);
        }

        if (declaredType != Type::Undefined() && rightType != Type::Undefined() && !isAssignableTo(rightType, declaredType))
        {
            const auto location = NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right);
            if (!tryAddArrayLengthMismatchError(unit, location, declaredType, rightType))
            {
                m_diagnostics.addExplicitVariableTypeMismatchError(
                    unit.source,
                    location,
                    formatTypeName(m_module, declaredType),
                    formatTypeName(m_module, rightType));
            }
        }

        noteType(unit, index, bindingType);
    }

    void TypeChecker::typeCheckExpressionStatement(const CompilationUnit& unit, u32 index)
    {
        const auto expressionType = typeCheckExpression(unit, NodeLayout::statementExpression(index));
        noteType(unit, index, expressionType);
    }

    bool TypeChecker::receiverChainIsConstant(const CompilationUnit& unit, u32 expressionIndex, std::string& rootName, bool& referencesConstant)
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, expressionIndex);
        const auto strippedKind = unit.nodeBuffer.kind(stripped);

        if (strippedKind == NodeKind::NameExpression)
        {
            const auto name = nameOf(unit, stripped);
            const auto* binding = tryGetBinding(nameIdOf(unit, stripped));
            if (binding != nullptr && binding->kind == VariableBindingKind::LocalConstant)
            {
                rootName = std::string(name);
                referencesConstant = false;
                return true;
            }

            if (binding != nullptr && binding->referencesConstant)
            {
                rootName = std::string(name);
                referencesConstant = true;
                return true;
            }

            return false;
        }

        if (strippedKind == NodeKind::BinaryExpression)
        {
            const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
            const auto binaryLayout = NodeLayout::binary(unit.nodeBuffer, stripped);
            if (!unit.nodeBuffer.isMemberAccessBinary(tokens, stripped)
                || unit.nodeBuffer.kind(binaryLayout.right) != NodeKind::NameExpression)
            {
                return false;
            }

            // a constant anywhere along the chain makes the whole tail const
            if (receiverChainIsConstant(unit, binaryLayout.left, rootName, referencesConstant))
            {
                return true;
            }

            const auto receiverType = m_module.typeOfNode(NodeIndex{ unit.fileId, binaryLayout.left }).toValue();
            if (receiverType.kind() != TypeKind::Type)
            {
                return false;
            }

            const auto fieldName = nameOf(unit, binaryLayout.right);
            const auto& fieldDefinition = m_module.getTypeDefinition(receiverType).tryGetFieldByName(fieldName);
            if (fieldDefinition.type() != Type::Undefined() && fieldDefinition.isConstant())
            {
                rootName = std::string(fieldName);
                referencesConstant = false;
                return true;
            }
        }

        return false;
    }

    void TypeChecker::typeCheckAssignmentStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto assignmentLayout = NodeLayout::assignment(unit.nodeBuffer, index);

        auto leftType = typeCheckExpression(unit, assignmentLayout.target);
        if (leftType.isReference())
        {
            leftType = leftType.toValue();
        }

        // global init constants are only allowed to be assigned once in caracalMain/main
        const auto leftKind = unit.nodeBuffer.kind(assignmentLayout.target);
        if (leftKind == NodeKind::NameExpression)
        {
            const auto targetName = std::string(nameOf(unit, assignmentLayout.target));
            if (m_initConstants.contains(targetName))
            {
                const auto location = nameLocation(unit, assignmentLayout.target);
                if (!IsInitConstantAssignmentSite(m_currentFunctionName))
                {
                    m_diagnostics.addAssignmentToInitConstantError(unit.source, location, targetName);
                }
                else if (const auto existing = m_initConstantAssignments.find(targetName); existing != m_initConstantAssignments.end())
                {
                    m_diagnostics.addInitConstantAlreadyInitializedError(unit.source, location, targetName, existing->second);
                }
                else
                {
                    m_initConstantAssignments.emplace(targetName, location);
                }
            }
            else if (const auto* targetBinding = tryGetBinding(nameIdOf(unit, assignmentLayout.target)); targetBinding != nullptr && targetBinding->kind == VariableBindingKind::LocalConstant)
            {
                m_diagnostics.addAssignmentToConstantError(
                    unit.source,
                    nameLocation(unit, assignmentLayout.target),
                    targetName);
            }
            else if (targetBinding != nullptr && targetBinding->referencesConstant)
            {
                m_diagnostics.addAssignmentThroughConstantReferenceError(
                    unit.source,
                    nameLocation(unit, assignmentLayout.target),
                    targetName);
            }
        }
        else if (leftKind == NodeKind::BinaryExpression)
        {
            const auto memberLayout = NodeLayout::binary(unit.nodeBuffer, assignmentLayout.target);
            if (unit.nodeBuffer.isMemberAccessBinary(tokens, assignmentLayout.target)
                && unit.nodeBuffer.kind(memberLayout.right) == NodeKind::NameExpression)
            {
                auto receiverType = m_module.typeOfNode(NodeIndex{ unit.fileId, memberLayout.left });
                if (receiverType.isReference())
                {
                    receiverType = receiverType.toValue();
                }

                if (receiverType.kind() == TypeKind::Type
                    && m_module.typeOfNode(NodeIndex{ unit.fileId, assignmentLayout.target }) != Type::Undefined())
                {
                    auto& typeDefinition = m_module.getTypeDefinition(receiverType);
                    const auto fieldName = std::string(nameOf(unit, memberLayout.right));
                    const auto& fieldDefinition = typeDefinition.tryGetFieldByName(fieldName);
                    if (fieldDefinition.type() != Type::Undefined() && fieldDefinition.isConstant())
                    {
                        m_diagnostics.addAssignmentToConstantError(
                            unit.source,
                            NodeSpans::expressionLocation(unit.nodeBuffer, tokens, assignmentLayout.target),
                            fieldName);
                    }
                    else
                    {
                        // a constant receiver makes every field along the chain const
                        auto rootName = std::string{};
                        auto rootReferencesConstant = false;
                        if (receiverChainIsConstant(unit, memberLayout.left, rootName, rootReferencesConstant))
                        {
                            if (rootReferencesConstant)
                            {
                                m_diagnostics.addAssignmentThroughConstantReferenceError(
                                    unit.source,
                                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, assignmentLayout.target),
                                    rootName);
                            }
                            else
                            {
                                m_diagnostics.addAssignmentToConstantError(
                                    unit.source,
                                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, assignmentLayout.target),
                                    rootName);
                            }
                        }
                    }
                }
            }
        }
        auto rightType = Type::Undefined();
        {
            auto context = ExpressionContext{};
            // use the assigned variable's type for the value
            context.contextualNumberType = contextualTypeForExpectedType(leftType);
            rightType = typeCheckExpression(unit, assignmentLayout.value, context);
        }
        if (rightType.isReference())
        {
            rightType = rightType.toValue();
        }

        if (leftType == Type::Discard())
        {
            return;
        }

        // either side being undefined means its error was already reported
        if (leftType == Type::Undefined() || rightType == Type::Undefined())
        {
            return;
        }

        if (!isAssignableTo(rightType, leftType))
        {
            const auto location = NodeSpans::expressionLocation(unit.nodeBuffer, tokens, assignmentLayout.value);
            if (!tryAddArrayLengthMismatchError(unit, location, leftType, rightType))
            {
                m_diagnostics.addAssignmentTypeMismatchError(
                    unit.source,
                    location,
                    formatTypeName(m_module, leftType),
                    formatTypeName(m_module, rightType));
            }
        }
    }

    void TypeChecker::typeCheckFunctionDefinitionStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const ScopedValue<Type> returnTypeScope{ m_currentReturnType, Type::Void() };
        pushScope(ScopeKind::Function);

        const auto functionType = m_module.typeOfNode(NodeIndex{ unit.fileId, index });
        if (functionType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& functionDefinition = m_module.getFunctionDefinition(functionType);
        const auto& parameters = functionDefinition.parameters();
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);
        NodeLayout::collectChildren(unit.nodeBuffer, functionLayout.parameters, m_childrenScratch);
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto location = std::optional<SourceLocation>{};
            if (i < m_childrenScratch.size())
            {
                location = NodeSpans::parameterLocation(unit.nodeBuffer, tokens, m_childrenScratch[i]);
            }
            addVariableBinding(internName(parameters[i].name()), parameters[i].type(), location, unit.source, VariableBindingKind::Parameter);
        }

        const auto& returnTypes = functionDefinition.returnTypes();
        if (returnTypes.size() == 1)
        {
            m_currentReturnType = returnTypes[0];
        }
        else if (returnTypes.size() > 1)
        {
            TODO("Handle multiple return types in function definition");
        }

        m_currentFunctionName = functionDefinition.fullName();
        typeCheckBlockNode(unit, functionLayout.body);
        m_currentFunctionName.clear();

        NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, m_annotationScratch);
        auto isExtern = false;
        for (const auto annotation : m_annotationScratch)
        {
            if (nameOf(unit, annotation) == ExternAnnotationName)
            {
                isExtern = true;
                break;
            }
        }

        if (!isExtern
            && m_currentReturnType != Type::Void()
            && m_currentReturnType != Type::Undefined()
            && !StatementGuaranteesReturn(unit.nodeBuffer, tokens, functionLayout.body))
        {
            m_diagnostics.addMissingReturnError(
                unit.source,
                nameLocation(unit, index),
                // the body walk can move the definition store, resolve again
                m_module.getFunctionDefinition(functionType).name(),
                formatTypeName(m_module, m_currentReturnType));
        }

        popScope(!isExtern);
    }

    void TypeChecker::typeCheckEnumDefinitionStatement(const CompilationUnit& unit, u32 index)
    {
        auto baseType = Type::Undefined();
        const auto defaultBaseType = m_defaultEnumBaseType;
        const auto enumType = m_module.tryGetTypeByName(nameOf(unit, index));
        if (enumType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& enumDefinition = m_module.getEnumDefinition(enumType);
        auto isFlag = false;
        auto stepValue = std::optional<i32>{};
        std::vector<u32> annotations;
        NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
        validateEnumAnnotation(unit, index, annotations, isFlag, stepValue);
        auto currentFieldValue = 0;
        if (isFlag)
        {
            currentFieldValue = 1;
        }
        const auto step = stepValue.value_or(1);

        std::vector<u32> children;
        NodeLayout::collectChildren(unit.nodeBuffer, index, children);
        auto firstField = size_t{ 0 };
        if (!children.empty() && unit.nodeBuffer.kind(children.front()) == NodeKind::TypeNameNode)
        {
            baseType = typeCheckTypeNameNode(unit, children.front());
            enumDefinition.setBaseType(baseType);
            firstField = 1;
        }

        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        for (auto fieldPosition = firstField; fieldPosition < children.size(); ++fieldPosition)
        {
            const auto fieldNode = children[fieldPosition];
            const auto fieldName = nameOf(unit, fieldNode);
            if (const auto* otherField = enumDefinition.tryGetFieldByName(fieldName); otherField != nullptr)
            {
                m_diagnostics.addDuplicateEnumFieldDeclarationError(
                    unit.source,
                    nameLocation(unit, fieldNode),
                    std::string(fieldName),
                    otherField->location());
                continue;
            }

            const auto fieldLocation = nameLocation(unit, fieldNode);
            const auto valueExpression = NodeLayout::optionalChild(unit.nodeBuffer, fieldNode);
            if (isFlag)
            {
                if (valueExpression.hasChild)
                {
                    m_diagnostics.addFlagEnumExplicitValueError(
                        unit.source,
                        NodeSpans::expressionLocation(unit.nodeBuffer, tokens, valueExpression.child),
                        std::string(fieldName));
                }

                enumDefinition.addField(fieldName, currentFieldValue, fieldLocation);

                if (currentFieldValue == 0)
                {
                    currentFieldValue = 1;
                }
                else
                {
                    currentFieldValue *= 2;
                }
            }
            else if (valueExpression.hasChild)
            {
                const auto expression = valueExpression.child;
                auto contextualType = defaultBaseType;
                if (baseType != Type::Undefined())
                {
                    contextualType = baseType;
                }
                else if (unit.nodeBuffer.kind(expression) == NodeKind::StringLiteral)
                {
                    // TODO enum string values are cstrings for now until enums can handle any types
                    contextualType = m_module.wellKnown().cstring;
                }

                auto fieldValueType = Type::Undefined();
                {
                    auto context = ExpressionContext{};
                    context.contextualNumberType = contextualType;
                    fieldValueType = typeCheckExpression(unit, expression, context);
                }
                if (baseType == Type::Undefined())
                {
                    baseType = fieldValueType;
                    enumDefinition.setBaseType(baseType);
                }
                else if (fieldValueType != baseType)
                {
                    if (fieldValueType != Type::Undefined())
                    {
                        m_diagnostics.addEnumFieldValueTypeMismatchError(
                            unit.source,
                            NodeSpans::expressionLocation(unit.nodeBuffer, tokens, expression),
                            formatTypeName(m_module, baseType),
                            formatTypeName(m_module, fieldValueType));
                    }
                }

                if (unit.nodeBuffer.kind(expression) == NodeKind::NumberLiteral)
                {
                    const auto value = TryConvertEnumFieldLiteralValue(m_module, nameOf(unit, expression), fieldValueType);
                    if (value.has_value())
                    {
                        enumDefinition.addField(fieldName, value.value(), fieldLocation);
                        currentFieldValue = value.value() + step;
                    }
                    else
                    {
                        enumDefinition.addField(fieldName, nullptr, fieldLocation);
                    }
                }
                else
                {
                    enumDefinition.addField(fieldName, nullptr, fieldLocation);
                }
            }
            else
            {
                enumDefinition.addField(fieldName, currentFieldValue, fieldLocation);
                currentFieldValue += step;
            }
        }

        if (enumDefinition.baseType() == Type::Undefined())
        {
            enumDefinition.setBaseType(defaultBaseType);
        }
    }

    void TypeChecker::typeCheckTypeFieldDeclaration(const CompilationUnit& unit, u32 index, TypeDefinition& typeDefinition, i32 fieldIndex)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto declarationLayout = NodeLayout::declaration(unit.nodeBuffer, index);

        const auto fieldName = nameOf(unit, declarationLayout.left);
        const auto& existingField = typeDefinition.tryGetFieldByName(fieldName);
        if (existingField.type() != Type::Undefined())
        {
            m_diagnostics.addDuplicateTypeFieldDeclarationError(
                unit.source,
                nameLocation(unit, declarationLayout.left),
                std::string(fieldName),
                TypeFieldLocation(m_compilationContext, typeDefinition, existingField));
            return;
        }

        auto fieldType = Type::Undefined();
        if (declarationLayout.hasExplicitType)
        {
            fieldType = typeCheckTypeNameNode(unit, declarationLayout.explicitType);
        }

        auto expressionType = Type::Undefined();
        {
            auto context = ExpressionContext{};
            // use the explicit field type for the initializer
            context.contextualNumberType = contextualTypeForExpectedType(fieldType);
            expressionType = typeCheckExpression(unit, declarationLayout.right, context);
        }

        if (fieldType == Type::Undefined())
        {
            fieldType = expressionType;
        }
        else if (expressionType != Type::Undefined() && !isAssignableTo(expressionType, fieldType))
        {
            const auto location = NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right);
            if (!tryAddArrayLengthMismatchError(unit, location, fieldType, expressionType))
            {
                m_diagnostics.addTypeFieldInitializerMismatchError(
                    unit.source,
                    location,
                    formatTypeName(m_module, fieldType),
                    formatTypeName(m_module, expressionType));
            }
        }

        if (fieldType == Type::Undefined())
        {
            return;
        }

        // ownership rule: we dont allow storing references or slices in types
        if (fieldType.isReference() || fieldType.kind() == TypeKind::Slice)
        {
            m_diagnostics.addReferenceOrSliceFieldError(
                unit.source,
                nameLocation(unit, declarationLayout.left));
        }

        noteType(unit, declarationLayout.left, fieldType);
        noteType(unit, index, fieldType);

        typeDefinition.addField(fieldType, std::string(fieldName), fieldIndex, nullptr, unit.nodeBuffer.isConstantTypeField(tokens, index));
    }

    void TypeChecker::typeCheckMethodDefinitionStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const ScopedValue<Type> returnTypeScope{ m_currentReturnType, Type::Void() };
        const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, index);

        const auto methodType = m_module.getTypeDefinition(m_currentType).tryGetMethodTypeByName(nameOf(unit, methodLayout.methodName));
        if (methodType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        const auto isStatic = unit.nodeBuffer.methodHasTypeName(tokens, methodLayout.methodName);
        if (isStatic)
        {
            pushScope(ScopeKind::StaticMethod);
        }
        else
        {
            pushScope(ScopeKind::Method);
        }

        auto& methodDefinition = m_module.getFunctionDefinition(methodType);
        const auto& parameters = methodDefinition.parameters();
        NodeLayout::collectChildren(unit.nodeBuffer, methodLayout.parameters, m_childrenScratch);
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto location = std::optional<SourceLocation>{};
            if (i < m_childrenScratch.size())
            {
                location = NodeSpans::parameterLocation(unit.nodeBuffer, tokens, m_childrenScratch[i]);
            }
            addVariableBinding(internName(parameters[i].name()), parameters[i].type(), location, unit.source, VariableBindingKind::Parameter);
        }

        const auto& returnTypes = methodDefinition.returnTypes();
        if (returnTypes.size() == 1)
        {
            m_currentReturnType = returnTypes[0];
        }
        else if (returnTypes.size() > 1)
        {
            TODO("Handle multiple return types in function definition");
        }

        m_currentFunctionName = methodDefinition.fullName();
        typeCheckBlockNode(unit, methodLayout.body);
        m_currentFunctionName.clear();

        collectMethodAnnotationsBefore(unit, index, m_module.getTypeDefinition(m_currentType), m_annotationScratch);
        auto isExtern = false;
        for (const auto annotation : m_annotationScratch)
        {
            if (nameOf(unit, annotation) == ExternAnnotationName)
            {
                isExtern = true;
                break;
            }
        }

        if (!isExtern
            && m_currentReturnType != Type::Void()
            && m_currentReturnType != Type::Undefined()
            && !StatementGuaranteesReturn(unit.nodeBuffer, tokens, methodLayout.body))
        {
            m_diagnostics.addMissingReturnError(
                unit.source,
                nameLocation(unit, methodLayout.methodName),
                // the body walk can move the definition store, resolve again
                m_module.getFunctionDefinition(methodType).name(),
                formatTypeName(m_module, m_currentReturnType));
        }

        popScope(!isExtern);
    }

    void TypeChecker::typeCheckIfStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto ifLayout = NodeLayout::ifStatement(unit.nodeBuffer, index);

        auto conditionType = typeCheckExpression(unit, ifLayout.condition);
        conditionType = coerceConditionType(conditionType, unit, ifLayout.condition);
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolIfConditionError(
                unit.source,
                NodeSpans::expressionLocation(unit.nodeBuffer, tokens, ifLayout.condition),
                formatTypeName(m_module, conditionType));
        }

        typeCheckStatement(unit, ifLayout.trueStatement);

        if (ifLayout.hasFalse)
        {
            typeCheckStatement(unit, ifLayout.falseStatement);
        }
    }

    void TypeChecker::typeCheckTrailingIfStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto trailingIfLayout = NodeLayout::trailingIf(unit.nodeBuffer, index);

        auto conditionType = typeCheckExpression(unit, trailingIfLayout.condition);
        conditionType = coerceConditionType(conditionType, unit, trailingIfLayout.condition);
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolIfConditionError(
                unit.source,
                NodeSpans::expressionLocation(unit.nodeBuffer, tokens, trailingIfLayout.condition),
                formatTypeName(m_module, conditionType));
        }

        typeCheckStatement(unit, trailingIfLayout.statement);
    }

    void TypeChecker::typeCheckWhileStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto whileLayout = NodeLayout::whileStatement(unit.nodeBuffer, index);

        auto conditionType = typeCheckExpression(unit, whileLayout.condition);
        conditionType = coerceConditionType(conditionType, unit, whileLayout.condition);
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolWhileConditionError(
                unit.source,
                NodeSpans::expressionLocation(unit.nodeBuffer, tokens, whileLayout.condition),
                formatTypeName(m_module, conditionType));
        }

        typeCheckStatement(unit, whileLayout.body);
    }

    void TypeChecker::typeCheckReturnStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto declaredReturnType = m_currentReturnType;

        const auto value = NodeLayout::optionalChild(unit.nodeBuffer, index);
        if (value.hasChild)
        {
            auto type = Type::Undefined();
            {
                auto context = ExpressionContext{};
                context.contextualNumberType = contextualTypeForExpectedType(declaredReturnType);
                type = typeCheckExpression(unit, value.child, context);
            }
            if (type.isReference())
            {
                // returning a ref is not allowed, so we need to coerce it to a value
                type = type.toValue();
            }

            noteType(unit, index, type);
            if (type == Type::Undefined())
            {
                return;
            }

            if (!isAssignableTo(type, declaredReturnType))
            {
                const auto location = NodeSpans::expressionLocation(unit.nodeBuffer, tokens, value.child);
                if (!tryAddArrayLengthMismatchError(unit, location, declaredReturnType, type))
                {
                    m_diagnostics.addReturnTypeMismatchError(
                        unit.source,
                        location,
                        formatTypeName(m_module, declaredReturnType),
                        formatTypeName(m_module, type));
                }
            }
        }
        else
        {
            noteType(unit, index, Type::Void());

            if (declaredReturnType != Type::Void())
            {
                const auto keywordToken = unit.nodeBuffer.mainToken(index);
                const auto semicolonToken = NodeSpans::findCloserToken(tokens, keywordToken + 1, TokenKind::Semicolon);
                const auto& returnKeywordLocation = TokenLocationAt(tokens, keywordToken);
                const auto& semicolonLocation = TokenLocationAt(tokens, semicolonToken);
                m_diagnostics.addReturnTypeMismatchError(
                    unit.source,
                    SourceLocation{ returnKeywordLocation.startIndex, semicolonLocation.endIndex },
                    formatTypeName(m_module, declaredReturnType),
                    formatTypeName(m_module, Type::Void()));
            }
        }
    }

    void TypeChecker::typeCheckBlockNode(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto childrenStart = m_childrenStack.size();
        NodeLayout::appendChildren(unit.nodeBuffer, index, m_childrenStack);
        const auto childrenEnd = m_childrenStack.size();

        auto reachedTerminator = false;
        auto warnedUnreachable = false;
        for (auto position = childrenStart; position < childrenEnd; ++position)
        {
            const auto child = m_childrenStack[position];
            if (unit.nodeBuffer.kind(child) == NodeKind::AnnotationNode)
            {
                continue;
            }

            if (reachedTerminator && !warnedUnreachable)
            {
                if (const auto location = NodeSpans::deadStatementLocation(unit.nodeBuffer, tokens, child); location.has_value())
                {
                    m_diagnostics.addUnreachableCodeWarning(unit.source, location.value());
                    warnedUnreachable = true;
                }
            }

            typeCheckStatement(unit, child);

            if (IsTerminatingStatement(unit.nodeBuffer, child))
            {
                reachedTerminator = true;
            }
        }

        m_childrenStack.resize(childrenStart);
    }

    Type TypeChecker::typeCheckExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        switch (unit.nodeBuffer.kind(index))
        {
            case NodeKind::StringLiteral:
            {
                auto literalType = m_module.wellKnown().string;
                if (literalType == Type::Undefined()
                    || (context.contextualNumberType.has_value() && context.contextualNumberType.value() == m_module.wellKnown().cstring))
                {
                    // string literals are cstrings for FFI function calls
                    literalType = m_module.wellKnown().cstring;
                }

                if (!m_isPreludePass && literalType == m_module.wellKnown().string)
                {
                    m_module.markPreludeTypeRequired(literalType);
                }

                return noteType(unit, index, literalType);
            }
            case NodeKind::BoolLiteral:
            {
                return noteType(unit, index, m_module.wellKnown().boolean);
            }
            case NodeKind::NumberLiteral:
            {
                return typeCheckNumberLiteral(unit, index, context);
            }
            case NodeKind::ArrayLiteral:
            {
                return typeCheckArrayLiteral(unit, index, context);
            }
            case NodeKind::GroupingExpression:
            {
                return typeCheckGroupingExpression(unit, index, context);
            }
            case NodeKind::UnaryExpression:
            {
                return typeCheckUnaryExpression(unit, index, context);
            }
            case NodeKind::BinaryExpression:
            {
                return typeCheckBinaryExpression(unit, index, context);
            }
            case NodeKind::NameExpression:
            {
                return typeCheckNameExpression(unit, index, context);
            }
            case NodeKind::FunctionCallExpression:
            {
                return typeCheckFunctionCallExpression(unit, index, context);
            }
            case NodeKind::MemberAccessExpression:
            {
                return typeCheckMemberAccessExpression(unit, index, context);
            }
            case NodeKind::DiscardLiteral:
            {
                return noteType(unit, index, Type::Discard());
            }
            default:
            {
                TODO("Missing Expression!!");
            }
        }
        return Type::Undefined();
    }

    Type TypeChecker::typeCheckGroupingExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        auto type = typeCheckExpression(unit, NodeLayout::groupedExpression(index), context);
        return noteType(unit, index, type);
    }

    Type TypeChecker::typeCheckUnaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto unaryOperator = ConvertToUnaryOperator(tokens.getToken(static_cast<i32>(buffer.mainToken(index))).kind);
        switch (unaryOperator)
        {
            case UnaryOperatorKind::LogicalNegation:
            {
                auto type = typeCheckExpression(unit, NodeLayout::unaryOperand(index), context);

                const auto operandType = type.toValue();
                if (operandType != Type::Undefined()
                    && m_module.tryGetOperatorSignature(operandType, UnaryOperatorKind::LogicalNegation) == nullptr)
                {
                    m_diagnostics.addUnaryOperandTypeMismatchError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        "!",
                        formatTypeName(m_module, operandType),
                        "a 'bool' operand");

                    return noteType(unit, index, Type::Undefined());
                }

                return noteType(unit, index, type);
            }
            case UnaryOperatorKind::ValueNegation:
            {
                const auto operand = NodeLayout::unaryOperand(index);

                auto operandContext = context;
                operandContext.negatedLiteral = buffer.kind(operand) == NodeKind::NumberLiteral;
                const auto type = typeCheckExpression(unit, operand, operandContext);
                const auto operandType = type.toValue();
                if (operandType != Type::Undefined()
                    && m_module.tryGetOperatorSignature(operandType, UnaryOperatorKind::ValueNegation) == nullptr)
                {
                    m_diagnostics.addUnaryOperandTypeMismatchError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        "-",
                        formatTypeName(m_module, operandType),
                        "a numeric operand");

                    return noteType(unit, index, Type::Undefined());
                }

                return noteType(unit, index, type);
            }
            case UnaryOperatorKind::ReferenceOf:
            {
                const auto operand = NodeLayout::unaryOperand(index);
                auto type = typeCheckExpression(unit, operand, context);
                if (type.isReference())
                {
                    m_diagnostics.addAlreadyReferenceError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index));
                    return Type::Undefined();
                }

                const auto strippedOperand = NodeLayout::stripGroupings(buffer, operand);
                auto operandIsMemberAccess = false;
                if (buffer.kind(strippedOperand) == NodeKind::BinaryExpression)
                {
                    const auto operandOperator = ConvertToBinaryOperator(tokens.getToken(static_cast<i32>(buffer.mainToken(strippedOperand))).kind);
                    const auto operandLayout = NodeLayout::binary(buffer, strippedOperand);
                    operandIsMemberAccess = operandOperator == BinaryOperatorKind::MemberAccess
                        && buffer.kind(operandLayout.right) == NodeKind::NameExpression;
                }
                if (buffer.kind(strippedOperand) != NodeKind::NameExpression && !operandIsMemberAccess)
                {
                    // cant return references to temporaries
                    m_diagnostics.addReferenceToNonVariableError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index));
                    return Type::Undefined();
                }

                if (operandIsMemberAccess)
                {
                    auto rootName = std::string{};
                    auto rootReferencesConstant = false;
                    if (receiverChainIsConstant(unit, strippedOperand, rootName, rootReferencesConstant))
                    {
                        // TODO set const ref flag
                    }
                }
                else if (const auto* operandBinding = tryGetBinding(nameIdOf(unit, strippedOperand)); operandBinding != nullptr && operandBinding->kind == VariableBindingKind::LocalConstant)
                {
                    // TODO set const ref flag
                }

                auto referenceType = type.toReference();
                return noteType(unit, index, referenceType);
            }
            default:
            {
                TODO("Missing UnaryOperatorKind!!");
                return Type::Undefined();
            }
        }
    }

    Type TypeChecker::typeCheckBinaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto binaryOperator = ConvertToBinaryOperator(tokens.getToken(static_cast<i32>(buffer.mainToken(index))).kind);
        const auto binaryLayout = NodeLayout::binary(buffer, index);
        switch (binaryOperator)
        {
            case BinaryOperatorKind::MemberAccess:
            {
                auto leftType = typeCheckExpression(unit, binaryLayout.left, context);
                if (leftType.kind() == TypeKind::Enum)
                {
                    auto& enumDefinition = m_module.getEnumDefinition(leftType);
                    if (buffer.kind(binaryLayout.right) == NodeKind::NameExpression)
                    {
                        const auto fieldName = nameOf(unit, binaryLayout.right);
                        if (!enumDefinition.hasField(fieldName))
                        {
                            m_diagnostics.addUnknownEnumFieldError(
                                unit.source,
                                nameLocation(unit, binaryLayout.right),
                                enumDefinition.name(),
                                std::string(fieldName));
                            return Type::Undefined();
                        }

                        noteType(unit, index, leftType);
                        noteType(unit, binaryLayout.right, leftType);
                        return leftType;
                    }

                    m_diagnostics.addInvalidEnumMemberAccessError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                        enumDefinition.name());
                    return Type::Undefined();
                }
                else if (leftType.kind() == TypeKind::Type)
                {
                    auto& typeDefinition = m_module.getTypeDefinition(leftType);

                    if (buffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression)
                    {
                        const auto nameExpressionIndex = NodeLayout::subtreeBegin(buffer, binaryLayout.right);
                        const auto name = std::string(nameOf(unit, nameExpressionIndex));

                        auto methodType = typeDefinition.tryGetMethodTypeByName(name);
                        if (methodType != Type::Undefined())
                        {
                            auto& methodDefinition = m_module.getFunctionDefinition(methodType);
                            if (methodDefinition.symbolName().has_value())
                            {
                                m_module.markExternRequired(methodType);
                            }

                            if (!m_isPreludePass)
                            {
                                m_module.markPreludeTypeRequired(leftType);
                                for (const auto callReturnType : methodDefinition.returnTypes())
                                {
                                    m_module.markPreludeTypeRequired(callReturnType);
                                }
                            }

                            if (methodDefinition.functionType() == FunctionType::PrivateMethod && m_currentType != leftType)
                            {
                                m_diagnostics.addPrivateMethodCallOutsideTypeError(
                                    unit.source,
                                    ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                                    name,
                                    typeDefinition.name());
                                return Type::Undefined();
                            }

                            if (!typeCheckCallArguments(unit, binaryLayout.right, methodType, context))
                            {
                                return Type::Undefined();
                            }

                            // the argument walk can move the definition store, resolve again
                            const auto& checkedMethodDefinition = m_module.getFunctionDefinition(methodType);
                            const auto& returnTypes = checkedMethodDefinition.returnTypes();
                            Type returnType = Type::Void();
                            if (returnTypes.size() == 1)
                            {
                                returnType = returnTypes[0];
                            }
                            else if (returnTypes.size() > 1)
                            {
                                TODO("Handle multiple return types");
                            }

                            if (checkedMethodDefinition.functionType() == FunctionType::SynthesizedConstructor)
                            {
                                noteType(unit, index, leftType);
                                noteType(unit, binaryLayout.right, leftType);
                                noteType(unit, nameExpressionIndex, methodType);
                                return leftType;
                            }

                            noteType(unit, index, returnType);
                            noteType(unit, binaryLayout.right, returnType);
                            noteType(unit, nameExpressionIndex, methodType);
                            return returnType;
                        }
                        else
                        {
                            m_diagnostics.addUnknownMethodError(
                                unit.source,
                                ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                                typeDefinition.name(),
                                name);
                            return Type::Undefined();
                        }
                    }
                    else if (buffer.kind(binaryLayout.right) == NodeKind::NameExpression)
                    {
                        const auto fieldName = nameOf(unit, binaryLayout.right);
                        const auto& fieldDefinition = typeDefinition.tryGetFieldByName(fieldName);
                        if (fieldDefinition.type() == Type::Undefined())
                        {
                            m_diagnostics.addUnknownFieldError(
                                unit.source,
                                nameLocation(unit, binaryLayout.right),
                                typeDefinition.name(),
                                std::string(fieldName));
                            return Type::Undefined();
                        }

                        // trying to access private fields isnt allowed
                        if (!fieldName.empty() && fieldName.front() == '_' && m_currentType != leftType)
                        {
                            m_diagnostics.addPrivateFieldAccessOutsideTypeError(
                                unit.source,
                                nameLocation(unit, binaryLayout.right),
                                std::string(fieldName),
                                typeDefinition.name());
                            return Type::Undefined();
                        }

                        auto fieldType = fieldDefinition.type();
                        noteType(unit, binaryLayout.right, fieldType);
                        noteType(unit, index, fieldType);
                        return fieldType;
                    }
                }
                else if (leftType.kind() == TypeKind::FixedArray
                    || leftType.kind() == TypeKind::DynamicArray
                    || leftType.kind() == TypeKind::Slice)
                {
                    const auto receiverType = leftType.toValue();
                    if (buffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression)
                    {
                        const auto nameExpressionIndex = NodeLayout::subtreeBegin(buffer, binaryLayout.right);
                        const auto name = std::string(nameOf(unit, nameExpressionIndex));

                        auto methodType = m_module.tryGetOrCreateArrayIntrinsic(receiverType, name);
                        if (methodType == Type::Undefined())
                        {
                            if (name == "add" || name == "remove")
                            {
                                const auto elementType = m_module.getArrayElementType(receiverType);
                                const auto dynamicTypeName = "[" + formatTypeName(m_module, elementType) + "; _]";
                                m_diagnostics.addMethodRequiresDynamicArrayError(
                                    unit.source,
                                    ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                                    formatTypeName(m_module, receiverType),
                                    dynamicTypeName);
                            }
                            else
                            {
                                m_diagnostics.addUnknownMethodError(
                                    unit.source,
                                    ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                                    formatTypeName(m_module, receiverType),
                                    name);
                            }
                            return Type::Undefined();
                        }

                        auto& methodDefinition = m_module.getFunctionDefinition(methodType);
                        if ((methodDefinition.intrinsicKind() == IntrinsicKind::ArraySet
                            || methodDefinition.intrinsicKind() == IntrinsicKind::ArrayAdd
                            || methodDefinition.intrinsicKind() == IntrinsicKind::ArrayRemove))
                        {
                            if (m_module.isImmutableSlice(receiverType))
                            {
                                m_diagnostics.addMutatingMethodOnImmutableSliceError(
                                    unit.source,
                                    ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.left),
                                    name);
                                return Type::Undefined();
                            }

                            auto rootName = std::string{};
                            auto rootReferencesConstant = false;
                            if (receiverChainIsConstant(unit, binaryLayout.left, rootName, rootReferencesConstant))
                            {
                                if (rootReferencesConstant)
                                {
                                    m_diagnostics.addMutatingMethodThroughConstantReferenceError(
                                        unit.source,
                                        ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.left),
                                        rootName,
                                        name);
                                }
                                else
                                {
                                    m_diagnostics.addMutatingMethodOnConstantError(
                                        unit.source,
                                        ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.left),
                                        rootName,
                                        name);
                                }
                            }
                        }

                        return finishResolvedMethodCall(unit, index, binaryLayout.right, nameExpressionIndex, methodType, context);
                    }
                    else if (buffer.kind(binaryLayout.right) == NodeKind::NameExpression)
                    {
                        const auto memberName = nameOf(unit, binaryLayout.right);
                        if (memberName == ArrayLengthMemberName)
                        {
                            const auto lengthType = m_module.wellKnown().i32;
                            noteType(unit, binaryLayout.right, lengthType);
                            noteType(unit, index, lengthType);
                            return lengthType;
                        }

                        m_diagnostics.addUnknownFieldError(
                            unit.source,
                            nameLocation(unit, binaryLayout.right),
                            formatTypeName(m_module, receiverType),
                            std::string(memberName));
                        return Type::Undefined();
                    }
                }
                else if (leftType.kind() == TypeKind::Builtin
                    && buffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression)
                {
                    const auto nameExpressionIndex = NodeLayout::subtreeBegin(buffer, binaryLayout.right);
                    const auto name = std::string(nameOf(unit, nameExpressionIndex));

                    // only intrinsic methods are callable on builtin types, operator declarations are just signatures
                    auto& typeDefinition = m_module.getTypeDefinition(leftType);
                    const auto methodType = typeDefinition.tryGetMethodTypeByName(name);
                    auto isIntrinsic = false;
                    if (methodType != Type::Undefined())
                    {
                        isIntrinsic = m_module.getFunctionDefinition(methodType).functionType() == FunctionType::Intrinsic;
                    }

                    if (!isIntrinsic)
                    {
                        m_diagnostics.addUnknownMethodError(
                            unit.source,
                            ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                            formatTypeName(m_module, leftType.toValue()),
                            name);
                        return Type::Undefined();
                    }

                    return finishResolvedMethodCall(unit, index, binaryLayout.right, nameExpressionIndex, methodType, context);
                }

                if (leftType != Type::Undefined())
                {
                    m_diagnostics.addInvalidMemberAccessReceiverError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        formatTypeName(m_module, leftType));
                }
                return Type::Undefined();
            }
            case BinaryOperatorKind::AdditionWrapping:
            case BinaryOperatorKind::SubtractionWrapping:
            case BinaryOperatorKind::MultiplicationWrapping:
            case BinaryOperatorKind::Division:
            {
                auto leftType = typeCheckExpression(unit, binaryLayout.left, context);
                if (leftType.isReference())
                {
                    leftType = leftType.toValue();
                }
                auto rightType = typeCheckExpression(unit, binaryLayout.right, context);
                if (rightType.isReference())
                {
                    rightType = rightType.toValue();
                }

                // TODO we need to be able look up the resulting type for a binary expression,
                // for now we'll just make sure left and right have the same type and use that one
                if (leftType != rightType)
                {
                    if (leftType != Type::Undefined() && rightType != Type::Undefined())
                    {
                        m_diagnostics.addArithmeticOperandTypeMismatchError(
                            unit.source,
                            nameLocation(unit, index),
                            formatBinaryOperator(binaryOperator),
                            formatTypeName(m_module, leftType),
                            formatTypeName(m_module, rightType));
                    }

                    return noteType(unit, index, Type::Undefined());
                }

                const auto* operatorSignature = m_module.tryGetOperatorSignature(leftType, binaryOperator);
                if (operatorSignature == nullptr)
                {
                    m_diagnostics.addBinaryOperandTypeMismatchError(
                        unit.source,
                        nameLocation(unit, index),
                        formatBinaryOperator(binaryOperator),
                        formatTypeName(m_module, leftType),
                        "numeric operands");

                    return noteType(unit, index, Type::Undefined());
                }

                return noteType(unit, index, operatorSignature->resultType);
            }
            case BinaryOperatorKind::Equal:
            case BinaryOperatorKind::NotEqual:
            case BinaryOperatorKind::LessThan:
            case BinaryOperatorKind::LessOrEqual:
            case BinaryOperatorKind::GreaterThan:
            case BinaryOperatorKind::GreaterOrEqual:
            case BinaryOperatorKind::LogicalAnd:
            case BinaryOperatorKind::LogicalOr:
            {
                auto leftType = typeCheckExpression(unit, binaryLayout.left, context);
                if (leftType.isReference())
                {
                    leftType = leftType.toValue();
                }
                auto rightType = typeCheckExpression(unit, binaryLayout.right, context);
                if (rightType.isReference())
                {
                    rightType = rightType.toValue();
                }

                if (!areComparableTypes(leftType, rightType))
                {
                    if (leftType != Type::Undefined() && rightType != Type::Undefined())
                    {
                        m_diagnostics.addComparisonOperandTypeMismatchError(
                            unit.source,
                            nameLocation(unit, index),
                            formatBinaryOperator(binaryOperator),
                            formatTypeName(m_module, leftType),
                            formatTypeName(m_module, rightType));
                    }

                    return noteType(unit, index, Type::Undefined());
                }

                const auto operation = binaryOperator;

                auto lookupType = leftType;
                if (lookupType.kind() == TypeKind::Enum)
                {
                    lookupType = m_module.getEnumDefinition(lookupType).baseType();
                }

                const auto* operatorSignature = m_module.tryGetOperatorSignature(lookupType, operation);
                if (operatorSignature == nullptr)
                {
                    const auto* expectedOperands = "numeric operands";
                    if (operation == BinaryOperatorKind::LogicalAnd || operation == BinaryOperatorKind::LogicalOr)
                    {
                        expectedOperands = "'bool' operands";
                    }
                    else if (operation == BinaryOperatorKind::Equal || operation == BinaryOperatorKind::NotEqual)
                    {
                        expectedOperands = "operands that can be compared for equality";
                    }

                    m_diagnostics.addBinaryOperandTypeMismatchError(
                        unit.source,
                        nameLocation(unit, index),
                        formatBinaryOperator(operation),
                        formatTypeName(m_module, leftType),
                        expectedOperands);

                    return noteType(unit, index, Type::Undefined());
                }

                auto resultType = operatorSignature->resultType;
                return noteType(unit, index, resultType);
            }
            default:
            {
                TODO("Missing BinaryOperatornKind!!");
                return Type::Undefined();
            }
        }

        return Type::Undefined();
    }

    Type TypeChecker::typeCheckNameExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        // one lookup covers the type, the kind and marking it read
        if (auto* binding = tryGetBindingMutable(nameIdOf(unit, index)); binding != nullptr)
        {
            const auto isLeadingDotRoot = m_leadingDotRootPending;
            m_leadingDotRootPending = false;
            if (!isLeadingDotRoot && binding->kind == VariableBindingKind::Field)
            {
                m_diagnostics.addFieldAccessMissingDotError(unit.source, nameLocation(unit, index), std::string(nameOf(unit, index)));
                return Type::Undefined();
            }

            binding->wasRead = true;
            return noteType(unit, index, binding->type);
        }

        const auto name = nameOf(unit, index);
        auto type = m_module.tryGetTypeByName(name);
        if (type != Type::Undefined())
        {
            return noteType(unit, index, type);
        }

        m_diagnostics.addUnknownNameError(unit.source, nameLocation(unit, index), std::string(name));

        return Type::Undefined();
    }

    Type TypeChecker::typeCheckFunctionCallExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        const auto nameExpressionIndex = NodeLayout::subtreeBegin(unit.nodeBuffer, index);
        const auto name = nameOf(unit, nameExpressionIndex);
        auto functionType = m_module.tryGetFunctionTypeByName(name);
        if (functionType == Type::Undefined())
        {
            if (m_currentType != Type::Undefined())
            {
                auto& typeDefinition = m_module.getTypeDefinition(m_currentType);
                if (typeDefinition.tryGetMethodTypeByName(name) != Type::Undefined())
                {
                    m_diagnostics.addMethodCallMissingDotError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        std::string(name),
                        typeDefinition.name());
                    return Type::Undefined();
                }
            }

            m_diagnostics.addUnknownFunctionError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index),
                std::string(name));
            return Type::Undefined();
        }

        return typeCheckResolvedFunctionCall(unit, index, functionType, context);
    }

    Type TypeChecker::typeCheckResolvedFunctionCall(const CompilationUnit& unit, u32 index, Type functionType, const ExpressionContext& context)
    {
        const auto& functionDefinition = m_module.getFunctionDefinition(functionType);

        // extern declarations only reach codegen when a call requires them
        if (functionDefinition.symbolName().has_value())
        {
            m_module.markExternRequired(functionType);
        }

        if (!m_isPreludePass)
        {
            for (const auto callReturnType : functionDefinition.returnTypes())
            {
                m_module.markPreludeTypeRequired(callReturnType);
            }
        }

        if (!typeCheckCallArguments(unit, index, functionType, context))
        {
            return Type::Undefined();
        }

        // the argument walk can move the definition store, resolve again
        const auto& returnTypes = m_module.getFunctionDefinition(functionType).returnTypes();
        Type returnType = Type::Void();
        if (returnTypes.size() == 1)
        {
            returnType = returnTypes[0];
        }
        else if (returnTypes.size() > 1)
        {
            TODO("Handle multiple return types");
        }

        noteType(unit, index, returnType);
        noteType(unit, NodeLayout::subtreeBegin(unit.nodeBuffer, index), functionType);
        return returnType;
    }

    Type TypeChecker::finishResolvedMethodCall(const CompilationUnit& unit, u32 index, u32 callIndex, u32 nameExpressionIndex, Type methodType, const ExpressionContext& context)
    {
        if (!typeCheckCallArguments(unit, callIndex, methodType, context))
        {
            return Type::Undefined();
        }

        const auto& returnTypes = m_module.getFunctionDefinition(methodType).returnTypes();
        auto returnType = Type::Void();
        if (returnTypes.size() == 1)
        {
            returnType = returnTypes[0];
        }

        noteType(unit, index, returnType);
        noteType(unit, callIndex, returnType);
        noteType(unit, nameExpressionIndex, methodType);
        return returnType;
    }

    Type TypeChecker::typeCheckMemberAccessExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        if (currentScopeKind() == ScopeKind::StaticMethod)
        {
            m_diagnostics.addMemberAccessInStaticMethodError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index),
                m_currentFunctionName);

            return noteType(unit, index, Type::Undefined());
        }

        if (m_currentType == Type::Undefined())
        {
            m_diagnostics.addMemberAccessInDefaultParameterError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index));

            return noteType(unit, index, Type::Undefined());
        }

        auto& typeDefinition = m_module.getTypeDefinition(m_currentType);
        const auto inner = NodeLayout::memberExpression(index);
        if (unit.nodeBuffer.kind(inner) == NodeKind::FunctionCallExpression)
        {
            const auto nameExpressionIndex = NodeLayout::subtreeBegin(unit.nodeBuffer, inner);
            const auto methodName = nameOf(unit, nameExpressionIndex);
            const auto methodType = typeDefinition.tryGetMethodTypeByName(methodName);
            if (methodType == Type::Undefined())
            {
                m_diagnostics.addUnknownMethodError(
                    unit.source,
                    ExpressionSourceLocation(m_compilationContext, unit, inner),
                    typeDefinition.name(),
                    std::string(methodName));

                return noteType(unit, index, Type::Undefined());
            }

            const auto type = typeCheckResolvedFunctionCall(unit, inner, methodType, context);
            return noteType(unit, index, type);
        }

        m_leadingDotRootPending = true;
        auto type = typeCheckExpression(unit, inner, context);

        return noteType(unit, index, type);
    }

    bool TypeChecker::typeCheckCallArguments(const CompilationUnit& unit, u32 callIndex, Type functionType, const ExpressionContext& context)
    {
        const auto& buffer = unit.nodeBuffer;

        // the argument walk below can grow the definition store, resolve again
        const auto& functionDefinition = m_module.getFunctionDefinition(functionType);
        const auto& parameterTypes = functionDefinition.parameters();
        auto isVariadic = functionDefinition.isVariadic();
        auto parameterCount = parameterTypes.size();
        if (isVariadic)
        {
            parameterCount = parameterTypes.size() - 1;
        }

        auto isExternCallComputed = false;
        auto isExternCall = false;
        // static intrinsics like i32.bitAnd have no receiver, array intrinsics carry an explicit .this parameter
        const bool hasImplicitThis =
            functionDefinition.functionType() == FunctionType::SynthesizedConstructor ||
            functionDefinition.functionType() == FunctionType::PublicMethod ||
            functionDefinition.functionType() == FunctionType::PrivateMethod ||
            (functionDefinition.functionType() == FunctionType::Intrinsic
                && !functionDefinition.parameters().empty()
                && functionDefinition.parameters().front().name() == ImplicitThisName);
        size_t parameterOffset = 0;
        if (hasImplicitThis)
        {
            parameterOffset = 1;
        }
        if (parameterCount < parameterOffset)
        {
            TODO("This shouldn't happen");
            return false;
        }

        NodeLayout::collectChildren(buffer, callIndex, m_childrenScratch);
        const auto argumentsStart = m_callArgumentStack.size();
        for (size_t childIndex = 1; childIndex < m_childrenScratch.size(); ++childIndex)
        {
            m_callArgumentStack.push_back(m_childrenScratch[childIndex]);
        }
        const auto argumentsEnd = m_callArgumentStack.size();
        const auto orderedStart = m_orderedSlotStack.size();
        auto popFrames = [&]
        {
            m_orderedSlotStack.resize(orderedStart);
            m_callArgumentStack.resize(argumentsStart);
        };

        auto binding = bindCallArguments(
            m_compilationContext,
            unit,
            callIndex,
            std::span<const u32>{ m_callArgumentStack }.subspan(argumentsStart),
            parameterTypes,
            parameterOffset,
            isVariadic,
            functionDefinition.name(),
            m_diagnostics,
            m_orderedSlotStack,
            orderedStart);
        const auto fixedParameterCount = m_orderedSlotStack.size() - orderedStart;
        if (!binding.ok)
        {
            popFrames();
            return false;
        }

        for (auto argumentPosition = argumentsStart; argumentPosition < argumentsEnd; ++argumentPosition)
        {
            const auto argumentValue = NodeLayout::argumentValue(buffer, m_callArgumentStack[argumentPosition]);
            // resolve again every iteration, the previous argument's walk may have moved the definition store
            const auto& parameters = m_module.getFunctionDefinition(functionType).parameters();

            auto contextualType = std::optional<Type>{};
            auto isVariadicArgument = true;
            for (size_t i = 0; i < fixedParameterCount; ++i)
            {
                if (m_orderedSlotStack[orderedStart + i] != argumentValue)
                {
                    continue;
                }

                contextualType = contextualTypeForExpectedType(parameters[i + parameterOffset].type());
                isVariadicArgument = false;
                break;
            }

            if (isVariadicArgument)
            {
                if (!isExternCallComputed)
                {
                    isExternCallComputed = true;
                    isExternCall = m_module.tryGetExternFunctionByFullName(m_module.getFunctionDefinition(functionType).fullName()) != Type::Undefined();
                }
                if (isExternCall)
                {
                    // extern calls to C need a cstring
                    contextualType = m_module.wellKnown().cstring;
                }
            }

            auto argumentContext = context;
            argumentContext.contextualNumberType = contextualType;
            static_cast<void>(typeCheckExpression(unit, argumentValue, argumentContext));
        }

        const auto expectedArgumentCount = parameterCount - parameterOffset;
        const auto& checkedDefinition = m_module.getFunctionDefinition(functionType);
        const auto& checkedParameters = checkedDefinition.parameters();
        std::vector<ArgumentTypeMismatchInfo> argumentTypeMismatches{};
        for (size_t i = 0; i < fixedParameterCount; ++i)
        {
            const auto argumentNode = m_orderedSlotStack[orderedStart + i];
            if (argumentNode == DefaultBoundArgumentSlot)
            {
                const auto& parameter = checkedParameters[i + parameterOffset];
                if (parameter.hasDefaultNode())
                {
                    const auto defaultNode = parameter.defaultValueNode();
                    const auto& defaultUnit = unitFor(defaultNode);
                    const auto defaultType = m_module.typeOfNode(defaultNode);
                    const auto expectedType = parameter.type();
                    if (!isAssignableTo(defaultType, expectedType))
                    {
                        argumentTypeMismatches.push_back(ArgumentTypeMismatchInfo{
                            ExpressionSourceLocation(m_compilationContext, defaultUnit, defaultNode.index),
                            static_cast<i32>(i + 1),
                            formatTypeName(m_module, expectedType),
                            formatTypeName(m_module, defaultType),
                            });
                    }
                }
                continue;
            }

            const auto argumentType = m_module.typeOfNode(NodeIndex{ unit.fileId, argumentNode });
            const auto expectedType = checkedParameters[i + parameterOffset].type();
            if (!isAssignableTo(argumentType, expectedType))
            {
                argumentTypeMismatches.push_back(ArgumentTypeMismatchInfo{
                    ExpressionSourceLocation(m_compilationContext, unit, argumentNode),
                    static_cast<i32>(i + 1),
                    formatTypeName(m_module, expectedType),
                    formatTypeName(m_module, argumentType),
                    });
            }
        }

        for (size_t j = 0; j < binding.variadic.size(); ++j)
        {
            const auto argumentNode = binding.variadic[j];
            const auto argumentType = m_module.typeOfNode(NodeIndex{ unit.fileId, argumentNode });
            if (argumentType == Type::Undefined() || argumentType == Type::Void())
            {
                m_diagnostics.addInvalidVariadicArgumentTypeError(
                    unit.source,
                    ExpressionSourceLocation(m_compilationContext, unit, argumentNode),
                    checkedDefinition.name(),
                    static_cast<i32>(expectedArgumentCount + j + 1),
                    formatTypeName(m_module, argumentType));
                popFrames();
                return false;
            }
        }

        if (!argumentTypeMismatches.empty())
        {
            const auto argumentLocation = CallArgumentsLocation(
                m_compilationContext,
                unit,
                callIndex,
                std::span<const u32>{ m_callArgumentStack }.subspan(argumentsStart, argumentsEnd - argumentsStart));
            m_diagnostics.addArgumentTypeMismatchError(
                unit.source,
                argumentLocation,
                checkedDefinition.name(),
                argumentTypeMismatches);
            popFrames();
            return false;
        }

        popFrames();
        return true;
    }

    Type TypeChecker::typeCheckNumberLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        auto numberType = Type::Undefined();
        auto parsedValue = std::optional<NumberLiteral::ParsedValue>{};
        const auto lexeme = nameOf(unit, index);
        const auto suffix = NodeLayout::optionalChild(unit.nodeBuffer, index);
        if (suffix.hasChild)
        {
            numberType = typeCheckTypeNameNode(unit, suffix.child);
        }
        else
        {
            const auto isFloatingLiteral = lexeme.find('.') != std::string_view::npos;
            if (context.contextualNumberType.has_value())
            {
                const auto contextualType = context.contextualNumberType.value();
                const auto* contextualDescription = m_module.tryGetBuiltinTypeDescription(contextualType);
                if (!isFloatingLiteral && contextualDescription != nullptr && contextualDescription->kind == BuiltinTypeKind::Int)
                {
                    numberType = contextualType;
                }
            }

            if (numberType == Type::Undefined() && isFloatingLiteral)
            {
                numberType = m_defaultFloatingType;
            }
            else if (numberType == Type::Undefined())
            {
                numberType = m_defaultIntegerType;
            }
        }

        const auto* numberDescription = m_module.tryGetBuiltinTypeDescription(numberType);
        if (numberType != Type::Undefined()
            && numberDescription != nullptr
            && (numberDescription->kind == BuiltinTypeKind::Int || numberDescription->kind == BuiltinTypeKind::Float))
        {
            parsedValue = tryParseNumberLiteralValue(lexeme, numberType, m_module);
            if (!parsedValue.has_value() && context.negatedLiteral
                && numberDescription->kind == BuiltinTypeKind::Int && numberDescription->isSigned && numberDescription->bits == 32)
            {
                const auto negatedValue = tryParseNegatedI32Literal(lexeme);
                if (negatedValue.has_value())
                {
                    parsedValue = NumberLiteral::ParsedValue{ negatedValue.value() };
                }
            }

            if (!parsedValue.has_value())
            {
                m_diagnostics.addNumberLiteralOutOfRangeError(
                    unit.source,
                    nameLocation(unit, index),
                    std::string(lexeme),
                    formatTypeName(m_module, numberType));
                numberType = Type::Undefined();
            }
        }

        return noteType(unit, index, numberType);
    }

    Type TypeChecker::typeCheckArrayLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto isDynamic = buffer.isDynamicArrayLiteral(tokens, index);

        const auto childrenStart = m_childrenStack.size();
        NodeLayout::appendChildren(buffer, index, m_childrenStack);
        const auto childrenEnd = m_childrenStack.size();
        const auto elementCount = childrenEnd - childrenStart;

        if (elementCount == 0)
        {
            // an empty dynamic literal has no elements to infer the type from, it
            // is only allowed where the expected type already says what it holds
            if (isDynamic && context.contextualNumberType.has_value()
                && context.contextualNumberType.value().kind() == TypeKind::DynamicArray)
            {
                const auto arrayType = context.contextualNumberType.value().toValue();
                return noteType(unit, index, arrayType);
            }

            m_diagnostics.addEmptyArrayLiteralError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index));
            return noteType(unit, index, Type::Undefined());
        }

        // an enclosing array type provides the element type, everything else provides none
        auto contextualElementType = std::optional<Type>{};
        if (context.contextualNumberType.has_value())
        {
            const auto contextualType = context.contextualNumberType.value();
            if (contextualType.kind() == TypeKind::FixedArray
                || contextualType.kind() == TypeKind::DynamicArray
                || contextualType.kind() == TypeKind::Slice)
            {
                contextualElementType = m_module.getArrayElementType(contextualType);
            }
        }

        auto elementType = Type::Undefined();
        auto hasElementError = false;
        auto reportedElementMismatch = false;
        {
            auto elementContext = context;
            elementContext.contextualNumberType = contextualElementType;
            for (auto position = childrenStart; position < childrenEnd; ++position)
            {
                const auto element = m_childrenStack[position];
                const auto type = typeCheckExpression(unit, element, elementContext);
                if (type == Type::Undefined())
                {
                    hasElementError = true;
                    continue;
                }

                if (elementType == Type::Undefined())
                {
                    elementType = type;
                }
                else if (type != elementType)
                {
                    if (!reportedElementMismatch)
                    {
                        m_diagnostics.addArrayElementTypeMismatchError(
                            unit.source,
                            ExpressionSourceLocation(m_compilationContext, unit, element),
                            formatTypeName(m_module, elementType),
                            formatTypeName(m_module, type));
                        reportedElementMismatch = true;
                    }
                    hasElementError = true;
                }
            }
        }
        m_childrenStack.resize(childrenStart);

        if (hasElementError || elementType == Type::Undefined())
        {
            return noteType(unit, index, Type::Undefined());
        }

        // ownership rule: arrays cant store references or slices
        if (elementType.isReference() || elementType.kind() == TypeKind::Slice)
        {
            m_diagnostics.addReferenceOrSliceArrayElementError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index));
        }
        else if (elementType.kind() == TypeKind::DynamicArray)
        {
            m_diagnostics.addDynamicArrayElementError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index));
        }

        auto arrayType = Type::Undefined();
        if (isDynamic)
        {
            arrayType = m_module.getOrCreateArrayType(TypeKind::DynamicArray, elementType, 0);
        }
        else
        {
            const auto length = static_cast<i32>(elementCount);
            arrayType = m_module.getOrCreateArrayType(TypeKind::FixedArray, elementType, length);
        }
        return noteType(unit, index, arrayType);
    }

    Type TypeChecker::coerceConditionType(Type conditionType, const CompilationUnit& unit, u32 conditionIndex)
    {
        if (conditionType == m_module.wellKnown().boolean)
        {
            return conditionType;
        }

        if (conditionType.kind() == TypeKind::Enum)
        {
            auto& enumDefinition = m_module.getEnumDefinition(conditionType);
            if (enumDefinition.baseType() == m_module.wellKnown().boolean)
            {
                noteType(unit, conditionIndex, m_module.wellKnown().boolean);
                return m_module.wellKnown().boolean;
            }
        }

        return conditionType;
    }

    bool TypeChecker::isAssignableTo(Type sourceType, Type targetType) const noexcept
    {
        if (sourceType == targetType)
        {
            return true;
        }

        // assigning mutable to immutable slice is allowed
        // assigning immutable to mutable slice is not allowed
        if (sourceType.kind() == TypeKind::Slice && targetType.kind() == TypeKind::Slice
            && !sourceType.isReference() && !targetType.isReference()
            && m_module.isImmutableSlice(targetType) && !m_module.isImmutableSlice(sourceType)
            && m_module.getArrayElementType(sourceType) == m_module.getArrayElementType(targetType))
        {
            return true;
        }

        // numbers are only allowed to be assigned to a bigger type and only same signedness
        if (sourceType.isBaseType() && targetType.isBaseType())
        {
            const auto* sourceDescription = m_module.tryGetBuiltinTypeDescription(sourceType);
            const auto* targetDescription = m_module.tryGetBuiltinTypeDescription(targetType);
            if (sourceDescription != nullptr && targetDescription != nullptr
                && sourceDescription->kind == targetDescription->kind
                && sourceDescription->bits < targetDescription->bits)
            {
                if (sourceDescription->kind == BuiltinTypeKind::Int)
                {
                    return sourceDescription->isSigned == targetDescription->isSigned;
                }
                if (sourceDescription->kind == BuiltinTypeKind::Float)
                {
                    return true;
                }
            }
        }

        return false;
    }

    std::optional<Type> TypeChecker::contextualTypeForExpectedType(Type expectedType) const noexcept
    {
        if (expectedType.kind() == TypeKind::FixedArray
            || expectedType.kind() == TypeKind::DynamicArray
            || expectedType.kind() == TypeKind::Slice)
        {
            return expectedType;
        }

        const auto* description = m_module.tryGetBuiltinTypeDescription(expectedType);
        if (description != nullptr && description->kind == BuiltinTypeKind::Int)
        {
            return expectedType;
        }

        // string literals stay cstrings for FFI
        if (expectedType == m_module.wellKnown().cstring)
        {
            return expectedType;
        }

        return std::nullopt;
    }

    bool TypeChecker::tryAddArrayLengthMismatchError(const CompilationUnit& unit, const SourceLocation& location, Type expectedType, Type actualType)
    {
        if (expectedType.kind() != TypeKind::FixedArray || actualType.kind() != TypeKind::FixedArray)
        {
            return false;
        }

        if (!expectedType.isBaseType() || !actualType.isBaseType())
        {
            return false;
        }

        if (m_module.getArrayElementType(expectedType) != m_module.getArrayElementType(actualType))
        {
            return false;
        }

        m_diagnostics.addArrayLengthMismatchError(
            unit.source,
            location,
            formatTypeName(m_module, expectedType),
            m_module.getArrayLength(expectedType),
            m_module.getArrayLength(actualType));
        return true;
    }

    bool TypeChecker::areComparableTypes(Type leftType, Type rightType)
    {
        if (leftType == rightType)
        {
            return true;
        }

        if (leftType.kind() == TypeKind::Enum)
        {
            auto& enumDefinition = m_module.getEnumDefinition(leftType);
            if (enumDefinition.baseType() == rightType)
            {
                return true;
            }
        }

        if (rightType.kind() == TypeKind::Enum)
        {
            auto& enumDefinition = m_module.getEnumDefinition(rightType);
            if (enumDefinition.baseType() == leftType)
            {
                return true;
            }
        }

        return false;
    }
}
