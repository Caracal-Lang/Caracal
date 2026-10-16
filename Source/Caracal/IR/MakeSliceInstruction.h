#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class MakeSliceInstruction final : public Instruction
    {
    public:
        MakeSliceInstruction(
            TemporaryId resultId,
            ValueRef baseAddress,
            ValueRef length,
            Type type) noexcept;

        [[nodiscard]] ValueRef baseAddress() const noexcept { return m_baseAddress; }
        [[nodiscard]] ValueRef length() const noexcept { return m_length; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        ValueRef m_baseAddress;
        ValueRef m_length;
        Type m_type;
    };
}
