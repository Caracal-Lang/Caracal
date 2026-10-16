#include <Caracal/IR/BitOrInstruction.h>

namespace Caracal
{
    BitOrInstruction::BitOrInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type) noexcept
        : Instruction{ InstructionKind::BitOr, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
    {
    }
}
