#pragma once

#include <Caracal/API.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Defines.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/IR/Module.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Text/SourceText.h>

#include <filesystem>
#include <string>
#include <vector>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::vector<std::string> collectPreludeSources(const std::filesystem::path& preludeDirectory) noexcept;
    
    CARACAL_API void loadPrelude(CompilationContext& compilationContext, const std::vector<std::string>& preludeSources);
    CARACAL_API void loadPrelude(CompilationContext& compilationContext, const std::filesystem::path& preludeDirectory);

    CARACAL_API TokenBuffer lex(const SourceTextSharedPtr& source, DiagnosticsBag& diagnostics, u16 fileId = 0) noexcept;
    CARACAL_API NodeBuffer parse(const TokenBuffer& tokens, DiagnosticsBag& diagnostics) noexcept;
    CARACAL_API void lexAndParse(CompilationContext& compilationContext, DiagnosticsBag& diagnostics);
    CARACAL_API bool typeCheck(CompilationContext& compilationContext, DiagnosticsBag& diagnostics, Module& irModule) noexcept;
    CARACAL_API void optimize(Module& irModule);
}
