#include <Caracal/IR/ParameterInstruction.h>

namespace Caracal
{
    ParameterInstruction::ParameterInstruction(TemporaryId resultId, i32 parameterIndex, IRParameter parameter) noexcept
        : Instruction{ InstructionKind::Parameter, resultId }
        , m_parameterIndex{ parameterIndex }
        , m_parameter{ std::move(parameter) }
    {
    }
}
