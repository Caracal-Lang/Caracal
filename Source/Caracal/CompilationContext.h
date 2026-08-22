#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/ParseTree.h>
#include <Caracal/Syntax/Token.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Text/SourceLocation.h>
#include <Caracal/Text/SourceText.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Caracal
{
    enum class UnitOrigin
    {
        Prelude,
        Core,
        User,
    };

    struct CompilationUnit
    {
        UnitOrigin origin = UnitOrigin::User;
        u16 fileId = 0;
        bool wasProcessed = false;
        SourceTextSharedPtr source;
        // TokenBuffer tokenBuffer;
        NodeBuffer nodeBuffer;

        // TODO remove this once we switch over to the new parser
        ParseTree* parseTree = nullptr;
    };

    class CARACAL_API CompilationContext
    {
    public:
        explicit CompilationContext(TypeCheckerOptions options = {});
        CARACAL_DELETE_COPY_DEFAULT_MOVE(CompilationContext)

        void addSource(
            std::string content,
            const std::filesystem::path& path,
            UnitOrigin origin);

        void storeParseTree(CompilationUnit& unit, ParseTreeUPtr parseTree);

        [[nodiscard]] const TypeCheckerOptions& options() const noexcept;
        [[nodiscard]] SemanticContext& semanticContext() noexcept;
        [[nodiscard]] const SemanticContext& semanticContext() const noexcept;

        [[nodiscard]] std::vector<CompilationUnit>& units() noexcept;
        [[nodiscard]] const std::vector<CompilationUnit>& units() const noexcept;

        [[nodiscard]] bool preludeWasLoaded() const noexcept;
        void markPreludeLoaded() noexcept;

        [[nodiscard]] const std::vector<ParseTreeUPtr>& parseTrees() const noexcept;
        [[nodiscard]] const std::vector<ParseTreeUPtr>& preludeParseTrees() const noexcept;
        [[nodiscard]] const std::vector<ParseTreeUPtr>& parseTreesFor(bool isPreludePass) const noexcept;

        [[nodiscard]] const TokenBuffer& tokenBuffer(const Token& token) const noexcept;
        [[nodiscard]] std::string_view getLexeme(const Token& token) const noexcept;
        [[nodiscard]] const SourceLocation& sourceLocation(const Token& token) const noexcept;
        [[nodiscard]] const SourceTextSharedPtr& source(const Token& token) const noexcept;

    private:
        std::vector<CompilationUnit> m_units;
        std::vector<ParseTreeUPtr> m_parseTrees;
        std::vector<ParseTreeUPtr> m_preludeParseTrees;
        TypeCheckerOptions m_options;
        bool m_preludeWasLoaded = false;

        // declared last so it is destroyed first, 
        // the semantic definitions hold pointers into the parse trees above
        SemanticContext m_semanticContext;
    };
}
