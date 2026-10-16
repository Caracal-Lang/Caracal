#include <Caracal/IR/IntToFloatInstruction.h>

namespace Caracal
{
    IntToFloatInstruction::IntToFloatInstruction(TemporaryId resultId, ValueRef operandValue, Type sourceType, Type type) noexcept
        : Instruction{ InstructionKind::IntToFloat, resultId }
        , m_operandValue{ operandValue }
        , m_sourceType{ sourceType }
        , m_type{ type }
    {
    }
}
