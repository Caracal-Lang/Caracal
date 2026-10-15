#include <Caracal/IR/MakeSliceInstruction.h>

namespace Caracal
{
    MakeSliceInstruction::MakeSliceInstruction(
        TemporaryId resultId,
        ValueRef baseAddress,
        ValueRef length,
        Type type) noexcept
        : Instruction{ InstructionKind::MakeSlice, resultId }
        , m_baseAddress{ baseAddress }
        , m_length{ length }
        , m_type{ type }
    {
    }

    void MakeSliceInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        m_baseAddress = ValueRef{ remapTemporaryId(remap, m_baseAddress.id()) };
        m_length = ValueRef{ remapTemporaryId(remap, m_length.id()) };
    }
}
