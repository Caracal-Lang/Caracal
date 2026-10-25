#include <Caracal/Semantic/TypeChecker.h>
#include <Caracal/Profiling.h>

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/ScopedValue.h>
#include <Caracal/Semantic/ArgumentBinder.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Semantic/OperatorRegistration.h>
#include <Caracal/Semantic/SliceParameterPromotion.h>
#include <Caracal/Syntax/BinaryOperatorKind.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/StringEscapes.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Syntax/UnaryOperatorKind.h>

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
    struct CallArgumentScope
    {
        std::vector<ValueRef>& stack;
        size_t start;

        ~CallArgumentScope()
        {
            stack.resize(start);
        }
    };

    static bool ShouldIgnoreUnusedVariableWarning(std::string_view name)
    {
        return name == "_" || name == ImplicitThisName;
    }

    [[nodiscard]] static u64 ArrayLiteralKey(u16 fileId, u32 literalIndex) noexcept
    {
        return (static_cast<u64>(fileId) << 32) | literalIndex;
    }

    [[nodiscard]] static FoldValue MakeFoldValue(const NumberLiteralValue& parsedValue) noexcept
    {
        return std::visit([](const auto value) { return FoldValue{ value }; }, parsedValue);
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
        DiagnosticsBag& diagnostics,
        Module& irModule)
        : m_compilationContext{ compilationContext }
        , m_isPreludePass{ isPreludePass }
        , m_module{ compilationContext.semanticContext() }
        , m_emitter{ compilationContext.semanticContext(), irModule }
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
        m_emitter.setNameLexemes(m_nameLexemes);
        m_thisNameId = internName(ImplicitThisName);
        m_emitter.setPreludeMode(isPreludePass);
        if (m_options.recordsSourceLocations)
        {
            m_emitter.recordSourceLines();
        }
    }

    bool TypeChecker::typeCheck()
    {
        CARACAL_ZONE_NAMED("check.typeCheck");
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

        if (!m_isPreludePass)
        {
            m_emitter.finishModule();
        }

        if (m_emitter.emissionFailed() && !m_diagnostics.hasErrors())
        {
            return false;
        }

        return !m_diagnostics.hasErrors();
    }

    void TypeChecker::collectDeclarations()
    {
        CARACAL_ZONE_NAMED("check.collectDeclarations");
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
            locateDefinitionName(
                existingDefinition.definitionNode(),
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
        auto& functionDefinition = m_module.createFunction(functionName, std::move(parameters), std::vector<Type>());
        functionDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, functionDefinition.type());
        m_functionDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    void TypeChecker::reportDuplicateTypeDeclaration(const CompilationUnit& unit, u32 index, std::string_view name, Type existingType)
    {
        auto otherSource = SourceTextSharedPtr{};
        auto otherLocation = std::optional<SourceLocation>{};
        auto otherDefinitionNode = NodeIndex{};
        if (existingType.kind() == TypeKind::Enum)
        {
            otherDefinitionNode = m_module.getEnumDefinition(existingType).definitionNode();
        }
        else if (existingType.kind() == TypeKind::Type)
        {
            otherDefinitionNode = m_module.getTypeDefinition(existingType).definitionNode();
        }
        locateDefinitionName(
            otherDefinitionNode,
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

        auto& enumDefinition = m_module.createEnum(enumName);
        enumDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, enumDefinition.type());
        m_enumDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    void TypeChecker::collectBuiltinTypeDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto typeName = nameOf(unit, index);
        const auto builtinType = m_module.tryGetTypeByName(typeName);
        if (builtinType == Type::Undefined())
        {
            const auto description = extractPendingBuiltinTypeDescription(unit);
            auto& typeDefinition = m_module.createBuiltinTypeFromDescription(typeName, description);
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
            locateDefinitionName(
                existingDefinition.definitionNode(),
                otherSource,
                otherLocation);

            m_diagnostics.addDuplicateBuiltinTypeBindingError(
                unit.source,
                nameLocation(unit, index),
                std::string(typeName),
                otherLocation);
            return;
        }

        auto& typeDefinition = m_module.bindBuiltinTypeDefinition(builtinType);
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

        auto& typeDefinition = m_module.createType(typeName);
        typeDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, typeDefinition.type());
        m_typeDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    const std::vector<u32>& TypeChecker::collectAddressTakenNameIds(const CompilationUnit& unit, u32 bodyIndex)
    {
        CARACAL_ZONE_NAMED("check.addressTakenScan");
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        auto& nameIds = m_addressTakenScratch;
        nameIds.clear();
        for (auto i = NodeLayout::subtreeBegin(unit.nodeBuffer, bodyIndex); i <= bodyIndex; ++i)
        {
            if (unit.nodeBuffer.kind(i) != NodeKind::UnaryExpression
                || TokenKindAt(tokens, unit.nodeBuffer.mainToken(i)) != TokenKind::RefKeyword)
            {
                continue;
            }

            const auto operand = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(i));
            if (unit.nodeBuffer.kind(operand) == NodeKind::NameExpression)
            {
                nameIds.push_back(nameIdOf(unit, operand));
            }
        }

        std::sort(nameIds.begin(), nameIds.end());
        nameIds.erase(std::unique(nameIds.begin(), nameIds.end()), nameIds.end());
        return nameIds;
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

                auto& methodDefinition = m_module.createMethod(typeDefinition, modifier, std::string(methodName), std::move(declarationParameters), std::vector<Type>());
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

    void TypeChecker::resolveWellKnownTypes()
    {
        m_module.refreshWellKnownTypes();
        m_defaultIntegerType = m_module.tryGetTypeByName(m_options.defaultIntegerType);
        m_defaultFloatingType = m_module.tryGetTypeByName(m_options.defaultFloatingType);
        m_defaultEnumBaseType = m_module.tryGetTypeByName(m_options.defaultEnumBaseType);
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

    SourceLine TypeChecker::tokenLine(const CompilationUnit& unit, u32 tokenIndex) const
    {
        if (!m_options.recordsSourceLocations || unit.origin != UnitOrigin::User)
        {
            return {};
        }

        const auto& location = m_compilationContext.sourceLocation(unit.fileId, tokenIndex);
        return { unit.fileId, unit.source->lineColumnAt(location.startIndex).line };
    }

    SourceLine TypeChecker::nameLine(const CompilationUnit& unit, u32 index) const
    {
        return tokenLine(unit, unit.nodeBuffer.mainToken(index));
    }

    SourceLine TypeChecker::statementLine(const CompilationUnit& unit, u32 index) const
    {
        // the main token of an expression statement is its semicolon, the first node has the first token
        const auto firstNodeToken = unit.nodeBuffer.mainToken(NodeLayout::subtreeBegin(unit.nodeBuffer, index));
        return tokenLine(unit, std::min(unit.nodeBuffer.mainToken(index), firstNodeToken));
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
        SourceTextSharedPtr& source,
        std::optional<SourceLocation>& location) const noexcept
    {
        if (definitionNode.index == InvalidNodeIndex)
        {
            return;
        }

        const auto& buffer = m_compilationContext.nodeBuffer(definitionNode.fileId);
        const auto nameToken = tokenAt(definitionNode.fileId, buffer.mainToken(definitionNode.index));
        source = m_compilationContext.source(nameToken);
        location = m_compilationContext.sourceLocation(nameToken);
    }

    CompilationUnit& TypeChecker::unitFor(NodeIndex index) const noexcept
    {
        return m_compilationContext.units()[index.fileId];
    }

    ValueRef TypeChecker::dynamicArraySourceAddress(const CompilationUnit& unit, u32 nameNode, Type arrayType)
    {
        const auto nameId = nameIdOf(unit, nameNode);
        const auto address = m_emitter.addressOfLocal(nameId);
        if (address.id() >= 0)
        {
            return address;
        }

        const auto value = m_emitter.peekLocal(nameId);
        if (value.id() >= 0)
        {
            return m_emitter.spillToTempSlot(value, arrayType);
        }

        return m_emitter.emitGlobalAddress(nameOf(unit, nameNode));
    }

    ValueRef TypeChecker::emitDefaultArgumentExpression(NodeIndex defaultNode, Type parameterType)
    {
        const auto defaultType = m_module.typeOfNode(defaultNode);
        if (defaultType == Type::Undefined())
        {
            return ValueRef{};
        }

        auto& defaultUnit = unitFor(defaultNode);
        auto context = ExpressionContext{};
        context.contextualNumberType = contextualTypeForExpectedType(parameterType);
        const auto checkedDefault = typeCheckExpression(defaultUnit, defaultNode.index, context);
        const auto defaultValue = materializeValue(checkedDefault);
        if (defaultValue.id() < 0)
        {
            return ValueRef{};
        }

        return m_emitter.emitWidened(defaultValue, checkedDefault.type.toValue(), parameterType.toValue());
    }

    ValueRef TypeChecker::materializeValue(const CheckedValue& checked)
    {
        if (checked.value.id() >= 0 || !checked.folded.has_value())
        {
            return checked.value;
        }

        return m_emitter.emitFoldedConstant(checked.folded.value(), checked.type.toValue());
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

    void TypeChecker::typeCheckFunctionSignatures()
    {
        CARACAL_ZONE_NAMED("check.functionSignatures");
        for (const auto functionDeclaration : m_functionDeclarations)
        {
            const auto& unit = unitFor(functionDeclaration);
            typeCheckFunctionSignature(unit, functionDeclaration.index);
        }
    }

    void TypeChecker::typeCheckTypeSignatures()
    {
        CARACAL_ZONE_NAMED("check.typeSignatures");
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
        if (isExtern && !m_isPreludePass)
        {
            m_emitter.recordExternDefinition(methodType);
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
            return recordType(unit, index, type);
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

        return recordType(unit, index, arrayType);
    }

    std::vector<Parameter> TypeChecker::typeCheckParametersNode(const CompilationUnit& unit, u32 index)
    {
        std::vector<Parameter> parameters{};
        auto seenDefault = false;

        auto parameterNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, index, parameterNodes);

        // parameter names stay out of scope in this loop
        // default values cannot reference other parameters
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
                const auto checkedDefault = typeCheckExpression(unit, parameterLayout.defaultValue, defaultContext);
                const auto defaultType = checkedDefault.type;
                if (checkedDefault.folded.has_value())
                {
                    m_emitter.recordDefaultArgumentFold(unit.fileId, parameterLayout.defaultValue, checkedDefault.folded.value());
                }
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
            // ownership rule: returning references or slices is not allowed
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
        const auto literalType = typeCheckNumberLiteral(unit, numberLiteralIndex, ExpressionContext{}).type;
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

    Type TypeChecker::tryResolveConstructedType(const CompilationUnit& unit, u32 strippedIndex)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        if (unit.nodeBuffer.kind(strippedIndex) != NodeKind::BinaryExpression
            || !unit.nodeBuffer.isMemberAccessBinary(tokens, strippedIndex))
        {
            return Type::Undefined();
        }

        const auto binaryLayout = NodeLayout::binary(unit.nodeBuffer, strippedIndex);
        if (unit.nodeBuffer.kind(binaryLayout.right) != NodeKind::FunctionCallExpression)
        {
            return Type::Undefined();
        }

        const auto left = NodeLayout::stripGroupings(unit.nodeBuffer, binaryLayout.left);
        if (unit.nodeBuffer.kind(left) != NodeKind::NameExpression
            || nameOf(unit, NodeLayout::subtreeBegin(unit.nodeBuffer, binaryLayout.right)) != "new")
        {
            return Type::Undefined();
        }

        const auto type = m_module.tryGetTypeByName(nameOf(unit, left));
        if (type.kind() != TypeKind::Type)
        {
            return Type::Undefined();
        }

        return type;
    }

    ValueRef TypeChecker::memberReceiverAddress(const CompilationUnit& unit, u32 leftIndex)
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, leftIndex);
        if (unit.nodeBuffer.kind(stripped) != NodeKind::NameExpression)
        {
            return ValueRef{};
        }

        const auto nameId = nameIdOf(unit, stripped);
        if (tryGetBinding(nameId) == nullptr)
        {
            return ValueRef{};
        }

        return m_emitter.localOrGlobalAddress(nameId, nameOf(unit, stripped));
    }

    std::string TypeChecker::escapedStringContent(const CompilationUnit& unit, u32 literalIndex)
    {
        const auto lexeme = nameOf(unit, literalIndex);
        if (lexeme.size() < 2)
        {
            return std::string{};
        }

        return replaceEscapeSequences(lexeme.substr(1, lexeme.size() - 2));
    }

    bool TypeChecker::memberLeftYieldsAddress(const CompilationUnit& unit, u32 leftIndex)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, leftIndex);
        if (unit.nodeBuffer.kind(stripped) == NodeKind::MemberAccessExpression)
        {
            return unit.nodeBuffer.kind(NodeLayout::memberExpression(stripped)) == NodeKind::NameExpression;
        }

        if (unit.nodeBuffer.kind(stripped) != NodeKind::BinaryExpression
            || !unit.nodeBuffer.isMemberAccessBinary(tokens, stripped))
        {
            return false;
        }

        const auto strippedLayout = NodeLayout::binary(unit.nodeBuffer, stripped);
        if (unit.nodeBuffer.kind(strippedLayout.right) == NodeKind::FunctionCallExpression)
        {
            const auto methodType = m_module.typeOfNode(NodeIndex{ unit.fileId, NodeLayout::subtreeBegin(unit.nodeBuffer, strippedLayout.right) });
            const auto* definition = m_module.tryGetFunctionDefinition(methodType);
            return definition != nullptr && definition->intrinsicKind() == IntrinsicKind::ArrayAt;
        }

        if (unit.nodeBuffer.kind(strippedLayout.right) == NodeKind::NameExpression)
        {
            return m_module.typeOfNode(NodeIndex{ unit.fileId, strippedLayout.left }).toValue().kind() == TypeKind::Type;
        }

        return false;
    }

    void TypeChecker::reportBinaryFoldFailure(const CompilationUnit& unit, FoldResultKind kind, Type operandType, u32 index, u32 rightIndex)
    {
        const auto* description = m_module.tryGetBuiltinTypeDescription(operandType);
        if (description == nullptr || description->kind != BuiltinTypeKind::Int)
        {
            return;
        }

        if (kind == FoldResultKind::DivideByZero)
        {
            m_diagnostics.addDivisionByZeroError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, rightIndex));
        }
        else if (kind == FoldResultKind::Overflow)
        {
            m_diagnostics.addConstantOverflowError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index),
                formatTypeName(m_module, operandType));
        }
    }

    void TypeChecker::reportUnaryFoldFailure(const CompilationUnit& unit, FoldResultKind kind, Type operandType, u32 index)
    {
        const auto* description = m_module.tryGetBuiltinTypeDescription(operandType);
        if (kind != FoldResultKind::Overflow || description == nullptr || description->kind != BuiltinTypeKind::Int)
        {
            return;
        }

        m_diagnostics.addConstantOverflowError(
            unit.source,
            ExpressionSourceLocation(m_compilationContext, unit, index),
            formatTypeName(m_module, operandType));
    }

    ValueRef TypeChecker::addressOfExpression(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, index);
        switch (unit.nodeBuffer.kind(stripped))
        {
            case NodeKind::NameExpression:
            {
                const auto nameId = nameIdOf(unit, stripped);
                if (tryGetBinding(nameId) == nullptr)
                {
                    return ValueRef{};
                }

                return m_emitter.localOrGlobalAddress(nameId, nameOf(unit, stripped));
            }
            case NodeKind::MemberAccessExpression:
            {
                const auto inner = NodeLayout::memberExpression(stripped);
                if (m_currentType == Type::Undefined() || unit.nodeBuffer.kind(inner) != NodeKind::NameExpression)
                {
                    return ValueRef{};
                }

                const auto& fieldDefinition = m_module.getTypeDefinition(m_currentType).tryGetFieldByName(nameOf(unit, inner));
                if (fieldDefinition.type() == Type::Undefined())
                {
                    return ValueRef{};
                }

                const auto thisAddress = m_emitter.addressOfLocal(m_thisNameId);
                return m_emitter.emitFieldAddress(thisAddress, m_currentType, fieldDefinition.index(), fieldDefinition.type());
            }
            case NodeKind::BinaryExpression:
            {
                if (!unit.nodeBuffer.isMemberAccessBinary(tokens, stripped))
                {
                    return ValueRef{};
                }

                const auto memberLayout = NodeLayout::binary(unit.nodeBuffer, stripped);
                const auto receiverType = m_module.typeOfNode(NodeIndex{ unit.fileId, memberLayout.left }).toValue();
                if (receiverType.kind() != TypeKind::Type || unit.nodeBuffer.kind(memberLayout.right) != NodeKind::NameExpression)
                {
                    return ValueRef{};
                }

                const auto& fieldDefinition = m_module.getTypeDefinition(receiverType).tryGetFieldByName(nameOf(unit, memberLayout.right));
                if (fieldDefinition.type() == Type::Undefined())
                {
                    return ValueRef{};
                }

                // the receiver is itself addressable, chains recurse
                const auto receiverAddress = addressOfExpression(unit, memberLayout.left);
                if (receiverAddress.id() < 0)
                {
                    return ValueRef{};
                }

                return m_emitter.emitFieldAddress(receiverAddress, receiverType, fieldDefinition.index(), fieldDefinition.type());
            }
            default:
            {
                return ValueRef{};
            }
        }
    }

    void TypeChecker::emitCapturedArrayLiteral(const CompilationUnit& unit, u32 literalIndex, ValueRef destination, Type arrayType)
    {
        const auto captured = m_arrayLiteralElements.find(ArrayLiteralKey(unit.fileId, literalIndex));
        if (captured == m_arrayLiteralElements.end() || destination.id() < 0)
        {
            return;
        }

        const auto elementType = m_module.getArrayElementType(arrayType);
        auto elementBase = destination;
        if (arrayType.kind() == TypeKind::DynamicArray)
        {
            // dynamic literals fill the calloc'd buffer behind the descriptor
            elementBase = m_emitter.emitDynamicArrayStorage(destination, arrayType, static_cast<i32>(captured->second.size()));
            if (elementBase.id() < 0)
            {
                return;
            }
        }
        for (size_t i = 0; i < captured->second.size(); ++i)
        {
            const auto element = captured->second[i];
            const auto indexValue = m_emitter.emitConstant(ConstantValue::FromI32(static_cast<i32>(i)), m_module.wellKnown().i32);
            const auto elementAddress = m_emitter.emitElementAddress(elementBase, arrayType, indexValue, elementType);

            const auto strippedElement = NodeLayout::stripGroupings(unit.nodeBuffer, element.node);
            if (unit.nodeBuffer.kind(strippedElement) == NodeKind::ArrayLiteral)
            {
                // nested literals fill their element storage directly
                emitCapturedArrayLiteral(unit, strippedElement, elementAddress, elementType);
                continue;
            }

            auto elementValue = element.value;
            if (element.folded.has_value())
            {
                // the checked value is reused unless it has another type than the array's elements
                const auto checkedType = m_module.typeOfNode(NodeIndex{ unit.fileId, element.node }).toValue();
                if (elementValue.id() < 0 || checkedType != elementType.toValue())
                {
                    elementValue = m_emitter.emitFoldedConstant(element.folded.value(), elementType.toValue());
                }
            }
            m_emitter.emitStoreValue(elementValue, elementAddress, elementType);
        }
    }

    std::optional<ConstantValue> TypeChecker::tryBuildAggregateConstant(const CompilationUnit& unit, u32 literalIndex)
    {
        const auto captured = m_arrayLiteralElements.find(ArrayLiteralKey(unit.fileId, literalIndex));
        if (captured == m_arrayLiteralElements.end())
        {
            return std::nullopt;
        }

        ConstantValue::AggregateData elements{};
        elements.reserve(captured->second.size());
        for (const auto& element : captured->second)
        {
            const auto strippedElement = NodeLayout::stripGroupings(unit.nodeBuffer, element.node);
            if (unit.nodeBuffer.kind(strippedElement) == NodeKind::ArrayLiteral)
            {
                const auto nested = tryBuildAggregateConstant(unit, strippedElement);
                if (!nested.has_value())
                {
                    return std::nullopt;
                }
                elements.push_back(nested.value());
                continue;
            }

            if (!element.folded.has_value())
            {
                return std::nullopt;
            }
            elements.push_back(IREmitter::constantFromFold(element.folded.value()));
        }

        return ConstantValue::FromAggregate(std::move(elements));
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
        CARACAL_ZONE_NAMED("check.globalConstants");
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
        CARACAL_ZONE_NAMED("check.functionDefinitions");
        for (const auto declaration : m_functionDeclarations)
        {
            const auto& unit = unitFor(declaration);
            typeCheckFunctionDefinitionStatement(unit, declaration.index);
        }
    }

    void TypeChecker::typeCheckEnumDefinitions()
    {
        CARACAL_ZONE_NAMED("check.enumDefinitions");
        for (const auto declaration : m_enumDeclarations)
        {
            const auto& unit = unitFor(declaration);
            typeCheckEnumDefinitionStatement(unit, declaration.index);
        }
    }

    void TypeChecker::typeCheckTypeFieldDefinitions()
    {
        CARACAL_ZONE_NAMED("check.typeFieldDefinitions");
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
            auto& typeDefinition = m_module.getTypeDefinition(typeType);
            const auto constructorType = typeDefinition.tryGetMethodTypeByName("new");
            const auto emitConstructor = constructorType != Type::Undefined();
            if (typeLayout.hasConstructorParameters)
            {
                static_cast<void>(typeCheckParametersNode(unit, typeLayout.constructorParameters));
            }

            if (emitConstructor)
            {
                auto& constructorDefinition = m_module.getFunctionDefinition(constructorType);
                m_emitter.setAddressTakenLocals({});
                m_emitter.beginFunction(constructorDefinition, nameLine(unit, index));
                const auto& constructorParameters = constructorDefinition.parameters();
                for (size_t i = 0; i < constructorParameters.size(); ++i)
                {
                    m_emitter.emitParameter(internName(constructorParameters[i].name()), static_cast<i32>(i), constructorParameters[i]);
                }
            }

            auto fieldIndex = 0;
            std::vector<u32> bodyStatements;
            NodeLayout::collectChildren(unit.nodeBuffer, typeLayout.body, bodyStatements);
            for (const auto bodyStatement : bodyStatements)
            {
                if (unit.nodeBuffer.kind(bodyStatement) != NodeKind::TypeFieldDeclaration)
                {
                    continue;
                }

                if (m_options.recordsSourceLocations)
                {
                    const ScopedValue<SourceLine> sourceLineScope{ m_emitter.currentSourceLine(), statementLine(unit, bodyStatement) };
                    typeCheckTypeFieldDeclaration(unit, bodyStatement, typeDefinition, fieldIndex);
                }
                else
                {
                    typeCheckTypeFieldDeclaration(unit, bodyStatement, typeDefinition, fieldIndex);
                }
                ++fieldIndex;
            }

            if (emitConstructor)
            {
                m_emitter.endFunction();
            }

            popScope(true);
        }
    }

    void TypeChecker::typeCheckTypeMethodDefinitions()
    {
        CARACAL_ZONE_NAMED("check.typeMethodDefinitions");
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
        if (!m_options.recordsSourceLocations || unit.nodeBuffer.kind(index) == NodeKind::BlockNode)
        {
            typeCheckStatementNode(unit, index);
            return;
        }

        const ScopedValue<SourceLine> sourceLineScope{ m_emitter.currentSourceLine(), statementLine(unit, index) };
        typeCheckStatementNode(unit, index);
    }

    void TypeChecker::typeCheckStatementNode(const CompilationUnit& unit, u32 index)
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
            case NodeKind::BreakStatement:
            {
                m_emitter.emitBreak();
                break;
            }
            case NodeKind::SkipStatement:
            {
                m_emitter.emitSkip();
                break;
            }
            default:
            {
                break;
            }
        }
    }

    void TypeChecker::emitLocalBinding(
        const CompilationUnit& unit, 
        u32 nameId,
        std::string_view name,
        Type bindingType,
        Type rightType,
        u32 strippedRight,
        const CheckedValue& checkedRight,
        bool referencesConstant,
        ValueRef aggregateDestination,
        bool constructorFilled)
    {
        CARACAL_ZONE_DETAIL("decl.emitBinding");
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto strippedRightKind = unit.nodeBuffer.kind(strippedRight);

        if (aggregateDestination.id() >= 0)
        {
            // a constructor call already filled the slot through its receiver
            if (constructorFilled)
            {
                return;
            }

            if (strippedRightKind == NodeKind::ArrayLiteral
                && (bindingType.kind() == TypeKind::FixedArray 
                || bindingType.kind() == TypeKind::DynamicArray))
            {
                emitCapturedArrayLiteral(unit, strippedRight, aggregateDestination, bindingType);
            }
            else if (strippedRightKind == NodeKind::StringLiteral && bindingType == m_module.wellKnown().string)
            {
                m_emitter.emitStringLiteralIntoAddress(aggregateDestination, escapedStringContent(unit, strippedRight));
            }
            else if (bindingType.kind() == TypeKind::DynamicArray && strippedRightKind == NodeKind::NameExpression)
            {
                // owning dynamic arrays deep copy on binding
                const auto sourceAddress = dynamicArraySourceAddress(unit, strippedRight, bindingType.toValue());
                const auto copied = m_emitter.emitArrayCopy(sourceAddress, bindingType.toValue());
                m_emitter.emitStoreValue(copied, aggregateDestination, bindingType.toValue());
            }
            else
            {
                m_emitter.emitStoreValue(materializeValue(checkedRight), aggregateDestination, bindingType.toValue());
            }
            return;
        }

        if ((bindingType.kind() == TypeKind::FixedArray || bindingType.kind() == TypeKind::DynamicArray)
            && strippedRightKind == NodeKind::ArrayLiteral)
        {
            // array literals with an inferred type allocate their slot here and fill it with the checked elements
            const auto destination = m_emitter.allocateAggregateSlot(nameId, name, bindingType);
            emitCapturedArrayLiteral(unit, strippedRight, destination, bindingType);
            return;
        }

        if (bindingType.isReference())
        {
            const auto isExplicitRefBinding = 
                strippedRightKind == NodeKind::UnaryExpression
                && TokenKindAt(tokens, unit.nodeBuffer.mainToken(strippedRight)) == TokenKind::RefKeyword;
            if (isExplicitRefBinding)
            {
                // an explicit ref binds to the address of its referent, field chains included
                // a referent without an address cannot be emitted
                const auto refOperand = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(strippedRight));
                const auto referentAddress = addressOfExpression(unit, refOperand);
                if (referentAddress.id() < 0)
                {
                    m_emitter.recordEmissionFailure();
                    return;
                }

                m_emitter.emitRefLocalDeclaration(nameId, bindingType, referentAddress);
            }
            else
            {
                // a ref typed initializer without an explicit ref copies the value for now
                m_emitter.emitLocalDeclaration(nameId, name, bindingType.toValue(), materializeValue(checkedRight));
            }
            return;
        }

        if (bindingType.kind() == TypeKind::FixedArray || bindingType.kind() == TypeKind::Type)
        {
            // aggregates without a preallocated slot allocate one here and store the value into it
            const auto destination = m_emitter.allocateAggregateSlot(nameId, name, bindingType);
            m_emitter.emitStoreValue(materializeValue(checkedRight), destination, bindingType.toValue());
            return;
        }

        const auto initializerValue = m_emitter.emitWidened(materializeValue(checkedRight), rightType.toValue(), bindingType.toValue());
        m_emitter.emitLocalDeclaration(nameId, name, bindingType, initializerValue);
    }

    Type TypeChecker::resolveAggregateInitializerType(
        const CompilationUnit& unit,
        Type declaredType,
        u32 strippedInitializer,
        Type constructedType,
        bool& initializerWantsAddress)
    {
        CARACAL_ZONE_NAMED("check.aggregateInitializerType");
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        auto aggregateType = constructedType;
        if (aggregateType == Type::Undefined() && !declaredType.isReference()
            && (declaredType.kind() == TypeKind::Type
                || declaredType.kind() == TypeKind::FixedArray
                || declaredType.kind() == TypeKind::DynamicArray))
        {
            aggregateType = declaredType;
        }
        if (aggregateType == Type::Undefined() && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::NameExpression
            && !declaredType.isReference())
        {
            if (const auto* initializerBinding = tryGetBinding(nameIdOf(unit, strippedInitializer));
                initializerBinding != nullptr
                && !initializerBinding->type.isReference()
                && (initializerBinding->type.kind() == TypeKind::Type
                    || initializerBinding->type.kind() == TypeKind::FixedArray
                    || initializerBinding->type.toValue().kind() == TypeKind::DynamicArray))
            {
                aggregateType = initializerBinding->type.toValue();
            }
        }
        if (aggregateType == Type::Undefined() 
            && !declaredType.isReference()
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::FunctionCallExpression)
        {
            // a call returning an aggregate stores into a preallocated slot
            const auto calleeType = m_module.tryGetFunctionTypeByName(nameOf(unit, NodeLayout::subtreeBegin(unit.nodeBuffer, strippedInitializer)));
            if (calleeType != Type::Undefined())
            {
                const auto& calleeReturns = m_module.getFunctionDefinition(calleeType).returnTypes();
                if (calleeReturns.size() == 1 && !calleeReturns.front().isReference()
                    && (calleeReturns.front().kind() == TypeKind::Type
                        || calleeReturns.front().kind() == TypeKind::FixedArray
                        || calleeReturns.front().kind() == TypeKind::DynamicArray))
                {
                    aggregateType = calleeReturns.front();
                }
            }
        }
        if (aggregateType == Type::Undefined()
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::StringLiteral
            && m_module.wellKnown().string != Type::Undefined()
            && (declaredType == Type::Undefined() || declaredType == m_module.wellKnown().string))
        {
            aggregateType = m_module.wellKnown().string;
        }
        if (aggregateType.kind() == TypeKind::DynamicArray
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::NameExpression)
        {
            // a dynamic array copied from a name is read through its address and deep copied
            initializerWantsAddress = true;
        }
        if (aggregateType != Type::Undefined() && aggregateType == m_module.wellKnown().string
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::StringLiteral)
        {
            // the literal fills the declaration's slot directly
            initializerWantsAddress = true;
        }
        if (aggregateType == Type::Undefined()
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::BinaryExpression
            && unit.nodeBuffer.isMemberAccessBinary(tokens, strippedInitializer))
        {
            // a method call returning an aggregate preallocates like a
            // free call, the receiver type resolves syntactically
            const auto callLayout = NodeLayout::binary(unit.nodeBuffer, strippedInitializer);
            if (unit.nodeBuffer.kind(callLayout.right) == NodeKind::FunctionCallExpression)
            {
                const auto receiverNode = NodeLayout::stripGroupings(unit.nodeBuffer, callLayout.left);
                auto receiverType = Type::Undefined();
                if (unit.nodeBuffer.kind(receiverNode) == NodeKind::NameExpression)
                {
                    if (const auto* receiverBinding = tryGetBinding(nameIdOf(unit, receiverNode)); receiverBinding != nullptr)
                    {
                        receiverType = receiverBinding->type.toValue();
                    }
                }
                else if (unit.nodeBuffer.kind(receiverNode) == NodeKind::StringLiteral)
                {
                    receiverType = m_module.wellKnown().string;
                }

                if (receiverType.kind() == TypeKind::Type)
                {
                    const auto methodType = m_module.getTypeDefinition(receiverType).tryGetMethodTypeByName(
                        nameOf(unit, NodeLayout::subtreeBegin(unit.nodeBuffer, callLayout.right)));
                    if (methodType != Type::Undefined())
                    {
                        const auto& methodReturns = m_module.getFunctionDefinition(methodType).returnTypes();
                        if (methodReturns.size() == 1 && !methodReturns.front().isReference()
                            && (methodReturns.front().kind() == TypeKind::Type
                                || methodReturns.front().kind() == TypeKind::FixedArray
                                || methodReturns.front().kind() == TypeKind::DynamicArray))
                        {
                            aggregateType = methodReturns.front();
                        }
                    }
                }
            }
        }
        if (aggregateType == Type::Undefined()
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::BinaryExpression
            && !unit.nodeBuffer.isMemberAccessBinary(tokens, strippedInitializer)
            && ConvertToBinaryOperator(TokenKindAt(tokens, unit.nodeBuffer.mainToken(strippedInitializer))) == BinaryOperatorKind::AdditionWrapping
            && m_module.wellKnown().string != Type::Undefined())
        {
            // only a concat chain is string typed, a comparison of strings is not
            auto leftmostOperand = strippedInitializer;
            while (unit.nodeBuffer.kind(leftmostOperand) == NodeKind::BinaryExpression)
            {
                leftmostOperand = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::binary(unit.nodeBuffer, leftmostOperand).left);
            }
            auto leftmostOperandType = Type::Undefined();
            if (unit.nodeBuffer.kind(leftmostOperand) == NodeKind::StringLiteral)
            {
                leftmostOperandType = m_module.wellKnown().string;
            }
            else if (unit.nodeBuffer.kind(leftmostOperand) == NodeKind::NameExpression)
            {
                if (const auto* leftmostBinding = tryGetBinding(nameIdOf(unit, leftmostOperand)); leftmostBinding != nullptr)
                {
                    leftmostOperandType = leftmostBinding->type.toValue();
                }
            }
            if (leftmostOperandType == m_module.wellKnown().string)
            {
                aggregateType = leftmostOperandType;
            }
        }

        return aggregateType;
    }

    void TypeChecker::typeCheckConstantDeclaration(const CompilationUnit& unit, u32 index)
    {
        CARACAL_ZONE_NAMED("check.constantDeclaration");
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

        auto constantAggregateDestination = ValueRef{};
        auto pushedConstantConstruction = false;
        auto constantInitializerWantsAddress = false;
        if (!currentScopeIsGlobal() && unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::NameExpression)
        {
            const auto strippedInitializer = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
            const auto constructedType = tryResolveConstructedType(unit, strippedInitializer);
            const auto aggregateType = resolveAggregateInitializerType(unit, declaredType, strippedInitializer, constructedType, constantInitializerWantsAddress);
            if (aggregateType != Type::Undefined())
            {
                constantAggregateDestination = m_emitter.allocateAggregateSlot(
                    nameIdOf(unit, declarationLayout.left),
                    nameOf(unit, declarationLayout.left),
                    aggregateType);
                if (constructedType != Type::Undefined() && constantAggregateDestination.id() >= 0)
                {
                    m_constructorDestinations.push_back(ConstructorDestination{ constantAggregateDestination, false });
                    pushedConstantConstruction = true;
                }
            }
        }

        // a constructed global is built inside .global_init
        // its slot address is pushed before the arguments are checked, like any constructor
        // a discarded global with a runtime call is emitted into .global_init for its side effects
        auto pushedGlobalConstruction = false;
        auto resumedGlobalInit = false;
        if (currentScopeIsGlobal() && !m_isPreludePass)
        {
            if (unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::DiscardLiteral)
            {
                if (ContainsRuntimeCall(m_module, unit, tokens, declarationLayout.right))
                {
                    m_emitter.resumeGlobalInitEffects();
                    resumedGlobalInit = true;
                }
            }
            else if (unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::NameExpression)
            {
                const auto constructedType = tryResolveConstructedType(unit, NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right));
                if (constructedType != Type::Undefined())
                {
                    resumedGlobalInit = true;
                    const auto destination = m_emitter.beginGlobalInitConstruction(nameOf(unit, declarationLayout.left), constructedType);
                    if (destination.id() >= 0)
                    {
                        m_constructorDestinations.push_back(ConstructorDestination{ destination, false });
                        pushedGlobalConstruction = true;
                    }
                }
            }
        }

        m_sawUnfoldedBitwiseCall = false;
        auto checkedRight = CheckedValue{ Type::Undefined() };
        {
            auto context = ExpressionContext{};
            // number literals get their type from the declared type
            context.contextualNumberType = contextualTypeForExpectedType(declaredType);
            // a ref initializer binds to the address, not to the value
            const auto addressRight = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
            context.wantsAddress = constantInitializerWantsAddress
                || (unit.nodeBuffer.kind(addressRight) == NodeKind::UnaryExpression
                    && TokenKindAt(tokens, unit.nodeBuffer.mainToken(addressRight)) == TokenKind::RefKeyword);
            checkedRight = typeCheckExpression(unit, declarationLayout.right, context);
        }
        if (pushedConstantConstruction)
        {
            m_constructorDestinations.pop_back();
        }
        if (pushedGlobalConstruction)
        {
            m_constructorDestinations.pop_back();
        }
        if (resumedGlobalInit)
        {
            m_emitter.suspendGlobalInit();
        }
        const auto rightType = checkedRight.type;

        // the binding takes the declared type if there is one
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
                recordType(unit, declarationLayout.left, Type::Undefined());
                recordType(unit, index, Type::Undefined());
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

            if (isGlobalConstant && m_sawUnfoldedBitwiseCall)
            {
                m_diagnostics.addGlobalConstantNotComputableError(
                    unit.source,
                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right),
                    std::string(name));
            }

            const auto nameId = nameIdOf(unit, declarationLayout.left);
            if (const auto* existing = tryGetBinding(nameId); existing == nullptr)
            {
                addVariableBinding(nameId, bindingType, nameLocation(unit, declarationLayout.left), unit.source, VariableBindingKind::LocalConstant);
                if (auto* newBinding = tryGetBindingMutable(nameId); newBinding != nullptr)
                {
                    newBinding->folded = checkedRight.folded;
                }

                if (isGlobalConstant)
                {
                    static_cast<void>(m_module.createConstant(name, rightType, NodeIndex{ unit.fileId, declarationLayout.right }));
                    if (!m_isPreludePass)
                    {
                        if (checkedRight.folded.has_value())
                        {
                            m_emitter.emitGlobalConstant(name, rightType, IREmitter::constantFromFold(checkedRight.folded.value()));
                        }
                        else
                        {
                            const auto strippedRight = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
                            const auto strippedKind = unit.nodeBuffer.kind(strippedRight);
                            if (strippedKind == NodeKind::UnaryExpression
                                && TokenKindAt(tokens, unit.nodeBuffer.mainToken(strippedRight)) == TokenKind::RefKeyword)
                            {
                                const auto refTarget = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(strippedRight));
                                if (unit.nodeBuffer.kind(refTarget) == NodeKind::NameExpression)
                                {
                                    m_emitter.emitGlobalReference(name, rightType, nameOf(unit, refTarget));
                                }
                            }
                            else if (strippedKind == NodeKind::StringLiteral)
                            {
                                m_emitter.emitGlobalConstant(name, rightType, ConstantValue::FromString(escapedStringContent(unit, strippedRight)));
                            }
                            else if (strippedKind == NodeKind::ArrayLiteral)
                            {
                                // constant element arrays store an aggregate value
                                if (const auto aggregate = tryBuildAggregateConstant(unit, strippedRight); aggregate.has_value())
                                {
                                    m_emitter.emitGlobalConstant(name, rightType, aggregate.value());
                                }
                            }
                            else if (strippedKind == NodeKind::BinaryExpression
                                && rightType.kind() == TypeKind::Enum
                                && unit.nodeBuffer.isMemberAccessBinary(tokens, strippedRight))
                            {
                                // an enum member global stores the underlying literal
                                const auto memberLayout = NodeLayout::binary(unit.nodeBuffer, strippedRight);
                                if (unit.nodeBuffer.kind(memberLayout.right) == NodeKind::NameExpression)
                                {
                                    auto& initializerEnum = m_module.getEnumDefinition(rightType);
                                    if (const auto* field = initializerEnum.tryGetFieldByName(nameOf(unit, memberLayout.right)); field != nullptr && field->hasConstantValue())
                                    {
                                        if (const auto constant = m_emitter.makeEnumConstantValue(initializerEnum.baseType(), field->value()); constant.has_value())
                                        {
                                            m_emitter.emitGlobalConstant(name, rightType, constant.value());
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    emitLocalBinding(unit, nameId, name, bindingType, rightType,
                        NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right),
                        checkedRight, false, constantAggregateDestination, pushedConstantConstruction);
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
            recordType(unit, declarationLayout.left, bindingType);
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

        recordType(unit, index, bindingType);
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
                    if (!m_isPreludePass)
                    {
                        m_emitter.emitConstructedGlobal(name, declaredType);
                    }
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
            recordType(unit, initLayout.left, declaredType);
        }

        recordType(unit, index, declaredType);
    }

    void TypeChecker::typeCheckVariableDeclaration(const CompilationUnit& unit, u32 index)
    {
        CARACAL_ZONE_NAMED("check.variableDeclaration");
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto declarationLayout = NodeLayout::declaration(unit.nodeBuffer, index);

        auto declaredType = Type::Undefined();
        if (declarationLayout.hasExplicitType)
        {
            declaredType = typeCheckTypeNameNode(unit, declarationLayout.explicitType);
        }

        auto pushedConstructorDestination = false;
        auto aggregateDestination = ValueRef{};
        auto initializerWantsAddress = false;
        if (unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::NameExpression && !currentScopeIsGlobal())
        {
            CARACAL_ZONE_DETAIL("decl.preallocateSlot");
            const auto strippedInitializer = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
            const auto constructedType = tryResolveConstructedType(unit, strippedInitializer);
            const auto aggregateType = resolveAggregateInitializerType(unit, declaredType, strippedInitializer, constructedType, initializerWantsAddress);

            if (aggregateType != Type::Undefined())
            {
                aggregateDestination = m_emitter.allocateAggregateSlot(
                    nameIdOf(unit, declarationLayout.left),
                    nameOf(unit, declarationLayout.left),
                    aggregateType);
                if (constructedType != Type::Undefined() && aggregateDestination.id() >= 0)
                {
                    m_constructorDestinations.push_back(ConstructorDestination{ aggregateDestination, false });
                    pushedConstructorDestination = true;
                }
            }
        }

        auto checkedRight = CheckedValue{ Type::Undefined() };
        {
            CARACAL_ZONE_DETAIL("decl.initializer");
            auto context = ExpressionContext{};
            // number literals get their type from the declared type
            context.contextualNumberType = contextualTypeForExpectedType(declaredType);
            // a ref initializer binds to the address, not to the value
            const auto addressRight = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
            context.wantsAddress = initializerWantsAddress
                || (unit.nodeBuffer.kind(addressRight) == NodeKind::UnaryExpression
                    && TokenKindAt(tokens, unit.nodeBuffer.mainToken(addressRight)) == TokenKind::RefKeyword);
            checkedRight = typeCheckExpression(unit, declarationLayout.right, context);
        }
        if (pushedConstructorDestination)
        {
            m_constructorDestinations.pop_back();
        }
        const auto rightType = checkedRight.type;

        // the binding takes the declared type if there is one
        auto bindingType = rightType;
        if (declaredType != Type::Undefined())
        {
            bindingType = declaredType;
        }

        auto referencesConstant = false;
        const auto strippedRight = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
        const auto strippedRightKind = unit.nodeBuffer.kind(strippedRight);
        {
            CARACAL_ZONE_DETAIL("decl.constness");
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
        }

        if (unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::NameExpression)
        {
            CARACAL_ZONE_DETAIL("decl.bind");
            const auto name = nameOf(unit, declarationLayout.left);
            if (bindingType == Type::Void())
            {
                m_diagnostics.addCallReturnsNoValueError(unit.source, nameLocation(unit, declarationLayout.left), std::string(name));
                recordType(unit, declarationLayout.left, Type::Undefined());
                recordType(unit, index, Type::Undefined());
                return;
            }

            const auto nameId = nameIdOf(unit, declarationLayout.left);
            if (const auto* existing = tryGetBinding(nameId); existing == nullptr)
            {
                addVariableBinding(nameId, bindingType, nameLocation(unit, declarationLayout.left), unit.source, VariableBindingKind::LocalVariable, referencesConstant);
                emitLocalBinding(unit, nameId, name, bindingType, rightType, strippedRight, checkedRight, referencesConstant, aggregateDestination, pushedConstructorDestination);
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
            recordType(unit, declarationLayout.left, bindingType);
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

        recordType(unit, index, bindingType);
    }

    void TypeChecker::typeCheckExpressionStatement(const CompilationUnit& unit, u32 index)
    {
        const auto expressionType = typeCheckExpression(unit, NodeLayout::statementExpression(index), ExpressionContext{}).type;
        recordType(unit, index, expressionType);
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

        auto targetContext = ExpressionContext{};
        targetContext.wantsAddress = true;
        auto leftType = typeCheckExpression(unit, assignmentLayout.target, targetContext).type;
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
        // a dynamic array assigned from a name is deep copied through the name's address
        // the right side is not loaded
        const auto strippedAssignedValue = NodeLayout::stripGroupings(unit.nodeBuffer, assignmentLayout.value);
        const auto dynamicAssignmentCopy = leftType.toValue().kind() == TypeKind::DynamicArray
            && !leftType.isReference()
            && unit.nodeBuffer.kind(strippedAssignedValue) == NodeKind::NameExpression
            && !currentScopeIsGlobal();

        // a constructor call on the right side writes straight into the target
        // the target's address is taken before the arguments are checked
        auto pushedAssignmentConstruction = false;
        if (leftKind == NodeKind::NameExpression && !currentScopeIsGlobal())
        {
            const auto constructedType = tryResolveConstructedType(unit, NodeLayout::stripGroupings(unit.nodeBuffer, assignmentLayout.value));
            if (constructedType != Type::Undefined())
            {
                const auto destination = m_emitter.localOrGlobalAddress(nameIdOf(unit, assignmentLayout.target), nameOf(unit, assignmentLayout.target));
                if (destination.id() >= 0)
                {
                    m_constructorDestinations.push_back(ConstructorDestination{ destination, false });
                    pushedAssignmentConstruction = true;
                }
            }
        }

        auto checkedRight = CheckedValue{ Type::Undefined() };
        {
            auto context = ExpressionContext{};
            // use the assigned variable's type for the value
            context.contextualNumberType = contextualTypeForExpectedType(leftType);
            context.wantsAddress = dynamicAssignmentCopy;
            checkedRight = typeCheckExpression(unit, assignmentLayout.value, context);
        }
        if (pushedAssignmentConstruction)
        {
            m_constructorDestinations.pop_back();
        }
        auto rightType = checkedRight.type;
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
            return;
        }

        // a constructor right side already stored through the target's address
        if (!pushedAssignmentConstruction && leftKind == NodeKind::NameExpression)
        {
            auto assignedValue = m_emitter.emitWidened(materializeValue(checkedRight), rightType.toValue(), leftType.toValue());
            if (dynamicAssignmentCopy)
            {
                const auto sourceAddress = m_emitter.localOrGlobalAddress(nameIdOf(unit, strippedAssignedValue), nameOf(unit, strippedAssignedValue));
                assignedValue = m_emitter.emitArrayCopy(sourceAddress, leftType.toValue());
            }
            const auto* emitBinding = tryGetBinding(nameIdOf(unit, assignmentLayout.target));
            if (emitBinding != nullptr && emitBinding->scopeIndex == 0 && !currentScopeIsGlobal())
            {
                m_emitter.emitGlobalAssignment(nameOf(unit, assignmentLayout.target), assignedValue);
            }
            else
            {
                m_emitter.emitLocalAssignment(nameIdOf(unit, assignmentLayout.target), assignedValue);
            }
        }
        else if (!pushedAssignmentConstruction
            && (leftKind == NodeKind::MemberAccessExpression
                || (leftKind == NodeKind::BinaryExpression && unit.nodeBuffer.isMemberAccessBinary(tokens, assignmentLayout.target))))
        {
            // a field assignment emits the value before the field's address
            // nested fields like a.b.c resolve their address recursively
            const auto assignedValue = m_emitter.emitWidened(materializeValue(checkedRight), rightType.toValue(), leftType.toValue());
            const auto targetAddress = addressOfExpression(unit, assignmentLayout.target);
            if (targetAddress.id() < 0)
            {
                m_emitter.recordEmissionFailure();
            }
            else
            {
                m_emitter.emitStoreValue(assignedValue, targetAddress, leftType);
            }
        }
    }

    void TypeChecker::typeCheckFunctionDefinitionStatement(const CompilationUnit& unit, u32 index)
    {
        CARACAL_ZONE_NAMED("check.functionBody");
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

        // the extern annotation is read before the body is emitted
        // annotation errors are reported after the body
        NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, m_annotationScratch);
        const auto isExternFunction = NodeLayout::annotationsContain(unit.nodeBuffer, tokens, m_annotationScratch, ExternAnnotationName);
        const auto emitBody = !isExternFunction;
        if (isExternFunction && !m_isPreludePass)
        {
            m_emitter.recordExternDefinition(functionDefinition.type());
        }
        if (emitBody)
        {
            m_emitter.setAddressTakenLocals(collectAddressTakenNameIds(unit, functionLayout.body));
            m_emitter.beginFunction(functionDefinition, nameLine(unit, index));
        }

        NodeLayout::collectChildren(unit.nodeBuffer, functionLayout.parameters, m_childrenScratch);
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto location = std::optional<SourceLocation>{};
            if (i < m_childrenScratch.size())
            {
                location = NodeSpans::parameterLocation(unit.nodeBuffer, tokens, m_childrenScratch[i]);
            }
            const auto nameId = internName(parameters[i].name());
            addVariableBinding(nameId, parameters[i].type(), location, unit.source, VariableBindingKind::Parameter);
            if (emitBody)
            {
                m_emitter.emitParameter(nameId, static_cast<i32>(i), parameters[i]);
            }
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

        if (emitBody)
        {
            m_emitter.endFunction();
        }

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

                auto checkedFieldValue = CheckedValue{ Type::Undefined() };
                {
                    auto context = ExpressionContext{};
                    context.contextualNumberType = contextualType;
                    checkedFieldValue = typeCheckExpression(unit, expression, context);
                }
                const auto fieldValueType = checkedFieldValue.type;
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
                        enumDefinition.addField(fieldName, fieldLocation);
                        recordExpressionEnumFieldConstant(unit, enumType, fieldName, expression, checkedFieldValue);
                    }
                }
                else
                {
                    enumDefinition.addField(fieldName, fieldLocation);
                    recordExpressionEnumFieldConstant(unit, enumType, fieldName, expression, checkedFieldValue);
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

    void TypeChecker::recordExpressionEnumFieldConstant(const CompilationUnit& unit, Type enumType, std::string_view fieldName, u32 expressionIndex, const CheckedValue& checkedFieldValue)
    {
        if (checkedFieldValue.folded.has_value())
        {
            m_emitter.recordEnumFieldConstant(enumType.id(), std::string(fieldName), IREmitter::constantFromFold(checkedFieldValue.folded.value()), checkedFieldValue.folded);
            return;
        }

        // cstring valued fields store their literal bytes
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, expressionIndex);
        if (unit.nodeBuffer.kind(stripped) == NodeKind::StringLiteral
            && checkedFieldValue.type == m_module.wellKnown().cstring)
        {
            m_emitter.recordEnumFieldConstant(enumType.id(), std::string(fieldName), ConstantValue::FromString(escapedStringContent(unit, stripped)), std::nullopt);
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

        auto checkedFieldRight = CheckedValue{ Type::Undefined() };
        {
            auto context = ExpressionContext{};
            // use the explicit field type for the initializer
            context.contextualNumberType = contextualTypeForExpectedType(fieldType);
            checkedFieldRight = typeCheckExpression(unit, declarationLayout.right, context);
        }
        const auto expressionType = checkedFieldRight.type;

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

        recordType(unit, declarationLayout.left, fieldType);
        recordType(unit, index, fieldType);

        typeDefinition.addField(fieldType, std::string(fieldName), fieldIndex, unit.nodeBuffer.isConstantTypeField(tokens, index));

        const auto thisAddress = m_emitter.addressOfLocal(m_thisNameId);
        if (thisAddress.id() < 0)
        {
            return;
        }

        const auto strippedField = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
        if (unit.nodeBuffer.kind(strippedField) == NodeKind::ArrayLiteral
            && (fieldType.kind() == TypeKind::FixedArray || fieldType.kind() == TypeKind::DynamicArray))
        {
            // an array literal fills the field directly
            const auto fieldAddress = m_emitter.emitFieldAddress(thisAddress, typeDefinition.type(), fieldIndex, fieldType);
            emitCapturedArrayLiteral(unit, strippedField, fieldAddress, fieldType);
            return;
        }

        auto fieldValue = m_emitter.emitWidened(materializeValue(checkedFieldRight), expressionType.toValue(), fieldType.toValue());
        if (fieldType.kind() == TypeKind::DynamicArray && unit.nodeBuffer.kind(strippedField) == NodeKind::NameExpression)
        {
            // an owning array field deep copies its source
            // string reserves the nul byte its c string view relies on
            const auto sourceAddress = dynamicArraySourceAddress(unit, strippedField, fieldType);
            const auto reserveNulByte = typeDefinition.type() == m_module.wellKnown().string;
            fieldValue = m_emitter.emitArrayCopy(sourceAddress, fieldType, reserveNulByte);
        }
        const auto fieldAddress = m_emitter.emitFieldAddress(thisAddress, typeDefinition.type(), fieldIndex, fieldType);
        m_emitter.emitStoreValue(fieldValue, fieldAddress, fieldType);
    }

    void TypeChecker::typeCheckMethodDefinitionStatement(const CompilationUnit& unit, u32 index)
    {
        CARACAL_ZONE_NAMED("check.methodBody");
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

        collectMethodAnnotationsBefore(unit, index, m_module.getTypeDefinition(m_currentType), m_annotationScratch);
        auto isExternMethod = false;
        for (const auto annotation : m_annotationScratch)
        {
            if (nameOf(unit, annotation) == ExternAnnotationName)
            {
                isExternMethod = true;
                break;
            }
        }

        const auto emitBody = !isExternMethod;
        if (emitBody)
        {
            m_emitter.setAddressTakenLocals(collectAddressTakenNameIds(unit, methodLayout.body));
            m_emitter.beginFunction(methodDefinition, nameLine(unit, index));
        }

        const auto& parameters = methodDefinition.parameters();
        NodeLayout::collectChildren(unit.nodeBuffer, methodLayout.parameters, m_childrenScratch);
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto location = std::optional<SourceLocation>{};
            if (i < m_childrenScratch.size())
            {
                location = NodeSpans::parameterLocation(unit.nodeBuffer, tokens, m_childrenScratch[i]);
            }
            const auto nameId = internName(parameters[i].name());
            addVariableBinding(nameId, parameters[i].type(), location, unit.source, VariableBindingKind::Parameter);
            if (emitBody)
            {
                m_emitter.emitParameter(nameId, static_cast<i32>(i), parameters[i]);
            }
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
        if (emitBody)
        {
            m_emitter.endFunction();
        }

        const auto isExtern = isExternMethod;
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

        const auto checkedCondition = typeCheckExpression(unit, ifLayout.condition, ExpressionContext{});
        auto conditionType = coerceConditionType(checkedCondition.type, unit, ifLayout.condition);
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolIfConditionError(
                unit.source,
                NodeSpans::expressionLocation(unit.nodeBuffer, tokens, ifLayout.condition),
                formatTypeName(m_module, conditionType));
        }

        auto ifFrame = m_emitter.beginIf(materializeValue(checkedCondition), ifLayout.hasFalse);
        typeCheckStatement(unit, ifLayout.trueStatement);
        m_emitter.endIfThen(ifFrame);

        if (ifLayout.hasFalse)
        {
            typeCheckStatement(unit, ifLayout.falseStatement);
        }
        m_emitter.endIf(ifFrame);
    }

    void TypeChecker::typeCheckTrailingIfStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto trailingIfLayout = NodeLayout::trailingIf(unit.nodeBuffer, index);

        const auto checkedCondition = typeCheckExpression(unit, trailingIfLayout.condition, ExpressionContext{});
        auto conditionType = coerceConditionType(checkedCondition.type, unit, trailingIfLayout.condition);
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolIfConditionError(
                unit.source,
                NodeSpans::expressionLocation(unit.nodeBuffer, tokens, trailingIfLayout.condition),
                formatTypeName(m_module, conditionType));
        }

        auto ifFrame = m_emitter.beginIf(materializeValue(checkedCondition), false);
        typeCheckStatement(unit, trailingIfLayout.statement);
        m_emitter.endIfThen(ifFrame);
        m_emitter.endIf(ifFrame);
    }

    void TypeChecker::typeCheckWhileStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto whileLayout = NodeLayout::whileStatement(unit.nodeBuffer, index);

        auto whileFrame = m_emitter.beginWhile();
        const auto checkedCondition = typeCheckExpression(unit, whileLayout.condition, ExpressionContext{});
        auto conditionType = coerceConditionType(checkedCondition.type, unit, whileLayout.condition);
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolWhileConditionError(
                unit.source,
                NodeSpans::expressionLocation(unit.nodeBuffer, tokens, whileLayout.condition),
                formatTypeName(m_module, conditionType));
        }

        m_emitter.beginWhileBody(whileFrame, materializeValue(checkedCondition));
        typeCheckStatement(unit, whileLayout.body);
        m_emitter.endWhile(whileFrame);
    }

    void TypeChecker::typeCheckReturnStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto declaredReturnType = m_currentReturnType;

        const auto value = NodeLayout::optionalChild(unit.nodeBuffer, index);
        if (value.hasChild)
        {
            // a constructor call builds the object in a temp slot and returns that
            auto pushedTempDestination = false;
            if (!currentScopeIsGlobal())
            {
                const auto constructedType = tryResolveConstructedType(unit, NodeLayout::stripGroupings(unit.nodeBuffer, value.child));
                if (constructedType != Type::Undefined())
                {
                    const auto destination = m_emitter.allocateTempAggregateSlot("constructed", constructedType);
                    if (destination.id() >= 0)
                    {
                        m_constructorDestinations.push_back(ConstructorDestination{ destination, true });
                        pushedTempDestination = true;
                    }
                }
            }

            auto checkedValue = CheckedValue{ Type::Undefined() };
            {
                auto context = ExpressionContext{};
                context.contextualNumberType = contextualTypeForExpectedType(declaredReturnType);
                checkedValue = typeCheckExpression(unit, value.child, context);
            }
            if (pushedTempDestination)
            {
                m_constructorDestinations.pop_back();
            }
            auto type = checkedValue.type;
            if (type.isReference())
            {
                // returning a ref is not allowed, so we need to coerce it to a value
                type = type.toValue();
            }

            recordType(unit, index, type);
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

            const auto strippedReturn = NodeLayout::stripGroupings(unit.nodeBuffer, value.child);
            if (unit.nodeBuffer.kind(strippedReturn) == NodeKind::ArrayLiteral
                && (type.kind() == TypeKind::FixedArray || type.kind() == TypeKind::DynamicArray))
            {
                // a literal materializes in a temp slot and returns that
                const auto destination = m_emitter.allocateTempAggregateSlot("temp", type);
                emitCapturedArrayLiteral(unit, strippedReturn, destination, type);
                checkedValue.value = m_emitter.emitLoad(destination, type);
            }

            m_emitter.emitReturnValue(m_emitter.emitWidened(materializeValue(checkedValue), type.toValue(), declaredReturnType.toValue()));
        }
        else
        {
            recordType(unit, index, Type::Void());
            m_emitter.emitReturn();

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

    TypeChecker::CheckedValue TypeChecker::typeCheckExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
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

                auto result = CheckedValue{ recordType(unit, index, literalType) };
                if (literalType == m_module.wellKnown().cstring)
                {
                    result.value = m_emitter.emitConstant(ConstantValue::FromString(escapedStringContent(unit, index)), literalType);
                }
                else if (literalType == m_module.wellKnown().string
                    && (!context.wantsAddress || context.receiverPosition))
                {
                    // a string literal used as a value is built in a temp slot and loaded back
                    const auto temp = m_emitter.allocateTempAggregateSlot("temp", literalType);
                    if (temp.id() >= 0)
                    {
                        m_emitter.emitStringLiteralIntoAddress(temp, escapedStringContent(unit, index));
                        result.value = m_emitter.emitLoad(temp, literalType);
                    }
                }
                return result;
            }
            case NodeKind::BoolLiteral:
            {
                auto result = CheckedValue{ recordType(unit, index, m_module.wellKnown().boolean) };
                const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
                const auto isTrue = TokenKindAt(tokens, unit.nodeBuffer.mainToken(index)) == TokenKind::TrueKeyword;
                result.folded = FoldValue{ isTrue };
                return result;
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
                return recordType(unit, index, Type::Discard());
            }
            default:
            {
                TODO("Missing Expression!!");
            }
        }
        return Type::Undefined();
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckGroupingExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        auto result = typeCheckExpression(unit, NodeLayout::groupedExpression(index), context);
        result.type = recordType(unit, index, result.type);
        return result;
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckUnaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.unary");
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto unaryOperator = ConvertToUnaryOperator(tokens.getToken(static_cast<i32>(buffer.mainToken(index))).kind);
        switch (unaryOperator)
        {
            case UnaryOperatorKind::LogicalNegation:
            {
                const auto checkedOperand = typeCheckExpression(unit, NodeLayout::unaryOperand(index), context);
                const auto type = checkedOperand.type;
                const auto operandType = type.toValue();
                const auto* operatorSignature = m_module.tryGetOperatorSignature(operandType, UnaryOperatorKind::LogicalNegation);
                if (operandType != Type::Undefined() && operatorSignature == nullptr)
                {
                    m_diagnostics.addUnaryOperandTypeMismatchError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        "!",
                        formatTypeName(m_module, operandType),
                        "a 'bool' operand");

                    return recordType(unit, index, Type::Undefined());
                }

                auto result = CheckedValue{ recordType(unit, index, type) };
                if (operatorSignature != nullptr && checkedOperand.folded.has_value() && operatorSignature->unaryFold != nullptr)
                {
                    const auto foldResult = operatorSignature->unaryFold(checkedOperand.folded.value());
                    if (foldResult.kind == FoldResultKind::Value)
                    {
                        result.folded = foldResult.value;
                        return result;
                    }

                    reportUnaryFoldFailure(unit, foldResult.kind, operandType, index);
                }

                result.value = m_emitter.emitUnary(UnaryOperatorKind::LogicalNegation, materializeValue(checkedOperand), type);
                return result;
            }
            case UnaryOperatorKind::ValueNegation:
            {
                const auto operand = NodeLayout::unaryOperand(index);

                auto operandContext = context;
                operandContext.negatedLiteral = buffer.kind(operand) == NodeKind::NumberLiteral;
                const auto checkedOperand = typeCheckExpression(unit, operand, operandContext);
                const auto type = checkedOperand.type;
                const auto operandType = type.toValue();
                const auto* operatorSignature = m_module.tryGetOperatorSignature(operandType, UnaryOperatorKind::ValueNegation);
                if (operandType != Type::Undefined() && operatorSignature == nullptr)
                {
                    m_diagnostics.addUnaryOperandTypeMismatchError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        "-",
                        formatTypeName(m_module, operandType),
                        "a numeric operand");

                    return recordType(unit, index, Type::Undefined());
                }

                auto result = CheckedValue{ recordType(unit, index, type) };
                if (checkedOperand.signFolded)
                {
                    // the literal already parsed negative, nothing to negate
                    result.folded = checkedOperand.folded;
                    result.value = checkedOperand.value;
                    return result;
                }

                if (operatorSignature != nullptr && checkedOperand.folded.has_value() && operatorSignature->unaryFold != nullptr)
                {
                    const auto foldResult = operatorSignature->unaryFold(checkedOperand.folded.value());
                    if (foldResult.kind == FoldResultKind::Value)
                    {
                        result.folded = foldResult.value;
                        return result;
                    }

                    reportUnaryFoldFailure(unit, foldResult.kind, operandType, index);
                }

                result.value = m_emitter.emitUnary(UnaryOperatorKind::ValueNegation, materializeValue(checkedOperand), type);
                return result;
            }
            case UnaryOperatorKind::ReferenceOf:
            {
                const auto operand = NodeLayout::unaryOperand(index);
                auto operandContext = context;
                operandContext.wantsAddress = true;
                auto type = typeCheckExpression(unit, operand, operandContext).type;
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
                auto result = CheckedValue{ recordType(unit, index, referenceType) };
                if (!context.wantsAddress && !operandIsMemberAccess)
                {
                    // a ref used as a value loads the value of its variable
                    // where an address is wanted, the consumer takes it from the node itself
                    result.value = m_emitter.emitRefValue(nameIdOf(unit, strippedOperand), type);
                    if (result.value.id() < 0)
                    {
                        result.value = m_emitter.readGlobal(nameOf(unit, strippedOperand));
                    }
                }
                return result;
            }
            default:
            {
                TODO("Missing UnaryOperatorKind!!");
                return Type::Undefined();
            }
        }
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckBinaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.binary");
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto binaryOperator = ConvertToBinaryOperator(tokens.getToken(static_cast<i32>(buffer.mainToken(index))).kind);
        const auto binaryLayout = NodeLayout::binary(buffer, index);
        switch (binaryOperator)
        {
            case BinaryOperatorKind::MemberAccess:
            {
                auto receiverContext = context;
                receiverContext.wantsAddress = true;
                receiverContext.receiverPosition = !context.wantsAddress || context.receiverPosition;
                const auto checkedLeft = typeCheckExpression(unit, binaryLayout.left, receiverContext);
                auto leftType = checkedLeft.type;
                if (leftType.kind() == TypeKind::Enum)
                {
                    auto& enumDefinition = m_module.getEnumDefinition(leftType);
                    if (buffer.kind(binaryLayout.right) == NodeKind::NameExpression)
                    {
                        const auto fieldName = nameOf(unit, binaryLayout.right);
                        const auto* field = enumDefinition.tryGetFieldByName(fieldName);
                        if (field == nullptr)
                        {
                            m_diagnostics.addUnknownEnumFieldError(
                                unit.source,
                                nameLocation(unit, binaryLayout.right),
                                enumDefinition.name(),
                                std::string(fieldName));
                            return Type::Undefined();
                        }

                        recordType(unit, index, leftType);
                        recordType(unit, binaryLayout.right, leftType);
                        auto result = CheckedValue{ leftType };
                        if (field->hasConstantValue())
                        {
                            if (const auto constant = m_emitter.makeEnumConstantValue(enumDefinition.baseType(), field->value()); constant.has_value())
                            {
                                result.value = m_emitter.emitEnumMemberConstant(leftType, enumDefinition.name(), std::string(fieldName), constant.value());
                            }
                        }
                        else if (const auto* fieldConstant = m_emitter.tryGetEnumFieldConstant(leftType.id(), std::string(fieldName)); fieldConstant != nullptr)
                        {
                            result.value = m_emitter.emitEnumMemberConstant(leftType, enumDefinition.name(), std::string(fieldName), fieldConstant->constant);
                        }
                        return result;
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

                            auto implicitReceiver = std::optional<ValueRef>{};
                            if (methodDefinition.functionType() == FunctionType::PublicMethod
                                || methodDefinition.functionType() == FunctionType::PrivateMethod
                                || (methodDefinition.functionType() == FunctionType::Intrinsic
                                    && !methodDefinition.parameters().empty()
                                    && methodDefinition.parameters().front().name() == ImplicitThisName))
                            {
                                auto receiverAddress = memberReceiverAddress(unit, binaryLayout.left);
                                if (receiverAddress.id() < 0 && memberLeftYieldsAddress(unit, binaryLayout.left))
                                {
                                    // the receiver was checked in address context, its value already is the address
                                    receiverAddress = checkedLeft.value;
                                }
                                if (receiverAddress.id() < 0)
                                {
                                    receiverAddress = m_emitter.spillToTempSlot(materializeValue(checkedLeft), leftType.toValue());
                                }
                                implicitReceiver = receiverAddress;
                            }

                            auto methodCallValue = ValueRef{};
                            auto methodCallFold = std::optional<FoldValue>{};
                            if (!typeCheckCallArguments(unit, binaryLayout.right, methodType, context, methodCallValue, methodCallFold, implicitReceiver))
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
                                recordType(unit, index, leftType);
                                recordType(unit, binaryLayout.right, leftType);
                                recordType(unit, nameExpressionIndex, methodType);
                                auto constructed = CheckedValue{ leftType };
                                constructed.value = methodCallValue;
                                return constructed;
                            }

                            recordType(unit, index, returnType);
                            recordType(unit, binaryLayout.right, returnType);
                            recordType(unit, nameExpressionIndex, methodType);
                            auto result = CheckedValue{ returnType };
                            result.value = methodCallValue;
                            result.folded = methodCallFold;
                            return result;
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
                        recordType(unit, binaryLayout.right, fieldType);
                        recordType(unit, index, fieldType);
                        auto result = CheckedValue{ fieldType };
                        if (context.receiverPosition || !context.wantsAddress)
                        {
                            auto receiverAddress = memberReceiverAddress(unit, binaryLayout.left);
                            if (receiverAddress.id() < 0 && memberLeftYieldsAddress(unit, binaryLayout.left))
                            {
                                receiverAddress = checkedLeft.value;
                            }
                            if (receiverAddress.id() < 0 && !context.wantsAddress)
                            {
                                // a temporary receiver spills so the field has an address
                                receiverAddress = m_emitter.spillToTempSlot(materializeValue(checkedLeft), leftType.toValue());
                            }
                            if (receiverAddress.id() >= 0)
                            {
                                const auto fieldAddress = m_emitter.emitFieldAddress(receiverAddress, leftType.toValue(), fieldDefinition.index(), fieldType);
                                if (context.receiverPosition)
                                {
                                    // a field used as a receiver returns its address, not its value
                                    result.value = fieldAddress;
                                }
                                else
                                {
                                    result.value = m_emitter.emitLoad(fieldAddress, fieldType);
                                }
                            }
                        }
                        return result;
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

                        auto arrayReceiver = memberReceiverAddress(unit, binaryLayout.left);
                        if (arrayReceiver.id() < 0)
                        {
                            if (memberLeftYieldsAddress(unit, binaryLayout.left))
                            {
                                // the receiver was checked in address context, its value already is the address
                                arrayReceiver = checkedLeft.value;
                            }
                            else
                            {
                                arrayReceiver = m_emitter.spillToTempSlot(materializeValue(checkedLeft), leftType.toValue());
                            }
                        }
                        return finishResolvedMethodCall(unit, index, binaryLayout.right, nameExpressionIndex, methodType, context, arrayReceiver);
                    }
                    else if (buffer.kind(binaryLayout.right) == NodeKind::NameExpression)
                    {
                        const auto memberName = nameOf(unit, binaryLayout.right);
                        if (memberName == ArrayLengthMemberName)
                        {
                            const auto lengthType = m_module.wellKnown().i32;
                            recordType(unit, binaryLayout.right, lengthType);
                            recordType(unit, index, lengthType);
                            auto result = CheckedValue{ lengthType };
                            if (receiverType.kind() == TypeKind::FixedArray && !context.wantsAddress)
                            {
                                // a fixed length is a compile time constant
                                const auto length = m_module.getArrayLength(receiverType);
                                result.folded = FoldValue{ length };
                                result.value = m_emitter.emitConstant(ConstantValue::FromI32(length), lengthType);
                            }
                            else if (!context.wantsAddress)
                            {
                                // slice and dynamic lengths load from the descriptor
                                auto receiverAddress = memberReceiverAddress(unit, binaryLayout.left);
                                if (receiverAddress.id() < 0 && memberLeftYieldsAddress(unit, binaryLayout.left))
                                {
                                    receiverAddress = checkedLeft.value;
                                }
                                if (receiverAddress.id() < 0)
                                {
                                    receiverAddress = m_emitter.spillToTempSlot(materializeValue(checkedLeft), receiverType);
                                }
                                if (receiverAddress.id() >= 0)
                                {
                                    const auto lengthAddress = m_emitter.emitFieldAddress(receiverAddress, receiverType, 1, lengthType);
                                    result.value = m_emitter.emitLoad(lengthAddress, lengthType);
                                }
                            }
                            return result;
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
                const auto checkedLeft = typeCheckExpression(unit, binaryLayout.left, context);
                auto leftType = checkedLeft.type;
                if (leftType.isReference())
                {
                    leftType = leftType.toValue();
                }
                const auto checkedRight = typeCheckExpression(unit, binaryLayout.right, context);
                auto rightType = checkedRight.type;
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

                    return recordType(unit, index, Type::Undefined());
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

                    return recordType(unit, index, Type::Undefined());
                }

                auto result = CheckedValue{ recordType(unit, index, operatorSignature->resultType) };
                if (checkedLeft.folded.has_value() && checkedRight.folded.has_value() && operatorSignature->binaryFold != nullptr)
                {
                    const auto foldResult = operatorSignature->binaryFold(checkedLeft.folded.value(), checkedRight.folded.value());
                    if (foldResult.kind == FoldResultKind::Value)
                    {
                        result.folded = foldResult.value;
                        return result;
                    }

                    reportBinaryFoldFailure(unit, foldResult.kind, leftType, index, binaryLayout.right);
                }

                auto leftValue = materializeValue(checkedLeft);
                auto rightValue = materializeValue(checkedRight);
                if (operatorSignature->callee != Type::Undefined())
                {
                    // runtime operators call their backing function by value
                    m_emitter.recordCalledPreludeFunction(operatorSignature->callee);
                    if (leftValue.id() >= 0 && rightValue.id() >= 0)
                    {
                        const ValueRef operands[] = { leftValue, rightValue };
                        result.value = m_emitter.emitCall(operatorSignature->callee.id(), operands, operatorSignature->resultType);
                    }
                    return result;
                }

                if (binaryOperator == BinaryOperatorKind::Division && leftValue.id() >= 0 && rightValue.id() >= 0)
                {
                    // convert the int operands to float for the float return type
                    const auto* operandDescription = m_module.tryGetBuiltinTypeDescription(leftType);
                    const auto* resultDescription = m_module.tryGetBuiltinTypeDescription(operatorSignature->resultType);
                    if (operandDescription != nullptr && operandDescription->kind == BuiltinTypeKind::Int
                        && resultDescription != nullptr && resultDescription->kind == BuiltinTypeKind::Float)
                    {
                        leftValue = m_emitter.emitIntToFloat(leftValue, leftType, operatorSignature->resultType);
                        rightValue = m_emitter.emitIntToFloat(rightValue, rightType, operatorSignature->resultType);
                    }
                }
                result.value = m_emitter.emitBinary(binaryOperator, leftValue, rightValue, operatorSignature->resultType, leftType);
                return result;
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
                const auto checkedLeft = typeCheckExpression(unit, binaryLayout.left, context);
                auto leftType = checkedLeft.type;
                if (leftType.isReference())
                {
                    leftType = leftType.toValue();
                }

                // and/or open the block for the right operand before it is checked
                // whether the whole expression folds and needs no blocks is only known afterwards
                auto shortCircuit = ShortCircuitFrame{};
                const auto isShortCircuitOperator = binaryOperator == BinaryOperatorKind::LogicalAnd || binaryOperator == BinaryOperatorKind::LogicalOr;
                if (isShortCircuitOperator && leftType == m_module.wellKnown().boolean)
                {
                    shortCircuit = m_emitter.beginShortCircuit(binaryOperator == BinaryOperatorKind::LogicalAnd, materializeValue(checkedLeft));
                }

                const auto checkedRight = typeCheckExpression(unit, binaryLayout.right, context);
                auto rightType = checkedRight.type;
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

                    return recordType(unit, index, Type::Undefined());
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

                    return recordType(unit, index, Type::Undefined());
                }

                auto result = CheckedValue{ recordType(unit, index, operatorSignature->resultType) };
                if (checkedLeft.folded.has_value() && checkedRight.folded.has_value() && operatorSignature->binaryFold != nullptr)
                {
                    const auto foldResult = operatorSignature->binaryFold(checkedLeft.folded.value(), checkedRight.folded.value());
                    if (foldResult.kind == FoldResultKind::Value)
                    {
                        m_emitter.discardShortCircuit(shortCircuit);
                        result.folded = foldResult.value;
                        return result;
                    }

                    reportBinaryFoldFailure(unit, foldResult.kind, lookupType, index, binaryLayout.right);
                }

                if (shortCircuit.active)
                {
                    result.value = m_emitter.endShortCircuit(shortCircuit, materializeValue(checkedRight));
                    return result;
                }

                auto leftValue = ValueRef{};
                auto rightValue = ValueRef{};
                if (!isShortCircuitOperator)
                {
                    leftValue = materializeValue(checkedLeft);
                    rightValue = materializeValue(checkedRight);
                }

                if (!isShortCircuitOperator && operatorSignature->callee != Type::Undefined())
                {
                    // runtime operators call their backing function by value
                    m_emitter.recordCalledPreludeFunction(operatorSignature->callee);
                    if (leftValue.id() >= 0 && rightValue.id() >= 0)
                    {
                        const ValueRef operands[] = { leftValue, rightValue };
                        result.value = m_emitter.emitCall(operatorSignature->callee.id(), operands, operatorSignature->resultType);
                    }
                }
                else if (!isShortCircuitOperator && leftType == m_module.wellKnown().cstring
                    && leftType != Type::Undefined())
                {
                    result.value = m_emitter.emitCStringEquality(operation, leftValue, rightValue);
                }
                else if (!isShortCircuitOperator)
                {
                    result.value = m_emitter.emitBinary(operation, leftValue, rightValue, operatorSignature->resultType, leftType);
                }
                return result;
            }
            default:
            {
                TODO("Missing BinaryOperatornKind!!");
                return Type::Undefined();
            }
        }

        return Type::Undefined();
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckNameExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.name");
        // one lookup covers the type, the kind and marking it read
        const auto nameId = nameIdOf(unit, index);
        if (auto* binding = tryGetBindingMutable(nameId); binding != nullptr)
        {
            const auto isLeadingDotRoot = m_leadingDotRootPending;
            m_leadingDotRootPending = false;
            if (!isLeadingDotRoot && binding->kind == VariableBindingKind::Field)
            {
                m_diagnostics.addFieldAccessMissingDotError(unit.source, nameLocation(unit, index), std::string(nameOf(unit, index)));
                return Type::Undefined();
            }

            binding->wasRead = true;
            auto result = CheckedValue{ recordType(unit, index, binding->type) };
            if (binding->kind == VariableBindingKind::Field)
            {
                if (m_currentType != Type::Undefined() && (context.receiverPosition || !context.wantsAddress))
                {
                    const auto thisAddress = m_emitter.addressOfLocal(m_thisNameId);
                    const auto& fieldDefinition = m_module.getTypeDefinition(m_currentType).tryGetFieldByName(nameOf(unit, index));
                    if (thisAddress.id() >= 0 && fieldDefinition.type() != Type::Undefined())
                    {
                        const auto fieldAddress = m_emitter.emitFieldAddress(thisAddress, m_currentType, fieldDefinition.index(), fieldDefinition.type());
                        if (context.receiverPosition)
                        {
                            // a field used as a receiver returns its address, not its value
                            result.value = fieldAddress;
                        }
                        else
                        {
                            result.value = m_emitter.emitLoad(fieldAddress, fieldDefinition.type());
                        }
                    }
                }
                return result;
            }
            if (context.wantsAddress)
            {
                // a local without a slot returns its value, the consumer can spill it
                // a local with a slot returns nothing, the consumer takes the address from the node
                result.value = m_emitter.peekLocal(nameId);
                return result;
            }
            if (binding->kind == VariableBindingKind::LocalConstant)
            {
                result.folded = binding->folded;
            }
            if (binding->scopeIndex == 0 && !currentScopeIsGlobal())
            {
                result.value = m_emitter.readGlobal(nameOf(unit, index));
            }
            else
            {
                result.value = m_emitter.readLocal(nameId);
            }
            return result;
        }

        const auto name = nameOf(unit, index);
        auto type = m_module.tryGetTypeByName(name);
        if (type != Type::Undefined())
        {
            return recordType(unit, index, type);
        }

        m_diagnostics.addUnknownNameError(unit.source, nameLocation(unit, index), std::string(name));

        return Type::Undefined();
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckFunctionCallExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.call");
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

    TypeChecker::CheckedValue TypeChecker::typeCheckResolvedFunctionCall(const CompilationUnit& unit, u32 index, Type functionType, const ExpressionContext& context)
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

        auto emittedCallValue = ValueRef{};
        auto emittedCallFold = std::optional<FoldValue>{};
        if (!typeCheckCallArguments(unit, index, functionType, context, emittedCallValue, emittedCallFold))
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

        auto result = CheckedValue{ recordType(unit, index, returnType) };
        result.value = emittedCallValue;
        recordType(unit, NodeLayout::subtreeBegin(unit.nodeBuffer, index), functionType);
        return result;
    }

    TypeChecker::CheckedValue TypeChecker::finishResolvedMethodCall(const CompilationUnit& unit, u32 index, u32 callIndex, u32 nameExpressionIndex, Type methodType, const ExpressionContext& context, std::optional<ValueRef> implicitArgument)
    {
        auto callValue = ValueRef{};
        auto callFold = std::optional<FoldValue>{};
        if (!typeCheckCallArguments(unit, callIndex, methodType, context, callValue, callFold, implicitArgument))
        {
            return Type::Undefined();
        }

        const auto& returnTypes = m_module.getFunctionDefinition(methodType).returnTypes();
        auto returnType = Type::Void();
        if (returnTypes.size() == 1)
        {
            returnType = returnTypes[0];
        }

        recordType(unit, index, returnType);
        recordType(unit, callIndex, returnType);
        recordType(unit, nameExpressionIndex, methodType);
        auto result = CheckedValue{ returnType };
        result.value = callValue;
        result.folded = callFold;
        return result;
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckMemberAccessExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.memberAccess");
        if (currentScopeKind() == ScopeKind::StaticMethod)
        {
            m_diagnostics.addMemberAccessInStaticMethodError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index),
                m_currentFunctionName);

            return recordType(unit, index, Type::Undefined());
        }

        if (m_currentType == Type::Undefined())
        {
            m_diagnostics.addMemberAccessInDefaultParameterError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index));

            return recordType(unit, index, Type::Undefined());
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

                return recordType(unit, index, Type::Undefined());
            }

            auto result = typeCheckResolvedFunctionCall(unit, inner, methodType, context);
            result.type = recordType(unit, index, result.type);
            return result;
        }

        m_leadingDotRootPending = true;
        auto result = typeCheckExpression(unit, inner, context);
        result.type = recordType(unit, index, result.type);
        return result;
    }

    bool TypeChecker::typeCheckCallArguments(const CompilationUnit& unit, u32 callIndex, Type functionType, const ExpressionContext& context, ValueRef& emittedCallValue, std::optional<FoldValue>& emittedCallFold, std::optional<ValueRef> implicitArgument)
    {
        CARACAL_ZONE_NAMED("check.callArguments");
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

        struct EmittedArgument
        {
            u32 node;
            ValueRef value;
            std::optional<FoldValue> folded;
        };
        std::vector<EmittedArgument> argumentValues;
        argumentValues.reserve(argumentsEnd - argumentsStart);
        for (auto argumentPosition = argumentsStart; argumentPosition < argumentsEnd; ++argumentPosition)
        {
            const auto argumentValue = NodeLayout::argumentValue(buffer, m_callArgumentStack[argumentPosition]);
            // resolve again every iteration, the previous argument's walk may have moved the definition store
            const auto& parameters = m_module.getFunctionDefinition(functionType).parameters();

            auto contextualType = std::optional<Type>{};
            auto parameterWantsAddress = false;
            auto isVariadicArgument = true;
            for (size_t i = 0; i < fixedParameterCount; ++i)
            {
                if (m_orderedSlotStack[orderedStart + i] != argumentValue)
                {
                    continue;
                }

                contextualType = contextualTypeForExpectedType(parameters[i + parameterOffset].type());
                parameterWantsAddress = parameters[i + parameterOffset].type().isReference();
                if (!parameterWantsAddress
                    && parameters[i + parameterOffset].type().kind() == TypeKind::DynamicArray
                    && unit.nodeBuffer.kind(NodeLayout::stripGroupings(buffer, argumentValue)) == NodeKind::NameExpression)
                {
                    // a dynamic array passed by name is deep copied through its address when the call is emitted
                    // the argument is not loaded
                    parameterWantsAddress = true;
                }
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

            // a constructor call as argument builds the object in a temp slot and passes it to the call
            auto pushedTempDestination = false;
            if (!currentScopeIsGlobal())
            {
                const auto constructedType = tryResolveConstructedType(unit, NodeLayout::stripGroupings(buffer, argumentValue));
                if (constructedType != Type::Undefined())
                {
                    const auto destination = m_emitter.allocateTempAggregateSlot("constructed", constructedType);
                    if (destination.id() >= 0)
                    {
                        m_constructorDestinations.push_back(ConstructorDestination{ destination, true });
                        pushedTempDestination = true;
                    }
                }
            }

            auto argumentContext = context;
            argumentContext.contextualNumberType = contextualType;
            argumentContext.wantsAddress = parameterWantsAddress;
            argumentContext.receiverPosition = false;
            const auto checkedArgument = typeCheckExpression(unit, argumentValue, argumentContext);
            if (pushedTempDestination)
            {
                m_constructorDestinations.pop_back();
            }
            argumentValues.push_back(EmittedArgument{ argumentValue, checkedArgument.value, checkedArgument.folded });
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
                    // either side being undefined means its error was already reported
                    if (defaultType != Type::Undefined() 
                        && expectedType != Type::Undefined()
                        && !isAssignableTo(defaultType, expectedType))
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
            // either side being undefined means its error was already reported
            if (argumentType != Type::Undefined() 
                && expectedType != Type::Undefined()
                && !isAssignableTo(argumentType, expectedType))
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
            // an undefined argument means its error was already reported
            if (argumentType == Type::Undefined())
            {
                popFrames();
                return false;
            }
            if (argumentType == Type::Void())
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

        m_emitter.recordCalledPreludeFunction(functionType);

        const auto intrinsicKind = checkedDefinition.intrinsicKind();
        const auto isElementIntrinsic = intrinsicKind == IntrinsicKind::ArrayAt
            || intrinsicKind == IntrinsicKind::ArraySet;
        const auto isSliceIntrinsic = intrinsicKind == IntrinsicKind::ArraySlice;
        const auto isToCStringIntrinsic = intrinsicKind == IntrinsicKind::StringToCString;
        const auto isGrowthIntrinsic = intrinsicKind == IntrinsicKind::ArrayAdd
            || intrinsicKind == IntrinsicKind::ArrayRemove;
        const auto isBitwiseIntrinsic = intrinsicKind == IntrinsicKind::BitAnd
            || intrinsicKind == IntrinsicKind::BitOr
            || intrinsicKind == IntrinsicKind::BitXor
            || intrinsicKind == IntrinsicKind::BitNot
            || intrinsicKind == IntrinsicKind::ShiftLeft
            || intrinsicKind == IntrinsicKind::ShiftRight;

        auto foldedCall = std::optional<FoldValue>{};
        if (isBitwiseIntrinsic)
        {
            std::vector<const FoldValue*> foldedArguments(fixedParameterCount, nullptr);
            for (size_t i = 0; i < fixedParameterCount; ++i)
            {
                const auto argumentNode = m_orderedSlotStack[orderedStart + i];
                for (const auto& argument : argumentValues)
                {
                    if (argument.node == argumentNode && argument.folded.has_value())
                    {
                        foldedArguments[i] = &argument.folded.value();
                    }
                }
            }

            auto foldResult = FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
            if (intrinsicKind == IntrinsicKind::BitNot)
            {
                if (fixedParameterCount >= 1 && foldedArguments[0] != nullptr)
                {
                    foldResult = foldBitNot(*foldedArguments[0]);
                }
            }
            else if (fixedParameterCount >= 2 && foldedArguments[0] != nullptr && foldedArguments[1] != nullptr)
            {
                switch (intrinsicKind)
                {
                    case IntrinsicKind::BitAnd:
                    {
                        foldResult = foldBitAnd(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    case IntrinsicKind::BitOr:
                    {
                        foldResult = foldBitOr(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    case IntrinsicKind::BitXor:
                    {
                        foldResult = foldBitXor(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    case IntrinsicKind::ShiftLeft:
                    {
                        foldResult = foldShiftLeft(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    case IntrinsicKind::ShiftRight:
                    {
                        foldResult = foldShiftRight(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }

            if (foldResult.kind == FoldResultKind::Value)
            {
                foldedCall = foldResult.value;
            }
            else
            {
                m_sawUnfoldedBitwiseCall = true;
            }
        }

        // the call is not emitted when it folded, a parameter got no argument or the receiver is missing
        const auto calleeKind = checkedDefinition.functionType();
        auto canEmitCall = !foldedCall.has_value()
            && (isBitwiseIntrinsic
                || isElementIntrinsic
                || isSliceIntrinsic
                || isGrowthIntrinsic
                || isToCStringIntrinsic
                || checkedDefinition.symbolName().has_value()
                || calleeKind == FunctionType::FreeFunction
                || calleeKind == FunctionType::StaticMethod
                || calleeKind == FunctionType::PublicMethod
                || calleeKind == FunctionType::PrivateMethod
                || calleeKind == FunctionType::SynthesizedConstructor);

        auto receiverAddress = ValueRef{};
        const auto needsReceiver = calleeKind == FunctionType::PublicMethod
            || calleeKind == FunctionType::PrivateMethod
            || calleeKind == FunctionType::SynthesizedConstructor;
        if (canEmitCall && (isElementIntrinsic || isSliceIntrinsic || isGrowthIntrinsic || isToCStringIntrinsic)
            && (!implicitArgument.has_value() || implicitArgument.value().id() < 0))
        {
            canEmitCall = false;
        }
        if (canEmitCall && needsReceiver)
        {
            if (implicitArgument.has_value())
            {
                receiverAddress = implicitArgument.value();
            }
            else if (calleeKind == FunctionType::SynthesizedConstructor)
            {
                if (!m_constructorDestinations.empty())
                {
                    receiverAddress = m_constructorDestinations.back().address;
                }
            }
            else
            {
                receiverAddress = m_emitter.addressOfLocal(m_thisNameId);
            }

            if (receiverAddress.id() < 0)
            {
                canEmitCall = false;
            }
        }
        const auto callArgumentStart = m_callArgumentValues.size();
        const CallArgumentScope callArgumentScope{ m_callArgumentValues, callArgumentStart };
        if (canEmitCall)
        {
            for (size_t i = 0; i < fixedParameterCount; ++i)
            {
                const auto argumentNode = m_orderedSlotStack[orderedStart + i];
                const auto& parameter = checkedParameters[i + parameterOffset];
                if (argumentNode == UnboundArgumentSlot)
                {
                    canEmitCall = false;
                    break;
                }

                if (argumentNode == DefaultBoundArgumentSlot)
                {
                    // an omitted argument re-emits the default's folded value at the call site
                    auto emittedDefault = ValueRef{};
                    if (parameter.hasDefaultNode())
                    {
                        const auto defaultNode = parameter.defaultValueNode();
                        emittedDefault = m_emitter.emitDefaultArgument(
                            defaultNode.fileId,
                            defaultNode.index,
                            m_module.typeOfNode(defaultNode).toValue(),
                            parameter.type().toValue());
                        if (emittedDefault.id() < 0)
                        {
                            // a non-scalar default has no fold, its expression emits here
                            emittedDefault = emitDefaultArgumentExpression(defaultNode, parameter.type());
                        }
                    }
                    if (emittedDefault.id() < 0)
                    {
                        canEmitCall = false;
                        break;
                    }
                    m_callArgumentValues.push_back(emittedDefault);
                    continue;
                }

                if (parameter.type().isReference())
                {
                    const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
                    const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, argumentNode);
                    auto addressNode = stripped;
                    auto aliasesConstant = false;
                    if (unit.nodeBuffer.kind(stripped) == NodeKind::UnaryExpression
                        && TokenKindAt(tokens, unit.nodeBuffer.mainToken(stripped)) == TokenKind::RefKeyword)
                    {
                        addressNode = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(stripped));
                        if (unit.nodeBuffer.kind(addressNode) == NodeKind::NameExpression)
                        {
                            const auto* innerBinding = tryGetBinding(nameIdOf(unit, addressNode));
                            aliasesConstant = innerBinding != nullptr
                                && (innerBinding->kind == VariableBindingKind::LocalConstant || innerBinding->referencesConstant);
                        }
                    }
                    else if (unit.nodeBuffer.kind(stripped) == NodeKind::NameExpression)
                    {
                        const auto* passthroughBinding = tryGetBinding(nameIdOf(unit, stripped));
                        aliasesConstant = passthroughBinding != nullptr && passthroughBinding->referencesConstant;
                    }

                    auto address = addressOfExpression(unit, addressNode);
                    if (aliasesConstant)
                    {
                        address = m_emitter.emitDefensiveRefCopy(address, parameter.type().toValue());
                    }
                    if (address.id() < 0)
                    {
                        canEmitCall = false;
                        break;
                    }
                    m_callArgumentValues.push_back(address);
                    continue;
                }

                const auto argumentType = m_module.typeOfNode(NodeIndex{ unit.fileId, argumentNode }).toValue();

                {
                    // a literal argument materializes in a temp slot and loads back
                    const auto strippedArgument = NodeLayout::stripGroupings(unit.nodeBuffer, argumentNode);
                    if (unit.nodeBuffer.kind(strippedArgument) == NodeKind::ArrayLiteral
                        && (argumentType.kind() == TypeKind::FixedArray || argumentType.kind() == TypeKind::DynamicArray))
                    {
                        const auto temp = m_emitter.allocateTempAggregateSlot("temp", argumentType);
                        emitCapturedArrayLiteral(unit, strippedArgument, temp, argumentType);
                        const auto loaded = m_emitter.emitLoad(temp, argumentType);
                        if (loaded.id() < 0)
                        {
                            canEmitCall = false;
                            break;
                        }
                        m_callArgumentValues.push_back(m_emitter.emitWidened(loaded, argumentType, parameter.type().toValue()));
                        continue;
                    }
                }

                if (parameter.type().kind() == TypeKind::DynamicArray && !parameter.type().isReference())
                {
                    // owning dynamic arrays deep copy on the way into a call
                    const auto strippedArgument = NodeLayout::stripGroupings(unit.nodeBuffer, argumentNode);
                    if (unit.nodeBuffer.kind(strippedArgument) == NodeKind::NameExpression)
                    {
                        auto sourceAddress = m_emitter.addressOfLocal(nameIdOf(unit, strippedArgument));
                        if (sourceAddress.id() < 0)
                        {
                            sourceAddress = m_emitter.emitGlobalAddress(nameOf(unit, strippedArgument));
                        }
                        const auto copied = m_emitter.emitArrayCopy(sourceAddress, parameter.type().toValue());
                        if (copied.id() < 0)
                        {
                            canEmitCall = false;
                            break;
                        }
                        m_callArgumentValues.push_back(copied);
                        continue;
                    }
                }

                auto argumentRef = ValueRef{};
                for (const auto& argument : argumentValues)
                {
                    if (argument.node != argumentNode)
                    {
                        continue;
                    }

                    argumentRef = argument.value;
                    if (argumentRef.id() < 0 && argument.folded.has_value())
                    {
                        argumentRef = m_emitter.emitFoldedConstant(argument.folded.value(), argumentType);
                    }
                    break;
                }
                if (argumentRef.id() < 0)
                {
                    canEmitCall = false;
                    break;
                }
                m_callArgumentValues.push_back(m_emitter.emitWidened(argumentRef, argumentType, parameter.type().toValue()));
            }

            // variadic arguments
            for (size_t i = 0; canEmitCall && i < binding.variadic.size(); ++i)
            {
                const auto variadicNode = binding.variadic[i];
                auto variadicRef = ValueRef{};
                for (const auto& argument : argumentValues)
                {
                    if (argument.node != variadicNode)
                    {
                        continue;
                    }

                    variadicRef = argument.value;
                    if (variadicRef.id() < 0 && argument.folded.has_value())
                    {
                        variadicRef = m_emitter.emitFoldedConstant(argument.folded.value(), m_module.typeOfNode(NodeIndex{ unit.fileId, variadicNode }).toValue());
                    }
                    break;
                }
                if (variadicRef.id() < 0)
                {
                    canEmitCall = false;
                    break;
                }
                m_callArgumentValues.push_back(variadicRef);
            }
        }

        if (!foldedCall.has_value() && !canEmitCall)
        {
            // TODO add diagnostics for calls the emitter cannot lower
            m_emitter.recordEmissionFailure();
        }

        if (foldedCall.has_value())
        {
            auto callReturnType = Type::Void();
            if (!checkedDefinition.returnTypes().empty())
            {
                callReturnType = checkedDefinition.returnTypes().front();
            }
            emittedCallFold = foldedCall;
            emittedCallValue = m_emitter.emitFoldedConstant(foldedCall.value(), callReturnType.toValue());
        }
        else if (canEmitCall)
        {
            auto callReturnType = Type::Void();
            if (!checkedDefinition.returnTypes().empty())
            {
                callReturnType = checkedDefinition.returnTypes().front();
            }
            if (needsReceiver)
            {
                m_callArgumentValues.insert(m_callArgumentValues.begin() + static_cast<std::ptrdiff_t>(callArgumentStart), receiverAddress);
            }
            const auto callArguments = std::span<const ValueRef>{ m_callArgumentValues }.subspan(callArgumentStart);
            if (isElementIntrinsic)
            {
                const auto receiverType = checkedDefinition.parameters().front().type().toBaseType();
                if (!callArguments.empty())
                {
                    if (intrinsicKind == IntrinsicKind::ArrayAt)
                    {
                        emittedCallValue = m_emitter.emitElementAccess(
                            implicitArgument.value(), receiverType, callArguments[0], callReturnType.toValue(), context.receiverPosition);
                    }
                    else if (callArguments.size() >= 2)
                    {
                        const auto elementAddress = m_emitter.emitElementAddressForReceiver(implicitArgument.value(), receiverType, callArguments[0]);
                        m_emitter.emitStoreValue(callArguments[1], elementAddress, checkedDefinition.parameters()[2].type());
                    }
                }
            }
            else if (isToCStringIntrinsic)
            {
                const auto receiverType = checkedDefinition.parameters().front().type().toBaseType();
                emittedCallValue = m_emitter.emitStringToCString(implicitArgument.value(), receiverType);
            }
            else if (isGrowthIntrinsic)
            {
                const auto receiverType = checkedDefinition.parameters().front().type().toBaseType();
                if (!callArguments.empty())
                {
                    if (intrinsicKind == IntrinsicKind::ArrayAdd)
                    {
                        m_emitter.emitArrayAdd(implicitArgument.value(), callArguments[0], receiverType);
                    }
                    else
                    {
                        m_emitter.emitArrayRemove(implicitArgument.value(), callArguments[0], receiverType);
                    }
                }
            }
            else if (isSliceIntrinsic)
            {
                emittedCallValue = m_emitter.emitSliceIntrinsic(
                    implicitArgument.value(), checkedDefinition.parameters().front().type().toBaseType(), callReturnType.toValue());
            }
            else if (isBitwiseIntrinsic)
            {
                emittedCallValue = m_emitter.emitBitwiseIntrinsic(intrinsicKind, callArguments, callReturnType.toValue());
            }
            else
            {
                emittedCallValue = m_emitter.emitCall(functionType.id(), callArguments, callReturnType);
            }

            if (calleeKind == FunctionType::SynthesizedConstructor
                && !m_constructorDestinations.empty()
                && m_constructorDestinations.back().loadResult)
            {
                emittedCallValue = m_emitter.emitLoad(m_constructorDestinations.back().address, checkedDefinition.parentType());
            }
        }

        popFrames();
        return true;
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckNumberLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.numberLiteral");
        auto numberType = Type::Undefined();
        auto literalConsumedSign = false;
        auto parsedValue = std::optional<NumberLiteralValue>{};
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
                    parsedValue = NumberLiteralValue{ negatedValue.value() };
                    literalConsumedSign = true;
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

        auto result = CheckedValue{ recordType(unit, index, numberType) };
        if (parsedValue.has_value())
        {
            result.signFolded = literalConsumedSign;
            result.folded = MakeFoldValue(parsedValue.value());
        }
        return result;
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckArrayLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
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
                // the empty fill still allocates storage, register the capture
                m_arrayLiteralElements[ArrayLiteralKey(unit.fileId, index)].clear();
                return recordType(unit, index, arrayType);
            }

            m_diagnostics.addEmptyArrayLiteralError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index));
            return recordType(unit, index, Type::Undefined());
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
            auto& capturedElements = m_arrayLiteralElements[ArrayLiteralKey(unit.fileId, index)];
            capturedElements.clear();
            for (auto position = childrenStart; position < childrenEnd; ++position)
            {
                const auto element = m_childrenStack[position];
                const auto checkedElement = typeCheckExpression(unit, element, elementContext);
                capturedElements.push_back(CapturedArrayElement{ element, checkedElement.value, checkedElement.folded });
                const auto type = checkedElement.type;
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
            return recordType(unit, index, Type::Undefined());
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
        return recordType(unit, index, arrayType);
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
                recordType(unit, conditionIndex, m_module.wellKnown().boolean);
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
