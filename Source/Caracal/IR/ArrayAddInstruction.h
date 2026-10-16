#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class ArrayAddInstruction final : public Instruction
    {
    public:
        ArrayAddInstruction(ValueRef descriptorAddress, ValueRef value, Type arrayType, FunctionId reallocFunctionId) noexcept;

        [[nodiscard]] ValueRef descriptorAddress() const noexcept { return m_descriptorAddress; }
        [[nodiscard]] ValueRef value() const noexcept { return m_value; }
        [[nodiscard]] Type arrayType() const noexcept { return m_arrayType; }
        [[nodiscard]] FunctionId reallocFunctionId() const noexcept { return m_reallocFunctionId; }

    private:
        ValueRef m_descriptorAddress;
        ValueRef m_value;
        Type m_arrayType;
        FunctionId m_reallocFunctionId;
    };
}
