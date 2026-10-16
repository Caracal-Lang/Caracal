#include <Caracal/IR/GreaterThanInstruction.h>

namespace Caracal
{
    GreaterThanInstruction::GreaterThanInstruction(TemporaryId resultId, ValueRef leftValue, ValueRef rightValue, Type type, Type operandType) noexcept
        : Instruction{ InstructionKind::GreaterThan, resultId }
        , m_leftValue{ leftValue }
        , m_rightValue{ rightValue }
        , m_type{ type }
        , m_operandType{ operandType }
    {
    }
}
