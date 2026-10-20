#include <Caracal/IR/FunctionBuilder.h>

#include <utility>

namespace Caracal
{
    u32 FunctionBuilder::addBlock(BlockId id, BlockLabel label)
    {
        const auto blockIndex = static_cast<u32>(m_blocks.size());
        m_blocks.push_back(BasicBlock{ id, label, 0, 0 });
        m_blockStates.push_back(BlockState{});
        if (static_cast<size_t>(id) >= m_blockIndexById.size())
        {
            m_blockIndexById.resize(static_cast<size_t>(id) + 1, 0);
        }
        m_blockIndexById[static_cast<size_t>(id)] = blockIndex;
        return blockIndex;
    }

    bool FunctionBuilder::popLastBlock(BlockId expectedId) noexcept
    {
        for (auto blockIndex = m_blocks.size(); blockIndex > 0; --blockIndex)
        {
            auto& state = m_blockStates[blockIndex - 1];
            if (state.isDropped)
            {
                continue;
            }

            if (m_blocks[blockIndex - 1].id != expectedId)
            {
                return false;
            }

            state.isDropped = true;
            return true;
        }

        return false;
    }

    u32 FunctionBuilder::appendInstruction(u32 blockIndex, const Instruction& instruction)
    {
        auto& state = m_blockStates[blockIndex];
        auto inPrologue = false;
        if (state.prologueOpen && instruction.kind == InstructionKind::Parameter)
        {
            inPrologue = true;
        }
        else
        {
            state.prologueOpen = false;
        }

        m_instructions.push_back(instruction);
        m_instructionPlacements.push_back(InstructionPlacement{ blockIndex, inPrologue });
        return static_cast<u32>(m_instructions.size() - 1);
    }

    u32 FunctionBuilder::appendPrologueInstruction(u32 blockIndex, const Instruction& instruction)
    {
        m_instructions.push_back(instruction);
        m_instructionPlacements.push_back(InstructionPlacement{ blockIndex, true });
        return static_cast<u32>(m_instructions.size() - 1);
    }

    void FunctionBuilder::setTerminator(u32 blockIndex, const Instruction& terminator)
    {
        m_blockStates[blockIndex].terminator = terminator;
    }

    bool FunctionBuilder::hasTerminator(u32 blockIndex) const noexcept
    {
        return m_blockStates[blockIndex].terminator.has_value();
    }

    u32 FunctionBuilder::appendOperands(std::span<const TemporaryId> operands)
    {
        const auto start = static_cast<u32>(m_operands.size());
        m_operands.insert(m_operands.end(), operands.begin(), operands.end());
        return start;
    }

    void FunctionBuilder::setPhiInputs(u32 instructionIndex, std::span<const PhiInput> inputs)
    {
        auto& phi = m_instructions[instructionIndex];
        phi.a = static_cast<i32>(m_phiInputs.size());
        phi.b = static_cast<i32>(inputs.size());
        m_phiInputs.insert(m_phiInputs.end(), inputs.begin(), inputs.end());
    }

    void FunctionBuilder::finish(Function& function)
    {
        const auto blockCount = m_blocks.size();
        m_prologueCounts.assign(blockCount, 0);
        m_bodyCounts.assign(blockCount, 0);
        for (const auto& placement : m_instructionPlacements)
        {
            if (placement.inPrologue)
            {
                m_prologueCounts[placement.blockIndex]++;
            }
            else
            {
                m_bodyCounts[placement.blockIndex]++;
            }
        }

        // places the blocks that are not dropped one after another, each as prologue, body, terminator
        m_prologueCursors.assign(blockCount, 0);
        m_bodyCursors.assign(blockCount, 0);
        std::vector<BasicBlock> keptBlocks;
        keptBlocks.reserve(blockCount);
        u32 instructionCount = 0;
        for (size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex)
        {
            const auto& state = m_blockStates[blockIndex];
            if (state.isDropped)
            {
                continue;
            }

            auto block = m_blocks[blockIndex];
            block.instructionStart = instructionCount;
            m_prologueCursors[blockIndex] = instructionCount;
            m_bodyCursors[blockIndex] = instructionCount + m_prologueCounts[blockIndex];
            instructionCount += m_prologueCounts[blockIndex] + m_bodyCounts[blockIndex];
            if (state.terminator.has_value())
            {
                instructionCount++;
            }
            block.instructionCount = instructionCount - block.instructionStart;
            keptBlocks.push_back(block);
        }

        std::vector<Instruction> sortedInstructions(instructionCount, Instruction{ InstructionKind::Unreachable, -1, Type::Undefined() });
        for (size_t instructionIndex = 0; instructionIndex < m_instructions.size(); ++instructionIndex)
        {
            const auto& placement = m_instructionPlacements[instructionIndex];
            if (m_blockStates[placement.blockIndex].isDropped)
            {
                continue;
            }

            if (placement.inPrologue)
            {
                sortedInstructions[m_prologueCursors[placement.blockIndex]++] = m_instructions[instructionIndex];
            }
            else
            {
                sortedInstructions[m_bodyCursors[placement.blockIndex]++] = m_instructions[instructionIndex];
            }
        }

        for (size_t blockIndex = 0; blockIndex < blockCount; ++blockIndex)
        {
            const auto& state = m_blockStates[blockIndex];
            if (!state.isDropped && state.terminator.has_value())
            {
                sortedInstructions[m_bodyCursors[blockIndex]] = state.terminator.value();
            }
        }

        function.setBody(std::move(sortedInstructions), std::move(keptBlocks), m_operands, m_phiInputs);
        clear();
    }

    void FunctionBuilder::clear() noexcept
    {
        m_instructions.clear();
        m_instructionPlacements.clear();
        m_blocks.clear();
        m_blockStates.clear();
        m_blockIndexById.clear();
        m_operands.clear();
        m_phiInputs.clear();
    }
}
