#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <vector>

namespace Caracal
{
    namespace NodeSpans
    {
        [[nodiscard]] static const SourceLocation& TokenLocationAt(const TokenBuffer& tokens, u32 tokenIndex) noexcept
        {
            return tokens.getSourceLocation(tokens.getToken(static_cast<i32>(tokenIndex)));
        }

        u32 findMatchingCloseToken(const TokenBuffer& tokens, u32 openTokenIndex, TokenKind openKind, TokenKind closeKind) noexcept
        {
            auto depth = 0;
            const auto count = tokens.size();
            for (auto position = static_cast<i32>(openTokenIndex); position < count; position++)
            {
                const auto kind = tokens.getToken(position).kind;
                if (kind == openKind)
                {
                    depth++;
                }
                else if (kind == closeKind)
                {
                    depth--;
                    if (depth == 0)
                    {
                        return static_cast<u32>(position);
                    }
                }
            }

            return openTokenIndex;
        }

        u32 findCloserToken(const TokenBuffer& tokens, u32 from, TokenKind closer) noexcept
        {
            auto position = from;
            const auto count = static_cast<u32>(tokens.size());
            while (position + 1 < count && tokens.getToken(static_cast<i32>(position)).kind != closer)
            {
                ++position;
            }

            return position;
        }

        u32 lastTokenOf(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index)
        {
            switch (buffer.kind(index))
            {
                case NodeKind::GroupingExpression:
                {
                    // the close parenthesis is directly after the expression
                    return lastTokenOf(buffer, tokens, NodeLayout::groupedExpression(index)) + 1;
                }
                case NodeKind::UnaryExpression:
                case NodeKind::MemberAccessExpression:
                case NodeKind::NamedArgument:
                {
                    return lastTokenOf(buffer, tokens, NodeLayout::onlyChild(index));
                }
                case NodeKind::BinaryExpression:
                {
                    return lastTokenOf(buffer, tokens, NodeLayout::binary(buffer, index).right);
                }
                case NodeKind::FunctionCallExpression:
                {
                    std::vector<u32> children;
                    NodeLayout::collectChildren(buffer, index, children);
                    auto from = buffer.mainToken(index) + 1;
                    if (children.size() > 1)
                    {
                        from = lastTokenOf(buffer, tokens, children.back()) + 1;
                    }

                    return findCloserToken(tokens, from, TokenKind::CloseParenthesis);
                }
                case NodeKind::ArrayLiteral:
                {
                    std::vector<u32> elements;
                    NodeLayout::collectChildren(buffer, index, elements);
                    auto from = buffer.mainToken(index) + 1;
                    if (!elements.empty())
                    {
                        from = lastTokenOf(buffer, tokens, elements.back()) + 1;
                    }

                    return findCloserToken(tokens, from, TokenKind::CloseBracket);
                }
                case NodeKind::NumberLiteral:
                {
                    const auto suffix = NodeLayout::optionalChild(buffer, index);
                    if (suffix.hasChild)
                    {
                        return lastTokenOf(buffer, tokens, suffix.child);
                    }

                    return buffer.mainToken(index);
                }
                case NodeKind::ArrayTypeNameNode:
                {
                    const auto arrayTypeLayout = NodeLayout::arrayTypeName(buffer, index);
                    auto from = lastTokenOf(buffer, tokens, arrayTypeLayout.elementType) + 1;
                    if (arrayTypeLayout.hasLength)
                    {
                        from = lastTokenOf(buffer, tokens, arrayTypeLayout.lengthLiteral) + 1;
                    }

                    return findCloserToken(tokens, from, TokenKind::CloseBracket);
                }
                default:
                {
                    return buffer.mainToken(index);
                }
            }
        }

        SourceLocation typeNameSpan(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index)
        {
            const auto mainToken = buffer.mainToken(index);
            if (buffer.kind(index) == NodeKind::ArrayTypeNameNode)
            {
                const auto closeBracket = findMatchingCloseToken(tokens, mainToken, TokenKind::OpenBracket, TokenKind::CloseBracket);
                const auto& closeLocation = TokenLocationAt(tokens, closeBracket);
                if (buffer.isReference(tokens, index))
                {
                    const auto& refLocation = TokenLocationAt(tokens, mainToken - 1);
                    return SourceLocation{ refLocation.startIndex, closeLocation.endIndex };
                }

                const auto& openLocation = TokenLocationAt(tokens, mainToken);
                return SourceLocation{ openLocation.startIndex, closeLocation.endIndex };
            }

            const auto& nameLocation = TokenLocationAt(tokens, mainToken);
            if (buffer.isReference(tokens, index))
            {
                const auto& refLocation = TokenLocationAt(tokens, mainToken - 1);
                return SourceLocation{ refLocation.startIndex, nameLocation.endIndex };
            }

            return nameLocation;
        }

