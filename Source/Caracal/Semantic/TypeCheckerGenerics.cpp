#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/SliceParameterPromotion.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Caracal
{
    static constexpr size_t MaximumSpecializationDepth = 64;

    TypeChecker::CheckedValue TypeChecker::typeCheckGenericFunctionCall(
        const CompilationUnit& unit,
        u32 index,
        u32 typeArgumentsNode,
        const OverloadSet& overloadSet,
        const ExpressionContext& context)
    {
        auto typeArgumentNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, typeArgumentsNode, typeArgumentNodes);

        auto hasTemplate = false;
        size_t typeParameterCount = 0;
        auto templates = std::vector<Type>{};
        for (const auto member : overloadSet.overloads)
        {
            const auto& memberDefinition = m_module.getFunctionDefinition(member);
            if (!memberDefinition.isTemplate())
            {
                continue;
            }

            hasTemplate = true;
            typeParameterCount = m_module.genericFunctionData(memberDefinition).typeParameters.size();
            if (typeParameterCount == typeArgumentNodes.size())
            {
                templates.push_back(member);
            }
        }

        const auto calleeIndex = NodeLayout::functionCallee(unit.nodeBuffer, index);
        const auto calleeName = std::string(nameOf(unit, calleeIndex));
        if (!hasTemplate)
        {
            m_diagnostics.addTypeArgumentsOnNonGenericFunctionError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index),
                calleeName);
            return Type::Undefined();
        }
        if (templates.empty())
        {
            m_diagnostics.addWrongNumberOfTypeArgumentsError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index),
                calleeName,
                typeParameterCount,
                typeArgumentNodes.size());
            return Type::Undefined();
        }

        auto typeArguments = std::vector<Type>{};
        for (const auto typeArgumentNode : typeArgumentNodes)
        {
            const auto typeArgument = typeCheckTypeNameNode(unit, typeArgumentNode);
            if (typeArgument == Type::Undefined())
            {
                if (m_isDeclarationCheck)
                {
                    checkArgumentsOfUnresolvedCall(unit, index);
                }
                return Type::Undefined();
            }
            if (typeArgument.isReference())
            {
                m_diagnostics.addReferenceTypeArgumentError(
                    unit.source,
                    NodeSpans::typeNameSpan(unit.nodeBuffer, m_compilationContext.tokenBuffer(unit.fileId), typeArgumentNode));
                return Type::Undefined();
            }
            typeArguments.push_back(typeArgument);
        }

        auto specializations = OverloadSet{};
        for (const auto templateType : templates)
        {
            specializations.overloads.push_back(specializationOf(templateType, typeArguments));
        }

        auto specialization = specializations.firstOverload();
        if (specializations.isOverloaded())
        {
            specialization = selectOverload(unit, index, calleeIndex, specializations);
            if (specialization == Type::Undefined())
            {
                return Type::Undefined();
            }
        }

        return typeCheckResolvedFunctionCall(unit, index, specialization, context);
    }

    void TypeChecker::reportInvalidTypeParameters(const CompilationUnit& unit, u32 typeParametersNode)
    {
        auto typeParameterNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, typeParametersNode, typeParameterNodes);
        for (size_t position = 0; position < typeParameterNodes.size(); ++position)
        {
            const auto name = nameOf(unit, typeParameterNodes[position]);
            auto isDuplicate = false;
            for (size_t earlier = 0; earlier < position && !isDuplicate; ++earlier)
            {
                if (nameOf(unit, typeParameterNodes[earlier]) == name)
                {
                    m_diagnostics.addDuplicateTypeParameterError(
                        unit.source,
                        nameLocation(unit, typeParameterNodes[position]),
                        std::string(name),
                        nameLocation(unit, typeParameterNodes[earlier]));
                    isDuplicate = true;
                }
            }

            if (!isDuplicate && m_module.tryGetTypeByName(name) != Type::Undefined())
            {
                m_diagnostics.addTypeParameterWithNameOfTypeError(
                    unit.source,
                    nameLocation(unit, typeParameterNodes[position]),
                    std::string(name));
            }
        }
    }

    void TypeChecker::reportGenericParameterDefaultValues(const CompilationUnit& unit, const std::vector<Parameter>& parameters, const std::vector<u32>& parameterNodes)
    {
        for (size_t position = 0; position < parameters.size() && position < parameterNodes.size(); ++position)
        {
            const auto& parameter = parameters[position];
            const auto baseType = parameter.type().toBaseType();
            if (parameter.hasDefaultNode() && baseType.kind() == TypeKind::TypeParameter)
            {
                m_diagnostics.addGenericParameterDefaultValueError(
                    unit.source,
                    ParameterSpan(m_compilationContext, unit, parameterNodes[position]),
                    parameter.name(),
                    std::string(m_module.getNameByType(baseType)));
            }
        }
    }

    [[nodiscard]] static std::optional<std::pair<size_t, Type>> MatchTypeParameter(
        const SemanticContext& module,
        const std::vector<Type>& typeParameters,
        Type parameterType,
        Type argumentType) noexcept
    {
        const auto parameterBase = parameterType.toBaseType();
        const auto argumentValue = argumentType.toValue();
        if (argumentValue == Type::Undefined())
        {
            return std::nullopt;
        }

        if (parameterBase.kind() == TypeKind::TypeParameter)
        {
            const auto found = std::ranges::find(typeParameters, parameterBase);
            if (found == typeParameters.end())
            {
                return std::nullopt;
            }
            return std::pair{ static_cast<size_t>(found - typeParameters.begin()), argumentValue };
        }

        const auto isArray = parameterBase.kind() == TypeKind::FixedArray
            || parameterBase.kind() == TypeKind::DynamicArray
            || parameterBase.kind() == TypeKind::Slice;
        if (!isArray || parameterBase.kind() != argumentValue.kind())
        {
            return std::nullopt;
        }
        if (parameterBase.kind() == TypeKind::FixedArray && module.getArrayLength(parameterBase) != module.getArrayLength(argumentValue))
        {
            return std::nullopt;
        }

        return MatchTypeParameter(module, typeParameters, module.getArrayElementType(parameterBase), module.getArrayElementType(argumentValue));
    }

    [[nodiscard]] static bool IsPlainNumberLiteral(const NodeBuffer& buffer, u32 index) noexcept
    {
        return buffer.kind(index) == NodeKind::NumberLiteral && buffer.subtreeSize(index) == 1;
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckInferredGenericFunctionCall(
        const CompilationUnit& unit,
        u32 index,
        Type templateType,
        const ExpressionContext& context)
    {
        auto argumentValues = std::vector<u32>{};
        NodeLayout::functionCall(unit.nodeBuffer, index, argumentValues);
        auto argumentTypes = std::vector<Type>{};
        auto argumentErrors = std::vector<Diagnostic>{};
        for (auto& argumentValue : argumentValues)
        {
            argumentValue = NodeLayout::argumentValue(unit.nodeBuffer, argumentValue);
            argumentTypes.push_back(checkArgumentWithoutContext(unit, argumentValue, argumentErrors));
        }

        if (!argumentErrors.empty())
        {
            m_diagnostics.addDiagnostics(std::move(argumentErrors));
            return Type::Undefined();
        }
        if (m_isDeclarationCheck && std::ranges::find(argumentTypes, Type::Undefined()) != argumentTypes.end())
        {
            checkArgumentsOfUnresolvedCall(unit, index);
            return Type::Undefined();
        }

        const auto typeArguments = inferTypeArguments(unit, index, templateType, argumentValues, argumentTypes, true);
        if (typeArguments.empty())
        {
            return Type::Undefined();
        }

        return typeCheckResolvedFunctionCall(unit, index, specializationOf(templateType, typeArguments), context);
    }

    // empty result means inference failed
    std::vector<Type> TypeChecker::inferTypeArguments(
        const CompilationUnit& unit,
        u32 callIndex,
        Type templateType,
        std::span<const u32> argumentValues,
        std::span<const Type> argumentTypes,
        bool reportsErrors)
    {
        const auto& templateDefinition = m_module.getFunctionDefinition(templateType);
        const auto functionName = templateDefinition.name();
        const auto typeParameters = m_module.genericFunctionData(templateDefinition).typeParameters;
        auto parameterTypes = std::vector<Type>{};
        for (const auto& parameter : templateDefinition.parameters())
        {
            parameterTypes.push_back(parameter.type().toBaseType());
        }

        const auto argumentCount = std::min(argumentValues.size(), parameterTypes.size());
        auto typeArguments = std::vector<Type>(typeParameters.size(), Type::Undefined());
        auto bindingArguments = std::vector<u32>(typeParameters.size(), InvalidNodeIndex);
        for (const auto bindsLiterals : { false, true })
        {
            for (size_t position = 0; position < argumentCount; ++position)
            {
                const auto argumentValue = argumentValues[position];
                const auto match = MatchTypeParameter(m_module, typeParameters, parameterTypes[position], argumentTypes[position]);
                if (!match.has_value() || IsPlainNumberLiteral(unit.nodeBuffer, argumentValue) != bindsLiterals)
                {
                    continue;
                }

                const auto [typeParameterPosition, argumentType] = match.value();
                auto& typeArgument = typeArguments[typeParameterPosition];
                // skip a literal if a non-literal argument set the type argument
                if (bindsLiterals && typeArgument != Type::Undefined() && !IsPlainNumberLiteral(unit.nodeBuffer, bindingArguments[typeParameterPosition]))
                {
                    continue;
                }
                if (typeArgument != Type::Undefined() && typeArgument != argumentType)
                {
                    if (reportsErrors)
                    {
                        m_diagnostics.addConflictingTypeInferenceError(
                            unit.source,
                            ExpressionSourceLocation(m_compilationContext, unit, argumentValue),
                            ExpressionSourceLocation(m_compilationContext, unit, bindingArguments[typeParameterPosition]),
                            functionName,
                            std::string(m_module.getNameByType(typeParameters[typeParameterPosition])),
                            formatTypeName(m_module, typeArgument),
                            formatTypeName(m_module, argumentType));
                    }
                    return {};
                }

                typeArgument = argumentType;
                bindingArguments[typeParameterPosition] = argumentValue;
            }
        }

        for (size_t position = 0; position < typeArguments.size(); ++position)
        {
            if (typeArguments[position] == Type::Undefined())
            {
                if (reportsErrors)
                {
                    m_diagnostics.addCannotInferTypeParameterError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, callIndex),
                        functionName,
                        std::string(m_module.getNameByType(typeParameters[position])),
                        typeParameters.size());
                }
                return {};
            }
        }

        return typeArguments;
    }

    Type TypeChecker::candidateForRanking(
        const CompilationUnit& unit,
        u32 callIndex,
        Type candidate,
        size_t argumentsStart,
        std::span<const Type> typesWithoutContext)
    {
        if (!m_module.getFunctionDefinition(candidate).isTemplate())
        {
            return candidate;
        }

        auto argumentValues = std::vector<u32>{};
        for (size_t position = 0; position < typesWithoutContext.size(); ++position)
        {
            argumentValues.push_back(NodeLayout::argumentValue(unit.nodeBuffer, m_callArgumentStack[argumentsStart + position]));
        }

        const auto typeArguments = inferTypeArguments(unit, callIndex, candidate, argumentValues, typesWithoutContext, false);
        if (typeArguments.empty())
        {
            return Type::Undefined();
        }

        return specializationOf(candidate, typeArguments);
    }

    Type TypeChecker::specializationOf(Type templateType, const std::vector<Type>& typeArguments)
    {
        const auto existing = m_module.tryGetSpecialization(templateType, typeArguments);
        if (existing != Type::Undefined())
        {
            return existing;
        }

        const auto specialization = m_module.createSpecialization(templateType, typeArguments);
        promoteReadOnlySliceParametersOfSpecialization(
            m_compilationContext,
            m_module,
            specialization,
            m_module.getFunctionDefinition(specialization).definitionNode());
        return specialization;
    }

    void TypeChecker::typeCheckGenericFunctionDeclaration(NodeIndex declaration)
    {
        const auto templateType = typeOfNode(declaration);
        if (templateType == Type::Undefined())
        {
            return;
        }

        const auto& unit = unitFor(declaration);
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, declaration.index);
        const auto typeParameterCount = m_module.genericFunctionData(m_module.getFunctionDefinition(templateType)).typeParameters.size();
        const auto undefinedTypeArguments = std::vector<Type>(typeParameterCount, Type::Undefined());
        bindTypeParameters(unit, functionLayout.typeParameters, undefinedTypeArguments);

        // its signature has Type::Undefined() for each T, and it gets no body
        const auto declarationSignature = specializationOf(templateType, undefinedTypeArguments);

        const auto bodyBegin = NodeLayout::subtreeBegin(unit.nodeBuffer, functionLayout.body);
        m_specializationNodeTypes.fileId = unit.fileId;
        m_specializationNodeTypes.firstIndex = bodyBegin;
        m_specializationNodeTypes.types.assign(functionLayout.body - bodyBegin + 1, Type::Undefined());
        m_unresolvedOverloadCalls.clear();

        const auto diagnosticCount = m_diagnostics.diagnostics().size();
        m_isDeclarationCheck = true;
        typeCheckFunctionBody(unit, declaration.index, declarationSignature);
        m_isDeclarationCheck = false;

        const auto& diagnostics = m_diagnostics.diagnostics();
        for (auto position = diagnosticCount; position < diagnostics.size(); ++position)
        {
            if (diagnostics[position].level() == DiagnosticLevel::Error)
            {
                m_genericFunctionsWithDeclarationErrors.insert(templateType.id());
                break;
            }
        }

        m_typeArguments.clear();
        m_specializationNodeTypes.types.clear();
    }

    void TypeChecker::checkArgumentsOfUnresolvedCall(const CompilationUnit& unit, u32 callIndex)
    {
        auto argumentNodes = std::vector<u32>{};
        NodeLayout::functionCall(unit.nodeBuffer, callIndex, argumentNodes);
        for (const auto argumentNode : argumentNodes)
        {
            static_cast<void>(typeCheckExpression(unit, NodeLayout::argumentValue(unit.nodeBuffer, argumentNode), ExpressionContext{}));
        }
    }

    void TypeChecker::addPendingSpecialization(Type specialization, NodeIndex call)
    {
        if (m_isDeclarationCheck || m_pendingSpecializationIds.contains(specialization.id()))
        {
            return;
        }

        size_t depth = 1;
        if (m_checkedPendingIndex.has_value())
        {
            depth = m_pendingSpecializations[m_checkedPendingIndex.value()].depth + 1;
        }
        if (depth > MaximumSpecializationDepth)
        {
            auto firstIndex = m_checkedPendingIndex.value();
            while (m_pendingSpecializations[firstIndex].callerIndex.has_value())
            {
                firstIndex = m_pendingSpecializations[firstIndex].callerIndex.value();
            }

            const auto& specializationDefinition = m_module.getFunctionDefinition(specialization);
            const auto templateName = m_module.getFunctionDefinition(m_module.genericFunctionData(specializationDefinition).templateType).name();
            const auto& unit = unitFor(call);
            const auto firstCall = m_pendingSpecializations[firstIndex].call;
            const auto& firstUnit = unitFor(firstCall);
            m_diagnostics.addGenericCallsNestedTooDeeplyError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, call.index),
                templateName,
                MaximumSpecializationDepth,
                firstUnit.source,
                ExpressionSourceLocation(m_compilationContext, firstUnit, firstCall.index));
            return;
        }

        m_pendingSpecializationIds.insert(specialization.id());
        m_pendingSpecializations.push_back(PendingSpecialization{ specialization, call, m_checkedPendingIndex, depth });
    }

    void TypeChecker::addSpecializationCallLabels(size_t diagnosticCount, size_t pendingIndex)
    {
        auto labelIndex = std::optional<size_t>{ pendingIndex };
        while (labelIndex.has_value())
        {
            const auto pending = m_pendingSpecializations[labelIndex.value()];
            const auto& callUnit = unitFor(pending.call);
            m_diagnostics.addSpecializationCallLabels(
                diagnosticCount,
                callUnit.source,
                ExpressionSourceLocation(m_compilationContext, callUnit, pending.call.index),
                m_module.getFunctionDefinition(pending.specialization).fullName());
            labelIndex = pending.callerIndex;
        }
    }

    void TypeChecker::typeCheckPendingSpecializations()
    {
        CARACAL_ZONE_NAMED("check.pendingSpecializations");
        for (size_t pendingIndex = 0; pendingIndex < m_pendingSpecializations.size(); ++pendingIndex)
        {
            const auto specialization = m_pendingSpecializations[pendingIndex].specialization;
            const auto& definition = m_module.getFunctionDefinition(specialization);

            if (m_genericFunctionsWithDeclarationErrors.contains(m_module.genericFunctionData(definition).templateType.id()))
            {
                continue;
            }

            const auto templateNode = definition.definitionNode();
            const auto& unit = unitFor(templateNode);
            const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, templateNode.index);
            bindTypeParameters(unit, functionLayout.typeParameters, m_module.genericFunctionData(definition).typeArguments);

            const auto bodyBegin = NodeLayout::subtreeBegin(unit.nodeBuffer, functionLayout.body);
            m_specializationNodeTypes.fileId = unit.fileId;
            m_specializationNodeTypes.firstIndex = bodyBegin;
            m_specializationNodeTypes.types.assign(functionLayout.body - bodyBegin + 1, Type::Undefined());
            m_unresolvedOverloadCalls.clear();
            const auto diagnosticCount = m_diagnostics.diagnostics().size();
            m_checkedPendingIndex = pendingIndex;
            typeCheckFunctionBody(unit, templateNode.index, specialization);
            m_checkedPendingIndex.reset();
            addSpecializationCallLabels(diagnosticCount, pendingIndex);
            m_typeArguments.clear();
            m_module.setBodyNodeTypes(m_module.getFunctionDefinition(specialization), std::move(m_specializationNodeTypes.types));
            m_specializationNodeTypes.types.clear();
        }

        m_pendingSpecializations.clear();
    }
}
