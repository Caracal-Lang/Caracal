#pragma once

#include <Caracal/Defines.h>
#include <Caracal/API.h>

#include <string>

enum class TokenKind : u8
{
    Unknown,
    Error,

    Plus,
    PercentPlus,
    Minus,
    PercentMinus,
    Star,
    PercentStar,
    Slash,
    Dot,
    Ellipsis,
    Comma,
    Colon,
    Semicolon,
    Underscore,
    SingleQuote,
    Hash,

    Equal,
    EqualEqual,
    Bang,
    BangEqual,
    LessThan,
    LessThanEqual,
    GreaterThan,
    GreaterThanEqual,

    OpenParenthesis,
    CloseParenthesis,
    OpenBrace,
    CloseBrace,
    OpenBracket,
    CloseBracket,

    Identifier,
    PrivateIdentifier,
    InitKeyword,
    NewKeyword,
    Number,
    String,

    DefKeyword,
    EnumKeyword,
    TypeKeyword,
    IfKeyword,
    ElseKeyword,
    WhileKeyword,
    BreakKeyword,
    SkipKeyword,
    ReturnKeyword,
    TrueKeyword,
    FalseKeyword,
    AndKeyword,
    OrKeyword,
    RefKeyword,

    EndOfFile
};

// Identifier, PrivateIdentifier, InitKeyword and NewKeyword
[[nodiscard]] inline bool isIdentifierKind(TokenKind kind)
{
    return kind >= TokenKind::Identifier && kind <= TokenKind::NewKeyword;
}

[[nodiscard]] CARACAL_API std::string stringify(TokenKind kind);

[[nodiscard]] inline i32 unaryOperatorPrecedence(TokenKind kind)
{
    switch (kind)
    {
        case TokenKind::RefKeyword:
            return 7;
        case TokenKind::Bang:
        case TokenKind::Minus:
            return 6;
        default:
            return 0;
    }
}

[[nodiscard]] inline i32 binaryOperatorPrecedence(TokenKind kind)
{
    switch (kind)
    {
        case TokenKind::Star:
        case TokenKind::PercentStar:
        case TokenKind::Slash:
            return 4;
        case TokenKind::Plus:
        case TokenKind::PercentPlus:
        case TokenKind::Minus:
        case TokenKind::PercentMinus:
            return 3;
        case TokenKind::EqualEqual:
        case TokenKind::BangEqual:
        case TokenKind::LessThan:
        case TokenKind::LessThanEqual:
        case TokenKind::GreaterThan:
        case TokenKind::GreaterThanEqual:
            return 2;
        case TokenKind::AndKeyword:
        case TokenKind::OrKeyword:
            return 1;
        default:
            return 0;
    }
}
