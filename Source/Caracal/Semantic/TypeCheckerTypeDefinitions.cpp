#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Profiling.h>
#include <Caracal/ScopedValue.h>
#include <Caracal/Semantic/OperatorRegistration.h>
#include <Caracal/Semantic/SliceParameterPromotion.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Caracal
{
    [[nodiscard]] static SourceLocation ParametersNodeSpan(const CompilationContext& context, const CompilationUnit& unit, u32 parametersIndex)
    {
        return NodeSpans::parametersNodeSpan(unit.nodeBuffer, context.tokenBuffer(unit.fileId), parametersIndex);
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
            auto annotations = std::vector<u32>{};
            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, typeIndex, annotations);
            const auto isBuiltin = NodeLayout::annotationsContain(unit.nodeBuffer, tokens, annotations, BuiltinAnnotationName);
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

                const auto existingType = tryGetOverload(typeDefinition.tryGetMethodOverloadSet(methodName));
                if (isBuiltin && existingType != Type::Undefined())
                {
                    m_diagnostics.addDuplicateBuiltinMethodDeclarationError(
                        unit.source,
                        nameLocation(unit, methodLayout.methodName),
                        std::string(methodName),
                        typeDefinition.name(),
                        methodNameLocation(m_module.getFunctionDefinition(existingType)));
                    continue;
                }

                auto& methodDefinition = m_module.createMethod(typeDefinition, modifier, std::string(methodName), std::move(declarationParameters), std::vector<Type>());
                methodDefinition.setDefinitionNode(NodeIndex{ unit.fileId, bodyStatement });
                m_module.setNodeType(NodeIndex{ unit.fileId, bodyStatement }, methodDefinition.type());
            }

            if (isBuiltin)
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
            if (!isBuiltin && rejectInvalidMethodOverload(unit, bodyStatement, typeDefinition))
            {
                continue;
            }

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

        auto methodType = m_module.typeOfNode(NodeIndex{ unit.fileId, methodIndex });
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
        methodDefinition.setIsExtern(isExtern);
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
        auto constructorType = tryGetOverload(typeDefinition.tryGetMethodOverloadSet("new"));
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

    void TypeChecker::reportTypesThatContainThemselves()
    {
        std::unordered_set<i32> finishedTypes;
        for (const auto& definition : m_module.typeDefinitions())
        {
            if (!finishedTypes.contains(definition.type().id()))
            {
                reportFieldsThatContainTheirType(definition, nullptr, finishedTypes);
            }
        }
    }

    // type A { b := B.new() } with type B { a := A.new() } reports only a
    void TypeChecker::reportFieldsThatContainTheirType(const TypeDefinition& definition, const TypeVisit* parent, std::unordered_set<i32>& finishedTypes)
    {
        const auto current = TypeVisit{ definition.type(), parent };
        auto hasTypeField = false;
        for (const auto& field : definition.fields())
        {
            auto fieldType = field.type();
            while (fieldType.kind() == TypeKind::FixedArray && !fieldType.isReference())
            {
                fieldType = m_module.getArrayElementType(fieldType);
            }

            // a dynamic array keeps its elements in a buffer
            if (fieldType.kind() != TypeKind::Type || fieldType.isReference() || finishedTypes.contains(fieldType.id()))
            {
                continue;
            }

            hasTypeField = true;
            auto closesPath = false;
            for (auto* visit = &current; visit != nullptr; visit = visit->parent)
            {
                if (visit->type == fieldType)
                {
                    closesPath = true;
                    break;
                }
            }

            if (!closesPath)
            {
                reportFieldsThatContainTheirType(m_module.getTypeDefinition(fieldType), &current, finishedTypes);
                continue;
            }

            const auto location = TypeFieldLocation(m_compilationContext, definition, field);
            if (location.has_value())
            {
                m_diagnostics.addTypeContainsItselfError(
                    unitFor(definition.definitionNode()).source,
                    location.value(),
                    field.name(),
                    definition.name(),
                    formatTypeName(m_module, fieldType));
            }
        }

        // a second visit of a type with no type field does nothing
        if (hasTypeField)
        {
            finishedTypes.insert(definition.type().id());
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
            const auto constructorType = tryGetOverload(typeDefinition.tryGetMethodOverloadSet("new"));
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

                const bool failedBeforeField = m_emitter.emissionFailed();

                if (m_options.recordsSourceLocations)
                {
                    const ScopedValue<SourceLine> sourceLineScope{ m_emitter.currentSourceLine(), statementLine(unit, bodyStatement) };
                    typeCheckTypeFieldDeclaration(unit, bodyStatement, typeDefinition, fieldIndex);
                }
                else
                {
                    typeCheckTypeFieldDeclaration(unit, bodyStatement, typeDefinition, fieldIndex);
                }

                recordLoweringFailure(unit, bodyStatement, failedBeforeField);
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

                if (m_module.typeOfNode(NodeIndex{ unit.fileId, bodyStatement }) == Type::Undefined())
                {
                    continue;
                }

                typeCheckMethodDefinitionStatement(unit, bodyStatement);
            }

            popScope(false);
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
        else
        {
            fieldValue = copyValueIfNeeded(unit, strippedField, fieldValue, fieldType);
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

        const auto methodType = m_module.typeOfNode(NodeIndex{ unit.fileId, index });
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
}
