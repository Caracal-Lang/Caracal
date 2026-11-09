#include <Caracal/Semantic/TypeChecker.h>
#include <Caracal/Profiling.h>

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/ScopedValue.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/StringEscapes.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Text/AsciiName.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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

    static bool IsInitConstantAssignmentSite(std::string_view functionName)
    {
        return functionName == EntryPointFunctionName || functionName == UserMainFunctionName;
    }

    [[nodiscard]] static const SourceLocation& TokenLocationAt(const TokenBuffer& tokens, u32 tokenIndex) noexcept
    {
        return tokens.getSourceLocation(tokens.getToken(static_cast<i32>(tokenIndex)));
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

    // __caracal_test_<file name>_<test name>
    [[nodiscard]] static std::string TestFunctionBaseName(const std::filesystem::path& filePath, std::string_view testName)
    {
        auto name = std::string(TestFunctionNamePrefix);
        const auto stem = filePath.stem().u8string();
        name.append(stem.begin(), stem.end());
        if (!stem.empty() && !testName.empty())
        {
            name.push_back('_');
        }
        name += testName;
        replaceNonAsciiCharacters(name);
        return name;
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
            m_emitter.storeLocalsInSlots();
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
        copyTestsStubParameters();
        typeCheckTypeSignatures();
        typeCheckEnumDefinitions();
        typeCheckGlobalConstants();
        typeCheckTypeFieldDefinitions();
        reportTypesThatContainThemselves();
        typeCheckFunctionDefinitions();
        typeCheckTestDefinitions();
        typeCheckTypeMethodDefinitions();

        checkUninitializedInitConstants();
        typeCheckTestsStub();

        if (!m_isPreludePass)
        {
            m_emitter.finishModule();
        }

        reportLoweringFailure();

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
                case NodeKind::TestDefinitionStatement:
                {
                    collectTestDeclaration(unit, index);
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
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);
        auto parameters = collectParameterNames(unit, functionLayout.parameters);
        auto& functionDefinition = m_module.createFunction(functionName, std::move(parameters), std::vector<Type>());
        functionDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, functionDefinition.type());
        m_functionDeclarations.push_back(NodeIndex{ unit.fileId, index });

        if (functionName == TestsStubName && m_testsStubDeclaration.index == InvalidNodeIndex)
        {
            m_testsStubDeclaration = NodeIndex{ unit.fileId, index };
        }
    }

    void TypeChecker::collectTestDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto baseName = TestFunctionBaseName(unit.source->filePath, escapedStringContent(unit, index));

        // we need to append a number in case the name is already in use
        auto functionName = baseName;
        for (auto suffix = 1; m_module.tryGetOverloadSet(functionName) != nullptr; ++suffix)
        {
            functionName = baseName + "_" + std::to_string(suffix);
        }

        auto& functionDefinition = m_module.createFunction(functionName, std::vector<Parameter>(), std::vector<Type>());
        functionDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, functionDefinition.type());
        m_testDeclarations.push_back(NodeIndex{ unit.fileId, index });
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
        if (!m_options.recordsSourceLocations || unit.origin == UnitOrigin::Prelude)
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

    std::optional<SourceLocation> TypeChecker::methodNameLocation(const FunctionDefinition& methodDefinition) const noexcept
    {
        const auto methodNode = methodDefinition.definitionNode();
        if (methodNode.index == InvalidNodeIndex)
        {
            return std::nullopt;
        }

        const auto& buffer = m_compilationContext.nodeBuffer(methodNode.fileId);
        const auto methodLayout = NodeLayout::methodDefinition(buffer, methodNode.index);
        return m_compilationContext.sourceLocation(methodNode.fileId, buffer.mainToken(methodLayout.methodName));
    }

    CompilationUnit& TypeChecker::unitFor(NodeIndex index) const noexcept
    {
        return m_compilationContext.units()[index.fileId];
    }

    ValueRef TypeChecker::dynamicArraySourceAddress(const CompilationUnit& unit, u32 nameNode, Type arrayType)
    {
        const auto address = addressOfName(unit, nameNode);
        if (address.id() >= 0)
        {
            return address;
        }

        const auto value = m_emitter.peekLocal(nameIdOf(unit, nameNode));
        if (value.id() >= 0)
        {
            return m_emitter.spillToTempSlot(value, arrayType);
        }

        return ValueRef{};
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

        reportInvalidOverloadSets();
        for (const auto functionDeclaration : m_functionDeclarations)
        {
            const auto& unit = unitFor(functionDeclaration);
            typeCheckFunctionDefaultValues(unit, functionDeclaration.index);
        }
    }

    void TypeChecker::typeCheckFunctionSignature(const CompilationUnit& unit, u32 index)
    {
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);

        pushScope(ScopeKind::Function);
        auto parameters = typeCheckParametersNode(unit, functionLayout.parameters, false);
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
        functionDefinition.setIsExtern(isExtern);
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

    void TypeChecker::typeCheckSourceTextDefault(const CompilationUnit& unit, u32 defaultIndex, std::span<const Parameter> earlierParameters)
    {
        const auto namedParameter = nameOf(unit, defaultIndex);
        for (const auto& parameter : earlierParameters)
        {
            if (parameter.name() == namedParameter)
            {
                return;
            }
        }

        m_diagnostics.addSourceTextMustReferToEarlierParameterError(unit.source, nameLocation(unit, defaultIndex), std::string(namedParameter));
    }

    void TypeChecker::typeCheckDefaultValue(const CompilationUnit& unit, u32 parameterIndex, Type parameterType, std::span<const Parameter> earlierParameters)
    {
        const auto parameterLayout = NodeLayout::parameter(unit.nodeBuffer, parameterIndex);
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
        if (unit.nodeBuffer.kind(parameterLayout.defaultValue) == NodeKind::SourceTextDefault)
        {
            typeCheckSourceTextDefault(unit, parameterLayout.defaultValue, earlierParameters);
        }
    }

    void TypeChecker::typeCheckFunctionDefaultValues(const CompilationUnit& unit, u32 index)
    {
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);
        const auto& parameters = m_module.getFunctionDefinition(m_module.typeOfNode(NodeIndex{ unit.fileId, index })).parameters();
        auto parameterNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, functionLayout.parameters, parameterNodes);

        pushScope(ScopeKind::Function);
        for (size_t position = 0; position < parameterNodes.size(); ++position)
        {
            if (NodeLayout::parameter(unit.nodeBuffer, parameterNodes[position]).hasDefaultValue)
            {
                typeCheckDefaultValue(unit, parameterNodes[position], parameters[position].type(), std::span<const Parameter>(parameters.data(), position));
            }
        }
        popScope(false);
    }

    std::vector<Parameter> TypeChecker::typeCheckParametersNode(const CompilationUnit& unit, u32 index, bool checksDefaultValues)
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
                if (checksDefaultValues)
                {
                    typeCheckDefaultValue(unit, parameterIndex, parameterType, parameters);
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

            // a parameter can have the name of a global
            const auto* existing = tryGetBinding(nameId);
            if (existing == nullptr || existing->scopeIndex != static_cast<u32>(m_scopeFrames.size() - 1))
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

    void TypeChecker::pushConstructorDestination(const CompilationUnit& unit, u32 constructorExpression, ValueRef address, bool loadResult)
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, constructorExpression);
        const auto call = NodeLayout::binary(unit.nodeBuffer, stripped).right;
        m_constructorDestinations.push_back(ConstructorDestination{ NodeIndex{ unit.fileId, call }, address, loadResult });
    }

    ValueRef TypeChecker::memberReceiverAddress(const CompilationUnit& unit, u32 leftIndex)
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, leftIndex);
        if (unit.nodeBuffer.kind(stripped) != NodeKind::NameExpression)
        {
            return ValueRef{};
        }

        return addressOfName(unit, stripped);
    }

    ValueRef TypeChecker::arrayReceiverAddress(const CompilationUnit& unit, u32 leftIndex, const CheckedValue& checkedLeft, Type receiverType)
    {
        auto address = memberReceiverAddress(unit, leftIndex);
        if (address.id() < 0 && memberLeftYieldsAddress(unit, leftIndex))
        {
            // the receiver was checked in address context, its value already is the address
            address = checkedLeft.value;
        }
        if (address.id() >= 0)
        {
            return address;
        }

        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, leftIndex);
        if (unit.nodeBuffer.kind(stripped) == NodeKind::ArrayLiteral)
        {
            return emitArrayLiteralToTempSlot(unit, stripped, receiverType);
        }

        return m_emitter.spillToTempSlot(materializeValue(checkedLeft), receiverType);
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
            return isArrayAtCall(unit, strippedLayout.right);
        }

        if (unit.nodeBuffer.kind(strippedLayout.right) == NodeKind::NameExpression)
        {
            return m_module.typeOfNode(NodeIndex{ unit.fileId, strippedLayout.left }).toValue().kind() == TypeKind::Type;
        }

        return false;
    }

    bool TypeChecker::isArrayAtCall(const CompilationUnit& unit, u32 callIndex) const
    {
        const auto methodType = m_module.typeOfNode(NodeIndex{ unit.fileId, NodeLayout::subtreeBegin(unit.nodeBuffer, callIndex) });
        const auto* definition = m_module.tryGetFunctionDefinition(methodType);
        return definition != nullptr && definition->intrinsicKind() == IntrinsicKind::ArrayAt;
    }

    void TypeChecker::reportWriteToConstantReceiver(const CompilationUnit& unit, u32 receiverIndex, u32 targetIndex)
    {
        auto rootName = std::string{};
        auto rootReferencesConstant = false;
        if (!receiverChainIsConstant(unit, receiverIndex, rootName, rootReferencesConstant))
        {
            return;
        }

        const auto location = NodeSpans::expressionLocation(unit.nodeBuffer, m_compilationContext.tokenBuffer(unit.fileId), targetIndex);
        if (rootReferencesConstant)
        {
            m_diagnostics.addAssignmentThroughConstantReferenceError(unit.source, location, rootName);
        }
        else
        {
            m_diagnostics.addAssignmentToConstantError(unit.source, location, rootName);
        }
    }

    bool TypeChecker::returnedNameNeedsCopy(const CompilationUnit& unit, u32 index, Type type) const
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, index);
        const auto needsCopy = type.toValue().kind() == TypeKind::DynamicArray || typeHasDynamicArrayField(type);
        if (!needsCopy || unit.nodeBuffer.kind(stripped) != NodeKind::NameExpression)
        {
            return false;
        }

        const auto* binding = tryGetBinding(nameIdOf(unit, stripped));
        return binding != nullptr && (binding->scopeIndex == 0 || binding->type.isReference());
    }

    bool TypeChecker::expressionIsAddressable(const CompilationUnit& unit, u32 index) const
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, index);
        const auto kind = unit.nodeBuffer.kind(stripped);
        if (kind == NodeKind::NameExpression || kind == NodeKind::MemberAccessExpression)
        {
            return true;
        }

        if (kind != NodeKind::BinaryExpression || !unit.nodeBuffer.isMemberAccessBinary(tokens, stripped))
        {
            return false;
        }

        const auto layout = NodeLayout::binary(unit.nodeBuffer, stripped);
        if (unit.nodeBuffer.kind(layout.right) == NodeKind::FunctionCallExpression && !isArrayAtCall(unit, layout.right))
        {
            return false;
        }

        return expressionIsAddressable(unit, layout.left);
    }

    bool TypeChecker::typeHasDynamicArrayField(Type type, const TypeVisit* parent) const
    {
        const auto valueType = type.toValue();
        if (valueType.kind() == TypeKind::FixedArray)
        {
            return typeHasDynamicArrayField(m_module.getArrayElementType(valueType), parent);
        }

        if (valueType.kind() != TypeKind::Type)
        {
            return false;
        }

        // a type can contain itself by value if the program has an error
        for (auto* visit = parent; visit != nullptr; visit = visit->parent)
        {
            if (visit->type == valueType)
            {
                return false;
            }
        }

        // nothing can write through a constant field
        const auto current = TypeVisit{ valueType, parent };
        for (const auto& field : m_module.getTypeDefinition(valueType).fields())
        {
            if (!field.isConstant() && (field.type().toValue().kind() == TypeKind::DynamicArray || typeHasDynamicArrayField(field.type(), &current)))
            {
                return true;
            }
        }

        return false;
    }

    void TypeChecker::emitDynamicArrayFieldCopies(ValueRef address, Type type, const TypeVisit* parent)
    {
        const auto valueType = type.toValue();
        if (valueType.kind() == TypeKind::FixedArray)
        {
            const auto elementType = m_module.getArrayElementType(valueType);
            for (auto position = 0; position < m_module.getArrayLength(valueType); ++position)
            {
                const auto index = m_emitter.emitConstant(ConstantValue::FromI32(position), m_module.wellKnown().i32);
                emitDynamicArrayFieldCopies(m_emitter.emitElementAddress(address, valueType, index, elementType), elementType, parent);
            }
            return;
        }

        for (auto* visit = parent; visit != nullptr; visit = visit->parent)
        {
            if (visit->type == valueType)
            {
                return;
            }
        }

        const auto current = TypeVisit{ valueType, parent };
        for (const auto& field : m_module.getTypeDefinition(valueType).fields())
        {
            if (field.isConstant())
            {
                continue;
            }

            if (field.type().toValue().kind() == TypeKind::DynamicArray)
            {
                const auto fieldAddress = m_emitter.emitFieldAddress(address, valueType, field.index(), field.type());
                m_emitter.emitStoreValue(m_emitter.emitArrayCopy(fieldAddress, field.type().toValue()), fieldAddress, field.type());
            }
            else if (typeHasDynamicArrayField(field.type(), &current))
            {
                emitDynamicArrayFieldCopies(m_emitter.emitFieldAddress(address, valueType, field.index(), field.type()), field.type(), &current);
            }
        }
    }

    ValueRef TypeChecker::emitCopyOfValue(ValueRef value, Type type)
    {
        const auto valueType = type.toValue();
        const auto temp = m_emitter.spillToTempSlot(value, valueType);
        if (valueType.kind() == TypeKind::DynamicArray)
        {
            return m_emitter.emitArrayCopy(temp, valueType);
        }

        emitDynamicArrayFieldCopies(temp, valueType);
        return m_emitter.emitLoad(temp, valueType);
    }

    ValueRef TypeChecker::copyValueIfNeeded(const CompilationUnit& unit, u32 index, ValueRef value, Type type)
    {
        const auto valueType = type.toValue();
        const auto isDynamicArray = valueType.kind() == TypeKind::DynamicArray;
        if (value.id() < 0 || (!isDynamicArray && !typeHasDynamicArrayField(valueType)) || !expressionIsAddressable(unit, index))
        {
            return value;
        }

        // the caller copies b in a := b from the address of b
        if (isDynamicArray && unit.nodeBuffer.kind(NodeLayout::stripGroupings(unit.nodeBuffer, index)) == NodeKind::NameExpression)
        {
            return value;
        }

        return emitCopyOfValue(value, valueType);
    }

    bool TypeChecker::assignmentTargetGoesThroughCall(const CompilationUnit& unit, u32 targetIndex) const
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        auto current = NodeLayout::stripGroupings(unit.nodeBuffer, targetIndex);
        while (unit.nodeBuffer.kind(current) == NodeKind::BinaryExpression && unit.nodeBuffer.isMemberAccessBinary(tokens, current))
        {
            const auto layout = NodeLayout::binary(unit.nodeBuffer, current);
            if (unit.nodeBuffer.kind(layout.right) == NodeKind::FunctionCallExpression)
            {
                return true;
            }

            current = NodeLayout::stripGroupings(unit.nodeBuffer, layout.left);
        }

        return unit.nodeBuffer.kind(current) == NodeKind::FunctionCallExpression;
    }

    ValueRef TypeChecker::emitAssignmentTargetAddress(const CompilationUnit& unit, u32 targetIndex)
    {
        if (!assignmentTargetGoesThroughCall(unit, targetIndex))
        {
            return addressOfExpression(unit, targetIndex);
        }

        const auto diagnosticCount = m_diagnostics.diagnostics().size();
        auto context = ExpressionContext{};
        context.wantsAddress = true;
        context.receiverPosition = true;
        const auto address = typeCheckExpression(unit, targetIndex, context).value;
        m_diagnostics.removeDiagnosticsAfter(diagnosticCount);
        return address;
    }

    ValueRef TypeChecker::addressOfExpression(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, index);
        switch (unit.nodeBuffer.kind(stripped))
        {
            case NodeKind::NameExpression:
            {
                return addressOfName(unit, stripped);
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

    ValueRef TypeChecker::emitArrayLiteralToTempSlot(const CompilationUnit& unit, u32 literalIndex, Type arrayType)
    {
        const auto temp = m_emitter.allocateTempAggregateSlot("temp", arrayType);
        emitCapturedArrayLiteral(unit, literalIndex, temp, arrayType);
        return temp;
    }

    ValueRef TypeChecker::materializeExpressionValue(const CompilationUnit& unit, u32 valueIndex, const CheckedValue& checkedValue)
    {
        // an array literal has no value until the emitter writes its elements to a slot
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, valueIndex);
        const auto type = checkedValue.type.toValue();
        if (unit.nodeBuffer.kind(stripped) == NodeKind::ArrayLiteral
            && (type.kind() == TypeKind::FixedArray || type.kind() == TypeKind::DynamicArray))
        {
            return m_emitter.emitLoad(emitArrayLiteralToTempSlot(unit, stripped, type), type);
        }

        return materializeValue(checkedValue);
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

    bool TypeChecker::isTypeNameExpression(const CompilationUnit& unit, u32 expression) const noexcept
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, expression);
        return unit.nodeBuffer.kind(stripped) == NodeKind::NameExpression
            && tryGetBinding(nameIdOf(unit, stripped)) == nullptr
            && m_module.tryGetTypeByName(nameOf(unit, stripped)) != Type::Undefined();
    }

    const TypeChecker::LexicalBinding* TypeChecker::tryGetConflictingBinding(u32 nameId) const noexcept
    {
        const auto* existing = tryGetBinding(nameId);
        if (existing != nullptr && existing->scopeIndex == 0 && !currentScopeIsGlobal())
        {
            return nullptr;
        }

        return existing;
    }

    ValueRef TypeChecker::addressOfName(const CompilationUnit& unit, u32 nameNode)
    {
        const auto nameId = nameIdOf(unit, nameNode);
        const auto* binding = tryGetBinding(nameId);
        if (binding == nullptr)
        {
            return ValueRef{};
        }

        if (binding->scopeIndex == 0)
        {
            return m_emitter.emitGlobalAddress(nameOf(unit, nameNode));
        }

        return m_emitter.addressOfLocal(nameId);
    }

    bool TypeChecker::nameIsInitConstant(const CompilationUnit& unit, u32 nameNode) const
    {
        const auto* binding = tryGetBinding(nameIdOf(unit, nameNode));
        return binding != nullptr && binding->scopeIndex == 0 && m_initConstants.contains(std::string(nameOf(unit, nameNode)));
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
            const auto failedBeforeGlobal = m_emitter.emissionFailed();
            if (unit.nodeBuffer.kind(declaration.index) == NodeKind::InitConstantDeclaration)
            {
                typeCheckInitConstantDeclaration(unit, declaration.index);
            }
            else
            {
                typeCheckConstantDeclaration(unit, declaration.index);
            }
            recordLoweringFailure(unit, declaration.index, failedBeforeGlobal);
        }
    }

    void TypeChecker::typeCheckFunctionDefinitions()
    {
        CARACAL_ZONE_NAMED("check.functionDefinitions");
        for (const auto declaration : m_functionDeclarations)
        {
            if (declaration == m_testsStubDeclaration)
            {
                continue;
            }

            const auto& unit = unitFor(declaration);
            typeCheckFunctionDefinitionStatement(unit, declaration.index);
        }
    }

    void TypeChecker::copyTestsStubParameters()
    {
        if (m_testsStubDeclaration.index == InvalidNodeIndex)
        {
            return;
        }

        const auto parameters = m_module.getFunctionDefinition(m_module.typeOfNode(m_testsStubDeclaration)).parameters();
        for (const auto declaration : m_testDeclarations)
        {
            m_module.getFunctionDefinition(m_module.typeOfNode(declaration)).setParameters(parameters);
        }
    }

    void TypeChecker::typeCheckTestsStub()
    {
        if (m_testsStubDeclaration.index == InvalidNodeIndex)
        {
            if (m_options.emitsTests && !m_testDeclarations.empty())
            {
                const auto firstTest = m_testDeclarations.front();
                const auto& firstTestUnit = unitFor(firstTest);
                m_diagnostics.addTestsStubMissingError(firstTestUnit.source, ExpressionSourceLocation(m_compilationContext, firstTestUnit, firstTest.index));
            }
            return;
        }

        CARACAL_ZONE_NAMED("check.testsStub");
        const auto& unit = unitFor(m_testsStubDeclaration);
        const auto index = m_testsStubDeclaration.index;
        const auto& functionDefinition = m_module.getFunctionDefinition(m_module.typeOfNode(m_testsStubDeclaration));
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);
        const ScopedValue<Type> returnTypeScope{ m_currentReturnType, Type::Void() };
        pushScope(ScopeKind::Function);

        m_emitter.setAddressTakenLocals(collectAddressTakenNameIds(unit, functionLayout.body));
        m_emitter.beginFunction(functionDefinition, nameLine(unit, index));
        bindParameters(unit, functionLayout.parameters, functionDefinition, true);
        if (m_options.emitsTests)
        {
            emitTestsStubBody();
        }
        m_emitter.endFunction();

        // we dont emit unused parameter warnings for tests
        popScope(false);
    }

    void TypeChecker::typeCheckTestDefinitions()
    {
        CARACAL_ZONE_NAMED("check.testDefinitions");
        for (const auto declaration : m_testDeclarations)
        {
            const auto& unit = unitFor(declaration);
            typeCheckTestDefinitionStatement(unit, declaration.index);
        }
    }

    void TypeChecker::typeCheckStatement(const CompilationUnit& unit, u32 index)
    {
        const bool failedBeforeStatement = m_emitter.emissionFailed();

        if (!m_options.recordsSourceLocations || unit.nodeBuffer.kind(index) == NodeKind::BlockNode)
        {
            typeCheckStatementNode(unit, index);
        }
        else
        {
            const ScopedValue<SourceLine> sourceLineScope{ m_emitter.currentSourceLine(), statementLine(unit, index) };
            typeCheckStatementNode(unit, index);
        }

        recordLoweringFailure(unit, index, failedBeforeStatement);
    }

    void TypeChecker::recordLoweringFailure(const CompilationUnit& unit, u32 index, bool failedBefore)
    {
        // only report the first lowering failed error
        if (!failedBefore && m_emitter.emissionFailed() && !m_firstLoweringFailure.has_value())
        {
            m_firstLoweringFailure = NodeIndex{ unit.fileId, index };
        }
    }

    void TypeChecker::reportLoweringFailure()
    {
        // only report the lowering failure if there are no other errors
        if (!m_firstLoweringFailure.has_value() || m_diagnostics.hasErrors())
        {
            return;
        }

        const auto index = m_firstLoweringFailure->index;
        const auto& unit = unitFor(*m_firstLoweringFailure);
        const auto location = NodeSpans::deadStatementLocation(unit.nodeBuffer, m_compilationContext.tokenBuffer(unit.fileId), index);
        m_diagnostics.addStatementLoweringFailedError(unit.source, location.value_or(ExpressionSourceLocation(m_compilationContext, unit, index)));
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
                typeCheckStatementInNewScope(unit, index);
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
            if (!unit.nodeBuffer.isMemberAccessBinary(tokens, stripped))
            {
                return false;
            }

            if (unit.nodeBuffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression)
            {
                return isArrayAtCall(unit, binaryLayout.right)
                    && receiverChainIsConstant(unit, binaryLayout.left, rootName, referencesConstant);
            }

            if (unit.nodeBuffer.kind(binaryLayout.right) != NodeKind::NameExpression)
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

        const auto emissionPosition = m_emitter.suspendEmission();
        auto leftType = typeCheckExpression(unit, assignmentLayout.target, targetContext).type;
        m_emitter.resumeEmission(emissionPosition);
        const auto targetIsReference = leftType.isReference();
        if (leftType.isReference())
        {
            leftType = leftType.toValue();
        }

        // global init constants are only allowed to be assigned once in caracalMain/main
        const auto leftKind = unit.nodeBuffer.kind(assignmentLayout.target);
        const auto targetIsInitConstant = leftKind == NodeKind::NameExpression && nameIsInitConstant(unit, assignmentLayout.target);
        const auto initializesReferenceGlobal = targetIsReference && targetIsInitConstant;
        if (leftKind == NodeKind::NameExpression)
        {
            const auto targetName = std::string(nameOf(unit, assignmentLayout.target));
            if (targetIsInitConstant)
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
        else if (leftKind == NodeKind::BinaryExpression
            && unit.nodeBuffer.isMemberAccessBinary(tokens, assignmentLayout.target)
            && leftType != Type::Undefined())
        {
            const auto memberLayout = NodeLayout::binary(unit.nodeBuffer, assignmentLayout.target);
            const auto receiverType = m_module.typeOfNode(NodeIndex{ unit.fileId, memberLayout.left }).toValue();
            const auto isField = unit.nodeBuffer.kind(memberLayout.right) == NodeKind::NameExpression && receiverType.kind() == TypeKind::Type;
            const auto isElement = unit.nodeBuffer.kind(memberLayout.right) == NodeKind::FunctionCallExpression && isArrayAtCall(unit, memberLayout.right);
            if (!isField && !isElement)
            {
                // p.plus(1) = 5 or xs.length = 5
                m_diagnostics.addAssignmentToNonVariableError(unit.source, NodeSpans::expressionLocation(unit.nodeBuffer, tokens, assignmentLayout.target));
                leftType = Type::Undefined();
            }
            else if (isElement && m_module.isImmutableSlice(receiverType))
            {
                m_diagnostics.addMutatingMethodOnImmutableSliceError(
                    unit.source,
                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, memberLayout.left),
                    "at");
            }
            else if (isField && m_module.getTypeDefinition(receiverType).tryGetFieldByName(nameOf(unit, memberLayout.right)).isConstant())
            {
                m_diagnostics.addAssignmentToConstantError(
                    unit.source,
                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, assignmentLayout.target),
                    std::string(nameOf(unit, memberLayout.right)));
            }
            else
            {
                reportWriteToConstantReceiver(unit, memberLayout.left, assignmentLayout.target);
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
        if (leftKind == NodeKind::NameExpression && !currentScopeIsGlobal() && !initializesReferenceGlobal)
        {
            const auto constructedType = tryResolveConstructedType(unit, NodeLayout::stripGroupings(unit.nodeBuffer, assignmentLayout.value));
            if (constructedType != Type::Undefined())
            {
                const auto destination = addressOfName(unit, assignmentLayout.target);
                if (destination.id() >= 0)
                {
                    pushConstructorDestination(unit, assignmentLayout.value, destination, false);
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

        if (initializesReferenceGlobal)
        {
            const auto targetName = std::string(nameOf(unit, assignmentLayout.target));
            const auto isExplicitRef = unit.nodeBuffer.kind(strippedAssignedValue) == NodeKind::UnaryExpression
                && TokenKindAt(tokens, unit.nodeBuffer.mainToken(strippedAssignedValue)) == TokenKind::RefKeyword;
            if (!isExplicitRef)
            {
                m_diagnostics.addInitReferenceWithoutRefError(
                    unit.source,
                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, assignmentLayout.value),
                    targetName);
                return;
            }

            const auto refOperand = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(strippedAssignedValue));
            const auto targetAddress = addressOfExpression(unit, refOperand);
            if (targetAddress.id() < 0)
            {
                m_emitter.recordEmissionFailure();
                return;
            }

            m_emitter.emitGlobalReferenceInitialization(targetName, targetAddress);
            return;
        }

        // a constructor right side already stored through the target's address
        if (pushedAssignmentConstruction)
        {
            return;
        }

        auto assignedValue = m_emitter.emitWidened(materializeExpressionValue(unit, assignmentLayout.value, checkedRight), rightType.toValue(), leftType.toValue());
        if (dynamicAssignmentCopy)
        {
            const auto sourceAddress = addressOfName(unit, strippedAssignedValue);
            assignedValue = m_emitter.emitArrayCopy(sourceAddress, leftType.toValue());
        }
        else
        {
            assignedValue = copyValueIfNeeded(unit, assignmentLayout.value, assignedValue, leftType);
        }

        if (leftKind == NodeKind::NameExpression)
        {
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
        else if (leftKind == NodeKind::MemberAccessExpression
            || (leftKind == NodeKind::BinaryExpression && unit.nodeBuffer.isMemberAccessBinary(tokens, assignmentLayout.target)))
        {
            // nested fields like a.b.c resolve their address recursively
            const auto targetAddress = emitAssignmentTargetAddress(unit, assignmentLayout.target);
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

        bindParameters(unit, functionLayout.parameters, functionDefinition, emitBody);

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

    void TypeChecker::bindParameters(const CompilationUnit& unit, u32 parametersNode, const FunctionDefinition& functionDefinition, bool emitBody)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto& parameters = functionDefinition.parameters();
        m_childrenScratch.clear();
        if (parametersNode != InvalidNodeIndex)
        {
            NodeLayout::collectChildren(unit.nodeBuffer, parametersNode, m_childrenScratch);
        }
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
    }

    void TypeChecker::emitTestsStubBody()
    {
        CARACAL_ZONE_NAMED("check.testsStubBody");
        const auto stringType = m_module.wellKnown().string;

        const auto& stubParameters = m_module.getFunctionDefinition(m_module.typeOfNode(m_testsStubDeclaration)).parameters();
        auto testArguments = std::vector<ValueRef>{};
        for (const auto& parameter : stubParameters)
        {
            const auto nameId = internName(parameter.name());
            if (parameter.type().isReference())
            {
                testArguments.push_back(m_emitter.addressOfLocal(nameId));
            }
            else
            {
                testArguments.push_back(m_emitter.readLocal(nameId));
            }
        }

        auto beginTestType = Type::Undefined();
        auto endTestType = Type::Undefined();
        auto receiver = ValueRef{};
        if (!stubParameters.empty() && stubParameters.front().type().toBaseType().kind() == TypeKind::Type)
        {
            const auto& receiverDefinition = m_module.getTypeDefinition(stubParameters.front().type().toBaseType());
            beginTestType = tryGetOverload(receiverDefinition.tryGetMethodOverloadSet(TestBeginMethodName));
            endTestType = tryGetOverload(receiverDefinition.tryGetMethodOverloadSet(TestEndMethodName));
            receiver = m_emitter.addressOfLocal(internName(stubParameters.front().name()));
        }

        for (const auto declaration : m_testDeclarations)
        {
            if (!emitsTestsOf(unitFor(declaration)))
            {
                continue;
            }

            if (beginTestType != Type::Undefined())
            {
                // Test.beginTest("name") before test
                m_module.markPreludeTypeRequired(stringType);
                const auto temp = m_emitter.allocateTempAggregateSlot("temp", stringType);
                m_emitter.emitStringLiteralIntoAddress(temp, escapedStringContent(unitFor(declaration), declaration.index));
                const ValueRef beginTestArguments[] = { receiver, m_emitter.emitLoad(temp, stringType) };
                static_cast<void>(m_emitter.emitCall(beginTestType.id(), beginTestArguments, Type::Void()));
            }

            static_cast<void>(m_emitter.emitCall(m_module.typeOfNode(declaration).id(), testArguments, Type::Void()));

            if (endTestType != Type::Undefined())
            {
                // Test.endTest() after test
                const ValueRef endTestArguments[] = { receiver };
                static_cast<void>(m_emitter.emitCall(endTestType.id(), endTestArguments, Type::Void()));
            }
        }
    }

    bool TypeChecker::emitsTestsOf(const CompilationUnit& unit) const noexcept
    {
        return m_options.emitsTests && unit.runsItsTests;
    }

    void TypeChecker::typeCheckTestDefinitionStatement(const CompilationUnit& unit, u32 index)
    {
        CARACAL_ZONE_NAMED("check.testBody");
        const ScopedValue<Type> returnTypeScope{ m_currentReturnType, Type::Void() };
        pushScope(ScopeKind::Function);

        const auto functionType = m_module.typeOfNode(NodeIndex{ unit.fileId, index });
        const auto& functionDefinition = m_module.getFunctionDefinition(functionType);
        const auto testLayout = NodeLayout::testDefinition(index);
        const auto emitsTest = emitsTestsOf(unit);

        if (emitsTest)
        {
            m_emitter.setAddressTakenLocals(collectAddressTakenNameIds(unit, testLayout.body));
            m_emitter.beginFunction(functionDefinition, nameLine(unit, index));
        }
        // the parameters come from the stub and have no source location, so no unused warning
        bindParameters(unit, InvalidNodeIndex, functionDefinition, emitsTest);

        m_currentFunctionName = functionDefinition.fullName();
        typeCheckBlockNode(unit, testLayout.body);
        m_currentFunctionName.clear();

        if (emitsTest)
        {
            m_emitter.endFunction();
        }
        popScope(true);
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
        typeCheckStatementInNewScope(unit, ifLayout.trueStatement);
        m_emitter.endIfThen(ifFrame);

        if (ifLayout.hasFalse)
        {
            typeCheckStatementInNewScope(unit, ifLayout.falseStatement);
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
        typeCheckStatementInNewScope(unit, whileLayout.body);
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
                        pushConstructorDestination(unit, value.child, destination, true);
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

            auto returnedValue = materializeExpressionValue(unit, value.child, checkedValue);
            if (unit.nodeBuffer.kind(NodeLayout::stripGroupings(unit.nodeBuffer, value.child)) != NodeKind::NameExpression)
            {
                returnedValue = copyValueIfNeeded(unit, value.child, returnedValue, type);
            }
            else if (returnedNameNeedsCopy(unit, value.child, type))
            {
                returnedValue = emitCopyOfValue(returnedValue, type);
            }
            m_emitter.emitReturnValue(m_emitter.emitWidened(returnedValue, type.toValue(), declaredReturnType.toValue()));
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

    void TypeChecker::typeCheckStatementInNewScope(const CompilationUnit& unit, u32 index)
    {
        pushScope(currentScopeKind());
        if (unit.nodeBuffer.kind(index) == NodeKind::BlockNode)
        {
            typeCheckBlockNode(unit, index);
        }
        else
        {
            typeCheckStatement(unit, index);
        }

        // endIf merges the branches after this removal
        // a local of both branches otherwise gets a phi
        for (auto position = static_cast<size_t>(m_scopeFrames.back().boundNamesStart); position < m_boundNames.size(); ++position)
        {
            m_emitter.removeLocal(m_boundNames[position]);
        }
        popScope(true);
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
            case NodeKind::SourceTextDefault:
            {
                m_module.markPreludeTypeRequired(m_module.wellKnown().string);
                return CheckedValue{ recordType(unit, index, m_module.wellKnown().string) };
            }
            case NodeKind::SourceLocationDefault:
            {
                const auto locationType = m_module.wellKnown().sourceLocation;
                if (locationType == Type::Undefined())
                {
                    m_diagnostics.addPreludeTypeMissingError("SourceLocation");
                    return Type::Undefined();
                }

                m_module.markPreludeTypeRequired(locationType);
                return CheckedValue{ recordType(unit, index, locationType) };
            }
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
                if (binding->scopeIndex != 0)
                {
                    result.value = m_emitter.peekLocal(nameId);
                }
                return result;
            }
            if (binding->kind == VariableBindingKind::LocalConstant)
            {
                result.folded = binding->folded;
            }
            if (binding->scopeIndex == 0)
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
            const auto* overloadSet = typeDefinition.tryGetMethodOverloadSet(methodName);
            if (overloadSet == nullptr || overloadSet->overloads.empty())
            {
                m_diagnostics.addUnknownMethodError(
                    unit.source,
                    ExpressionSourceLocation(m_compilationContext, unit, inner),
                    typeDefinition.name(),
                    std::string(methodName));

                return recordType(unit, index, Type::Undefined());
            }

            const auto methodType = resolveMethodCallee(unit, inner, nameExpressionIndex, *overloadSet, false);
            if (methodType == Type::Undefined())
            {
                return recordType(unit, index, Type::Undefined());
            }

            if (m_module.getFunctionDefinition(methodType).functionType() == FunctionType::StaticMethod)
            {
                m_diagnostics.addStaticMethodCalledWithoutTypeNameError(
                    unit.source,
                    ExpressionSourceLocation(m_compilationContext, unit, inner),
                    std::string(methodName),
                    typeDefinition.name());

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
                else if (isFloatingLiteral && contextualDescription != nullptr && contextualDescription->kind == BuiltinTypeKind::Float)
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
            elementContext.wantsAddress = false;
            elementContext.receiverPosition = false;
            auto& capturedElements = m_arrayLiteralElements[ArrayLiteralKey(unit.fileId, index)];
            capturedElements.clear();
            for (auto position = childrenStart; position < childrenEnd; ++position)
            {
                const auto element = m_childrenStack[position];
                const auto checkedElement = typeCheckExpression(unit, element, elementContext);
                const auto elementValue = copyValueIfNeeded(unit, element, checkedElement.value, checkedElement.type);
                capturedElements.push_back(CapturedArrayElement{ element, elementValue, checkedElement.folded });
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
        if (description != nullptr && (description->kind == BuiltinTypeKind::Int || description->kind == BuiltinTypeKind::Float))
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
}
