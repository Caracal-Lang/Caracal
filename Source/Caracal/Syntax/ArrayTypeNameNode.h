#pragma once

#include <Caracal/API.h>
#include <Caracal/Syntax/ArrayTypeKind.h>
#include <Caracal/Syntax/NumberLiteral.h>
#include <Caracal/Syntax/TypeNameNode.h>

#include <optional>

namespace Caracal
{
    class CARACAL_API ArrayTypeNameNode final : public TypeNameNode
    {
    public:
        ArrayTypeNameNode(
            const std::optional<Token>& refToken,
            const Token& openBracketToken,
            TypeNameNodeUPtr elementType,
            ArrayTypeKind arrayKind,
            const std::optional<Token>& semicolonToken,
            NumberLiteralUPtr lengthLiteral,
            const std::optional<Token>& underscoreToken,
            const Token& closeBracketToken);

        CARACAL_DELETE_COPY_DEFAULT_MOVE(ArrayTypeNameNode)

        [[nodiscard]] const Token& openBracketToken() const noexcept { return m_openBracketToken; }
        [[nodiscard]] const TypeNameNodeUPtr& elementType() const noexcept { return m_elementType; }
        [[nodiscard]] ArrayTypeKind arrayKind() const noexcept { return m_arrayKind; }
        [[nodiscard]] const std::optional<Token>& semicolonToken() const noexcept { return m_semicolonToken; }
        [[nodiscard]] const NumberLiteralUPtr& lengthLiteral() const noexcept { return m_lengthLiteral; }
        [[nodiscard]] const std::optional<Token>& underscoreToken() const noexcept { return m_underscoreToken; }
        [[nodiscard]] const Token& closeBracketToken() const noexcept { return m_closeBracketToken; }
        [[nodiscard]] SourceLocation sourceLocation(const TokenBuffer& tokens) const override;

    private:
        Token m_openBracketToken;
        TypeNameNodeUPtr m_elementType;
        ArrayTypeKind m_arrayKind;
        std::optional<Token> m_semicolonToken;
        NumberLiteralUPtr m_lengthLiteral;
        std::optional<Token> m_underscoreToken;
        Token m_closeBracketToken;
    };

    using ArrayTypeNameNodeUPtr = std::unique_ptr<ArrayTypeNameNode>;
}
