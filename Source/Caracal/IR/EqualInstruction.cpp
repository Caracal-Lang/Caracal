#include <Caracal/IR/EqualInstruction.h>

namespace Caracal
{
    EqualInstruction::EqualInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type, Type operandType) noexcept
        : Instruction{ InstructionKind::Equal, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
        , m_operandType{ operandType }
    {
    }

    void EqualInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        m_leftValue = ValueRef{ remapTemporaryId(remap, m_leftValue.id()) };
        m_rightValue = ValueRef{ remapTemporaryId(remap, m_rightValue.id()) };
    }
}
