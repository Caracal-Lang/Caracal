#include <Caracal/Syntax/UnaryOperatorKind.h>

#include <Caracal/Defines.h>

namespace Caracal
{
    std::string stringify(UnaryOperatorKind kind)
    {
        switch (kind)
        {
            case UnaryOperatorKind::LogicalNegation:
            {
                return std::string("LogicalNegation");
            }
            case UnaryOperatorKind::ValueNegation:
            {
                return std::string("ValueNegation");
            }
            case UnaryOperatorKind::ReferenceOf:
            {
                return std::string("ReferenceOf");
            }
            default:
            {
                TODO("Invalid unary operator kind");
            }
        }
        return std::string();
    }
}
