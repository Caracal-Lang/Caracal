#include <Caracal/IR/Function.h>

namespace Caracal
{
    Function::Function(FunctionId id, std::string name, std::vector<IRParameter> parameters, Type returnType)
        : m_id{ id }
        , m_name{ std::move(name) }
        , m_parameters{ std::move(parameters) }
        , m_returnType{ returnType }
    {
    }

    BasicBlock* Function::tryGetBlock(BlockId id) noexcept
    {
        const auto result = m_blockIndices.find(id);
        if (result == m_blockIndices.end())
            return nullptr;

        return m_blocks[result->second].get();
    }

    const BasicBlock* Function::tryGetBlock(BlockId id) const noexcept
    {
        const auto result = m_blockIndices.find(id);
        if (result == m_blockIndices.end())
            return nullptr;

        return m_blocks[result->second].get();
    }

    void Function::addParameter(IRParameter parameter)
    {
        m_parameters.push_back(std::move(parameter));
    }

    void Function::addBlock(BasicBlock block)
    {
        const auto blockId = block.id();
        m_blockIndices.insert_or_assign(blockId, m_blocks.size());
        m_blocks.push_back(std::make_unique<BasicBlock>(std::move(block)));
    }

    bool Function::popLastBlock(BlockId expectedId) noexcept
    {
        if (m_blocks.empty() || m_blocks.back()->id() != expectedId)
        {
            return false;
        }

        m_blockIndices.erase(expectedId);
        m_blocks.pop_back();
        return true;
    }
}
