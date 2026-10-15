#include <Caracal/IR/ConstantInstruction.h>

namespace Caracal
{
    ConstantInstruction::ConstantInstruction(TemporaryId resultId, ConstantValue value, Type type) noexcept
        : Instruction{ InstructionKind::Constant, resultId }
        , m_value{ value }
        , m_type{ type }
    {
    }

    void ConstantInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
    }
}
