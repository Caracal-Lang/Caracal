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
}
