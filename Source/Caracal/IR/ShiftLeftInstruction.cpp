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
}
