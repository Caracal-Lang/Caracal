#include <Caracal/IR/CallVoidInstruction.h>

namespace Caracal
{
    CallVoidInstruction::CallVoidInstruction(FunctionId functionId, std::vector<ValueRef> arguments) noexcept
        : Instruction{ InstructionKind::CallVoid }
        , m_functionId{ functionId }
        , m_arguments{ std::move(arguments) }
    {
    }
}
