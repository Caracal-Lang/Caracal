#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Text/SourceLocation.h>

#include <optional>
#include <span>

namespace Caracal
{
    namespace NodeSpans
    {
        [[nodiscard]] CARACAL_API u32 findMatchingCloseToken(const TokenBuffer& tokens, u32 openTokenIndex, TokenKind openKind, TokenKind closeKind) noexcept;
        [[nodiscard]] CARACAL_API u32 findCloserToken(const TokenBuffer& tokens, u32 from, TokenKind closer) noexcept;
        [[nodiscard]] CARACAL_API u32 lastTokenOf(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index);
        [[nodiscard]] CARACAL_API SourceLocation typeNameSpan(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index);
        [[nodiscard]] CARACAL_API SourceLocation parameterSpan(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 parameterIndex);
        [[nodiscard]] CARACAL_API SourceLocation parametersNodeSpan(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 parametersIndex);
        [[nodiscard]] CARACAL_API SourceLocation expressionSourceLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index);
        [[nodiscard]] CARACAL_API SourceLocation expressionLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index);
        [[nodiscard]] CARACAL_API SourceLocation parameterLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 parameterIndex);
        [[nodiscard]] CARACAL_API std::optional<SourceLocation> deadStatementLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index);
        [[nodiscard]] CARACAL_API SourceLocation callArgumentsLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 callIndex, std::span<const u32> argumentNodes);
        [[nodiscard]] CARACAL_API SourceLocation annotationArgumentsLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index, const SourceLocation& annotationLocation);
    }
}
