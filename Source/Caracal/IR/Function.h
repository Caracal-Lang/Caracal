#pragma once

#include <Caracal/Defines.h>
#include <Caracal/IR/BasicBlock.h>
#include <Caracal/IR/IRParameter.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Caracal
{
    class Function
    {
    public:
        Function() = default;
        Function(FunctionId id, std::string name, std::vector<IRParameter> parameters, Type returnType);
        CARACAL_DELETE_COPY_DEFAULT_MOVE(Function)

        [[nodiscard]] FunctionId id() const noexcept { return m_id; }
        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        [[nodiscard]] const std::optional<std::string>& symbolName() const noexcept { return m_symbolName; }
        [[nodiscard]] const std::vector<IRParameter>& parameters() const noexcept { return m_parameters; }
        [[nodiscard]] Type returnType() const noexcept { return m_returnType; }
        [[nodiscard]] const std::vector<BasicBlockUPtr>& blocks() const noexcept { return m_blocks; }
        [[nodiscard]] std::vector<BasicBlockUPtr>& blocks() noexcept { return m_blocks; }
        [[nodiscard]] bool hasBlocks() const noexcept { return !m_blocks.empty(); }
        [[nodiscard]] const BasicBlock& firstBlock() const noexcept { return *m_blocks.front(); }
        [[nodiscard]] BasicBlock* tryGetBlock(BlockId id) noexcept;
        [[nodiscard]] const BasicBlock* tryGetBlock(BlockId id) const noexcept;
        [[nodiscard]] TemporaryId temporaryCount() const noexcept { return m_temporaryCount; }

        void addParameter(IRParameter parameter);
        void addBlock(BasicBlock block);
        void setSymbolName(std::optional<std::string> symbolName) noexcept { m_symbolName = std::move(symbolName); }
        void setTemporaryCount(TemporaryId temporaryCount) noexcept { m_temporaryCount = temporaryCount; }
        [[nodiscard]] bool popLastBlock(BlockId expectedId) noexcept;

    private:
        FunctionId m_id{ -1 };
        std::string m_name;
        std::optional<std::string> m_symbolName;
        std::vector<IRParameter> m_parameters;
        Type m_returnType{ Type::Void() };
        std::vector<BasicBlockUPtr> m_blocks;
        std::unordered_map<BlockId, size_t> m_blockIndices;
        TemporaryId m_temporaryCount{ 0 };
    };
}
