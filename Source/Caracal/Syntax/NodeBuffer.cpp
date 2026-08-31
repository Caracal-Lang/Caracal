#include <Caracal/Syntax/NodeBuffer.h>

namespace Caracal
{
    NodeBuffer::NodeBuffer(u32 capacity)
        : m_capacity{ capacity }
        , m_kinds{ std::make_unique_for_overwrite<NodeKind[]>(capacity) }
        , m_mainTokens{ std::make_unique_for_overwrite<u32[]>(capacity) }
        , m_subtreeSizes{ std::make_unique_for_overwrite<u32[]>(capacity) }
    {
    }

    bool NodeBuffer::isReference(const TokenBuffer& tokens, u32 index) const noexcept
    {
        const auto mainTokenIndex = m_mainTokens[index];
        if (mainTokenIndex == 0)
        {
            return false;
        }

        return tokens.getToken(static_cast<i32>(mainTokenIndex - 1)).kind == TokenKind::RefKeyword;
    }

    ArrayTypeKind NodeBuffer::arrayKind(const TokenBuffer& tokens, u32 index) const noexcept
    {
        // a fixed array's last child is its length literal
        // the element type can never be a number literal
        if (kind(index - 1) == NodeKind::NumberLiteral)
        {
            return ArrayTypeKind::Fixed;
        }

        // the dynamic marker is an underscore between the element type and 
        // the close bracket, search the buffer from the open bracket
        auto tokenIndex = m_mainTokens[index];
        auto depth = 0;
        while (true)
        {
            const auto token = tokens.getToken(static_cast<i32>(tokenIndex));
            if (token.kind == TokenKind::OpenBracket)
            {
                depth++;
            }
            else if (token.kind == TokenKind::Underscore && depth == 1)
            {
                return ArrayTypeKind::Dynamic;
            }
            else if (token.kind == TokenKind::CloseBracket)
            {
                depth--;
                if (depth == 0)
                {
                    return ArrayTypeKind::Slice;
                }
            }
            else if (token.kind == TokenKind::EndOfFile)
            {
                return ArrayTypeKind::Slice;
            }
            tokenIndex++;
        }
    }

    bool NodeBuffer::isDynamicArrayLiteral(const TokenBuffer& tokens, u32 index) const noexcept
    {
        // a dynamic array literal ends with an ellipsis before its close bracket,
        // search from the open bracket and stay depth aware for nested literals
        auto tokenIndex = m_mainTokens[index];
        auto depth = 0;
        while (true)
        {
            const auto token = tokens.getToken(static_cast<i32>(tokenIndex));
            if (token.kind == TokenKind::OpenBracket)
            {
                depth++;
            }
            else if (token.kind == TokenKind::CloseBracket)
            {
                depth--;
                if (depth == 0)
                {
                    return false;
                }
            }
            else if (token.kind == TokenKind::Ellipsis && depth == 1)
            {
                return true;
            }
            else if (token.kind == TokenKind::EndOfFile)
            {
                return false;
            }
            tokenIndex++;
        }
    }

    bool NodeBuffer::isConstantTypeField(const TokenBuffer& tokens, u32 index) const noexcept
    {
        // a field is const when the second token is a colon
        auto tokenIndex = m_mainTokens[index] + 1;
        while (true)
        {
            const auto token = tokens.getToken(static_cast<i32>(tokenIndex));
            if (token.kind == TokenKind::Colon)
            {
                return true;
            }
            if (token.kind == TokenKind::Equal || token.kind == TokenKind::EndOfFile)
            {
                return false;
            }
            tokenIndex++;
        }
    }

    bool NodeBuffer::methodHasTypeName(const TokenBuffer& tokens, u32 index) const noexcept
    {
        const auto mainTokenIndex = m_mainTokens[index];
        if (mainTokenIndex == 0)
        {
            return false;
        }

        return tokens.getToken(static_cast<i32>(mainTokenIndex - 1)).kind == TokenKind::Dot;
    }

    // the type name token sits two indeces before the method name token
    u32 NodeBuffer::methodTypeNameToken(const TokenBuffer& tokens, u32 methodNameIndex) const noexcept
    {
        const auto mainTokenIndex = m_mainTokens[methodNameIndex];
        if (mainTokenIndex < 2 || !methodHasTypeName(tokens, methodNameIndex))
        {
            return InvalidTokenIndex;
        }

        return mainTokenIndex - 2;
    }

    bool NodeBuffer::methodNameIsNew(const TokenBuffer& tokens, u32 methodNameIndex) const noexcept
    {
        return tokens.getToken(static_cast<i32>(m_mainTokens[methodNameIndex])).kind == TokenKind::NewKeyword;
    }

    MethodModifier NodeBuffer::methodModifier(const TokenBuffer& tokens, u32 methodNameIndex) const noexcept
    {
        if (methodHasTypeName(tokens, methodNameIndex))
        {
            return MethodModifier::Static;
        }

        const auto nameKind = tokens.getToken(static_cast<i32>(m_mainTokens[methodNameIndex])).kind;
        if (nameKind == TokenKind::PrivateIdentifier)
        {
            return MethodModifier::Private;
        }

        return MethodModifier::Public;
    }

    bool NodeBuffer::isMemberAccessBinary(const TokenBuffer& tokens, u32 index) const noexcept
    {
        if (kind(index) != NodeKind::BinaryExpression)
        {
            return false;
        }

        return tokens.getToken(static_cast<i32>(m_mainTokens[index])).kind == TokenKind::Dot;
    }

    bool NodeBuffer::annotationHasParentheses(const TokenBuffer& tokens, u32 index) const noexcept
    {
        return tokens.getToken(static_cast<i32>(m_mainTokens[index] + 1)).kind == TokenKind::OpenParenthesis;
    }
}