        SourceLocation parameterSpan(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 parameterIndex)
        {
            const auto parameterLayout = NodeLayout::parameter(buffer, parameterIndex);
            const auto& startLocation = TokenLocationAt(tokens, buffer.mainToken(parameterIndex));
            const auto& endLocation = TokenLocationAt(tokens, buffer.mainToken(parameterLayout.typeName));
            return SourceLocation{ startLocation.startIndex, endLocation.endIndex };
        }

        SourceLocation parametersNodeSpan(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 parametersIndex)
        {
            const auto openParenthesis = buffer.mainToken(parametersIndex);
            const auto closeParenthesis = findMatchingCloseToken(tokens, openParenthesis, TokenKind::OpenParenthesis, TokenKind::CloseParenthesis);
            const auto& openLocation = TokenLocationAt(tokens, openParenthesis);
            const auto& closeLocation = TokenLocationAt(tokens, closeParenthesis);
            return SourceLocation{ openLocation.startIndex, closeLocation.endIndex };
        }

        SourceLocation expressionSourceLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index)
        {
            const auto mainToken = buffer.mainToken(index);
            switch (buffer.kind(index))
            {
                case NodeKind::GroupingExpression:
                case NodeKind::ArrayLiteral:
                {
                    auto openKind = TokenKind::OpenParenthesis;
                    auto closeKind = TokenKind::CloseParenthesis;
                    if (buffer.kind(index) == NodeKind::ArrayLiteral)
                    {
                        openKind = TokenKind::OpenBracket;
                        closeKind = TokenKind::CloseBracket;
                    }

                    const auto closeToken = findMatchingCloseToken(tokens, mainToken, openKind, closeKind);
                    const auto& openLocation = TokenLocationAt(tokens, mainToken);
                    const auto& closeLocation = TokenLocationAt(tokens, closeToken);
                    return SourceLocation{ openLocation.startIndex, closeLocation.endIndex };
                }
                case NodeKind::UnaryExpression:
                case NodeKind::MemberAccessExpression:
                {
                    const auto& operatorLocation = TokenLocationAt(tokens, mainToken);
                    const auto childLocation = expressionSourceLocation(buffer, tokens, NodeLayout::onlyChild(index));
                    return SourceLocation{ operatorLocation.startIndex, childLocation.endIndex };
                }
                case NodeKind::BinaryExpression:
                {
                    const auto binaryLayout = NodeLayout::binary(buffer, index);
                    const auto leftLocation = expressionSourceLocation(buffer, tokens, binaryLayout.left);
                    const auto rightLocation = expressionSourceLocation(buffer, tokens, binaryLayout.right);
                    return SourceLocation{ leftLocation.startIndex, rightLocation.endIndex };
                }
                case NodeKind::FunctionCallExpression:
                {
                    const auto nameIndex = NodeLayout::subtreeBegin(buffer, index);
                    const auto nameLocation = expressionSourceLocation(buffer, tokens, nameIndex);
                    const auto closeToken = findMatchingCloseToken(tokens, mainToken, TokenKind::OpenParenthesis, TokenKind::CloseParenthesis);
                    const auto& closeLocation = TokenLocationAt(tokens, closeToken);
                    return SourceLocation{ nameLocation.startIndex, closeLocation.endIndex };
                }
                case NodeKind::TypeNameNode:
                case NodeKind::ArrayTypeNameNode:
                {
                    return typeNameSpan(buffer, tokens, index);
                }
                default:
                {
                    return TokenLocationAt(tokens, mainToken);
                }
            }
        }

