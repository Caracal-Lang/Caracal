#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class ElementAddressInstruction final : public Instruction
    {
    public:
        ElementAddressInstruction(
            TemporaryId resultId,
            ValueRef baseAddress,
            Type arrayType,
            ValueRef index,
            Type type) noexcept;

        [[nodiscard]] ValueRef baseAddress() const noexcept { return m_baseAddress; }
        [[nodiscard]] Type arrayType() const noexcept { return m_arrayType; }
        [[nodiscard]] ValueRef index() const noexcept { return m_index; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        ValueRef m_baseAddress;
        Type m_arrayType;
        ValueRef m_index;
        Type m_type;
    };
}
