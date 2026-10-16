#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class IntWidenInstruction final : public Instruction
    {
    public:
        IntWidenInstruction(TemporaryId resultId, ValueRef operandValue, Type sourceType, Type type) noexcept;

        [[nodiscard]] ValueRef operandValue() const noexcept { return m_operandValue; }
        // type of the narrower operand, decides sign extension vs zero extension during codegen
        [[nodiscard]] Type sourceType() const noexcept { return m_sourceType; }
        [[nodiscard]] Type type() const noexcept { return m_type; }

    private:
        ValueRef m_operandValue;
        Type m_sourceType;
        Type m_type;
    };
}