        SourceLocation expressionLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index)
        {
            switch (buffer.kind(index))
            {
                case NodeKind::BinaryExpression:
                {
                    const auto binaryLayout = NodeLayout::binary(buffer, index);
                    const auto leftLocation = expressionLocation(buffer, tokens, binaryLayout.left);
                    const auto rightLocation = expressionLocation(buffer, tokens, binaryLayout.right);
                    return SourceLocation{ leftLocation.startIndex, rightLocation.endIndex };
                }
                case NodeKind::UnaryExpression:
                case NodeKind::MemberAccessExpression:
                {
                    const auto& operatorLocation = TokenLocationAt(tokens, buffer.mainToken(index));
                    const auto childLocation = expressionLocation(buffer, tokens, NodeLayout::onlyChild(index));
                    return SourceLocation{ operatorLocation.startIndex, childLocation.endIndex };
                }
                case NodeKind::GroupingExpression:
                case NodeKind::ArrayLiteral:
                {
                    const auto& openLocation = TokenLocationAt(tokens, buffer.mainToken(index));
                    const auto& closeLocation = TokenLocationAt(tokens, lastTokenOf(buffer, tokens, index));
                    return SourceLocation{ openLocation.startIndex, closeLocation.endIndex };
                }
                case NodeKind::FunctionCallExpression:
                {
                    const auto nameLocation = expressionLocation(buffer, tokens, NodeLayout::firstChild(buffer, index));
                    const auto& closeLocation = TokenLocationAt(tokens, lastTokenOf(buffer, tokens, index));
                    return SourceLocation{ nameLocation.startIndex, closeLocation.endIndex };
                }
                default:
                {
                    return TokenLocationAt(tokens, buffer.mainToken(index));
                }
            }
        }

        SourceLocation parameterLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 parameterIndex)
        {
            const auto parameterLayout = NodeLayout::parameter(buffer, parameterIndex);
            const auto& startLocation = TokenLocationAt(tokens, buffer.mainToken(parameterIndex));
            const auto& endLocation = TokenLocationAt(tokens, buffer.mainToken(parameterLayout.typeName));
            return SourceLocation{ startLocation.startIndex, endLocation.endIndex };
        }

        std::optional<SourceLocation> deadStatementLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index)
        {
            switch (buffer.kind(index))
            {
                case NodeKind::ReturnStatement:
                case NodeKind::BreakStatement:
                case NodeKind::SkipStatement:
                case NodeKind::IfStatement:
                case NodeKind::WhileStatement:
                case NodeKind::TrailingIfStatement:
                {
                    // the main token is the keyword, a trailing if carries the if keyword
                    return TokenLocationAt(tokens, buffer.mainToken(index));
                }
                case NodeKind::ExpressionStatement:
                {
                    return expressionLocation(buffer, tokens, NodeLayout::statementExpression(index));
                }
                case NodeKind::AssignmentStatement:
                {
                    return expressionLocation(buffer, tokens, NodeLayout::assignment(buffer, index).target);
                }
                case NodeKind::ConstantDeclaration:
                case NodeKind::VariableDeclaration:
                {
                    return expressionLocation(buffer, tokens, NodeLayout::declaration(buffer, index).left);
                }
                case NodeKind::InitConstantDeclaration:
                {
                    return expressionLocation(buffer, tokens, NodeLayout::initConstant(buffer, index).left);
                }
                default:
                {
                    return std::nullopt;
                }
            }
        }

        SourceLocation callArgumentsLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 callIndex, std::span<const u32> argumentNodes)
        {
            if (argumentNodes.empty())
            {
                const auto openToken = buffer.mainToken(callIndex);
                const auto closeToken = findMatchingCloseToken(tokens, openToken, TokenKind::OpenParenthesis, TokenKind::CloseParenthesis);
                const auto& openParenthesisLocation = TokenLocationAt(tokens, openToken);
                const auto& closeParenthesisLocation = TokenLocationAt(tokens, closeToken);
                return SourceLocation{ openParenthesisLocation.startIndex, closeParenthesisLocation.endIndex };
            }

            const auto firstArgumentLocation = expressionSourceLocation(buffer, tokens, NodeLayout::argumentValue(buffer, argumentNodes.front()));
            const auto lastArgumentLocation = expressionSourceLocation(buffer, tokens, NodeLayout::argumentValue(buffer, argumentNodes.back()));
            return SourceLocation{ firstArgumentLocation.startIndex, lastArgumentLocation.endIndex };
        }

        SourceLocation annotationArgumentsLocation(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index, const SourceLocation& annotationLocation)
        {
            if (!buffer.annotationHasParentheses(tokens, index))
            {
                return annotationLocation;
            }

            std::vector<u32> arguments;
            NodeLayout::collectChildren(buffer, index, arguments);
            if (arguments.empty())
            {
                const auto openParenthesisToken = static_cast<i32>(buffer.mainToken(index)) + 1;
                const auto openParenthesisLocation = tokens.getSourceLocation(tokens.getToken(openParenthesisToken));
                const auto closeParenthesisLocation = tokens.getSourceLocation(tokens.getToken(openParenthesisToken + 1));
                return SourceLocation{ openParenthesisLocation.startIndex, closeParenthesisLocation.endIndex };
            }

            const auto firstArgument = arguments.front();
            auto startIndex = 0;
            if (buffer.kind(firstArgument) == NodeKind::NamedArgument)
            {
                startIndex = tokens.getSourceLocation(tokens.getToken(static_cast<i32>(buffer.mainToken(firstArgument)))).startIndex;
            }
            else
            {
                startIndex = expressionSourceLocation(buffer, tokens, firstArgument).startIndex;
            }

            auto lastArgument = arguments.back();
            if (buffer.kind(lastArgument) == NodeKind::NamedArgument)
            {
                lastArgument = NodeLayout::namedArgumentValue(lastArgument);
            }
            const auto lastArgumentLocation = expressionSourceLocation(buffer, tokens, lastArgument);
            return SourceLocation{ startIndex, lastArgumentLocation.endIndex };
        }
    }
}
