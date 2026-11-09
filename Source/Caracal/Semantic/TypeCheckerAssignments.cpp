#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <string>
#include <string_view>

namespace Caracal
{
    static bool IsInitConstantAssignmentSite(std::string_view functionName)
    {
        return functionName == EntryPointFunctionName || functionName == UserMainFunctionName;
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
}
