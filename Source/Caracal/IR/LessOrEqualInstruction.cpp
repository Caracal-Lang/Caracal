#include <Caracal/IR/LessOrEqualInstruction.h>

namespace Caracal
{
    LessOrEqualInstruction::LessOrEqualInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type, Type operandType) noexcept
        : Instruction{ InstructionKind::LessOrEqual, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
        , m_operandType{ operandType }
    {
    }
}
