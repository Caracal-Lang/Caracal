#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class ArrayRemoveInstruction final : public Instruction
    {
    public:
        ArrayRemoveInstruction(ValueRef descriptorAddress, ValueRef index, Type arrayType, FunctionId memmoveFunctionId) noexcept;

        [[nodiscard]] ValueRef descriptorAddress() const noexcept { return m_descriptorAddress; }
        [[nodiscard]] ValueRef index() const noexcept { return m_index; }
        [[nodiscard]] Type arrayType() const noexcept { return m_arrayType; }
        [[nodiscard]] FunctionId memmoveFunctionId() const noexcept { return m_memmoveFunctionId; }

    private:
        ValueRef m_descriptorAddress;
        ValueRef m_index;
        Type m_arrayType;
        FunctionId m_memmoveFunctionId;
    };
}
