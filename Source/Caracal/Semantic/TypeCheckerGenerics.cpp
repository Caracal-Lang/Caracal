#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Profiling.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>

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
            m_pendingSpecializations.push_back(PendingSpecialization{ specialization, call });
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
            typeCheckFunctionBody(unit, templateNode.index, specialization);
            m_typeArguments.clear();
            m_module.setBodyNodeTypes(m_module.getFunctionDefinition(specialization), std::move(m_specializationNodeTypes.types));
            m_specializationNodeTypes.types.clear();
        }

        m_pendingSpecializations.clear();
    }
}
