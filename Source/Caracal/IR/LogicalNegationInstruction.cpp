#include <Caracal/IR/LogicalNegationInstruction.h>

namespace Caracal
{
    LogicalNegationInstruction::LogicalNegationInstruction(TemporaryId resultId, ValueRef operandValue, Type type) noexcept
        : Instruction{ InstructionKind::LogicalNegation, resultId }
        , m_operandValue{ operandValue }
        , m_type{ type }
    {
    }

    void LogicalNegationInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        m_operandValue = ValueRef{ remapTemporaryId(remap, m_operandValue.id()) };
    }
}
