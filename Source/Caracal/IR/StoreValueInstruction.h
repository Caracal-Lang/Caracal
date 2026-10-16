#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class StoreValueInstruction final : public Instruction
    {
    public:
        StoreValueInstruction(ValueRef value, ValueRef address, Type type) noexcept;

        [[nodiscard]] ValueRef value() const noexcept { return m_value; }
        [[nodiscard]] ValueRef address() const noexcept { return m_address; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        ValueRef m_value;
        ValueRef m_address;
        Type m_type;
    };
}
