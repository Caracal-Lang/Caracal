#include <Caracal/IR/DivideInstruction.h>

namespace Caracal
{
    DivideInstruction::DivideInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type) noexcept
        : Instruction{ InstructionKind::Divide, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
    {
    }
}
