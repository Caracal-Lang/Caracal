#include <Caracal/IR/ValueNegationInstruction.h>

namespace Caracal
{
    ValueNegationInstruction::ValueNegationInstruction(TemporaryId resultId, ValueRef operandValue, Type type) noexcept
        : Instruction{ InstructionKind::ValueNegation, resultId }
        , m_operandValue{ operandValue }
        , m_type{ type }
    {
    }
}
