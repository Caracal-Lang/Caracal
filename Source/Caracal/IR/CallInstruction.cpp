#include <Caracal/IR/CallInstruction.h>

namespace Caracal
{
    CallInstruction::CallInstruction(TemporaryId resultId, FunctionId functionId, std::vector<ValueRef> arguments, Type type) noexcept
        : Instruction{ InstructionKind::Call, resultId }
        , m_functionId{ functionId }
        , m_arguments{ std::move(arguments) }
        , m_type{ type }
    {
    }

    void CallInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        for (auto& argument : m_arguments)
        {
            argument = ValueRef{ remapTemporaryId(remap, argument.id()) };
        }
    }
}
