#include "GroupingExpression.h"

namespace Caracal 
{
    GroupingExpression::GroupingExpression(
        const Token& openParenthesisToken,
        ExpressionUPtr&& expression,
        const Token& closeParenthesisToken)
        : Expression(NodeKind::GroupingExpression, expression->type())
        , m_openParenthesisToken{ openParenthesisToken }
        , m_expression{ std::move(expression)}
        , m_closeParenthesisToken{ closeParenthesisToken }
    {
    }

    SourceLocation GroupingExpression::sourceLocation(const TokenBuffer& tokens) const
    {
        const auto openLocation = tokens.getSourceLocation(m_openParenthesisToken);
        const auto closeLocation = tokens.getSourceLocation(m_closeParenthesisToken);
        return SourceLocation{ openLocation.startIndex, closeLocation.endIndex };
    }

    const Expression* stripGroupings(const Expression* expression) noexcept
    {
        while (expression != nullptr && expression->kind() == NodeKind::GroupingExpression)
        {
            expression = static_cast<const GroupingExpression*>(expression)->expression().get();
        }

        return expression;
    }
}
