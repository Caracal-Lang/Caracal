#include <Caracal/Compilation.h>
#include <Caracal/Syntax/LexerSimd.h>
#include <Caracal/Profiling.h>

#include <cstring>

namespace Caracal
{
    static void AddTokenKindAndAdvance(TokenBuffer& tokenBuffer, std::string_view source, const char*& currentIndex, TokenKind tokenKind, i32 tokenSize) noexcept
    {
        const auto startIndex = static_cast<i32>(currentIndex - source.data());
        tokenBuffer.addToken(tokenKind, { .startIndex = startIndex, .endIndex = startIndex + tokenSize });
        currentIndex += tokenSize;
    };

    static void AddTokenFromStart(TokenBuffer& tokenBuffer, std::string_view source, const char* currentIndex, TokenKind tokenKind, i32 startIndex) noexcept
    {
        const auto endIndex = static_cast<i32>(currentIndex - source.data());
        tokenBuffer.addToken(tokenKind, { .startIndex = startIndex, .endIndex = endIndex });
    };

    // use string literals as template parameter for compile-time constants in msvc
    template<size_t N>
    struct KeywordLiteral
    {
        constexpr KeywordLiteral(const char (&text)[N]) noexcept
        {
            for (size_t index = 0; index < N; index++)
            {
                characters[index] = text[index];
            }
        }

        char characters[N] = {};
    };

    template<size_t N>
    [[nodiscard]] static constexpr u64 PackKeywordBits(const char (&text)[N]) noexcept
    {
        u64 bits = 0;
        for (size_t index = 0; index + 1 < N; index++)
        {
            bits |= static_cast<u64>(static_cast<u8>(text[index])) << (8 * index);
        }

        return bits;
    }

    template<KeywordLiteral Keyword>
    [[nodiscard]] static bool EqualsKeyword(const char* start) noexcept
    {
        constexpr auto length = sizeof(Keyword.characters) - 1;
        static_assert(length >= 2 && length <= 6, "keywords are 2 to 6 bytes long");
        constexpr auto lengthMask = ~u64{ 0 } >> (8 * (8 - length));
        constexpr auto keywordBits = PackKeywordBits(Keyword.characters);

        u64 identifierBits = 0;
        std::memcpy(&identifierBits, start, sizeof(identifierBits));
        return (identifierBits & lengthMask) == keywordBits;
    }

    // switch on the length to reduce the possible keywords
    static TokenKind IdentifierKind(const char* start, i32 length) noexcept
    {
        if (start[0] == '_')
            return TokenKind::PrivateIdentifier;

        switch (length)
        {
            case 2:
            {
                if (EqualsKeyword<"if">(start))
                    return TokenKind::IfKeyword;
                if (EqualsKeyword<"or">(start))
                    return TokenKind::OrKeyword;
                break;
            }
            case 3:
            {
                if (EqualsKeyword<"def">(start))
                    return TokenKind::DefKeyword;
                if (EqualsKeyword<"and">(start))
                    return TokenKind::AndKeyword;
                if (EqualsKeyword<"ref">(start))
                    return TokenKind::RefKeyword;
                if (EqualsKeyword<"new">(start))
                    return TokenKind::NewKeyword;
                break;
            }
            case 4:
            {
                if (EqualsKeyword<"enum">(start))
                    return TokenKind::EnumKeyword;
                if (EqualsKeyword<"type">(start))
                    return TokenKind::TypeKeyword;
                if (EqualsKeyword<"else">(start))
                    return TokenKind::ElseKeyword;
                if (EqualsKeyword<"skip">(start))
                    return TokenKind::SkipKeyword;
                if (EqualsKeyword<"true">(start))
                    return TokenKind::TrueKeyword;
                if (EqualsKeyword<"init">(start))
                    return TokenKind::InitKeyword;
                break;
            }
            case 5:
            {
                if (EqualsKeyword<"while">(start))
                    return TokenKind::WhileKeyword;
                if (EqualsKeyword<"break">(start))
                    return TokenKind::BreakKeyword;
                if (EqualsKeyword<"false">(start))
                    return TokenKind::FalseKeyword;
                break;
            }
            case 6:
            {
                if (EqualsKeyword<"return">(start))
                    return TokenKind::ReturnKeyword;
                break;
            }
        }

        return TokenKind::Identifier;
    }

