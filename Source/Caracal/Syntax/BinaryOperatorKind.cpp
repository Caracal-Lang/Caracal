#include <Caracal/Syntax/BinaryOperatorKind.h>

#include <Caracal/Defines.h>

#include <unordered_map>

namespace Caracal
{
    std::string stringify(BinaryOperatorKind operation)
    {
        static const std::unordered_map<BinaryOperatorKind, std::string> opToString{
            { BinaryOperatorKind::Invalid,                      std::string("Invalid") },
            { BinaryOperatorKind::ConstructorCall,              std::string("ConstructorCall") },
            { BinaryOperatorKind::MethodCall,                   std::string("MethodCall") },
            { BinaryOperatorKind::MemberAccess,                 std::string("MemberAccess") },
            { BinaryOperatorKind::AdditionWrapping,             std::string("AdditionWrapping") },
            { BinaryOperatorKind::SubtractionWrapping,          std::string("SubtractionWrapping") },
            { BinaryOperatorKind::MultiplicationWrapping,       std::string("MultiplicationWrapping") },
            { BinaryOperatorKind::Division,                     std::string("Division") },
            { BinaryOperatorKind::Equal,                        std::string("Equal") },
            { BinaryOperatorKind::NotEqual,                     std::string("NotEqual") },
            { BinaryOperatorKind::LessThan,                     std::string("LessThan") },
            { BinaryOperatorKind::LessOrEqual,                  std::string("LessOrEqual") },
            { BinaryOperatorKind::GreaterThan,                  std::string("GreaterThan") },
            { BinaryOperatorKind::GreaterOrEqual,               std::string("GreaterOrEqual") },
            { BinaryOperatorKind::LogicalAnd,                   std::string("LogicalAnd") },
            { BinaryOperatorKind::LogicalOr,                    std::string("LogicalOr") },
        };

        const auto it = opToString.find(operation);
        if (it != opToString.end())
            return it->second;

        TODO("Invalid binary operator kind");
        return std::string();
    }
}
