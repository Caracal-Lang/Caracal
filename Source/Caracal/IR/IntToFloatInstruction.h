#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class IntToFloatInstruction final : public Instruction
    {
    public:
        IntToFloatInstruction(TemporaryId resultId, ValueRef operandValue, Type sourceType, Type type) noexcept;

        [[nodiscard]] ValueRef operandValue() const noexcept { return m_operandValue; }
        // type of the integer operand, decides signed vs unsigned conversion during codegen
        [[nodiscard]] Type sourceType() const noexcept { return m_sourceType; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        ValueRef m_operandValue;
        Type m_sourceType;
        Type m_type;
    };
}
