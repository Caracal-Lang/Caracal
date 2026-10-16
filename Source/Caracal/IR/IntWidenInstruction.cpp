#include <Caracal/IR/IntWidenInstruction.h>

namespace Caracal
{
    IntWidenInstruction::IntWidenInstruction(TemporaryId resultId, ValueRef operandValue, Type sourceType, Type type) noexcept
        : Instruction{ InstructionKind::IntWiden, resultId }
        , m_operandValue{ operandValue }
        , m_sourceType{ sourceType }
        , m_type{ type }
    {
    }
}
