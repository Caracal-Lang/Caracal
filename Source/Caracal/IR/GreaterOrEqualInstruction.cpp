#include <Caracal/IR/GreaterOrEqualInstruction.h>

namespace Caracal
{
    GreaterOrEqualInstruction::GreaterOrEqualInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type, Type operandType) noexcept
        : Instruction{ InstructionKind::GreaterOrEqual, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
        , m_operandType{ operandType }
    {
    }
}
