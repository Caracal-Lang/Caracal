#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

#include <string>

namespace Caracal
{
    class AddressOfFieldInstruction final : public Instruction
    {
    public:
        AddressOfFieldInstruction(
            TemporaryId resultId, 
            ValueRef objectAddress,
            Type objectType,
            std::string fieldName, 
            i32 fieldIndex,
            Type type) noexcept;

        [[nodiscard]] ValueRef objectAddress() const noexcept { return m_objectAddress; }
        [[nodiscard]] Type objectType() const noexcept { return m_objectType; }
        [[nodiscard]] const std::string& fieldName() const noexcept { return m_fieldName; }
        [[nodiscard]] i32 fieldIndex() const noexcept { return m_fieldIndex; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        ValueRef m_objectAddress;
        Type m_objectType;
        std::string m_fieldName;
        i32 m_fieldIndex;
        Type m_type;
    };
}
