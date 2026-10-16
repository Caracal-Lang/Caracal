#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class SizeOfInstruction final : public Instruction
    {
    public:
        SizeOfInstruction(TemporaryId resultId, Type measuredType, Type type) noexcept;

        [[nodiscard]] Type measuredType() const noexcept { return m_measuredType; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        Type m_measuredType;
        Type m_type;
    };
}
