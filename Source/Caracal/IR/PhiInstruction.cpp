#include <Caracal/IR/PhiInstruction.h>

namespace Caracal
{
    PhiInstruction::PhiInstruction(TemporaryId resultId, std::vector<PhiInput> inputs, Type type) noexcept
        : Instruction{ InstructionKind::Phi, resultId }
        , m_inputs{ std::move(inputs) }
        , m_type{ type }
    {
    }

    void PhiInstruction::setInputs(std::vector<PhiInput> inputs) noexcept
    {
        m_inputs = std::move(inputs);
    }
}
