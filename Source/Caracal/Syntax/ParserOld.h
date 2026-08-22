#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Syntax/BlockNode.h>
#include <Caracal/Syntax/EnumFieldDeclaration.h>
#include <Caracal/Syntax/Expression.h>
#include <Caracal/Syntax/MethodNameNode.h>
#include <Caracal/Syntax/NameExpression.h>
#include <Caracal/Syntax/NumberLiteral.h>
#include <Caracal/Syntax/StringLiteral.h>
#include <Caracal/Syntax/ParametersNode.h>
#include <Caracal/Syntax/ParseTree.h>
#include <Caracal/Syntax/ReturnTypesNode.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Syntax/TypeNameNode.h>
#include <Caracal/Syntax/AnnotationNode.h>

#include <initializer_list>

namespace Caracal
{
    class CARACAL_API ParserOld
    {
    public:
        ParserOld(const TokenBuffer& tokens, DiagnosticsBag& diagnostics);

        CARACAL_DELETE_COPY_DEFAULT_MOVE(ParserOld)

        ParseTreeUPtr parse();

    private:
        enum class StatementScope
        {
            Global,
            Function,
            Method,
            Type,
            Enum
        };

        std::vector<StatementUPtr> parseStatements(StatementScope scope);
        StatementUPtr parseStatement(StatementScope scope);
        StatementUPtr parseExpressionStatement(ExpressionUPtr&& expression);
        StatementUPtr parseFunctionDefinitionStatement();
        StatementUPtr parseConstantOrVariableDeclaration(ExpressionUPtr&& leftExpression, StatementScope scope);
        StatementUPtr parseAssignmentStatement(ExpressionUPtr&& leftExpression, StatementScope scope);
        StatementUPtr parseEnumDefinitionStatement();
        std::vector<EnumFieldDeclarationUPtr> parseEnumFields();
        StatementUPtr parseTypeDefinitionStatement();
        StatementUPtr parseTypeFieldDeclaration(ExpressionUPtr&& leftExpression);
        StatementUPtr parseMethodDefinitionStatement();
        StatementUPtr parseIfStatement(StatementScope scope);
        StatementUPtr parseWhileStatement(StatementScope scope);
        StatementUPtr parseBreakStatement();
        StatementUPtr parseSkipStatement();
        StatementUPtr parseReturnStatement(StatementScope scope);
        ExpressionUPtr parseExpression(StatementScope scope, bool stopAtLineBreak = false, bool allowLineBreakBeforeDot = true);
        ExpressionUPtr parseBinaryExpression(i32 parentPrecedence, StatementScope scope, bool stopAtLineBreak, bool allowLineBreakBeforeDot);
        ExpressionUPtr parsePostfixExpression(StatementScope scope, bool allowLineBreakBeforeDot);
        ExpressionUPtr parsePrimaryExpression(StatementScope scope);
        ExpressionUPtr parseGroupingExpression(StatementScope scope);
        ExpressionUPtr parseMemberAccessExpression();
        NameExpressionUPtr parseNameExpression();
        ExpressionUPtr parseFunctionCallOrNameExpression(StatementScope scope);
        ExpressionUPtr parseFunctionCallExpression(StatementScope scope);
        TypeNameNodeUPtr parseTypeNameNode();
        TypeNameNodeUPtr parseArrayTypeNameNode(const std::optional<Token>& refToken);
        MethodNameNodeUPtr parseMethodNameNode();
        ParametersNodeUPtr parseParametersNode(StatementScope scope);
        ReturnTypesNodeUPtr parseReturnTypesNode();
        void parseArgumentList(StatementScope scope, std::vector<Argument>& arguments);
        BlockNodeUPtr parseFunctionBody();
        BlockNodeUPtr parseTypeBody();
        BlockNodeUPtr parseMethodBody();
        BlockNodeUPtr parseBlockNode(StatementScope scope);
        ParameterNodeUPtr parseParameterNode(StatementScope scope);
        NumberLiteralUPtr parseNumberLiteral();
        StringLiteralUPtr parseStringLiteral();
        ExpressionUPtr parseArrayLiteralExpression(StatementScope scope);
        void buildAnnotationNode(StatementScope scope);
        std::vector<AnnotationNodeUPtr> takeCurrentAnnotations();
        void flushDanglingAnnotations();

        Token peek(i32 offset);
        Token currentToken() { return peek(0); }
        Token nextToken() { return peek(1); }
        void advanceCurrentIndex() { m_currentIndex++; }
        bool hasLeadingLineBreak(const Token& token) const noexcept;
        Token advanceOnMatch(TokenKind kind);
        Token advanceOnMatch(TokenKind kind1, TokenKind kind2);
        Token advanceOnIdentifier();
        std::optional<Token> tryMatchKind(TokenKind kind);
        void skipUntil(std::initializer_list<TokenKind> syncKinds);
        void synchronizeToNextStatement();

        TokenBuffer m_tokens;
        DiagnosticsBag& m_diagnostics;
        i32 m_currentIndex;

        std::vector<AnnotationNodeUPtr> m_currentAnnotations;
    };
}
