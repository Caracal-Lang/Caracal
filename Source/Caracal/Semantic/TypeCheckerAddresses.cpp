#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/TokenBuffer.h>

namespace Caracal
{
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
}
