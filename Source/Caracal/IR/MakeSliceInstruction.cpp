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
}
