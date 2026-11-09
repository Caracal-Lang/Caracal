#pragma once

#include <Caracal/CompilationContext.h>
#include <Caracal/Semantic/FunctionDefinition.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Syntax/BinaryOperatorKind.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <vector>

namespace Caracal
{
    [[nodiscard]] inline SourceLocation ParameterSpan(const CompilationContext& context, const CompilationUnit& unit, u32 parameterIndex)
    {
        return NodeSpans::parameterSpan(unit.nodeBuffer, context.tokenBuffer(unit.fileId), parameterIndex);
    }

    [[nodiscard]] inline SourceLocation ExpressionSourceLocation(const CompilationContext& context, const CompilationUnit& unit, u32 index)
    {
        return NodeSpans::expressionSourceLocation(unit.nodeBuffer, context.tokenBuffer(unit.fileId), index);
    }

    [[nodiscard]] inline TokenKind TokenKindAt(const TokenBuffer& tokens, u32 tokenIndex) noexcept
    {
        return tokens.getToken(static_cast<i32>(tokenIndex)).kind;
    }

    [[nodiscard]] inline bool ContainsRuntimeCall(
        SemanticContext& module,
        const CompilationUnit& unit,
        const TokenBuffer& tokens,
        u32 index)
    {
        switch (unit.nodeBuffer.kind(index))
        {
            case NodeKind::FunctionCallExpression:
            {
                return true;
            }
            case NodeKind::GroupingExpression:
            case NodeKind::UnaryExpression:
            case NodeKind::MemberAccessExpression:
            {
                return ContainsRuntimeCall(module, unit, tokens, NodeLayout::onlyChild(index));
            }
            case NodeKind::BinaryExpression:
            {
                const auto binaryLayout = NodeLayout::binary(unit.nodeBuffer, index);
                const auto isMemberCall = unit.nodeBuffer.isMemberAccessBinary(tokens, index)
                    && unit.nodeBuffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression;
                if (!isMemberCall)
                {
                    return ContainsRuntimeCall(module, unit, tokens, binaryLayout.left)
                        || ContainsRuntimeCall(module, unit, tokens, binaryLayout.right);
                }

                if (ContainsRuntimeCall(module, unit, tokens, binaryLayout.left))
                {
                    return true;
                }

                const auto methodType = module.typeOfNode(NodeIndex{ unit.fileId, NodeLayout::subtreeBegin(unit.nodeBuffer, binaryLayout.right) });
                if (methodType != Type::Undefined())
                {
                    const auto* definition = module.tryGetFunctionDefinition(methodType);
                    if (definition != nullptr && isBitwiseIntrinsicKind(definition->intrinsicKind()))
                    {
                        // foldable builtins are fine but arguments can force this into a runtime call
                        std::vector<u32> arguments;
                        NodeLayout::collectChildren(unit.nodeBuffer, binaryLayout.right, arguments);
                        for (size_t argumentIndex = 1; argumentIndex < arguments.size(); ++argumentIndex)
                        {
                            auto valueIndex = arguments[argumentIndex];
                            if (unit.nodeBuffer.kind(valueIndex) == NodeKind::NamedArgument)
                            {
                                valueIndex = NodeLayout::namedArgumentValue(valueIndex);
                            }

                            if (ContainsRuntimeCall(module, unit, tokens, valueIndex))
                            {
                                return true;
                            }
                        }

                        return false;
                    }
                }

                return true;
            }
            case NodeKind::ArrayLiteral:
            {
                std::vector<u32> elements;
                NodeLayout::collectChildren(unit.nodeBuffer, index, elements);
                for (const auto element : elements)
                {
                    if (ContainsRuntimeCall(module, unit, tokens, element))
                    {
                        return true;
                    }
                }

                return false;
            }
            default:
            {
                return false;
            }
        }
    }

    inline bool ConditionIsLiteralTrue(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 conditionIndex)
    {
        const auto condition = NodeLayout::stripGroupings(buffer, conditionIndex);
        return buffer.kind(condition) == NodeKind::BoolLiteral
            && TokenKindAt(tokens, buffer.mainToken(condition)) == TokenKind::TrueKeyword;
    }

