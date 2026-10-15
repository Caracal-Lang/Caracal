#include <Caracal/IR/AllocateLocalInstruction.h>

namespace Caracal
{
    AllocateLocalInstruction::AllocateLocalInstruction(LocalSlotId slotId, std::string localName, Type type) noexcept
        : Instruction{ InstructionKind::AllocateLocal }
        , m_slotId{ slotId }
        , m_localName{ std::move(localName) }
        , m_type{ type }
    {
    }
}
