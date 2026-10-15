#pragma once

#include <Caracal/IR/ConstantValue.h>
#include <Caracal/IR/Instruction.h>

namespace Caracal
{
    class ConstantInstruction final : public Instruction
    {
    public:
        ConstantInstruction(TemporaryId resultId, ConstantValue value, Type type) noexcept;

        [[nodiscard]] const ConstantValue& value() const noexcept { return m_value; }
        [[nodiscard]] Type type() const noexcept { return m_type; }
        void remapValueIds(const ValueIdMap& remap) noexcept override;

    private:
        ConstantValue m_value;
        Type m_type;
    };
}
