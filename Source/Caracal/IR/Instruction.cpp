#include <Caracal/IR/Instruction.h>

namespace Caracal
{
    Instruction::Instruction(InstructionKind kind) noexcept
        : m_kind{ kind }
        , m_resultId{ -1 }
    {
    }

    Instruction::Instruction(InstructionKind kind, TemporaryId resultId) noexcept
        : m_kind{ kind }
        , m_resultId{ resultId }
    {
    }
}
