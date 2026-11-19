#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace Caracal
{
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

        addPendingSpecialization(specialization, NodeIndex{ unit.fileId, index });
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
        const auto& templateDefinition = m_module.getFunctionDefinition(templateType);
        const auto functionName = templateDefinition.name();
        const auto typeParameters = m_module.genericFunctionData(templateDefinition).typeParameters;
        auto parameterTypes = std::vector<Type>{};
        for (const auto& parameter : templateDefinition.parameters())
        {
            parameterTypes.push_back(parameter.type().toBaseType());
        }

        auto argumentNodes = std::vector<u32>{};
        NodeLayout::functionCall(unit.nodeBuffer, index, argumentNodes);
        const auto argumentCount = std::min(argumentNodes.size(), parameterTypes.size());

        auto typeArguments = std::vector<Type>(typeParameters.size(), Type::Undefined());
        auto bindingArguments = std::vector<u32>(typeParameters.size(), InvalidNodeIndex);
        for (const auto bindsLiterals : { false, true })
        {
            for (size_t position = 0; position < argumentCount; ++position)
            {
                const auto typeParameter = std::ranges::find(typeParameters, parameterTypes[position]);
                const auto argumentValue = NodeLayout::argumentValue(unit.nodeBuffer, argumentNodes[position]);
                if (typeParameter == typeParameters.end() || IsPlainNumberLiteral(unit.nodeBuffer, argumentValue) != bindsLiterals)
                {
                    continue;
                }

                const auto typeParameterPosition = static_cast<size_t>(typeParameter - typeParameters.begin());
                auto& typeArgument = typeArguments[typeParameterPosition];
                if (bindsLiterals && typeArgument != Type::Undefined())
                {
                    continue;
                }

                const auto argumentType = checkArgumentWithoutEmission(unit, argumentValue, ExpressionContext{}).toValue();
                if (argumentType == Type::Undefined())
                {
                    continue;
                }
                if (typeArgument != Type::Undefined() && typeArgument != argumentType)
                {
                    m_diagnostics.addConflictingTypeInferenceError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, argumentValue),
                        ExpressionSourceLocation(m_compilationContext, unit, bindingArguments[typeParameterPosition]),
                        functionName,
                        std::string(m_module.getNameByType(*typeParameter)),
                        formatTypeName(m_module, typeArgument),
                        formatTypeName(m_module, argumentType));
                    return Type::Undefined();
                }

                typeArgument = argumentType;
                bindingArguments[typeParameterPosition] = argumentValue;
            }
        }

        for (size_t position = 0; position < typeArguments.size(); ++position)
        {
            if (typeArguments[position] == Type::Undefined())
            {
                m_diagnostics.addCannotInferTypeParameterError(
                    unit.source,
                    ExpressionSourceLocation(m_compilationContext, unit, index),
                    functionName,
                    std::string(m_module.getNameByType(typeParameters[position])));
                return Type::Undefined();
            }
        }

        const auto specialization = specializationOf(templateType, typeArguments);
        addPendingSpecialization(specialization, NodeIndex{ unit.fileId, index });
        return typeCheckResolvedFunctionCall(unit, index, specialization, context);
    }

    Type TypeChecker::specializationOf(Type templateType, const std::vector<Type>& typeArguments)
    {
        const auto existing = m_module.tryGetSpecialization(templateType, typeArguments);
        if (existing != Type::Undefined())
        {
            return existing;
        }

        return m_module.createSpecialization(templateType, typeArguments);
    }

    void TypeChecker::addPendingSpecialization(Type specialization, NodeIndex call)
    {
        if (m_pendingSpecializationIds.insert(specialization.id()).second)
        {
            m_pendingSpecializations.push_back(PendingSpecialization{ specialization, call, m_checkedPendingIndex });
        }
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
