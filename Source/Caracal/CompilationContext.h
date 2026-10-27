#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/Token.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Text/SourceLocation.h>
#include <Caracal/Text/SourceText.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Caracal
{
    enum class UnitOrigin
    {
        Prelude,
        Core,
        Project,
        User,
    };

    struct CompilationUnit
    {
        UnitOrigin origin = UnitOrigin::User;
        u16 fileId = 0;
        bool wasProcessed = false;
        SourceTextSharedPtr source;
        std::optional<TokenBuffer> tokenBuffer;
        NodeBuffer nodeBuffer;
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

        [[nodiscard]] const TypeCheckerOptions& options() const noexcept;
        [[nodiscard]] SemanticContext& semanticContext() noexcept;
        [[nodiscard]] const SemanticContext& semanticContext() const noexcept;

        [[nodiscard]] std::vector<CompilationUnit>& units() noexcept;
        [[nodiscard]] const std::vector<CompilationUnit>& units() const noexcept;

        [[nodiscard]] bool preludeWasLoaded() const noexcept;
        void markPreludeLoaded() noexcept;

        [[nodiscard]] const TokenBuffer& tokenBuffer(const Token& token) const noexcept;
        [[nodiscard]] const TokenBuffer& tokenBuffer(u16 fileId) const noexcept;
        [[nodiscard]] const NodeBuffer& nodeBuffer(u16 fileId) const noexcept;
        [[nodiscard]] std::string_view getLexeme(const Token& token) const noexcept;
        [[nodiscard]] std::string_view getLexeme(u16 fileId, u32 tokenIndex) const noexcept;
        [[nodiscard]] const SourceLocation& sourceLocation(const Token& token) const noexcept;
        [[nodiscard]] const SourceLocation& sourceLocation(u16 fileId, u32 tokenIndex) const noexcept;
        [[nodiscard]] const SourceTextSharedPtr& source(const Token& token) const noexcept;

    private:
        std::vector<CompilationUnit> m_units;
        TypeCheckerOptions m_options;
        bool m_preludeWasLoaded = false;
        SemanticContext m_semanticContext;
    };
}
