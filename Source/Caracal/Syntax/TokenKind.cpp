#include <Caracal/Syntax/TokenKind.h>
#include <unordered_map>

std::string stringify(TokenKind kind)
{
    const std::unordered_map<TokenKind, std::string_view> kindToString{
        { TokenKind::Unknown,           std::string_view("Unknown") },
        { TokenKind::Error,             std::string_view("Error") },

        { TokenKind::Plus,              std::string_view("Plus") },
        { TokenKind::PercentPlus,       std::string_view("PercentPlus") },
        { TokenKind::Minus,             std::string_view("Minus") },
        { TokenKind::PercentMinus,      std::string_view("PercentMinus") },
        { TokenKind::Star,              std::string_view("Star") },
        { TokenKind::PercentStar,       std::string_view("PercentStar") },
        { TokenKind::Slash,             std::string_view("Slash") },
        { TokenKind::Dot,               std::string_view("Dot") },
        { TokenKind::Ellipsis,          std::string_view("Ellipsis") },
        { TokenKind::Comma,             std::string_view("Comma") },
        { TokenKind::Colon,             std::string_view("Colon") },
        { TokenKind::Semicolon,         std::string_view("Semicolon") },
        { TokenKind::Underscore,        std::string_view("Underscore") },
        { TokenKind::SingleQuote,       std::string_view("SingleQuote") },
        { TokenKind::Hash,              std::string_view("Hash") },

        { TokenKind::Equal,             std::string_view("Equal") },
        { TokenKind::EqualEqual,        std::string_view("EqualEqual") },
        { TokenKind::Bang,              std::string_view("Bang") },
        { TokenKind::BangEqual,         std::string_view("BangEqual") },
        { TokenKind::LessThan,          std::string_view("LessThan") },
        { TokenKind::LessThanEqual,     std::string_view("LessThanEqual") },
        { TokenKind::GreaterThan,       std::string_view("GreaterThan") },
        { TokenKind::GreaterThanEqual,  std::string_view("GreaterThanEqual") },
        { TokenKind::LessThan,          std::string_view("LessThan") },
        { TokenKind::GreaterThan,       std::string_view("GreaterThan") },

        { TokenKind::OpenParenthesis,   std::string_view("OpenParenthesis") },
        { TokenKind::CloseParenthesis,  std::string_view("CloseParenthesis") },
        { TokenKind::OpenBrace,       std::string_view("OpenBrace") },
        { TokenKind::CloseBrace,      std::string_view("CloseBrace") },
        { TokenKind::OpenBracket,     std::string_view("OpenBracket") },
        { TokenKind::CloseBracket,    std::string_view("CloseBracket") },

        { TokenKind::Identifier,        std::string_view("Identifier") },
        { TokenKind::PrivateIdentifier, std::string_view("PrivateIdentifier") },
        { TokenKind::InitKeyword,       std::string_view("InitKeyword") },
        { TokenKind::NewKeyword,        std::string_view("NewKeyword") },
        { TokenKind::Number,            std::string_view("Number") },
        { TokenKind::String,            std::string_view("String") },
                                        
        { TokenKind::DefKeyword,        std::string_view("DefKeyword") },
        { TokenKind::EnumKeyword,       std::string_view("EnumKeyword") },
        { TokenKind::TypeKeyword,       std::string_view("TypeKeyword") },
        { TokenKind::IfKeyword,         std::string_view("IfKeyword") },
        { TokenKind::ElseKeyword,       std::string_view("ElseKeyword") },
        { TokenKind::WhileKeyword,      std::string_view("WhileKeyword") },
        { TokenKind::BreakKeyword,      std::string_view("BreakKeyword") },
        { TokenKind::SkipKeyword,       std::string_view("SkipKeyword") },
        { TokenKind::ReturnKeyword,     std::string_view("ReturnKeyword") },
        { TokenKind::TrueKeyword,       std::string_view("TrueKeyword") },
        { TokenKind::FalseKeyword,      std::string_view("FalseKeyword") },
        { TokenKind::AndKeyword,        std::string_view("AndKeyword") },
        { TokenKind::OrKeyword,         std::string_view("OrKeyword") },
        { TokenKind::RefKeyword,        std::string_view("RefKeyword") },

        { TokenKind::EndOfFile,         std::string_view("EndOfFile") },
    };

    const auto it = kindToString.find(kind);
    if (it != kindToString.end())
        return std::string(it->second);

    TODO("String for TokenKind value was not defined yet");
    return std::string();
}
