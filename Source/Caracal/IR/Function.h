#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/IR/BasicBlock.h>
#include <Caracal/IR/IRParameter.h>
#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/PhiInput.h>
#include <Caracal/IR/SourceLine.h>

#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Caracal
{
    class CARACAL_API Function
    {
    public:
        Function() = default;
        Function(FunctionId id, std::string name, std::vector<IRParameter> parameters, Type returnType);
        CARACAL_DELETE_COPY_DEFAULT_MOVE(Function)

        [[nodiscard]] FunctionId id() const noexcept { return m_id; }
        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        // the name the function is emitted under when it differs from name, like main for caracalMain
        [[nodiscard]] const std::optional<std::string>& symbolName() const noexcept { return m_symbolName; }
        [[nodiscard]] const std::vector<IRParameter>& parameters() const noexcept { return m_parameters; }
        [[nodiscard]] Type returnType() const noexcept { return m_returnType; }
        [[nodiscard]] i32 temporaryCount() const noexcept { return m_temporaryCount; }
        [[nodiscard]] SourceLine sourceLine() const noexcept { return m_sourceLine; }
        void setSymbolName(std::string symbolName) { m_symbolName = std::move(symbolName); }
        void setTemporaryCount(i32 temporaryCount) noexcept { m_temporaryCount = temporaryCount; }
        void setSourceLine(SourceLine sourceLine) noexcept { m_sourceLine = sourceLine; }
        void setBody(std::vector<Instruction> instructions, std::vector<BasicBlock> blocks, std::span<const TemporaryId> operands, std::span<const PhiInput> phiInputs);
        
        // dead instructions turn into Nop without a result
        void removeInstruction(u32 instructionIndex) noexcept
        {
            m_instructions[instructionIndex].kind = InstructionKind::Nop;
            m_instructions[instructionIndex].resultId = -1;
        }

        [[nodiscard]] u32 instructionCount() const noexcept { return static_cast<u32>(m_instructions.size()); }
        [[nodiscard]] const Instruction& instructionAt(u32 instructionIndex) const noexcept { return m_instructions[instructionIndex]; }
        [[nodiscard]] InstructionKind kindAt(u32 instructionIndex) const noexcept { return m_instructions[instructionIndex].kind; }
        [[nodiscard]] TemporaryId resultIdAt(u32 instructionIndex) const noexcept { return m_instructions[instructionIndex].resultId; }
        [[nodiscard]] i32 aAt(u32 instructionIndex) const noexcept { return m_instructions[instructionIndex].a; }
        [[nodiscard]] i32 bAt(u32 instructionIndex) const noexcept { return m_instructions[instructionIndex].b; }
        [[nodiscard]] i32 cAt(u32 instructionIndex) const noexcept { return m_instructions[instructionIndex].c; }
        [[nodiscard]] const std::vector<BasicBlock>& blocks() const noexcept { return m_blocks; }
        [[nodiscard]] bool hasBlocks() const noexcept { return !m_blocks.empty(); }
        [[nodiscard]] std::span<const TemporaryId> operands(u32 start, u32 count) const noexcept;
        [[nodiscard]] std::span<const PhiInput> phiInputs(const Instruction& phi) const noexcept;
        [[nodiscard]] std::span<const PhiInput> phiInputsAt(u32 instructionIndex) const noexcept;

    private:
        FunctionId m_id{ -1 };
        std::string m_name;
        std::vector<IRParameter> m_parameters;
        Type m_returnType{ Type::Void() };
        std::optional<std::string> m_symbolName;
        i32 m_temporaryCount = 0;
        SourceLine m_sourceLine;
        std::vector<Instruction> m_instructions;
        std::vector<BasicBlock> m_blocks;
        std::vector<TemporaryId> m_operands;
        std::vector<PhiInput> m_phiInputs;
    };
}
