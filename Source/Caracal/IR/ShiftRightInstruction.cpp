#include <Caracal/IR/ShiftRightInstruction.h>

namespace Caracal
{
    ShiftRightInstruction::ShiftRightInstruction(TemporaryId resultId, ValueRef value, ValueRef amount, Type type) noexcept
        : Instruction{ InstructionKind::ShiftRight, resultId }
        , m_value{ value }
        , m_amount{ amount }
        , m_type{ type }
    {
    }

    void ShiftRightInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        m_value = ValueRef{ remapTemporaryId(remap, m_value.id()) };
        m_amount = ValueRef{ remapTemporaryId(remap, m_amount.id()) };
    }
}
