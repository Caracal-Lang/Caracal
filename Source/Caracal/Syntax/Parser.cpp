#include <Caracal/Syntax/Parser.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

namespace Caracal
{
    Parser::Parser(const TokenBuffer& tokens, DiagnosticsBag& diagnostics)
        : m_tokens{ tokens }
        , m_diagnostics{ diagnostics }
        , m_currentIndex{ 0 }
        // double the capacity for safe error recovery bounds
        , m_nodeBuffer{ std::max(2 * static_cast<u32>(tokens.size()), 16U) }
    {
    }

    NodeBuffer Parser::parse()
    {
        parseStatements(StatementScope::Global);

        flushDanglingAnnotations();

        if (m_currentIndex < m_tokens.size() - 1)
        {
            const auto& location = m_tokens.getSourceLocation(m_tokens.getToken(m_currentIndex));
            m_diagnostics.addExtraTokensRemainingError(m_tokens.source(), location);
        }

        return std::move(m_nodeBuffer);
    }

    void Parser::append(NodeKind kind, u32 mainToken, u32 subtreeStart)
    {
        if (m_nodeBuffer.nodeCount() >= m_nodeBuffer.capacity())
        {
            std::cerr << "Parser: node capacity exceeded\n";
            std::abort();
        }

        m_nodeBuffer.append(kind, mainToken, m_nodeBuffer.nodeCount() - subtreeStart + 1);
    }

    void Parser::appendNode(NodeKind kind, u32 mainToken)
    {
        append(kind, mainToken, m_nodeBuffer.nodeCount());
    }

    NodeKind Parser::lastKind() const noexcept
    {
        return m_nodeBuffer.kind(m_nodeBuffer.nodeCount() - 1);
    }

    SourceLocation Parser::tokenRangeLocation(i32 firstToken, i32 lastToken) const
    {
        const auto& firstLocation = m_tokens.getSourceLocation(m_tokens.getToken(firstToken));
        const auto& lastLocation = m_tokens.getSourceLocation(m_tokens.getToken(lastToken));
        return SourceLocation{ firstLocation.startIndex, lastLocation.endIndex };
    }

    SourceLocation Parser::annotationLocation(u32 nameToken) const
    {
        // the annotation location includes hash and name
        return tokenRangeLocation(static_cast<i32>(nameToken) - 1, static_cast<i32>(nameToken));
    }

    SourceLocation Parser::typeNameLocation(u32 typeNameIndex) const
    {
        // mirrors TypeNameNode/ArrayTypeNameNode::sourceLocation: an optional leading
        // ref widens the start, an array type ends at its matching close bracket
        const auto mainToken = m_nodeBuffer.mainToken(typeNameIndex);
        auto firstToken = static_cast<i32>(mainToken);
        if (firstToken > 0 && m_tokens.getToken(firstToken - 1).kind == TokenKind::RefKeyword)
        {
            firstToken--;
        }

        if (m_nodeBuffer.kind(typeNameIndex) != NodeKind::ArrayTypeNameNode)
        {
            return tokenRangeLocation(firstToken, static_cast<i32>(mainToken));
        }

        auto lastToken = static_cast<i32>(mainToken);
        auto depth = 0;
        while (true)
        {
            const auto token = m_tokens.getToken(lastToken);
            if (token.kind == TokenKind::OpenBracket)
            {
                depth++;
            }
            else if (token.kind == TokenKind::CloseBracket)
            {
                depth--;
                if (depth == 0)
                {
                    break;
                }
            }
            else if (token.kind == TokenKind::EndOfFile)
            {
                break;
            }
            lastToken++;
        }

        return tokenRangeLocation(firstToken, lastToken);
    }

    void Parser::parseStatements(StatementScope scope)
    {
        auto current = currentToken();
        while (true)
        {
            switch (current.kind)
            {
                case TokenKind::Hash:
                {
                    if (scope == StatementScope::Global || scope == StatementScope::Type)
                    {
                        buildAnnotationNode(scope);
                        break;
                    }

                    // annotations arent allowed here but we want to continue parsing to catch other diagnostics
                    buildAnnotationNode(scope);
                    m_diagnostics.addAnnotationNotAllowedHereError(m_tokens.source(), annotationLocation(m_pendingAnnotations.back()));
                    m_pendingAnnotations.pop_back();
                    break;
                }
                case TokenKind::CloseBrace:
                {
                    if (scope == StatementScope::Global)
                    {
                        const auto& location = m_tokens.getSourceLocation(current);
                        m_diagnostics.addUnexpectedTopLevelTokenError(m_tokens.source(), location, current.kind);

                        advanceCurrentIndex();
                        break;
                    }
                    [[fallthrough]];
                }
                case TokenKind::EndOfFile:
                {
                    return;
                }
                default:
                {
                    parseStatement(scope);
                    break;
                }
            }

            current = currentToken();
        }
    }

