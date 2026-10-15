#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class BitNotInstruction final : public Instruction
    {
    public:
        BitNotInstruction(TemporaryId resultId, ValueRef operandValue, Type type) noexcept;

        [[nodiscard]] ValueRef operandValue() const noexcept { return m_operandValue; }
        [[nodiscard]] Type type() const noexcept { return m_type; }
        void remapValueIds(const ValueIdMap& remap) noexcept override;

    private:
        ValueRef m_operandValue;
        Type m_type;
    };
}
