#include <Caracal/IR/ValueNegationInstruction.h>

namespace Caracal
{
    ValueNegationInstruction::ValueNegationInstruction(TemporaryId resultId, ValueRef operandValue, Type type) noexcept
        : Instruction{ InstructionKind::ValueNegation, resultId }
        , m_operandValue{ operandValue }
        , m_type{ type }
    {
    }

    void ValueNegationInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        m_operandValue = ValueRef{ remapTemporaryId(remap, m_operandValue.id()) };
    }
}
