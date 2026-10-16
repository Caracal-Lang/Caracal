#include <Caracal/IR/NotEqualInstruction.h>

namespace Caracal
{
    NotEqualInstruction::NotEqualInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type, Type operandType) noexcept
        : Instruction{ InstructionKind::NotEqual, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
        , m_operandType{ operandType }
    {
    }
}
