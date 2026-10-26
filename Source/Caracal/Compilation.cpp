#include <Caracal/Compilation.h>
#include <Caracal/Optimization/DeadCodeElimination.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/TypeChecker.h>
#include <Caracal/Syntax/Parser.h>
#include <Caracal/Text/File.h>
#include <Caracal/Text/SourceEncoding.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace Caracal
{
    static void LexAndParseUnit(CompilationContext& compilationContext, CompilationUnit& unit, DiagnosticsBag& diagnostics)
    {
        unit.wasProcessed = true;
        if (!validateSourceEncoding(unit.source, diagnostics))
        {
            return;
        }

        const auto errorCount = diagnostics.errorCount();
        unit.tokenBuffer = lex(unit.source, diagnostics, unit.fileId);
        if (diagnostics.errorCount() != errorCount)
        {
            // skip parsing when lexing reported errors
            return;
        }

        unit.nodeBuffer = parse(unit.tokenBuffer.value(), diagnostics);
    }

    std::vector<std::string> collectPreludeSources(const std::filesystem::path& preludeDirectory) noexcept
    {
        std::vector<std::filesystem::path> caraFilePaths{};
        if (std::filesystem::exists(preludeDirectory) && std::filesystem::is_directory(preludeDirectory))
        {
            for (const auto& file : std::filesystem::directory_iterator(preludeDirectory))
            {
                if (file.is_regular_file() && file.path().extension() == ".cara")
                {
                    caraFilePaths.push_back(file.path());
                }
            }
        }
        std::sort(caraFilePaths.begin(), caraFilePaths.end());

        std::vector<std::string> sources{};
        for (const auto& caraFilePath : caraFilePaths)
        {
            auto content = File::readText(caraFilePath);
            if (content.has_value())
                sources.push_back(std::move(content.value()));
        }

        return sources;
    }

    bool loadPrelude(CompilationContext& compilationContext, const std::vector<std::string>& preludeSources, DiagnosticsBag& diagnostics)
    {
        if (compilationContext.preludeWasLoaded())
        {
            std::cerr << "error: the prelude was loaded twice into one compilation\n";
            std::abort();
        }
        compilationContext.markPreludeLoaded();

        const auto diagnosticCount = diagnostics.diagnostics().size();
        const auto errorCount = diagnostics.errorCount();
        const auto firstPreludeUnit = compilationContext.units().size();
        const std::filesystem::path preludePath{ "<prelude>" };
        for (const auto& preludeSource : preludeSources)
        {
            compilationContext.addSource(preludeSource, preludePath, UnitOrigin::Prelude);
        }

        auto& units = compilationContext.units();
        for (auto index = firstPreludeUnit; index < units.size(); index++)
        {
            LexAndParseUnit(compilationContext, units[index], diagnostics);
        }

        if (diagnostics.errorCount() == errorCount && units.size() > firstPreludeUnit)
        {
            Module preludeModule{};
            constexpr bool isPreludePass = true;
            TypeChecker preludeChecker{ compilationContext, isPreludePass, diagnostics, preludeModule };
            if (!preludeChecker.typeCheck() && diagnostics.diagnostics().size() == diagnosticCount)
            {
                std::cerr << "error: the prelude type checks but the compiler could not lower it\n";
                std::abort();
            }
            compilationContext.semanticContext().preludeFunctionPool().setPools(preludeModule.constants(), preludeModule.names());
        }

        // warnings or errors in the prelude stop the compilation
        if (diagnostics.diagnostics().size() != diagnosticCount)
        {
            return false;
        }

        return compilationContext.semanticContext().finalizePrelude(!preludeSources.empty(), diagnostics);
    }

    bool loadPrelude(CompilationContext& compilationContext, const std::filesystem::path& preludeDirectory, DiagnosticsBag& diagnostics)
    {
        return loadPrelude(compilationContext, collectPreludeSources(preludeDirectory), diagnostics);
    }

    NodeBuffer parse(const TokenBuffer& tokens, DiagnosticsBag& diagnostics) noexcept
    {
        CARACAL_ZONE_NAMED("parse");
        Parser parser{ tokens, diagnostics };
        return parser.parse();
    }

    void lexAndParse(CompilationContext& compilationContext, DiagnosticsBag& diagnostics)
    {
        for (auto& unit : compilationContext.units())
        {
            if (unit.wasProcessed)
            {
                continue;
            }

            LexAndParseUnit(compilationContext, unit, diagnostics);
        }
    }

    static void RecordSourceFileNames(const CompilationContext& compilationContext, Module& irModule)
    {
        for (const auto& unit : compilationContext.units())
        {
            if (unit.origin == UnitOrigin::Prelude)
            {
                continue;
            }

            irModule.setSourceFileName(unit.fileId, unit.source->filePath.generic_string());
            if (unit.origin == UnitOrigin::User && !irModule.firstUserSourceFileId().has_value())
            {
                irModule.setFirstUserSourceFileId(unit.fileId);
            }
        }
    }

    bool typeCheck(CompilationContext& compilationContext, DiagnosticsBag& diagnostics, Module& irModule) noexcept
    {
        CARACAL_ZONE_NAMED("typeCheck");
        if (compilationContext.options().recordsSourceLocations)
        {
            RecordSourceFileNames(compilationContext, irModule);
        }

        TypeChecker typeChecker{ compilationContext, false, diagnostics, irModule };
        return typeChecker.typeCheck();
    }

    void optimize(Module& irModule)
    {
        CARACAL_ZONE_NAMED("optimize");
        eliminateDeadCode(irModule);
    }
}
