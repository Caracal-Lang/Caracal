#include <Caracal/IR/LoadValueInstruction.h>

namespace Caracal
{
    LoadValueInstruction::LoadValueInstruction(TemporaryId resultId, ValueRef address, Type type) noexcept
        : Instruction{ InstructionKind::LoadValue, resultId }
        , m_address{ address }
        , m_type{ type }
    {
    }

    void LoadValueInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        m_address = ValueRef{ remapTemporaryId(remap, m_address.id()) };
    }
}
