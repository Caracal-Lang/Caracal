#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class BitOrInstruction final : public Instruction
    {
    public:
        BitOrInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type) noexcept;

        [[nodiscard]] ValueRef leftValue() const noexcept { return m_leftValue; }
        [[nodiscard]] ValueRef rightValue() const noexcept { return m_rightValue; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        ValueRef m_leftValue;
        ValueRef m_rightValue;
        Type m_type;
    };
}
