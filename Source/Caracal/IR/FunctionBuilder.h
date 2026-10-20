#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/IR/BasicBlock.h>
#include <Caracal/IR/BlockLabel.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/PhiInput.h>

#include <optional>
#include <span>
#include <vector>

namespace Caracal
{
    class CARACAL_API FunctionBuilder
    {
    public:
        [[nodiscard]] u32 addBlock(BlockId id, BlockLabel label);
        [[nodiscard]] u32 blockIndex(BlockId id) const noexcept { return m_blockIndexById[static_cast<size_t>(id)]; }
        [[nodiscard]] BlockId blockId(u32 blockIndex) const noexcept { return m_blocks[blockIndex].id; }
        [[nodiscard]] bool popLastBlock(BlockId expectedId) noexcept;

        u32 appendInstruction(u32 blockIndex, const Instruction& instruction);
        u32 appendPrologueInstruction(u32 blockIndex, const Instruction& instruction);
        void setTerminator(u32 blockIndex, const Instruction& terminator);
        [[nodiscard]] bool hasTerminator(u32 blockIndex) const noexcept;

        [[nodiscard]] u32 appendOperands(std::span<const TemporaryId> operands);
        void setPhiInputs(u32 instructionIndex, std::span<const PhiInput> inputs);

        void finish(Function& function);

    private:
        struct InstructionPlacement
        {
            u32 blockIndex;
            bool inPrologue;
        };

        struct BlockState
        {
            bool isDropped = false;
            bool prologueOpen = true;
            std::optional<Instruction> terminator;
        };

        void clear() noexcept;

        std::vector<Instruction> m_instructions;
        std::vector<InstructionPlacement> m_instructionPlacements;
        std::vector<BasicBlock> m_blocks;
        std::vector<BlockState> m_blockStates;
        std::vector<u32> m_blockIndexById;
        std::vector<TemporaryId> m_operands;
        std::vector<PhiInput> m_phiInputs;
        std::vector<u32> m_prologueCounts;
        std::vector<u32> m_bodyCounts;
        std::vector<u32> m_prologueCursors;
        std::vector<u32> m_bodyCursors;
    };
}