    static void LexIdentifier(TokenBuffer& tokenBuffer, std::string_view source, const char*& currentIndex) noexcept
    {
        const char* start = currentIndex;
        currentIndex += LexerScan::identifierRunLength(currentIndex);

        const auto startIndex = static_cast<i32>(start - source.data());
        const auto maybeKeywordKind = IdentifierKind(start, static_cast<i32>(currentIndex - start));

        AddTokenFromStart(tokenBuffer, source, currentIndex, maybeKeywordKind, startIndex);
    };

    static void LexNumber(TokenBuffer& tokenBuffer, std::string_view source, const char*& currentIndex) noexcept
    {
        const auto startIndex = static_cast<i32>(currentIndex - source.data());

        // checking the char after underscore is safe, it is EOF terminator in the worst case
        while (true)
        {
            currentIndex += LexerScan::Scalar::digitRunLength(currentIndex);
            if (*currentIndex == '_' && currentIndex[1] != '.')
            {
                currentIndex++;
                continue;
            }

            break;
        }

        if (*currentIndex == '.' && LexerScan::isNumber(currentIndex[1]))
        {
            currentIndex++;

            while (true)
            {
                currentIndex += LexerScan::Scalar::digitRunLength(currentIndex);
                if (*currentIndex == '_')
                {
                    currentIndex++;
                    continue;
                }

                break;
            }
        }

        AddTokenFromStart(tokenBuffer, source, currentIndex, TokenKind::Number, startIndex);
    };

    static void LexString(
        TokenBuffer& tokenBuffer,
        DiagnosticsBag& diagnostics,
        const SourceTextSharedPtr& sourceText,
        std::string_view source,
        const char*& currentIndex) noexcept
    {
        const auto startIndex = static_cast<i32>(currentIndex - source.data());

        // Consume opening quotation mark
        currentIndex++;
        while (true)
        {
            currentIndex += LexerScan::findAnyOf<'"', '\\', '\r', '\n', '\0'>(currentIndex);
            if (*currentIndex == '\\')
            {
                // backslash consumes the next char unless we are at EOF
                if (currentIndex[1] != '\0')
                {
                    currentIndex += 2;
                    continue;
                }

                // a trailing backslash is part of the unterminated string
                currentIndex++;
            }

            break;
        }

        if (*currentIndex == '\"')
        {
            // Consume closing quotation mark
            currentIndex++;
            AddTokenFromStart(tokenBuffer, source, currentIndex, TokenKind::String, startIndex);
        }
        else
        {
            AddTokenFromStart(tokenBuffer, source, currentIndex, TokenKind::Error, startIndex);
            const auto& lastToken = tokenBuffer.getLastToken();
            const auto& location = tokenBuffer.getSourceLocation(lastToken);
            diagnostics.addUnterminatedStringError(sourceText, location);
        }
    };

