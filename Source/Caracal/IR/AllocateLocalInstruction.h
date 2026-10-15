#pragma once

#include <Caracal/IR/Instruction.h>

#include <string>

namespace Caracal
{
    class AllocateLocalInstruction final : public Instruction
    {
    public:
        AllocateLocalInstruction(LocalSlotId slotId, std::string localName, Type type) noexcept;

        [[nodiscard]] LocalSlotId slotId() const noexcept { return m_slotId; }
        [[nodiscard]] const std::string& localName() const noexcept { return m_localName; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        LocalSlotId m_slotId;
        std::string m_localName;
        Type m_type;
    };
}
