#include <Caracal/Constants.h>
#include <Caracal/Syntax/AssignmentStatement.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/BoolLiteral.h>
#include <Caracal/Syntax/BreakStatement.h>
#include <Caracal/Syntax/ConstantDeclaration.h>
#include <Caracal/Syntax/DiscardLiteral.h>
#include <Caracal/Syntax/EnumDefinitionStatement.h>
#include <Caracal/Syntax/ArrayLiteral.h>
#include <Caracal/Syntax/ArrayTypeNameNode.h>
#include <Caracal/Syntax/ErrorExpression.h>
#include <Caracal/Syntax/ExpressionStatement.h>
#include <Caracal/Syntax/FunctionCallExpression.h>
#include <Caracal/Syntax/FunctionDefinitionStatement.h>
#include <Caracal/Syntax/GroupingExpression.h>
#include <Caracal/Syntax/IfStatement.h>
#include <Caracal/Syntax/MemberAccessExpression.h>
#include <Caracal/Syntax/MethodDefinitionStatement.h>
#include <Caracal/Syntax/NameExpression.h>
#include <Caracal/Syntax/ParserOld.h>
#include <Caracal/Syntax/ReturnStatement.h>
#include <Caracal/Syntax/SkipStatement.h>
#include <Caracal/Syntax/StringLiteral.h>
#include <Caracal/Syntax/TypeDefinitionStatement.h>
#include <Caracal/Syntax/TypeFieldDeclaration.h>
#include <Caracal/Syntax/UnaryExpression.h>
#include <Caracal/Syntax/VariableDeclaration.h>
#include <Caracal/Syntax/WhileStatement.h>

namespace Caracal
{
    static SourceLocation GetAnnotationLocation(const AnnotationNode* annotation, const TokenBuffer& tokens)
    {
        const auto hashLocation = tokens.getSourceLocation(annotation->hashToken());
        const auto nameLocation = tokens.getSourceLocation(annotation->nameToken());
        return { hashLocation.startIndex, nameLocation.endIndex };
    }

    static AnnotationKind ParseAnnotationKind(std::string_view name)
    {
        if (name == ExternAnnotationName)
        {
            return AnnotationKind::Extern;
        }

        if (name == FlagAnnotationName)
        {
            return AnnotationKind::Flag;
        }

        if (name == StepAnnotationName)
        {
            return AnnotationKind::Step;
        }

        if (name == BuiltinAnnotationName)
        {
            return AnnotationKind::Builtin;
        }

        return AnnotationKind::Error;
    }

    static std::string ReplaceEscapeSequences(std::string_view input)
    {
        std::string result;
        result.reserve(input.size());

        for (size_t index = 0; index < input.size(); index++)
        {
            const auto character = input[index];
            if (character != '\\' || index + 1 == input.size())
            {
                result.push_back(character);
                continue;
            }

            index++;
            const auto escapedCharacter = input[index];
            switch (escapedCharacter)
            {
                case '\'':
                {
                    result.push_back('\'');
                    break;
                }
                case '\"':
                {
                    result.push_back('\"');
                    break;
                }
                case 'a':
                {
                    result.push_back('\a');
                    break;
                }
                case 'b':
                {
                    result.push_back('\b');
                    break;
                }
                case 'f':
                {
                    result.push_back('\f');
                    break;
                }
                case 'n':
                {
                    result.push_back('\n');
                    break;
                }
                case 'r':
                {
                    result.push_back('\r');
                    break;
                }
                case 't':
                {
                    result.push_back('\t');
                    break;
                }
                case 'v':
                {
                    result.push_back('\v');
                    break;
                }
                case '\\':
                {
                    result.push_back('\\');
                    break;
                }
                default:
                {
                    // unknown escapes stay verbatim
                    result.push_back('\\');
                    result.push_back(escapedCharacter);
                    break;
                }
            }
        }

        return result;
    }

    ParserOld::ParserOld(const TokenBuffer& tokens, DiagnosticsBag& diagnostics)
        : m_tokens{ tokens }
        , m_diagnostics{ diagnostics }
        , m_currentIndex{ 0 }
    {
    }

    ParseTreeUPtr ParserOld::parse()
    {
        auto statements = parseStatements(StatementScope::Global);

        flushDanglingAnnotations();

        if (m_currentIndex < m_tokens.size() - 1)
        {
            const auto& location = m_tokens.getSourceLocation(m_tokens.getToken(m_currentIndex));
            m_diagnostics.addExtraTokensRemainingError(m_tokens.source(), location);
        }

        return std::make_unique<ParseTree>(std::move(m_tokens), std::move(statements));
    }