    [[nodiscard]] TokenBuffer lex(const SourceTextSharedPtr& sourceText, DiagnosticsBag& diagnostics, u16 fileId) noexcept
    {
        CARACAL_ZONE_NAMED("lex");

        const auto source = std::string_view(sourceText->text);
        const char* currentIndex = source.data();

        // skip optional BOM at start of file
        if (source.length() >= 3 &&
            static_cast<u8>(source[0]) == 0xEF &&
            static_cast<u8>(source[1]) == 0xBB &&
            static_cast<u8>(source[2]) == 0xBF)
        {
            currentIndex += 3;
        }

        // first token's trivia starts where lexing starts
        const auto firstTokenStart = static_cast<i32>(currentIndex - source.data());
        TokenBuffer tokenBuffer{ sourceText, fileId, firstTokenStart };

        while (true)
        {
            const auto current = *currentIndex;

            // eating all whitespaces before the switch is slightly faster
            if (LexerScan::isWhitespace(current))
            {
                if (current == ' ' && !LexerScan::isWhitespace(currentIndex[1]))
                {
                    currentIndex++;
                    continue;
                }

                currentIndex += LexerScan::whitespaceRunLength(currentIndex);
                continue;
            }

            switch (current)
            {
                case '\0':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::EndOfFile, 0);
                    tokenBuffer.finalizePadding();
                    return tokenBuffer;
                }
                case '+':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Plus, 1);
                    break;
                }
                case '-':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Minus, 1);
                    break;
                }
                case '*':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Star, 1);
                    break;
                }
                case '%':
                {
                    const auto nextChar = currentIndex[1];
                    if (nextChar == '+')
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::PercentPlus, 2);
                        break;
                    }
                    if (nextChar == '-')
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::PercentMinus, 2);
                        break;
                    }
                    if (nextChar == '*')
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::PercentStar, 2);
                        break;
                    }

                    // % is not a valid operator
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Unknown, 1);
                    const auto& lastToken = tokenBuffer.getLastToken();
                    const auto& location = tokenBuffer.getSourceLocation(lastToken);
                    diagnostics.addIllegalCharacterError(sourceText, location);
                    break;
                }
                case '/':
                {
                    const auto nextChar = currentIndex[1];
                    if (nextChar == '/')
                    {
                        // Consume comment until end of line
                        currentIndex += 2;
                        currentIndex += LexerScan::findAnyOf<'\n', '\0'>(currentIndex);
                        break;
                    }
                    else if (nextChar == '*')
                    {
                        // Consume multi-line comment until closing */
                        currentIndex += 2;
                        currentIndex += LexerScan::findStarSlashOrEOF(currentIndex);
                        if (*currentIndex != '\0')
                        {
                            currentIndex += 2;
                        }
                        break;
                    }
                    else
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Slash, 1);
                        break;
                    }
                }
                case '.':
                {
                    if (currentIndex[1] == '.' && currentIndex[2] == '.')
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Ellipsis, 3);
                    }
                    else
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Dot, 1);
                    }
                    break;
                }
                case ':':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Colon, 1);
                    break;
                }
                case ';':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Semicolon, 1);
                    break;
                }
                case '\'':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::SingleQuote, 1);
                    break;
                }
                case '#':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Hash, 1);
                    break;
                }
                case ',':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Comma, 1);
                    break;
                }
                case '=':
                {
                    if (currentIndex[1] == '=')
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::EqualEqual, 2);
                    }
                    else
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Equal, 1);
                    }
                    break;
                }
                case '!':
                {
                    if (currentIndex[1] == '=')
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::BangEqual, 2);
                    }
                    else
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Bang, 1);
                    }
                    break;
                }
                case '<':
                {
                    if (currentIndex[1] == '=')
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::LessThanEqual, 2);
                    }
                    else
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::LessThan, 1);
                    }
                    break;
                }
                case '>':
                {
                    if (currentIndex[1] == '=')
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::GreaterThanEqual, 2);
                    }
                    else
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::GreaterThan, 1);
                    }
                    break;
                }
                case '(':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::OpenParenthesis, 1);
                    break;
                }
                case ')':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::CloseParenthesis, 1);
                    break;
                }
                case '{':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::OpenBrace, 1);
                    break;
                }
                case '}':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::CloseBrace, 1);
                    break;
                }
                case '[':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::OpenBracket, 1);
                    break;
                }
                case ']':
                {
                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::CloseBracket, 1);
                    break;
                }
                case '\"':
                {
                    LexString(tokenBuffer, diagnostics, sourceText, source, currentIndex);
                    break;
                }
                default:
                {
                    if (current == '_' && !LexerScan::isUnderscoreOrLetterOrNumber(currentIndex[1]))
                    {
                        AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Underscore, 1);
                        break;
                    }
                    else if (LexerScan::isUnderscoreOrLetter(current))
                    {
                        LexIdentifier(tokenBuffer, source, currentIndex);
                        break;
                    }
                    else if (LexerScan::isNumber(current))
                    {
                        LexNumber(tokenBuffer, source, currentIndex);
                        break;
                    }

                    AddTokenKindAndAdvance(tokenBuffer, source, currentIndex, TokenKind::Unknown, 1);
                    const auto& lastToken = tokenBuffer.getLastToken();
                    const auto& location = tokenBuffer.getSourceLocation(lastToken);
                    diagnostics.addIllegalCharacterError(sourceText, location);
                    break;
                }
            }
        }
    }
}
