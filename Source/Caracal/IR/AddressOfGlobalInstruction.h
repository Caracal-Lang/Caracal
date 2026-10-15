#pragma once

#include <Caracal/IR/Instruction.h>

#include <string>

namespace Caracal
{
    class AddressOfGlobalInstruction final : public Instruction
    {
    public:
        AddressOfGlobalInstruction(TemporaryId resultId, std::string name, Type type) noexcept;

        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        [[nodiscard]] Type type() const noexcept { return m_type; }
        void remapValueIds(const ValueIdMap& remap) noexcept override;

    private:
        std::string m_name;
        Type m_type;
    };
}
