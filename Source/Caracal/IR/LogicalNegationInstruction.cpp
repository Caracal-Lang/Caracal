#include <Caracal/IR/LogicalNegationInstruction.h>

namespace Caracal
{
    LogicalNegationInstruction::LogicalNegationInstruction(TemporaryId resultId, ValueRef operandValue, Type type) noexcept
        : Instruction{ InstructionKind::LogicalNegation, resultId }
        , m_operandValue{ operandValue }
        , m_type{ type }
    {
    }
}
