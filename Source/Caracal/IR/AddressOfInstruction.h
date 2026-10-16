#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/LocalSlotRef.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class AddressOfInstruction final : public Instruction
    {
    public:
        AddressOfInstruction(TemporaryId resultId, LocalSlotRef local, Type type) noexcept;

        [[nodiscard]] LocalSlotRef local() const noexcept { return m_local; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        LocalSlotRef m_local;
        Type m_type;
    };
}
