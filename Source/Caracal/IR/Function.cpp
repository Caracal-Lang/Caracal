#include <Caracal/IR/Function.h>

#include <utility>

namespace Caracal
{
    Function::Function(FunctionId id, std::string name, std::vector<IRParameter> parameters, Type returnType)
        : m_id{ id }
        , m_name{ std::move(name) }
        , m_parameters{ std::move(parameters) }
        , m_returnType{ returnType }
    {
    }

    void Function::setBody(std::vector<Instruction> instructions, std::vector<BasicBlock> blocks, std::span<const TemporaryId> operands, std::span<const PhiInput> phiInputs)
    {
        m_instructions = std::move(instructions);
        m_blocks = std::move(blocks);
        m_operands.assign(operands.begin(), operands.end());
        m_phiInputs.assign(phiInputs.begin(), phiInputs.end());
    }

    std::span<const TemporaryId> Function::operands(u32 start, u32 count) const noexcept
    {
        return std::span<const TemporaryId>{ m_operands }.subspan(start, count);
    }

    std::span<const PhiInput> Function::phiInputsAt(u32 instructionIndex) const noexcept
    {
        return std::span<const PhiInput>{ m_phiInputs }.subspan(static_cast<size_t>(aAt(instructionIndex)), static_cast<size_t>(bAt(instructionIndex)));
    }

    std::span<const PhiInput> Function::phiInputs(const Instruction& phi) const noexcept
    {
        return std::span<const PhiInput>{ m_phiInputs }.subspan(static_cast<size_t>(phi.a), static_cast<size_t>(phi.b));
    }
}
