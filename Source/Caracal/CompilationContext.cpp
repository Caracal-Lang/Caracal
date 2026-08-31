#include <Caracal/CompilationContext.h>

#include <cstdlib>
#include <iostream>
#include <limits>

namespace Caracal
{
    CompilationContext::CompilationContext(TypeCheckerOptions options)
        : m_options{ std::move(options) }
        , m_semanticContext{ SemanticContext::WithBuiltins() }
    {
    }

    void CompilationContext::addSource(
        std::string content,
        const std::filesystem::path& path,
        UnitOrigin origin)
    {
        if (m_units.size() > std::numeric_limits<u16>::max())
        {
            std::cerr << "error: too many compilation units, the token fileId is 16 bits wide\n";
            std::abort();
        }

        m_units.push_back({
            .origin = origin,
            .fileId = static_cast<u16>(m_units.size()),
            .source = std::make_shared<SourceText>(std::move(content), path),
        });
    }

    void CompilationContext::storeParseTree(CompilationUnit& unit, ParseTreeUPtr parseTree)
    {
        unit.parseTree = parseTree.get();
        if (unit.origin == UnitOrigin::Prelude)
        {
            m_preludeParseTrees.push_back(std::move(parseTree));
        }
        else
        {
            m_parseTrees.push_back(std::move(parseTree));
        }
    }

    const TypeCheckerOptions& CompilationContext::options() const noexcept
    {
        return m_options;
    }

    SemanticContext& CompilationContext::semanticContext() noexcept
    {
        return m_semanticContext;
    }

    const SemanticContext& CompilationContext::semanticContext() const noexcept
    {
        return m_semanticContext;
    }

    std::vector<CompilationUnit>& CompilationContext::units() noexcept
    {
        return m_units;
    }

    const std::vector<CompilationUnit>& CompilationContext::units() const noexcept
    {
        return m_units;
    }

    bool CompilationContext::preludeWasLoaded() const noexcept
    {
        return m_preludeWasLoaded;
    }

    void CompilationContext::markPreludeLoaded() noexcept
    {
        m_preludeWasLoaded = true;
    }

    const std::vector<ParseTreeUPtr>& CompilationContext::parseTrees() const noexcept
    {
        return m_parseTrees;
    }

    const std::vector<ParseTreeUPtr>& CompilationContext::preludeParseTrees() const noexcept
    {
        return m_preludeParseTrees;
    }

    const std::vector<ParseTreeUPtr>& CompilationContext::parseTreesFor(bool isPreludePass) const noexcept
    {
        if (isPreludePass)
        {
            return m_preludeParseTrees;
        }

        return m_parseTrees;
    }

    const NodeBuffer& CompilationContext::nodeBuffer(u16 fileId) const noexcept
    {
        return m_units[fileId].nodeBuffer;
    }
    const TokenBuffer& CompilationContext::tokenBuffer(const Token& token) const noexcept
    {
        return m_units[token.fileId].parseTree->tokens();
    }

    const TokenBuffer& CompilationContext::tokenBuffer(u16 fileId) const noexcept
    {
        return m_units[fileId].parseTree->tokens();
    }

    std::string_view CompilationContext::getLexeme(const Token& token) const noexcept
    {
        return tokenBuffer(token).getLexeme(token);
    }

    std::string_view CompilationContext::getLexeme(u16 fileId, u32 tokenIndex) const noexcept
    {
        const auto& tokens = tokenBuffer(fileId);
        return tokens.getLexeme(tokens.getToken(static_cast<i32>(tokenIndex)));
    }

    const SourceLocation& CompilationContext::sourceLocation(const Token& token) const noexcept
    {
        return tokenBuffer(token).getSourceLocation(token);
    }

    const SourceLocation& CompilationContext::sourceLocation(u16 fileId, u32 tokenIndex) const noexcept
    {
        const auto& tokens = tokenBuffer(fileId);
        return tokens.getSourceLocation(tokens.getToken(static_cast<i32>(tokenIndex)));
    }

    const SourceTextSharedPtr& CompilationContext::source(const Token& token) const noexcept
    {
        return m_units[token.fileId].source;
    }
}