    std::vector<StatementUPtr> ParserOld::parseStatements(StatementScope scope)
    {
        std::vector<StatementUPtr> statements{};
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
                    const auto location = GetAnnotationLocation(m_currentAnnotations.back().get(), m_tokens);
                    m_diagnostics.addAnnotationNotAllowedHereError(m_tokens.source(), location);
                    m_currentAnnotations.pop_back();
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
                    return statements;
                }
                default:
                {
                    statements.push_back(parseStatement(scope));
                    break;
                }
            }
            
            current = currentToken();
        }
    }

    StatementUPtr ParserOld::parseStatement(StatementScope scope)
    {
        const auto current = currentToken();
        switch (current.kind)
        {
            case TokenKind::DefKeyword:
            {
                if (scope == StatementScope::Global)
                {
                    return parseFunctionDefinitionStatement();
                }
                else if (scope == StatementScope::Type)
                {
                    return parseMethodDefinitionStatement();
                }
                break;
            }
            case TokenKind::EnumKeyword:
            {
                if (scope == StatementScope::Global)
                {
                    return parseEnumDefinitionStatement();
                }
                break;
            }
            case TokenKind::TypeKeyword:
            {
                if (scope == StatementScope::Global)
                {
                    return parseTypeDefinitionStatement();
                }
                break;
            }
            case TokenKind::IfKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    return parseIfStatement(scope);
                }

                break;
            }
            case TokenKind::WhileKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    return parseWhileStatement(scope);
                }
                break;
            }
            case TokenKind::BreakKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    return parseBreakStatement();
                }
                break;
            }
            case TokenKind::SkipKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    return parseSkipStatement();
                }
                break;
            }
            case TokenKind::ReturnKeyword:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    return parseReturnStatement(scope);
                }
                break;
            }
            case TokenKind::OpenBrace:
            {
                if (scope == StatementScope::Function || scope == StatementScope::Method)
                {
                    return parseBlockNode(scope);
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
                auto expression = parseExpression(scope);
                if (currentToken().kind == TokenKind::Colon)
                {
                    if (scope == StatementScope::Global || scope == StatementScope::Function || scope == StatementScope::Method)
                    {
                        return parseConstantOrVariableDeclaration(std::move(expression), scope);
                    }
                    else if (scope == StatementScope::Type && expression->kind() == NodeKind::NameExpression)
                    {
                        return parseTypeFieldDeclaration(std::move(expression));
                    }
                    break;
                }
                if ((expression->kind() == NodeKind::NameExpression || expression->kind() == NodeKind::BinaryExpression || expression->kind() == NodeKind::MemberAccessExpression || expression->kind() == NodeKind::DiscardLiteral) && currentToken().kind == TokenKind::Equal)
                {
                    if (scope == StatementScope::Function || scope == StatementScope::Method)
                    {
                        return parseAssignmentStatement(std::move(expression), scope);
                    }
                    break;
                }
                if (expression->kind() == NodeKind::FunctionCallExpression || expression->kind() == NodeKind::BinaryExpression || expression->kind() == NodeKind::MemberAccessExpression)
                {
                    if (scope == StatementScope::Function || scope == StatementScope::Method)
                    {
                        return parseExpressionStatement(std::move(expression));
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

        auto errorExpression = std::make_unique<ErrorExpression>(current);

        if (current.kind != TokenKind::CloseBrace && current.kind != TokenKind::EndOfFile)
        {
            advanceCurrentIndex();
            synchronizeToNextStatement();
        }

        return std::make_unique<ExpressionStatement>(std::move(errorExpression), Token::ToError(current));
    }

    StatementUPtr ParserOld::parseExpressionStatement(ExpressionUPtr&& expression)
    {
        auto semicolon = advanceOnMatch(TokenKind::Semicolon);

        return std::make_unique<ExpressionStatement>(std::move(expression), semicolon);
    }

    StatementUPtr ParserOld::parseFunctionDefinitionStatement()
    {
        auto keyword = advanceOnMatch(TokenKind::DefKeyword);
        auto nameToken = advanceOnIdentifier();
        auto name = m_tokens.getLexeme(nameToken);
        if (name.starts_with('_'))
        {
            m_diagnostics.addPrivateFreeFunctionError(
                m_tokens.source(),
                m_tokens.getSourceLocation(nameToken));
        }

        auto parameters = parseParametersNode(StatementScope::Function);
        auto returnTypes = parseReturnTypesNode();
        auto body = parseFunctionBody();

        auto annotations = takeCurrentAnnotations();

        return std::make_unique<FunctionDefinitionStatement>(keyword, nameToken, name, std::move(parameters), std::move(returnTypes), std::move(body), std::move(annotations));
    }

    StatementUPtr ParserOld::parseConstantOrVariableDeclaration(ExpressionUPtr&& leftExpression, StatementScope scope)
    {
        auto firstColon = advanceOnMatch(TokenKind::Colon);

        std::optional<TypeNameNodeUPtr> explicitType;
        auto currentTokenKind = currentToken().kind;
        if (currentTokenKind != TokenKind::Colon && currentTokenKind != TokenKind::Equal)
        {
            explicitType = parseTypeNameNode();
        }
        
        auto secondToken = advanceOnMatch(TokenKind::Colon, TokenKind::Equal);
        auto afterSecondToken = currentToken();
        auto isInitDeclaration = afterSecondToken.kind == TokenKind::InitKeyword;
        if (secondToken.kind == TokenKind::Colon && isInitDeclaration)
        {
            auto initKeyword = advanceOnMatch(TokenKind::InitKeyword);
            auto initType = parseTypeNameNode();
            auto semicolon = advanceOnMatch(TokenKind::Semicolon);

            if (explicitType.has_value())
            {
                auto constantName = std::string("name");
                if (leftExpression->kind() == NodeKind::NameExpression)
                {
                    constantName = static_cast<NameExpression*>(leftExpression.get())->name();
                }

                m_diagnostics.addExplicitTypeOnInitConstantError(
                    m_tokens.source(),
                    explicitType.value()->sourceLocation(m_tokens),
                    constantName,
                    initType->name());
            }

            auto annotations = std::vector<AnnotationNodeUPtr>{};
            if (scope == StatementScope::Global)
            {
                annotations = takeCurrentAnnotations();
            }

            return std::make_unique<ConstantDeclaration>(
                std::move(leftExpression),
                firstColon,
                secondToken,
                initKeyword,
                std::move(initType),
                semicolon,
                scope == StatementScope::Global,
                std::move(annotations));
        }

        auto rightExpression = parseExpression(scope);
        auto semicolon = advanceOnMatch(TokenKind::Semicolon);

        if (secondToken.kind == TokenKind::Colon)
        {
            auto isGlobalConstant = scope == StatementScope::Global;
            auto annotations = std::vector<AnnotationNodeUPtr>{};
            if (scope == StatementScope::Global)
            {
                annotations = takeCurrentAnnotations();
            }

            return std::make_unique<ConstantDeclaration>(std::move(leftExpression), firstColon, std::move(explicitType), secondToken, std::move(rightExpression), semicolon, isGlobalConstant, std::move(annotations));
        }
        else
        {
            auto annotations = std::vector<AnnotationNodeUPtr>{};
            if (scope == StatementScope::Global)
            {
                annotations = takeCurrentAnnotations();
            }

            return std::make_unique<VariableDeclaration>(std::move(leftExpression), firstColon, std::move(explicitType), secondToken, std::move(rightExpression), semicolon, std::move(annotations));
        }
    }

    StatementUPtr ParserOld::parseAssignmentStatement(ExpressionUPtr&& leftExpression, StatementScope scope)
    {
        auto equal = advanceOnMatch(TokenKind::Equal);
        auto rightExpression = parseExpression(scope);
        auto semicolon = advanceOnMatch(TokenKind::Semicolon);
     
        return std::make_unique<AssignmentStatement>(std::move(leftExpression), equal, std::move(rightExpression), semicolon);
    }

    StatementUPtr ParserOld::parseEnumDefinitionStatement()
    {
        auto keyword = advanceOnMatch(TokenKind::EnumKeyword);
        auto nameToken = advanceOnIdentifier();
        auto name = m_tokens.getLexeme(nameToken);

        auto current = currentToken();
        std::optional<Token> colonToken;
        std::optional<TypeNameNodeUPtr> baseType;
        if (current.kind == TokenKind::Colon)
        {
            colonToken = advanceOnMatch(TokenKind::Colon);
            baseType = parseTypeNameNode();
        }
    
        auto openBracket = advanceOnMatch(TokenKind::OpenBrace);
        auto enumFields = parseEnumFields();
        auto closeBracket = advanceOnMatch(TokenKind::CloseBrace);

        auto annotations = takeCurrentAnnotations();

        return std::make_unique<EnumDefinitionStatement>(keyword, nameToken, name, colonToken, std::move(baseType), openBracket, std::move(enumFields), closeBracket, std::move(annotations));
    }

    std::vector<EnumFieldDeclarationUPtr> ParserOld::parseEnumFields()
    {
        std::vector<EnumFieldDeclarationUPtr> fields;
        auto current = currentToken();
        while (current.kind != TokenKind::CloseBrace && current.kind != TokenKind::EndOfFile)
        {
            if (isIdentifierKind(current.kind))
            {
                auto nameToken = advanceOnIdentifier();
                auto name = m_tokens.getLexeme(nameToken);
                if (currentToken().kind == TokenKind::Colon && nextToken().kind == TokenKind::Colon)
                {
                    auto colon1 = advanceOnMatch(TokenKind::Colon);
                    auto colon2 = advanceOnMatch(TokenKind::Colon);
                    auto valueExpression = parseExpression(StatementScope::Enum);
                    fields.push_back(std::make_unique<EnumFieldDeclaration>(nameToken, name, colon1, colon2, std::move(valueExpression)));
                }
                else
                {
                    fields.push_back(std::make_unique<EnumFieldDeclaration>(nameToken, name));
                }
            }
            else
            {
                const auto& location = m_tokens.getSourceLocation(current);
                m_diagnostics.addExpectedEnumFieldError(m_tokens.source(), location, current.kind);
                advanceCurrentIndex();
            }
            current = currentToken();
        }
        return fields;
    }

    StatementUPtr ParserOld::parseTypeDefinitionStatement()
    {
        auto keyword = advanceOnMatch(TokenKind::TypeKeyword);
        auto nameToken = advanceOnIdentifier();
        auto name = m_tokens.getLexeme(nameToken);
        auto annotations = takeCurrentAnnotations();

        std::optional<ParametersNodeUPtr> maybeParameters;
        if (currentToken().kind == TokenKind::OpenParenthesis)
        {
            maybeParameters = parseParametersNode(StatementScope::Type);
        }

        auto body = parseTypeBody();

        return std::make_unique<TypeDefinitionStatement>(keyword, nameToken, name, std::move(maybeParameters), std::move(body), std::move(annotations));
    }

    StatementUPtr ParserOld::parseTypeFieldDeclaration(ExpressionUPtr&& leftExpression)
    {
        // the left expression is always a NameExpression
        auto nameExpression = std::unique_ptr<NameExpression>(static_cast<NameExpression*>(leftExpression.release()));
        auto firstColon = advanceOnMatch(TokenKind::Colon);
        
        std::optional<TypeNameNodeUPtr> explicitType;
        auto currentTokenKind = currentToken().kind;
        if (currentTokenKind != TokenKind::Colon && currentTokenKind != TokenKind::Equal)
        {
            explicitType = parseTypeNameNode();
        }

        const auto current = currentToken();
        if (current.kind != TokenKind::Colon && current.kind != TokenKind::Equal)
        {
            auto fieldLocation = nameExpression->sourceLocation(m_tokens);
            if (explicitType.has_value())
                fieldLocation.endIndex = explicitType.value()->sourceLocation(m_tokens).endIndex;
            else
                fieldLocation.endIndex = m_tokens.getSourceLocation(firstColon).endIndex;

            m_diagnostics.addUninitializedTypeFieldError(m_tokens.source(), fieldLocation, nameExpression->name());
            return std::make_unique<TypeFieldDeclaration>(std::move(nameExpression), firstColon, std::move(explicitType), Token::ToError(current), std::make_unique<ErrorExpression>(current), false);
        }

        auto secondToken = advanceOnMatch(TokenKind::Colon, TokenKind::Equal);
        auto rightExpression = parseExpression(StatementScope::Type);
        auto isConstant = secondToken.kind == TokenKind::Colon;

        return std::make_unique<TypeFieldDeclaration>(std::move(nameExpression), firstColon, std::move(explicitType), secondToken, std::move(rightExpression), isConstant);
    }

    StatementUPtr ParserOld::parseMethodDefinitionStatement()
    {
        auto modifier = MethodModifier::Public;
        auto keyword = advanceOnMatch(TokenKind::DefKeyword);
        auto methodNameNode = parseMethodNameNode();
        auto parameters = parseParametersNode(StatementScope::Method);
        auto returnTypes = parseReturnTypesNode();
        auto body = parseMethodBody();

        if(methodNameNode->hasTypeName())
        {
            // Methods defined as "def Type.function()" are considered static
            modifier = MethodModifier::Static;
        }

        const auto& methodName = methodNameNode->methodName();
        const auto isPrivate = methodName.starts_with('_');
        if(modifier == MethodModifier::Public && isPrivate)
        {
            modifier = MethodModifier::Private;
        }
        else if (modifier == MethodModifier::Static && isPrivate)
        {
            m_diagnostics.addPrivateStaticMethodError(
                m_tokens.source(),
                m_tokens.getSourceLocation(methodNameNode->methodNameToken()));
        }

        auto specialFunctionType = SpecialFunctionType::None;
        if (!methodNameNode->hasTypeName() && methodName == "new")
        {
            specialFunctionType = SpecialFunctionType::Constructor;
        }

        auto annotations = takeCurrentAnnotations();
        return std::make_unique<MethodDefinitionStatement>(
            keyword,
            std::move(methodNameNode),
            std::move(parameters),
            std::move(returnTypes),
            std::move(body),
            modifier,
            specialFunctionType,
            std::move(annotations));
    }
    
    BlockNodeUPtr ParserOld::parseFunctionBody()
    {
        return parseBlockNode(StatementScope::Function);
    }

    BlockNodeUPtr ParserOld::parseTypeBody()
    {
        return parseBlockNode(StatementScope::Type);
    }

    BlockNodeUPtr ParserOld::parseMethodBody()
    {
        return parseBlockNode(StatementScope::Method);
    }

    BlockNodeUPtr ParserOld::parseBlockNode(StatementScope scope)
    {
        auto openBracket = advanceOnMatch(TokenKind::OpenBrace);
        auto statements = parseStatements(scope);
        auto closeBracket = advanceOnMatch(TokenKind::CloseBrace);

        return std::make_unique<BlockNode>(openBracket, std::move(statements), closeBracket);
    }

    ParameterNodeUPtr ParserOld::parseParameterNode(StatementScope scope)
    {
        if (isIdentifierKind(currentToken().kind))
        {
            auto nameToken = advanceOnIdentifier();
            auto name = m_tokens.getLexeme(nameToken);
            auto colon = advanceOnMatch(TokenKind::Colon);
            auto typeName = parseTypeNameNode();

            ExpressionUPtr defaultValue;
            if (currentToken().kind == TokenKind::Equal)
            {
                advanceOnMatch(TokenKind::Equal);
                // always parse with method scope so leading-dot member access reaches the type checker, which rejects it with T0076
                defaultValue = parseExpression(StatementScope::Method);
            }

            return std::make_unique<ParameterNode>(nameToken, name, colon, std::move(typeName), false, std::move(defaultValue));
        }
        else
        {
            auto ellipsis = advanceOnMatch(TokenKind::Ellipsis);
            auto fakeTypeName = std::make_unique<TypeNameNode>(std::nullopt, ellipsis, "...");

            return std::make_unique<ParameterNode>(ellipsis, "varargs", ellipsis, std::move(fakeTypeName), true);
        }
    }

    NumberLiteralUPtr ParserOld::parseNumberLiteral()
    {
        auto literal = advanceOnMatch(TokenKind::Number);
        auto singleQuote = tryMatchKind(TokenKind::SingleQuote);
        std::optional<TypeNameNodeUPtr> explicitType;
        if (singleQuote.has_value())
        {
            explicitType = parseTypeNameNode();
        }

        const auto lexeme = m_tokens.getLexeme(literal);
        return std::make_unique<NumberLiteral>(literal, lexeme, singleQuote, std::move(explicitType));
    }

    StringLiteralUPtr ParserOld::parseStringLiteral()
    {
        auto literal = advanceOnMatch(TokenKind::String);

        const auto lexeme = m_tokens.getLexeme(literal);
        const auto withoutQuotes = lexeme.substr(1, lexeme.size() - 2);
        const auto escapedContent = ReplaceEscapeSequences(withoutQuotes);

        return std::make_unique<StringLiteral>(literal, escapedContent);
    }

    StatementUPtr ParserOld::parseIfStatement(StatementScope scope)
    {
        auto ifKeyword = advanceOnMatch(TokenKind::IfKeyword);
        auto condition = parseExpression(scope, true, false);
        auto trueStatement = parseStatement(scope);

        if (currentToken().kind == TokenKind::ElseKeyword)
        {
            auto elseKeyword = advanceOnMatch(TokenKind::ElseKeyword);
            auto falseStatement = parseStatement(scope);
            return std::make_unique<IfStatement>(ifKeyword, std::move(condition), std::move(trueStatement), elseKeyword, std::move(falseStatement));
        }

        return std::make_unique<IfStatement>(ifKeyword, std::move(condition), std::move(trueStatement));
    }

    StatementUPtr ParserOld::parseWhileStatement(StatementScope scope)
    {
        auto whileKeyword = advanceOnMatch(TokenKind::WhileKeyword);
        auto condition = parseExpression(scope, true, false);
        auto trueStatement = parseStatement(scope);

        return std::make_unique<WhileStatement>(whileKeyword, std::move(condition), std::move(trueStatement));
    }

    StatementUPtr ParserOld::parseBreakStatement()
    {
        auto keyword = advanceOnMatch(TokenKind::BreakKeyword);

        // parse trailing if statement like "break if condition;"
        if(currentToken().kind == TokenKind::IfKeyword)
        {
            auto ifKeyword = advanceOnMatch(TokenKind::IfKeyword);
            auto condition = parseExpression(StatementScope::Function, true, false);
            auto semicolon = advanceOnMatch(TokenKind::Semicolon);
            auto breakStatement = std::make_unique<BreakStatement>(keyword, semicolon);
            return std::make_unique<IfStatement>(ifKeyword, std::move(condition), std::move(breakStatement));
        }

        auto semicolon = advanceOnMatch(TokenKind::Semicolon);
        return std::make_unique<BreakStatement>(keyword, semicolon);
    }

    StatementUPtr ParserOld::parseSkipStatement()
    {
        auto keyword = advanceOnMatch(TokenKind::SkipKeyword);

        // parse trailing if statement like "skip if condition;"
        if(currentToken().kind == TokenKind::IfKeyword)
        {
            auto ifKeyword = advanceOnMatch(TokenKind::IfKeyword);
            auto condition = parseExpression(StatementScope::Function, true, false);
            auto semicolon = advanceOnMatch(TokenKind::Semicolon);
            auto skipStatement = std::make_unique<SkipStatement>(keyword, semicolon);

            return std::make_unique<IfStatement>(ifKeyword, std::move(condition), std::move(skipStatement));
        }

        auto semicolon = advanceOnMatch(TokenKind::Semicolon);
        return std::make_unique<SkipStatement>(keyword, semicolon);
    }
    
    StatementUPtr ParserOld::parseReturnStatement(StatementScope scope)
    {
        auto returnKeyword = advanceOnMatch(TokenKind::ReturnKeyword);
        std::optional<ExpressionUPtr> expression;
        if (currentToken().kind != TokenKind::Semicolon && currentToken().kind != TokenKind::IfKeyword)
        {
            expression = parseExpression(scope);
        }

        // parse trailing if statement like "return expression if condition;"
        if (currentToken().kind == TokenKind::IfKeyword)
        {
            auto ifKeyword = advanceOnMatch(TokenKind::IfKeyword);
            auto condition = parseExpression(scope, true, false);
            auto semicolon = advanceOnMatch(TokenKind::Semicolon);
            auto returnStatement = std::make_unique<ReturnStatement>(returnKeyword, std::move(expression), semicolon);

            return std::make_unique<IfStatement>(ifKeyword, std::move(condition), std::move(returnStatement));
        }

        auto semicolon = advanceOnMatch(TokenKind::Semicolon);
        return std::make_unique<ReturnStatement>(returnKeyword, std::move(expression), semicolon);
    }

    ExpressionUPtr ParserOld::parseExpression(StatementScope scope, bool stopAtLineBreak, bool allowLineBreakBeforeDot)
    {
        return parseBinaryExpression(0, scope, stopAtLineBreak, allowLineBreakBeforeDot);
    }

    ExpressionUPtr ParserOld::parseBinaryExpression(i32 parentPrecedence, StatementScope scope, bool stopAtLineBreak, bool allowLineBreakBeforeDot)
    {
        ExpressionUPtr left{};
        auto unaryOperatorToken = currentToken();

        auto unaryPrecedence = unaryOperatorPrecedence(unaryOperatorToken.kind);
        if (unaryPrecedence == 0 || unaryPrecedence < parentPrecedence)
        {
            left = parsePostfixExpression(scope, allowLineBreakBeforeDot);
        }
        else
        {
            advanceCurrentIndex();
            auto expression = parseBinaryExpression(unaryPrecedence, scope, stopAtLineBreak, allowLineBreakBeforeDot);
            left = std::make_unique<UnaryExpression>(unaryOperatorToken, std::move(expression));
        }

        while (true)
        {
            auto binaryOperatorToken = currentToken();
            if (binaryOperatorToken.kind == TokenKind::EndOfFile)
                break;

            if (stopAtLineBreak && hasLeadingLineBreak(binaryOperatorToken))
                break;

            auto binaryPrecedence = binaryOperatorPrecedence(binaryOperatorToken.kind);
            if (binaryPrecedence == 0 || binaryPrecedence <= parentPrecedence)
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
            auto right = parseBinaryExpression(binaryPrecedence, scope, stopAtLineBreak, allowLineBreakBeforeDot);
            left = std::make_unique<BinaryExpression>(std::move(left), binaryOperatorToken, std::move(right));
        }

        return left;
    }

    ExpressionUPtr ParserOld::parsePostfixExpression(StatementScope scope, bool allowLineBreakBeforeDot)
    {
        auto left = parsePrimaryExpression(scope);

        while (currentToken().kind == TokenKind::Dot)
        {
            auto dotToken = currentToken();
            if (hasLeadingLineBreak(dotToken) && !allowLineBreakBeforeDot)
                break;

            advanceCurrentIndex();
            auto right = parseFunctionCallOrNameExpression(scope);
            left = std::make_unique<BinaryExpression>(std::move(left), dotToken, std::move(right));
        }

        return left;
    }

    ExpressionUPtr ParserOld::parsePrimaryExpression(StatementScope scope)
    {
        auto current = currentToken();
        switch (current.kind)
        {
            case TokenKind::Underscore:
            {
                advanceCurrentIndex();
                return std::make_unique<DiscardLiteral>(current);
            }
            case TokenKind::Identifier:
            case TokenKind::PrivateIdentifier:
            case TokenKind::InitKeyword:
            case TokenKind::NewKeyword:
            {
                return parseFunctionCallOrNameExpression(scope);
            }
            case TokenKind::TrueKeyword:
            {
                advanceCurrentIndex();
                return std::make_unique<BoolLiteral>(current, true);
            }
            case TokenKind::FalseKeyword:
            {
                advanceCurrentIndex();
                return std::make_unique<BoolLiteral>(current, false);
            }
            case TokenKind::Number:
            {
                return parseNumberLiteral();
            }
            case TokenKind::String:
            {
                return parseStringLiteral();
            }
            case TokenKind::OpenBracket:
            {
                return parseArrayLiteralExpression(scope);
            }
            case TokenKind::OpenParenthesis:
            {
                return parseGroupingExpression(scope);
            }
            case TokenKind::Dot:
            {
                if (scope == StatementScope::Method)
                {
                    return parseMemberAccessExpression();
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

                return std::make_unique<ErrorExpression>(current);
            }
        }
    }

    bool ParserOld::hasLeadingLineBreak(const Token& token) const noexcept
    {
        const auto trivia = m_tokens.getTrivia(token);
        return trivia.find('\n') != std::string_view::npos || trivia.find('\r') != std::string_view::npos;
    }
    
    ExpressionUPtr ParserOld::parseGroupingExpression(StatementScope scope)
    {
        auto openParenthesis = advanceOnMatch(TokenKind::OpenParenthesis);
        auto expression = parseExpression(scope);
        auto closeParenthesis = advanceOnMatch(TokenKind::CloseParenthesis);

        return std::make_unique<GroupingExpression>(openParenthesis, std::move(expression), closeParenthesis);
    }

    ExpressionUPtr ParserOld::parseArrayLiteralExpression(StatementScope scope)
    {
        auto openBracket = advanceOnMatch(TokenKind::OpenBracket);

        std::vector<ExpressionUPtr> elements;
        std::optional<Token> ellipsisToken{};
        auto current = currentToken();
        while (current.kind != TokenKind::CloseBracket && current.kind != TokenKind::EndOfFile)
        {
            if (current.kind == TokenKind::Ellipsis)
            {
                // a trailing ellipsis marks a dynamic array literal and must be the last entry
                ellipsisToken = advanceOnMatch(TokenKind::Ellipsis);
                break;
            }

            const auto positionBeforeElement = m_currentIndex;
            elements.push_back(parseExpression(scope));

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

        auto closeBracket = advanceOnMatch(TokenKind::CloseBracket);

        return std::make_unique<ArrayLiteral>(openBracket, std::move(elements), ellipsisToken, closeBracket);
    }

    ExpressionUPtr ParserOld::parseMemberAccessExpression()
    {
        auto dotToken = advanceOnMatch(TokenKind::Dot);
        auto expression = parseFunctionCallOrNameExpression(StatementScope::Method);
     
        return std::make_unique<MemberAccessExpression>(dotToken, std::move(expression));
    }

    NameExpressionUPtr ParserOld::parseNameExpression()
    {
        auto nameToken = advanceOnIdentifier();
        const auto name = m_tokens.getLexeme(nameToken);
        return std::make_unique<NameExpression>(nameToken, name);
    }

    ExpressionUPtr ParserOld::parseFunctionCallOrNameExpression(StatementScope scope)
    {
        auto next = nextToken();
        if (next.kind == TokenKind::OpenParenthesis)
        {
            return parseFunctionCallExpression(scope);
        }
        else
        {
            return parseNameExpression();
        }
    }

    ExpressionUPtr ParserOld::parseFunctionCallExpression(StatementScope scope)
    {
        auto nameExpression = parseNameExpression();

        auto openParenthesis = advanceOnMatch(TokenKind::OpenParenthesis);
        std::vector<Argument> arguments;
        parseArgumentList(scope, arguments);
        auto closeParenthesis = advanceOnMatch(TokenKind::CloseParenthesis);

        return std::make_unique<FunctionCallExpression>(std::move(nameExpression), openParenthesis, std::move(arguments), closeParenthesis);
    }

    TypeNameNodeUPtr ParserOld::parseTypeNameNode()
    {
        auto refToken = tryMatchKind(TokenKind::RefKeyword);
        if (currentToken().kind == TokenKind::OpenBracket)
        {
            return parseArrayTypeNameNode(refToken);
        }

        auto nameToken = advanceOnIdentifier();
        auto name = m_tokens.getLexeme(nameToken);
        return std::make_unique<TypeNameNode>(refToken, nameToken, name);
    }

    TypeNameNodeUPtr ParserOld::parseArrayTypeNameNode(const std::optional<Token>& refToken)
    {
        auto openBracket = advanceOnMatch(TokenKind::OpenBracket);
        auto elementType = parseTypeNameNode();

        auto arrayKind = ArrayTypeKind::Slice;
        std::optional<Token> semicolonToken{};
        NumberLiteralUPtr lengthLiteral{};
        std::optional<Token> underscoreToken{};

        if (currentToken().kind == TokenKind::Semicolon)
        {
            semicolonToken = advanceOnMatch(TokenKind::Semicolon);
            if (currentToken().kind == TokenKind::Number)
            {
                arrayKind = ArrayTypeKind::Fixed;
                lengthLiteral = parseNumberLiteral();
            }
            else if (currentToken().kind == TokenKind::Underscore)
            {
                arrayKind = ArrayTypeKind::Dynamic;
                underscoreToken = advanceOnMatch(TokenKind::Underscore);
            }
            else
            {
                m_diagnostics.addExpectedArrayLengthError(m_tokens.source(), m_tokens.getSourceLocation(currentToken()));
                arrayKind = ArrayTypeKind::Fixed;
                if (currentToken().kind != TokenKind::CloseBracket && currentToken().kind != TokenKind::EndOfFile)
                {
                    advanceCurrentIndex();
                }
            }
        }

        auto closeBracket = advanceOnMatch(TokenKind::CloseBracket);
        return std::make_unique<ArrayTypeNameNode>(
            refToken,
            openBracket,
            std::move(elementType),
            arrayKind,
            semicolonToken,
            std::move(lengthLiteral),
            underscoreToken,
            closeBracket);
    }

    MethodNameNodeUPtr ParserOld::parseMethodNameNode()
    {
        auto firstNameToken = advanceOnIdentifier();
        auto firstName = m_tokens.getLexeme(firstNameToken);
        auto dotToken = tryMatchKind(TokenKind::Dot);
        if (dotToken.has_value())
        {
            auto secondNameToken = advanceOnIdentifier();
            auto secondName = m_tokens.getLexeme(secondNameToken);
            return std::make_unique<MethodNameNode>(firstNameToken, firstName, dotToken.value(), secondNameToken, secondName);
        }

        return std::make_unique<MethodNameNode>(firstNameToken, firstName);
    }

    ParametersNodeUPtr ParserOld::parseParametersNode(StatementScope scope)
    {
        auto openParenthesis = advanceOnMatch(TokenKind::OpenParenthesis);

        std::vector<ParameterNodeUPtr> parameters;
        auto current = currentToken();
        while (current.kind != TokenKind::CloseParenthesis && current.kind != TokenKind::EndOfFile)
        {
            const auto positionBeforeParameter = m_currentIndex;
            if (isIdentifierKind(current.kind) || current.kind == TokenKind::Ellipsis)
            {
                parameters.push_back(parseParameterNode(scope));
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
        
        auto closeParenthesis = advanceOnMatch(TokenKind::CloseParenthesis);

        return std::make_unique<ParametersNode>(openParenthesis, std::move(parameters), closeParenthesis);
    }

    ReturnTypesNodeUPtr ParserOld::parseReturnTypesNode()
    {
        std::vector<TypeNameNodeUPtr> returnTypes;
        auto current = currentToken();
        if (current.kind != TokenKind::OpenBrace)
        {
            auto typeName = parseTypeNameNode();
            returnTypes.push_back(std::move(typeName));

            // TODO multiple return types
        }

        return std::make_unique<ReturnTypesNode>(std::move(returnTypes));
    }

    void ParserOld::parseArgumentList(StatementScope scope, std::vector<Argument>& arguments)
    {
        auto seenNamed = false;
        auto reportedPositionalAfterNamed = false;
        auto current = currentToken();
        while (current.kind != TokenKind::CloseParenthesis && current.kind != TokenKind::EndOfFile)
        {
            if (isIdentifierKind(current.kind) && nextToken().kind == TokenKind::Equal)
            {
                auto nameToken = advanceOnIdentifier();
                auto argumentName = m_tokens.getLexeme(nameToken);
                advanceOnMatch(TokenKind::Equal);
                auto value = parseExpression(scope);
                arguments.emplace_back(nameToken, argumentName, std::move(value));
                seenNamed = true;
            }
            else
            {
                auto value = parseExpression(scope);
                // positional arguments must be before named ones, flag the first error but keep parsing for recovery
                if (seenNamed && !reportedPositionalAfterNamed)
                {
                    m_diagnostics.addPositionalArgumentAfterNamedError(m_tokens.source(), value->sourceLocation(m_tokens));
                    reportedPositionalAfterNamed = true;
                }
                arguments.emplace_back(std::move(value));
            }

            if (currentToken().kind == TokenKind::Comma)
            {
                advanceCurrentIndex();
            }
            current = currentToken();
        }
    }

    void ParserOld::buildAnnotationNode(StatementScope scope)
    {
        auto hashToken = advanceOnMatch(TokenKind::Hash);
        auto nameToken = advanceOnIdentifier();
        auto name = m_tokens.getLexeme(nameToken);

        std::optional<Token> openParenthesis;
        std::optional<Token> closeParenthesis;
        std::vector<Argument> arguments;
        if (currentToken().kind == TokenKind::OpenParenthesis)
        {
            openParenthesis = advanceOnMatch(TokenKind::OpenParenthesis);
            parseArgumentList(scope, arguments);
            closeParenthesis = advanceOnMatch(TokenKind::CloseParenthesis);
        }

        m_currentAnnotations.push_back(std::make_unique<AnnotationNode>(
            ParseAnnotationKind(name),
            hashToken,
            nameToken,
            name,
            std::move(openParenthesis),
            std::move(arguments),
            std::move(closeParenthesis)));
    }

    std::vector<AnnotationNodeUPtr> ParserOld::takeCurrentAnnotations()
    {
        auto annotations = std::move(m_currentAnnotations);
        m_currentAnnotations.clear();
        return annotations;
    }

    void ParserOld::flushDanglingAnnotations()
    {
        for (const auto& annotation : m_currentAnnotations)
        {
            m_diagnostics.addDanglingAnnotationError(
                m_tokens.source(),
                GetAnnotationLocation(annotation.get(), m_tokens));
        }
        m_currentAnnotations.clear();
    }

    Token ParserOld::advanceOnIdentifier()
    {
        auto current = currentToken();
        if (isIdentifierKind(current.kind))
        {
            advanceCurrentIndex();
            return current;
        }
        else
        {
            const auto& location = m_tokens.getSourceLocation(current);
            m_diagnostics.addExpectedTokenError(m_tokens.source(), location, TokenKind::Identifier, current.kind);
            return Token::ToError(current);
        }
    }
    Token ParserOld::advanceOnMatch(TokenKind kind)
    {
        auto current = currentToken();
        if (current.kind == kind)
        {
            advanceCurrentIndex();
            return current;
        }
        else
        {
            const auto& location = m_tokens.getSourceLocation(current);
            m_diagnostics.addExpectedTokenError(m_tokens.source(), location, kind, current.kind);
            return Token::ToError(current);
        }
    }

    Token ParserOld::advanceOnMatch(TokenKind kind1, TokenKind kind2)
    {
        auto current = currentToken();
        if (current.kind == kind1 || current.kind == kind2)
        {
            advanceCurrentIndex();
            return current;
        }
        else
        {
            const auto& location = m_tokens.getSourceLocation(current);
            m_diagnostics.addExpectedTokenError(m_tokens.source(), location, kind1, kind2, current.kind);
            return Token::ToError(current);
        }
    }

    std::optional<Token> ParserOld::tryMatchKind(TokenKind kind)
    {
        auto current = currentToken();
        if (current.kind == kind)
        {
            advanceCurrentIndex();
            return std::make_optional<Token>(current);
        }
        return std::optional<Token>();
    }

    void ParserOld::skipUntil(std::initializer_list<TokenKind> syncKinds)
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

    void ParserOld::synchronizeToNextStatement()
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

    Token ParserOld::peek(i32 offset)
    {
        auto index = m_currentIndex + offset;
        if (index >= m_tokens.size())
            return Token{ .kind = TokenKind::EndOfFile };

        return m_tokens.getToken(index);
    }
}

//
//i32 ParserOld::lineDistanceSinceLastToken()
//{
//    auto lastToken = peek(-1);
//    auto current = currentToken();
//    auto& lastTokenLocation = m_tokens.getSourceLocation(lastToken);
//    auto& currentTokenLocation = m_tokens.getSourceLocation(current);
//
//    //return currentTokenLocation.startLine - lastTokenLocation.endLine;
//    return 0;
//}
