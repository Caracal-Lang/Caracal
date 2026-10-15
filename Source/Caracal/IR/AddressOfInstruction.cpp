#include <Caracal/IR/AddressOfInstruction.h>

namespace Caracal
{
    AddressOfInstruction::AddressOfInstruction(TemporaryId resultId, LocalSlotRef local, Type type) noexcept
        : Instruction{ InstructionKind::AddressOf, resultId }
        , m_local{ local }
        , m_type{ type }
    {
    }

    void AddressOfInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
    }
}
