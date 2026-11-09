#include <Caracal/Semantic/TypeChecker.h>
#include <Caracal/Profiling.h>

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/ScopedValue.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Caracal
{
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
