#pragma once

#include <Caracal/Defines.h>
#include <Caracal/API.h>
#include <Caracal/Syntax/Token.h>
#include <Caracal/Text/SourceText.h>
#include <Caracal/Text/SourceLocation.h>

#include <memory>

namespace Caracal
{
    class CARACAL_API TokenBuffer
    {
    public:
        explicit TokenBuffer(const SourceTextSharedPtr& source, u16 fileId = 0, i32 firstTokenStart = 0);

        TokenBuffer(const TokenBuffer& other);
        TokenBuffer& operator=(const TokenBuffer& other);
        TokenBuffer(TokenBuffer&&) noexcept = default;
        TokenBuffer& operator=(TokenBuffer&&) noexcept = default;

        inline void addToken(TokenKind kind, const SourceLocation& sourceLocation) noexcept
        {
            m_kinds[m_count] = kind;
            m_sourceLocations[m_count] = sourceLocation;
            m_count++;
        }

        // write two eof tokens so we dont need a bounds check for peek()
        inline void finalizePadding() noexcept
        {
            m_kinds[m_count] = TokenKind::EndOfFile;
            m_kinds[m_count + 1] = TokenKind::EndOfFile;
        }

        [[nodiscard]] const SourceTextSharedPtr& source() const noexcept;
        [[nodiscard]] inline u16 fileId() const noexcept { return m_fileId; }
        [[nodiscard]] inline i32 size() const noexcept
        {
            return m_count;
        }

        [[nodiscard]] inline Token getToken(i32 position) const noexcept
        {
            return { .kind = m_kinds[position], .fileId = m_fileId, .index = position };
        }
        [[nodiscard]] Token getLastToken() const noexcept;
        [[nodiscard]] std::string_view getLexeme(const Token& token) const noexcept;
        [[nodiscard]] const SourceLocation& getSourceLocation(const Token& token) const noexcept;

        // having the body in the header made parsing slightly faster
        [[nodiscard]] inline std::string_view getTrivia(const Token& token) const noexcept
        {
            if (token.index < 0 || token.index >= m_count)
                return {};

            auto start = m_firstTokenStart;
            if (token.index > 0)
            {
                start = m_sourceLocations[token.index - 1].endIndex;
            }

            const auto end = m_sourceLocations[token.index].startIndex;
            if (end <= start)
                return {};

            return std::string_view(m_source->text).substr(start, end - start);
        }

        // having the body in the header made parsing slightly faster
        [[nodiscard]] inline bool hasLineBreakInLeadingTrivia(const Token& token) const noexcept
        {
            const auto trivia = getTrivia(token);
            for (const auto character : trivia)
            {
                if (character == '\n' || character == '\r')
                {
                    return true;
                }
            }

            return false;
        }

    private:
        SourceTextSharedPtr m_source;
        u16 m_fileId;
        i32 m_firstTokenStart;
        i32 m_count = 0;
        std::unique_ptr<TokenKind[]> m_kinds;

        // an array of SourceLocations would default initialize its members so this is faster
        // the measurements was around +50% lexing time with the array of SourceLocations
        std::unique_ptr<u8[]> m_locationStorage;
        SourceLocation* m_sourceLocations = nullptr;
    };
}
