#include <Caracal/IR/BitNotInstruction.h>

namespace Caracal
{
    BitNotInstruction::BitNotInstruction(TemporaryId resultId, ValueRef operandValue, Type type) noexcept
        : Instruction{ InstructionKind::BitNot, resultId }
        , m_operandValue{ operandValue }
        , m_type{ type }
    {
    }
}
