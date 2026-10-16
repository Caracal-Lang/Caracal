#include <Caracal/IR/MultiplyInstruction.h>

namespace Caracal
{
    MultiplyInstruction::MultiplyInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type) noexcept
        : Instruction{ InstructionKind::Multiply, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
    {
    }
}