    inline bool ContainsLoopBreak(const NodeBuffer& buffer, u32 index)
    {
        switch (buffer.kind(index))
        {
            case NodeKind::BreakStatement:
            {
                return true;
            }
            case NodeKind::BlockNode:
            {
                std::vector<u32> children;
                NodeLayout::collectChildren(buffer, index, children);
                for (const auto inner : children)
                {
                    if (ContainsLoopBreak(buffer, inner))
                    {
                        return true;
                    }
                }

                return false;
            }
            case NodeKind::IfStatement:
            {
                const auto ifLayout = NodeLayout::ifStatement(buffer, index);
                if (ContainsLoopBreak(buffer, ifLayout.trueStatement))
                {
                    return true;
                }

                return ifLayout.hasFalse && ContainsLoopBreak(buffer, ifLayout.falseStatement);
            }
            case NodeKind::TrailingIfStatement:
            {
                return ContainsLoopBreak(buffer, NodeLayout::trailingIf(buffer, index).statement);
            }
            default:
            {
                return false;
            }
        }
    }

    inline bool StatementGuaranteesReturn(const NodeBuffer& buffer, const TokenBuffer& tokens, u32 index)
    {
        switch (buffer.kind(index))
        {
            case NodeKind::ReturnStatement:
            {
                return true;
            }
            case NodeKind::BlockNode:
            {
                std::vector<u32> children;
                NodeLayout::collectChildren(buffer, index, children);
                for (const auto inner : children)
                {
                    if (StatementGuaranteesReturn(buffer, tokens, inner))
                    {
                        return true;
                    }
                }

                return false;
            }
            case NodeKind::IfStatement:
            {
                const auto ifLayout = NodeLayout::ifStatement(buffer, index);
                return ifLayout.hasFalse
                    && StatementGuaranteesReturn(buffer, tokens, ifLayout.trueStatement)
                    && StatementGuaranteesReturn(buffer, tokens, ifLayout.falseStatement);
            }
            case NodeKind::WhileStatement:
            {
                const auto whileLayout = NodeLayout::whileStatement(buffer, index);
                return ConditionIsLiteralTrue(buffer, tokens, whileLayout.condition)
                    && !ContainsLoopBreak(buffer, whileLayout.body);
            }
            default:
            {
                return false;
            }
        }
    }

    [[nodiscard]] inline BinaryOperatorKind ConvertToBinaryOperator(TokenKind kind) noexcept
    {
        switch (kind)
        {
            case TokenKind::Dot:
            {
                return BinaryOperatorKind::MemberAccess;
            }
            case TokenKind::PercentPlus:
            {
                return BinaryOperatorKind::AdditionWrapping;
            }
            case TokenKind::PercentMinus:
            {
                return BinaryOperatorKind::SubtractionWrapping;
            }
            case TokenKind::PercentStar:
            {
                return BinaryOperatorKind::MultiplicationWrapping;
            }
            case TokenKind::Slash:
            {
                return BinaryOperatorKind::Division;
            }
            case TokenKind::EqualEqual:
            {
                return BinaryOperatorKind::Equal;
            }
            case TokenKind::BangEqual:
            {
                return BinaryOperatorKind::NotEqual;
            }
            case TokenKind::LessThan:
            {
                return BinaryOperatorKind::LessThan;
            }
            case TokenKind::LessThanEqual:
            {
                return BinaryOperatorKind::LessOrEqual;
            }
            case TokenKind::GreaterThan:
            {
                return BinaryOperatorKind::GreaterThan;
            }
            case TokenKind::GreaterThanEqual:
            {
                return BinaryOperatorKind::GreaterOrEqual;
            }
            case TokenKind::AndKeyword:
            {
                return BinaryOperatorKind::LogicalAnd;
            }
            case TokenKind::OrKeyword:
            {
                return BinaryOperatorKind::LogicalOr;
            }
            default:
            {
                return BinaryOperatorKind::Invalid;
            }
        }
    }
}
