#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/Syntax/ArrayTypeKind.h>
#include <Caracal/Syntax/NodeKind.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <memory>

namespace Caracal
{
    class CARACAL_API NodeBuffer
    {
    public:
        NodeBuffer() = default;
        explicit NodeBuffer(u32 capacity);

        CARACAL_DELETE_COPY_DEFAULT_MOVE(NodeBuffer)

        inline void append(NodeKind kind, u32 mainToken, u32 subtreeSize) noexcept
        {
            const auto index = m_count;
            const auto kinds = m_kinds.get();
            const auto mainTokens = m_mainTokens.get();
            const auto subtreeSizes = m_subtreeSizes.get();
            kinds[index] = kind;
            mainTokens[index] = mainToken;
            subtreeSizes[index] = subtreeSize;
            m_count = index + 1;
        }

        [[nodiscard]] u32 nodeCount() const noexcept { return m_count; }
        [[nodiscard]] u32 capacity() const noexcept { return m_capacity; }
        [[nodiscard]] NodeKind kind(u32 index) const noexcept { return m_kinds[index]; }
        [[nodiscard]] u32 mainToken(u32 index) const noexcept { return m_mainTokens[index]; }
        [[nodiscard]] u32 subtreeSize(u32 index) const noexcept { return m_subtreeSizes[index]; }

        [[nodiscard]] bool isReference(const TokenBuffer& tokens, u32 index) const noexcept;
        [[nodiscard]] ArrayTypeKind arrayKind(const TokenBuffer& tokens, u32 index) const noexcept;
        [[nodiscard]] bool isDynamicArrayLiteral(const TokenBuffer& tokens, u32 index) const noexcept;
        [[nodiscard]] bool isConstantTypeField(const TokenBuffer& tokens, u32 index) const noexcept;
        [[nodiscard]] bool methodHasTypeName(const TokenBuffer& tokens, u32 index) const noexcept;
        [[nodiscard]] bool annotationHasParentheses(const TokenBuffer& tokens, u32 index) const noexcept;

    private:
        u32 m_capacity = 0;
        u32 m_count = 0;
        std::unique_ptr<NodeKind[]> m_kinds;
        std::unique_ptr<u32[]> m_mainTokens;
        std::unique_ptr<u32[]> m_subtreeSizes;
    };

    // token index 0 is valid, so we need another value
    inline constexpr u32 InvalidTokenIndex = 0xFFFFFFFF;
}
