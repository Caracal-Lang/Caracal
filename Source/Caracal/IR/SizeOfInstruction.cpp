#include <Caracal/IR/SizeOfInstruction.h>

namespace Caracal
{
    SizeOfInstruction::SizeOfInstruction(TemporaryId resultId, Type measuredType, Type type) noexcept
        : Instruction{ InstructionKind::SizeOf, resultId }
        , m_measuredType{ measuredType }
        , m_type{ type }
    {
    }

    void SizeOfInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
    }
}
