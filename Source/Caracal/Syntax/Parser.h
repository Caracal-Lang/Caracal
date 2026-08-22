#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <initializer_list>
#include <optional>
#include <vector>

namespace Caracal
{
    // fills the nodebuffer with indexes to the tokenbuffer and subtree sizes in the postorder
    class CARACAL_API Parser
    {
    public:
        Parser(const TokenBuffer& tokens, DiagnosticsBag& diagnostics);

        CARACAL_DELETE_COPY_DEFAULT_MOVE(Parser)

        [[nodiscard]] NodeBuffer parse();

    private:
        enum class StatementScope
        {
            Global,
            Function,
            Method,
            Type,
            Enum
        };

        void parseStatements(StatementScope scope);
        void parseStatement(StatementScope scope);
        void parseFunctionDefinitionStatement();
        void parseConstantOrVariableDeclaration(u32 subtreeStart, StatementScope scope);
        void parseAssignmentStatement(u32 subtreeStart, StatementScope scope);
        void parseEnumDefinitionStatement();
        void parseEnumFields();
        void parseTypeDefinitionStatement();
        void parseTypeFieldDeclaration(u32 subtreeStart);
        void parseMethodDefinitionStatement();
        void parseIfStatement(StatementScope scope);
        void parseWhileStatement(StatementScope scope);
        void parseBreakStatement();
        void parseSkipStatement();
        void parseReturnStatement(StatementScope scope);
        void parseExpression(StatementScope scope, bool stopAtLineBreak = false, bool allowLineBreakBeforeDot = true);
        void parseBinaryExpression(i32 parentPrecedence, StatementScope scope, bool stopAtLineBreak, bool allowLineBreakBeforeDot);
        void parsePostfixExpression(StatementScope scope, bool allowLineBreakBeforeDot);
        void parsePrimaryExpression(StatementScope scope);
        void parseGroupingExpression(StatementScope scope);
        void parseMemberAccessExpression();
        void parseNameExpression();
        void parseFunctionCallOrNameExpression(StatementScope scope);
        void parseFunctionCallExpression(StatementScope scope);
        void parseTypeNameNode();
        void parseArrayTypeNameNode();
        [[nodiscard]] u32 parseMethodNameNode();
        void parseParametersNode(StatementScope scope);
        void parseReturnTypesNode();
        void parseArgumentList(StatementScope scope);
        void parseNumberLiteral();
        void parseStringLiteral();
        void parseArrayLiteralExpression(StatementScope scope);
        void buildAnnotationNode(StatementScope scope);
        void flushDanglingAnnotations();

        void append(NodeKind kind, u32 mainToken, u32 subtreeStart);
        void appendNode(NodeKind kind, u32 mainToken);
        [[nodiscard]] NodeKind lastKind() const noexcept;

        [[nodiscard]] u32 tokenIndexOf(const Token& token) const noexcept
        {
            return static_cast<u32>(token.index);
        }

        [[nodiscard]] u32 tokenIndexOf(const std::optional<Token>& token) const noexcept
        {
            if (!token.has_value())
            {
                return InvalidTokenIndex;
            }

            return tokenIndexOf(token.value());
        }
        [[nodiscard]] SourceLocation annotationLocation(u32 nameToken) const;
        [[nodiscard]] SourceLocation typeNameLocation(u32 typeNameIndex) const;
        [[nodiscard]] SourceLocation tokenRangeLocation(i32 firstToken, i32 lastToken) const;

        Token peek(i32 offset);
        Token currentToken() { return peek(0); }
        Token nextToken() { return peek(1); }
        void advanceCurrentIndex() { m_currentIndex++; }
        [[nodiscard]] bool hasLeadingLineBreak(const Token& token) const noexcept;

        // match helers get force inlined for performance
        CARACAL_FORCE_INLINE Token advanceOnMatch(TokenKind kind)
        {
            auto current = currentToken();
            if (current.kind == kind)
            {
                advanceCurrentIndex();
                return current;
            }

            return reportExpectedToken(kind, current);
        }

        CARACAL_FORCE_INLINE Token advanceOnMatch(TokenKind kind1, TokenKind kind2)
        {
            auto current = currentToken();
            if (current.kind == kind1 || current.kind == kind2)
            {
                advanceCurrentIndex();
                return current;
            }

            return reportExpectedToken(kind1, kind2, current);
        }

        CARACAL_FORCE_INLINE Token advanceOnIdentifier()
        {
            auto current = currentToken();
            if (isIdentifierKind(current.kind))
            {
                advanceCurrentIndex();
                return current;
            }

            return reportExpectedToken(TokenKind::Identifier, current);
        }

        CARACAL_NO_INLINE Token reportExpectedToken(TokenKind kind, const Token& current);
        CARACAL_NO_INLINE Token reportExpectedToken(TokenKind kind1, TokenKind kind2, const Token& current);
        [[nodiscard]] std::optional<Token> tryMatchKind(TokenKind kind);
        void skipUntil(std::initializer_list<TokenKind> syncKinds);
        void synchronizeToNextStatement();

        const TokenBuffer& m_tokens;
        DiagnosticsBag& m_diagnostics;
        i32 m_currentIndex;
        NodeBuffer m_nodeBuffer;
        std::vector<u32> m_pendingAnnotations;
    };
}
