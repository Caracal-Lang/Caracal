#include <Caracal/IR/BitXorInstruction.h>

namespace Caracal
{
    BitXorInstruction::BitXorInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type) noexcept
        : Instruction{ InstructionKind::BitXor, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
    {
    }
}
