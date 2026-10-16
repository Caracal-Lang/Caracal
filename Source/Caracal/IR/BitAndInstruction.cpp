#include <Caracal/IR/BitAndInstruction.h>

namespace Caracal
{
    BitAndInstruction::BitAndInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type) noexcept
        : Instruction{ InstructionKind::BitAnd, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
    {
    }
}
