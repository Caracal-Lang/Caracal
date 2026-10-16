#include <Caracal/IR/LoadValueInstruction.h>

namespace Caracal
{
    LoadValueInstruction::LoadValueInstruction(TemporaryId resultId, ValueRef address, Type type) noexcept
        : Instruction{ InstructionKind::LoadValue, resultId }
        , m_address{ address }
        , m_type{ type }
    {
    }
}
