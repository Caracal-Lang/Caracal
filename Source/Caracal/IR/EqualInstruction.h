#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class EqualInstruction final : public Instruction
    {
    public:
        EqualInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type, Type operandType) noexcept;

        [[nodiscard]] ValueRef leftValue() const noexcept { return m_leftValue; }
        [[nodiscard]] ValueRef rightValue() const noexcept { return m_rightValue; }
        [[nodiscard]] Type type() const noexcept { return m_type; }
        [[nodiscard]] Type operandType() const noexcept { return m_operandType; }

    private:
        ValueRef m_leftValue;
        ValueRef m_rightValue;
        Type m_type;
        Type m_operandType;
    };
}
