#include <Caracal/IR/NotEqualInstruction.h>

namespace Caracal
{
    NotEqualInstruction::NotEqualInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type, Type operandType) noexcept
        : Instruction{ InstructionKind::NotEqual, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
        , m_operandType{ operandType }
    {
    }

    void NotEqualInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        m_leftValue = ValueRef{ remapTemporaryId(remap, m_leftValue.id()) };
        m_rightValue = ValueRef{ remapTemporaryId(remap, m_rightValue.id()) };
    }
}
