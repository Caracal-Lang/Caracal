#include "DiagnosticFormatting.h"

namespace Caracal
{
    std::string formatTypeName(SemanticContext& module, Type type)
    {
        if (type == Type::Undefined())
        {
            return "undefined";
        }

        if (type == Type::Void())
        {
            return "void";
        }

        auto name = std::string(module.getNameByType(type));
        if (type.isReference())
        {
            return "ref " + name;
        }

        return name;
    }

    std::string formatBinaryOperator(BinaryOperatorKind binaryOperator)
    {
        switch (binaryOperator)
        {
            case BinaryOperatorKind::AdditionWrapping:
                return "%+";
            case BinaryOperatorKind::SubtractionWrapping:
                return "%-";
            case BinaryOperatorKind::MultiplicationWrapping:
                return "%*";
            case BinaryOperatorKind::Division:
                return "/";
            case BinaryOperatorKind::Equal:
                return "==";
            case BinaryOperatorKind::NotEqual:
                return "!=";
            case BinaryOperatorKind::LessThan:
                return "<";
            case BinaryOperatorKind::LessOrEqual:
                return "<=";
            case BinaryOperatorKind::GreaterThan:
                return ">";
            case BinaryOperatorKind::GreaterOrEqual:
                return ">=";
            case BinaryOperatorKind::LogicalAnd:
                return "and";
            case BinaryOperatorKind::LogicalOr:
                return "or";
            default:
                return stringify(binaryOperator);
        }
    }

}
