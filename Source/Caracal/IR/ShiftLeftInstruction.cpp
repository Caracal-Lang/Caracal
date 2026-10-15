#include <Caracal/IR/ShiftLeftInstruction.h>

namespace Caracal
{
    ShiftLeftInstruction::ShiftLeftInstruction(TemporaryId resultId, ValueRef value, ValueRef amount, Type type) noexcept
        : Instruction{ InstructionKind::ShiftLeft, resultId }
        , m_value{ value }
        , m_amount{ amount }
        , m_type{ type }
    {
    }

    void ShiftLeftInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        m_value = ValueRef{ remapTemporaryId(remap, m_value.id()) };
        m_amount = ValueRef{ remapTemporaryId(remap, m_amount.id()) };
    }
}