    void Parser::parseStatement(StatementScope scope)
    {
        const auto current = currentToken();
        switch (current.kind)
        {
            case TokenKind::DefKeyword:
            {
                if (scope == StatementScope::Global)
                {
                    parseFunctionDefinitionStatement();
                    return;
                }
                else if (scope == StatementScope::Type)
                {
                    parseMethodDefinitionStatement();
                    return;
                }
                break;
            }
            case TokenKind::EnumKeyword:
            {
                if (scope == StatementScope::Global)
                {
                    parseEnumDefinitionStatement();
                    return;
                }
                break;
            }
            case TokenKind::TypeKeyword:
            {
                if (scope == StatementScope::Global)
                {
                    parseTypeDefinitionStatement();
                    return;
                }
                break;
            }
            case TokenKind::IfKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    parseIfStatement(scope);
                    return;
                }
                break;
            }
            case TokenKind::WhileKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    parseWhileStatement(scope);
                    return;
                }
                break;
            }
            case TokenKind::BreakKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    parseBreakStatement();
                    return;
                }
                break;
            }
            case TokenKind::SkipKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    parseSkipStatement();
                    return;
                }
                break;
            }
            case TokenKind::ReturnKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    parseReturnStatement(scope);
                    return;
                }
                break;
            }
            case TokenKind::OpenBrace:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    const auto subtreeStart = m_nodeBuffer.nodeCount();
                    auto openBracket = advanceOnMatch(TokenKind::OpenBrace);
                    parseStatements(scope);
                    static_cast<void>(advanceOnMatch(TokenKind::CloseBrace));
                    append(NodeKind::BlockNode, tokenIndexOf(openBracket), subtreeStart);
                    return;
                }
                break;
            }
            case TokenKind::Dot:
            {
                if (scope != StatementScope::Method)
                {
                    break;
                }
                [[fallthrough]];
            }
            case TokenKind::Underscore:
            case TokenKind::Identifier:
            case TokenKind::PrivateIdentifier:
            case TokenKind::InitKeyword:
            case TokenKind::NewKeyword:
            {
                const auto subtreeStart = m_nodeBuffer.nodeCount();

                // directly checking for underscore increased parsing performance
                const auto next = nextToken();
                if (next.kind == TokenKind::Colon || next.kind == TokenKind::Equal)
                {
                    advanceCurrentIndex();
                    if (current.kind == TokenKind::Underscore)
                    {
                        appendNode(NodeKind::DiscardLiteral, tokenIndexOf(current));
                    }
                    else
                    {
                        appendNode(NodeKind::NameExpression, tokenIndexOf(current));
                    }
                }
                else
                {
                    parseExpression(scope);
                }

                const auto expressionKind = lastKind();
                if (currentToken().kind == TokenKind::Colon)
                {
                    if (scope == StatementScope::Global || scope == StatementScope::Function || scope == StatementScope::Method)
                    {
                        parseConstantOrVariableDeclaration(subtreeStart, scope);
                        return;
                    }
                    else if (scope == StatementScope::Type && expressionKind == NodeKind::NameExpression)
                    {
                        parseTypeFieldDeclaration(subtreeStart);
                        return;
                    }
                    break;
                }
                if ((expressionKind == NodeKind::NameExpression || expressionKind == NodeKind::BinaryExpression || expressionKind == NodeKind::MemberAccessExpression || expressionKind == NodeKind::DiscardLiteral) && currentToken().kind == TokenKind::Equal)
                {
                    if (scope == StatementScope::Function || scope == StatementScope::Method)
                    {
                        parseAssignmentStatement(subtreeStart, scope);
                        return;
                    }
                    break;
                }
                if (expressionKind == NodeKind::FunctionCallExpression || expressionKind == NodeKind::BinaryExpression || expressionKind == NodeKind::MemberAccessExpression)
                {
                    if (scope == StatementScope::Function || scope == StatementScope::Method)
                    {
                        auto semicolon = advanceOnMatch(TokenKind::Semicolon);
                        append(NodeKind::ExpressionStatement, tokenIndexOf(semicolon), subtreeStart);
                        return;
                    }
                    break;
                }

                break;
            }
            default:
            {
                break;
            }
        }

        flushDanglingAnnotations();

        const auto& location = m_tokens.getSourceLocation(current);
        if (scope == StatementScope::Global)
        {
            m_diagnostics.addUnexpectedTopLevelTokenError(m_tokens.source(), location, current.kind);
        }
        else
        {
            m_diagnostics.addUnexpectedStatementTokenError(m_tokens.source(), location, current.kind);
        }

        const auto subtreeStart = m_nodeBuffer.nodeCount();
        appendNode(NodeKind::Error, tokenIndexOf(current));

        if (current.kind != TokenKind::CloseBrace && current.kind != TokenKind::EndOfFile)
        {
            advanceCurrentIndex();
            synchronizeToNextStatement();
        }

        append(NodeKind::ExpressionStatement, tokenIndexOf(current), subtreeStart);
    }

    void Parser::parseFunctionDefinitionStatement()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        static_cast<void>(advanceOnMatch(TokenKind::DefKeyword));
        auto nameToken = advanceOnIdentifier();
        if (nameToken.kind == TokenKind::PrivateIdentifier)
        {
            m_diagnostics.addPrivateFreeFunctionError(
                m_tokens.source(),
                m_tokens.getSourceLocation(nameToken));
        }

        parseParametersNode(StatementScope::Function);
        parseReturnTypesNode();

        {
            const auto bodyStart = m_nodeBuffer.nodeCount();
            auto openBracket = advanceOnMatch(TokenKind::OpenBrace);
            parseStatements(StatementScope::Function);
            static_cast<void>(advanceOnMatch(TokenKind::CloseBrace));
            append(NodeKind::BlockNode, tokenIndexOf(openBracket), bodyStart);
        }

        m_pendingAnnotations.clear();
        append(NodeKind::FunctionDefinitionStatement, tokenIndexOf(nameToken), subtreeStart);
    }

    void Parser::parseConstantOrVariableDeclaration(u32 subtreeStart, StatementScope scope)
    {
        const auto leftExpression = m_nodeBuffer.nodeCount() - 1;
        auto firstColon = advanceOnMatch(TokenKind::Colon);

        auto explicitType = InvalidTokenIndex;
        auto currentTokenKind = currentToken().kind;
        if (currentTokenKind != TokenKind::Colon && currentTokenKind != TokenKind::Equal)
        {
            parseTypeNameNode();
            explicitType = m_nodeBuffer.nodeCount() - 1;
        }

        auto secondToken = advanceOnMatch(TokenKind::Colon, TokenKind::Equal);
        auto afterSecondToken = currentToken();
        auto isInitDeclaration = afterSecondToken.kind == TokenKind::InitKeyword;
        if (secondToken.kind == TokenKind::Colon && isInitDeclaration)
        {
            static_cast<void>(advanceOnMatch(TokenKind::InitKeyword));
            parseTypeNameNode();
            const auto initType = m_nodeBuffer.nodeCount() - 1;
            static_cast<void>(advanceOnMatch(TokenKind::Semicolon));

            if (explicitType != InvalidTokenIndex)
            {
                auto constantName = std::string("name");
                if (m_nodeBuffer.kind(leftExpression) == NodeKind::NameExpression)
                {
                    const auto nameTokenIndex = static_cast<i32>(m_nodeBuffer.mainToken(leftExpression));
                    const auto nameToken = m_tokens.getToken(nameTokenIndex);
                    constantName = std::string(m_tokens.getLexeme(nameToken));
                }

                const auto initTypeNameTokenIndex = static_cast<i32>(m_nodeBuffer.mainToken(initType));
                const auto initTypeNameToken = m_tokens.getToken(initTypeNameTokenIndex);
                m_diagnostics.addExplicitTypeOnInitConstantError(
                    m_tokens.source(),
                    typeNameLocation(explicitType),
                    constantName,
                    std::string(m_tokens.getLexeme(initTypeNameToken)));
            }

            m_pendingAnnotations.clear();
            append(NodeKind::InitConstantDeclaration, tokenIndexOf(firstColon), subtreeStart);
            return;
        }

        parseExpression(scope);
        static_cast<void>(advanceOnMatch(TokenKind::Semicolon));

        m_pendingAnnotations.clear();
        if (secondToken.kind == TokenKind::Colon)
        {
            append(NodeKind::ConstantDeclaration, tokenIndexOf(firstColon), subtreeStart);
        }
        else
        {
            append(NodeKind::VariableDeclaration, tokenIndexOf(firstColon), subtreeStart);
        }
    }

    void Parser::parseAssignmentStatement(u32 subtreeStart, StatementScope scope)
    {
        auto equal = advanceOnMatch(TokenKind::Equal);
        parseExpression(scope);
        static_cast<void>(advanceOnMatch(TokenKind::Semicolon));

        append(NodeKind::AssignmentStatement, tokenIndexOf(equal), subtreeStart);
    }

    void Parser::parseEnumDefinitionStatement()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        static_cast<void>(advanceOnMatch(TokenKind::EnumKeyword));
        auto nameToken = advanceOnIdentifier();

        if (currentToken().kind == TokenKind::Colon)
        {
            static_cast<void>(advanceOnMatch(TokenKind::Colon));
            parseTypeNameNode();
        }

        static_cast<void>(advanceOnMatch(TokenKind::OpenBrace));
        parseEnumFields();
        static_cast<void>(advanceOnMatch(TokenKind::CloseBrace));

        m_pendingAnnotations.clear();
        append(NodeKind::EnumDefinitionStatement, tokenIndexOf(nameToken), subtreeStart);
    }

    void Parser::parseEnumFields()
    {
        auto current = currentToken();
        while (current.kind != TokenKind::CloseBrace && current.kind != TokenKind::EndOfFile)
        {
            if (isIdentifierKind(current.kind))
            {
                const auto subtreeStart = m_nodeBuffer.nodeCount();
                auto nameToken = advanceOnIdentifier();
                if (currentToken().kind == TokenKind::Colon && nextToken().kind == TokenKind::Colon)
                {
                    static_cast<void>(advanceOnMatch(TokenKind::Colon));
                    static_cast<void>(advanceOnMatch(TokenKind::Colon));
                    parseExpression(StatementScope::Enum);
                }
                append(NodeKind::EnumFieldDeclaration, tokenIndexOf(nameToken), subtreeStart);
            }
            else
            {
                const auto& location = m_tokens.getSourceLocation(current);
                m_diagnostics.addExpectedEnumFieldError(m_tokens.source(), location, current.kind);
                advanceCurrentIndex();
            }
            current = currentToken();
        }
    }

    void Parser::parseTypeDefinitionStatement()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        static_cast<void>(advanceOnMatch(TokenKind::TypeKeyword));
        auto nameToken = advanceOnIdentifier();
        m_pendingAnnotations.clear();

        if (currentToken().kind == TokenKind::OpenParenthesis)
        {
            parseParametersNode(StatementScope::Type);
        }

        {
            const auto bodyStart = m_nodeBuffer.nodeCount();
            auto openBracket = advanceOnMatch(TokenKind::OpenBrace);
            parseStatements(StatementScope::Type);
            static_cast<void>(advanceOnMatch(TokenKind::CloseBrace));
            append(NodeKind::BlockNode, tokenIndexOf(openBracket), bodyStart);
        }

        append(NodeKind::TypeDefinitionStatement, tokenIndexOf(nameToken), subtreeStart);
    }

    void Parser::parseTypeFieldDeclaration(u32 subtreeStart)
    {
        const auto leftExpression = m_nodeBuffer.nodeCount() - 1;
        auto firstColon = advanceOnMatch(TokenKind::Colon);

        auto explicitType = InvalidTokenIndex;
        auto currentTokenKind = currentToken().kind;
        if (currentTokenKind != TokenKind::Colon && currentTokenKind != TokenKind::Equal)
        {
            parseTypeNameNode();
            explicitType = m_nodeBuffer.nodeCount() - 1;
        }

        const auto current = currentToken();
        if (current.kind != TokenKind::Colon && current.kind != TokenKind::Equal)
        {
            const auto fieldNameTokenIndex = static_cast<i32>(m_nodeBuffer.mainToken(leftExpression));
            const auto fieldNameToken = m_tokens.getToken(fieldNameTokenIndex);
            auto fieldLocation = m_tokens.getSourceLocation(fieldNameToken);
            if (explicitType != InvalidTokenIndex)
            {
                fieldLocation.endIndex = typeNameLocation(explicitType).endIndex;
            }
            else
            {
                fieldLocation.endIndex = m_tokens.getSourceLocation(firstColon).endIndex;
            }

            const auto fieldName = m_tokens.getLexeme(fieldNameToken);
            m_diagnostics.addUninitializedTypeFieldError(m_tokens.source(), fieldLocation, std::string(fieldName));

            appendNode(NodeKind::Error, tokenIndexOf(current));
            append(NodeKind::TypeFieldDeclaration, tokenIndexOf(firstColon), subtreeStart);
            return;
        }

        static_cast<void>(advanceOnMatch(TokenKind::Colon, TokenKind::Equal));
        parseExpression(StatementScope::Type);

        append(NodeKind::TypeFieldDeclaration, tokenIndexOf(firstColon), subtreeStart);
    }

    void Parser::parseMethodDefinitionStatement()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto keyword = advanceOnMatch(TokenKind::DefKeyword);
        const auto methodNameIndex = parseMethodNameNode();
        parseParametersNode(StatementScope::Method);
        parseReturnTypesNode();

        {
            const auto bodyStart = m_nodeBuffer.nodeCount();
            auto openBracket = advanceOnMatch(TokenKind::OpenBrace);
            parseStatements(StatementScope::Method);
            static_cast<void>(advanceOnMatch(TokenKind::CloseBrace));
            append(NodeKind::BlockNode, tokenIndexOf(openBracket), bodyStart);
        }

        const auto methodNameTokenIndex = static_cast<i32>(m_nodeBuffer.mainToken(methodNameIndex));
        const auto methodNameToken = m_tokens.getToken(methodNameTokenIndex);
        const auto methodName = m_tokens.getLexeme(methodNameToken);
        const auto hasTypeName = m_nodeBuffer.methodHasTypeName(m_tokens, methodNameIndex);
        const auto isPrivate = methodName.starts_with('_');
        if (isPrivate && hasTypeName)
        {
            m_diagnostics.addPrivateStaticMethodError(
                m_tokens.source(),
                m_tokens.getSourceLocation(methodNameToken));
        }

        m_pendingAnnotations.clear();
        append(NodeKind::MethodDefinitionStatement, tokenIndexOf(keyword), subtreeStart);
    }

    void Parser::parseIfStatement(StatementScope scope)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto ifKeyword = advanceOnMatch(TokenKind::IfKeyword);
        parseExpression(scope, true, false);
        parseStatement(scope);

        if (currentToken().kind == TokenKind::ElseKeyword)
        {
            static_cast<void>(advanceOnMatch(TokenKind::ElseKeyword));
            parseStatement(scope);
        }

        append(NodeKind::IfStatement, tokenIndexOf(ifKeyword), subtreeStart);
    }

    void Parser::parseWhileStatement(StatementScope scope)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto whileKeyword = advanceOnMatch(TokenKind::WhileKeyword);
        parseExpression(scope, true, false);
        parseStatement(scope);

        append(NodeKind::WhileStatement, tokenIndexOf(whileKeyword), subtreeStart);
    }

    void Parser::parseBreakStatement()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto keyword = advanceOnMatch(TokenKind::BreakKeyword);

        // parse trailing if statement like "break if condition;"
        if (currentToken().kind == TokenKind::IfKeyword)
        {
            auto ifKeyword = advanceOnMatch(TokenKind::IfKeyword);
            appendNode(NodeKind::BreakStatement, tokenIndexOf(keyword));
            parseExpression(StatementScope::Function, true, false);
            static_cast<void>(advanceOnMatch(TokenKind::Semicolon));
            append(NodeKind::TrailingIfStatement, tokenIndexOf(ifKeyword), subtreeStart);
            return;
        }

        static_cast<void>(advanceOnMatch(TokenKind::Semicolon));
        appendNode(NodeKind::BreakStatement, tokenIndexOf(keyword));
    }

    void Parser::parseSkipStatement()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto keyword = advanceOnMatch(TokenKind::SkipKeyword);

        // parse trailing if statement like "skip if condition;"
        if (currentToken().kind == TokenKind::IfKeyword)
        {
            auto ifKeyword = advanceOnMatch(TokenKind::IfKeyword);
            appendNode(NodeKind::SkipStatement, tokenIndexOf(keyword));
            parseExpression(StatementScope::Function, true, false);
            static_cast<void>(advanceOnMatch(TokenKind::Semicolon));
            append(NodeKind::TrailingIfStatement, tokenIndexOf(ifKeyword), subtreeStart);
            return;
        }

        static_cast<void>(advanceOnMatch(TokenKind::Semicolon));
        appendNode(NodeKind::SkipStatement, tokenIndexOf(keyword));
    }

    void Parser::parseReturnStatement(StatementScope scope)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto returnKeyword = advanceOnMatch(TokenKind::ReturnKeyword);
        if (currentToken().kind != TokenKind::Semicolon && currentToken().kind != TokenKind::IfKeyword)
        {
            parseExpression(scope);
        }

        // parse trailing if statement like "return expression if condition;"
        if (currentToken().kind == TokenKind::IfKeyword)
        {
            auto ifKeyword = advanceOnMatch(TokenKind::IfKeyword);
            append(NodeKind::ReturnStatement, tokenIndexOf(returnKeyword), subtreeStart);
            parseExpression(scope, true, false);
            static_cast<void>(advanceOnMatch(TokenKind::Semicolon));
            append(NodeKind::TrailingIfStatement, tokenIndexOf(ifKeyword), subtreeStart);
            return;
        }

        static_cast<void>(advanceOnMatch(TokenKind::Semicolon));
        append(NodeKind::ReturnStatement, tokenIndexOf(returnKeyword), subtreeStart);
    }

    void Parser::parseExpression(StatementScope scope, bool stopAtLineBreak, bool allowLineBreakBeforeDot)
    {
        parseBinaryExpression(0, scope, stopAtLineBreak, allowLineBreakBeforeDot);
    }

    void Parser::parseBinaryExpression(i32 parentPrecedence, StatementScope scope, bool stopAtLineBreak, bool allowLineBreakBeforeDot)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto unaryOperatorToken = currentToken();

        auto unaryPrecedence = unaryOperatorPrecedence(unaryOperatorToken.kind);
        if (unaryPrecedence == 0 || unaryPrecedence < parentPrecedence)
        {
            parsePostfixExpression(scope, allowLineBreakBeforeDot);
        }
        else
        {
            advanceCurrentIndex();
            parseBinaryExpression(unaryPrecedence, scope, stopAtLineBreak, allowLineBreakBeforeDot);
            append(NodeKind::UnaryExpression, tokenIndexOf(unaryOperatorToken), subtreeStart);
        }

        while (true)
        {
            auto binaryOperatorToken = currentToken();
            if (binaryOperatorToken.kind == TokenKind::EndOfFile)
                break;

            auto binaryPrecedence = binaryOperatorPrecedence(binaryOperatorToken.kind);
            if (binaryPrecedence == 0 || binaryPrecedence <= parentPrecedence)
                break;

            if (stopAtLineBreak && hasLeadingLineBreak(binaryOperatorToken))
                break;

            // TODO remove this once we got both wrapping and non-wrapping operators working
            if (binaryOperatorToken.kind == TokenKind::Plus
                || binaryOperatorToken.kind == TokenKind::Minus
                || binaryOperatorToken.kind == TokenKind::Star)
            {
                m_diagnostics.addReservedOperatorError(
                    m_tokens.source(),
                    m_tokens.getSourceLocation(binaryOperatorToken),
                    binaryOperatorToken.kind);
            }

            advanceCurrentIndex();
            parseBinaryExpression(binaryPrecedence, scope, stopAtLineBreak, allowLineBreakBeforeDot);
            append(NodeKind::BinaryExpression, tokenIndexOf(binaryOperatorToken), subtreeStart);
        }
    }

    void Parser::parsePostfixExpression(StatementScope scope, bool allowLineBreakBeforeDot)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();

        // single token operands append directly instead of walking the primary
        // expression call chain, anything else takes the full path
        // this increased parsing performance
        const auto current = currentToken();
        switch (current.kind)
        {
            case TokenKind::Identifier:
            case TokenKind::PrivateIdentifier:
            case TokenKind::InitKeyword:
            case TokenKind::NewKeyword:
            {
                if (nextToken().kind == TokenKind::OpenParenthesis)
                {
                    parsePrimaryExpression(scope);
                }
                else
                {
                    advanceCurrentIndex();
                    appendNode(NodeKind::NameExpression, tokenIndexOf(current));
                }
                break;
            }
            case TokenKind::Number:
            {
                if (nextToken().kind == TokenKind::SingleQuote)
                {
                    parsePrimaryExpression(scope);
                }
                else
                {
                    advanceCurrentIndex();
                    appendNode(NodeKind::NumberLiteral, tokenIndexOf(current));
                }
                break;
            }
            case TokenKind::TrueKeyword:
            case TokenKind::FalseKeyword:
            {
                advanceCurrentIndex();
                appendNode(NodeKind::BoolLiteral, tokenIndexOf(current));
                break;
            }
            case TokenKind::String:
            {
                advanceCurrentIndex();
                appendNode(NodeKind::StringLiteral, tokenIndexOf(current));
                break;
            }
            case TokenKind::Underscore:
            {
                advanceCurrentIndex();
                appendNode(NodeKind::DiscardLiteral, tokenIndexOf(current));
                break;
            }
            default:
            {
                parsePrimaryExpression(scope);
                break;
            }
        }

        while (currentToken().kind == TokenKind::Dot)
        {
            auto dotToken = currentToken();
            if (hasLeadingLineBreak(dotToken) && !allowLineBreakBeforeDot)
                break;

            advanceCurrentIndex();
            parseFunctionCallOrNameExpression(scope);
            append(NodeKind::BinaryExpression, tokenIndexOf(dotToken), subtreeStart);
        }
    }

    void Parser::parsePrimaryExpression(StatementScope scope)
    {
        auto current = currentToken();
        switch (current.kind)
        {
            case TokenKind::Underscore:
            {
                advanceCurrentIndex();
                appendNode(NodeKind::DiscardLiteral, tokenIndexOf(current));
                return;
            }
            case TokenKind::Identifier:
            case TokenKind::PrivateIdentifier:
            case TokenKind::InitKeyword:
            case TokenKind::NewKeyword:
            {
                parseFunctionCallOrNameExpression(scope);
                return;
            }
            case TokenKind::TrueKeyword:
            case TokenKind::FalseKeyword:
            {
                advanceCurrentIndex();
                appendNode(NodeKind::BoolLiteral, tokenIndexOf(current));
                return;
            }
            case TokenKind::Number:
            {
                parseNumberLiteral();
                return;
            }
            case TokenKind::String:
            {
                parseStringLiteral();
                return;
            }
            case TokenKind::OpenBracket:
            {
                parseArrayLiteralExpression(scope);
                return;
            }
            case TokenKind::OpenParenthesis:
            {
                parseGroupingExpression(scope);
                return;
            }
            case TokenKind::Dot:
            {
                if (scope == StatementScope::Method)
                {
                    parseMemberAccessExpression();
                    return;
                }
                [[fallthrough]];
            }
            default:
            {
                const auto& location = m_tokens.getSourceLocation(current);
                m_diagnostics.addUnexpectedExpressionTokenError(m_tokens.source(), location, current.kind);

                if (current.kind != TokenKind::EndOfFile)
                {
                    advanceCurrentIndex();
                }

                appendNode(NodeKind::Error, tokenIndexOf(current));
                return;
            }
        }
    }

    bool Parser::hasLeadingLineBreak(const Token& token) const noexcept
    {
        return m_tokens.hasLineBreakInLeadingTrivia(token);
    }

    void Parser::parseGroupingExpression(StatementScope scope)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto openParenthesis = advanceOnMatch(TokenKind::OpenParenthesis);
        parseExpression(scope);
        static_cast<void>(advanceOnMatch(TokenKind::CloseParenthesis));

        append(NodeKind::GroupingExpression, tokenIndexOf(openParenthesis), subtreeStart);
    }

    void Parser::parseArrayLiteralExpression(StatementScope scope)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto openBracket = advanceOnMatch(TokenKind::OpenBracket);

        auto current = currentToken();
        while (current.kind != TokenKind::CloseBracket && current.kind != TokenKind::EndOfFile)
        {
            if (current.kind == TokenKind::Ellipsis)
            {
                // a trailing ellipsis marks a dynamic array literal and must be the last entry
                static_cast<void>(advanceOnMatch(TokenKind::Ellipsis));
                break;
            }

            const auto positionBeforeElement = m_currentIndex;
            parseExpression(scope);

            if (currentToken().kind == TokenKind::Comma)
            {
                advanceCurrentIndex();
            }
            else if (m_currentIndex == positionBeforeElement)
            {
                // parseExpression made no progress, stop instead of looping forever
                break;
            }
            current = currentToken();
        }

        static_cast<void>(advanceOnMatch(TokenKind::CloseBracket));

        append(NodeKind::ArrayLiteral, tokenIndexOf(openBracket), subtreeStart);
    }

    void Parser::parseMemberAccessExpression()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto dotToken = advanceOnMatch(TokenKind::Dot);
        parseFunctionCallOrNameExpression(StatementScope::Method);

        append(NodeKind::MemberAccessExpression, tokenIndexOf(dotToken), subtreeStart);
    }

    void Parser::parseNameExpression()
    {
        auto nameToken = advanceOnIdentifier();
        appendNode(NodeKind::NameExpression, tokenIndexOf(nameToken));
    }

    void Parser::parseFunctionCallOrNameExpression(StatementScope scope)
    {
        auto next = nextToken();
        if (next.kind == TokenKind::OpenParenthesis)
        {
            parseFunctionCallExpression(scope);
        }
        else
        {
            parseNameExpression();
        }
    }

    void Parser::parseFunctionCallExpression(StatementScope scope)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        parseNameExpression();

        auto openParenthesis = advanceOnMatch(TokenKind::OpenParenthesis);
        parseArgumentList(scope);
        static_cast<void>(advanceOnMatch(TokenKind::CloseParenthesis));

        append(NodeKind::FunctionCallExpression, tokenIndexOf(openParenthesis), subtreeStart);
    }

    void Parser::parseTypeNameNode()
    {
        static_cast<void>(tryMatchKind(TokenKind::RefKeyword));
        if (currentToken().kind == TokenKind::OpenBracket)
        {
            parseArrayTypeNameNode();
            return;
        }

        auto nameToken = advanceOnIdentifier();
        appendNode(NodeKind::TypeNameNode, tokenIndexOf(nameToken));
    }

    void Parser::parseArrayTypeNameNode()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto openBracket = advanceOnMatch(TokenKind::OpenBracket);
        parseTypeNameNode();

        if (currentToken().kind == TokenKind::Semicolon)
        {
            static_cast<void>(advanceOnMatch(TokenKind::Semicolon));
            if (currentToken().kind == TokenKind::Number)
            {
                parseNumberLiteral();
            }
            else if (currentToken().kind == TokenKind::Underscore)
            {
                static_cast<void>(advanceOnMatch(TokenKind::Underscore));
            }
            else
            {
                m_diagnostics.addExpectedArrayLengthError(m_tokens.source(), m_tokens.getSourceLocation(currentToken()));
                if (currentToken().kind != TokenKind::CloseBracket && currentToken().kind != TokenKind::EndOfFile)
                {
                    advanceCurrentIndex();
                }
            }
        }

        static_cast<void>(advanceOnMatch(TokenKind::CloseBracket));
        append(NodeKind::ArrayTypeNameNode, tokenIndexOf(openBracket), subtreeStart);
    }

    u32 Parser::parseMethodNameNode()
    {
        auto firstNameToken = advanceOnIdentifier();
        auto dotToken = tryMatchKind(TokenKind::Dot);
        if (dotToken.has_value())
        {
            auto secondNameToken = advanceOnIdentifier();
            appendNode(NodeKind::MethodNameNode, tokenIndexOf(secondNameToken));
            return m_nodeBuffer.nodeCount() - 1;
        }

        appendNode(NodeKind::MethodNameNode, tokenIndexOf(firstNameToken));
        return m_nodeBuffer.nodeCount() - 1;
    }

    void Parser::parseParametersNode(StatementScope scope)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto openParenthesis = advanceOnMatch(TokenKind::OpenParenthesis);

        auto current = currentToken();
        while (current.kind != TokenKind::CloseParenthesis && current.kind != TokenKind::EndOfFile)
        {
            const auto positionBeforeParameter = m_currentIndex;
            if (isIdentifierKind(current.kind))
            {
                const auto parameterStart = m_nodeBuffer.nodeCount();
                auto nameToken = advanceOnIdentifier();
                static_cast<void>(advanceOnMatch(TokenKind::Colon));
                parseTypeNameNode();

                if (currentToken().kind == TokenKind::Equal)
                {
                    static_cast<void>(advanceOnMatch(TokenKind::Equal));
                    // parse with method scope so member access reaches the type checker, which rejects it
                    parseExpression(StatementScope::Method);
                }

                append(NodeKind::ParameterNode, tokenIndexOf(nameToken), parameterStart);
            }
            else if (current.kind == TokenKind::Ellipsis)
            {
                const auto parameterStart = m_nodeBuffer.nodeCount();
                auto ellipsis = advanceOnMatch(TokenKind::Ellipsis);
                appendNode(NodeKind::TypeNameNode, tokenIndexOf(ellipsis));
                append(NodeKind::ParameterNode, tokenIndexOf(ellipsis), parameterStart);
            }

            if (currentToken().kind == TokenKind::Comma)
            {
                advanceCurrentIndex();
            }
            else if (m_currentIndex == positionBeforeParameter)
            {
                const auto& unexpected = currentToken();
                m_diagnostics.addUnexpectedParameterTokenError(
                    m_tokens.source(),
                    m_tokens.getSourceLocation(unexpected),
                    unexpected.kind);

                skipUntil({ TokenKind::Comma, TokenKind::CloseParenthesis });
                if (currentToken().kind == TokenKind::Comma)
                {
                    advanceCurrentIndex();
                }
            }
            current = currentToken();
        }

        static_cast<void>(advanceOnMatch(TokenKind::CloseParenthesis));

        append(NodeKind::ParametersNode, tokenIndexOf(openParenthesis), subtreeStart);
    }

    void Parser::parseReturnTypesNode()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto current = currentToken();
        if (current.kind != TokenKind::OpenBrace)
        {
            parseTypeNameNode();

            // TODO multiple return types
        }

        append(NodeKind::ReturnTypesNode, InvalidTokenIndex, subtreeStart);
    }

    void Parser::parseArgumentList(StatementScope scope)
    {
        auto seenNamed = false;
        auto reportedPositionalAfterNamed = false;
        auto current = currentToken();
        while (current.kind != TokenKind::CloseParenthesis && current.kind != TokenKind::EndOfFile)
        {
            if (isIdentifierKind(current.kind) && nextToken().kind == TokenKind::Equal)
            {
                const auto subtreeStart = m_nodeBuffer.nodeCount();
                auto nameToken = advanceOnIdentifier();
                static_cast<void>(advanceOnMatch(TokenKind::Equal));
                parseExpression(scope);
                append(NodeKind::NamedArgument, tokenIndexOf(nameToken), subtreeStart);
                seenNamed = true;
            }
            else
            {
                const auto firstValueToken = m_currentIndex;
                parseExpression(scope);
                // positional arguments must be before named ones, flag the first error but keep parsing for recovery
                if (seenNamed && !reportedPositionalAfterNamed)
                {
                    m_diagnostics.addPositionalArgumentAfterNamedError(
                        m_tokens.source(),
                        tokenRangeLocation(firstValueToken, m_currentIndex - 1));
                    reportedPositionalAfterNamed = true;
                }
            }

            if (currentToken().kind == TokenKind::Comma)
            {
                advanceCurrentIndex();
            }
            current = currentToken();
        }
    }

    void Parser::parseNumberLiteral()
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        auto literal = advanceOnMatch(TokenKind::Number);
        auto singleQuote = tryMatchKind(TokenKind::SingleQuote);
        if (singleQuote.has_value())
        {
            parseTypeNameNode();
        }

        append(NodeKind::NumberLiteral, tokenIndexOf(literal), subtreeStart);
    }

    void Parser::parseStringLiteral()
    {
        auto literal = advanceOnMatch(TokenKind::String);
        appendNode(NodeKind::StringLiteral, tokenIndexOf(literal));
    }

    void Parser::buildAnnotationNode(StatementScope scope)
    {
        const auto subtreeStart = m_nodeBuffer.nodeCount();
        static_cast<void>(advanceOnMatch(TokenKind::Hash));
        auto nameToken = advanceOnIdentifier();

        if (currentToken().kind == TokenKind::OpenParenthesis)
        {
            static_cast<void>(advanceOnMatch(TokenKind::OpenParenthesis));
            parseArgumentList(scope);
            static_cast<void>(advanceOnMatch(TokenKind::CloseParenthesis));
        }

        append(NodeKind::AnnotationNode, tokenIndexOf(nameToken), subtreeStart);
        m_pendingAnnotations.push_back(tokenIndexOf(nameToken));
    }

    void Parser::flushDanglingAnnotations()
    {
        for (const auto nameToken : m_pendingAnnotations)
        {
            m_diagnostics.addDanglingAnnotationError(
                m_tokens.source(),
                annotationLocation(nameToken));
        }
        m_pendingAnnotations.clear();
    }

    Token Parser::reportExpectedToken(TokenKind kind, const Token& current)
    {
        const auto& location = m_tokens.getSourceLocation(current);
        m_diagnostics.addExpectedTokenError(m_tokens.source(), location, kind, current.kind);
        return Token::ToError(current);
    }

    Token Parser::reportExpectedToken(TokenKind kind1, TokenKind kind2, const Token& current)
    {
        const auto& location = m_tokens.getSourceLocation(current);
        m_diagnostics.addExpectedTokenError(m_tokens.source(), location, kind1, kind2, current.kind);
        return Token::ToError(current);
    }

    std::optional<Token> Parser::tryMatchKind(TokenKind kind)
    {
        auto current = currentToken();
        if (current.kind == kind)
        {
            advanceCurrentIndex();
            return std::make_optional<Token>(current);
        }
        return std::optional<Token>();
    }

    void Parser::skipUntil(std::initializer_list<TokenKind> syncKinds)
    {
        while (true)
        {
            const auto kind = currentToken().kind;
            if (kind == TokenKind::EndOfFile)
                return;

            for (const auto syncKind : syncKinds)
            {
                if (kind == syncKind)
                    return;
            }

            advanceCurrentIndex();
        }
    }

    void Parser::synchronizeToNextStatement()
    {
        skipUntil({
            TokenKind::Semicolon,
            TokenKind::OpenBrace,
            TokenKind::CloseBrace,
            TokenKind::Hash,
            TokenKind::DefKeyword,
            TokenKind::EnumKeyword,
            TokenKind::TypeKeyword,
            TokenKind::IfKeyword,
            TokenKind::WhileKeyword,
            TokenKind::BreakKeyword,
            TokenKind::SkipKeyword,
            TokenKind::ReturnKeyword,
            });

        if (currentToken().kind == TokenKind::Semicolon)
        {
            advanceCurrentIndex();
        }
    }

    Token Parser::peek(i32 offset)
    {
        // we dont need the bounds check, the buffer has extra padding
        return m_tokens.getToken(m_currentIndex + offset);
    }
}
