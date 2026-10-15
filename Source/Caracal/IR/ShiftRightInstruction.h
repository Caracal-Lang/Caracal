#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    // the operand type decides the shift semantics, signed types shift arithmetic and unsigned types shift logical
    class ShiftRightInstruction final : public Instruction
    {
    public:
        ShiftRightInstruction(TemporaryId resultId, ValueRef value, ValueRef amount, Type type) noexcept;

        [[nodiscard]] ValueRef value() const noexcept { return m_value; }
        [[nodiscard]] ValueRef amount() const noexcept { return m_amount; }
        [[nodiscard]] Type type() const noexcept { return m_type; }
        void remapValueIds(const ValueIdMap& remap) noexcept override;

    private:
        ValueRef m_value;
        ValueRef m_amount;
        Type m_type;
    };
}
