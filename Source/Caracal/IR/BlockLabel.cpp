#include <Caracal/IR/BlockLabel.h>

namespace Caracal
{
    std::string_view blockLabelText(BlockLabel label) noexcept
    {
        switch (label)
        {
            case BlockLabel::Entry:
            {
                return "entry";
            }
            case BlockLabel::IfTrue:
            {
                return "if.true";
            }
            case BlockLabel::IfFalse:
            {
                return "if.false";
            }
            case BlockLabel::IfContinuation:
            {
                return "if.continuation";
            }
            case BlockLabel::WhileCondition:
            {
                return "while.condition";
            }
            case BlockLabel::WhileBody:
            {
                return "while.body";
            }
            case BlockLabel::WhileContinuation:
            {
                return "while.continuation";
            }
            case BlockLabel::AndRhs:
            {
                return "and.rhs";
            }
            case BlockLabel::AndMerge:
            {
                return "and.merge";
            }
            case BlockLabel::OrRhs:
            {
                return "or.rhs";
            }
            case BlockLabel::OrMerge:
            {
                return "or.merge";
            }
        }

        return "";
    }
}
