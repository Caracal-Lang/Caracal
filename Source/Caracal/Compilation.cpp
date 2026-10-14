#include <Caracal/Compilation.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Optimization/ConstantFolder.h>
#include <Caracal/Optimization/DeadCodeElimination.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/TypeChecker.h>
#include <Caracal/Semantic/TypeCheckerOld.h>
#include <Caracal/Syntax/Parser.h>
#include <Caracal/Syntax/ParserOld.h>
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
        const auto tokens = lex(unit.source, diagnostics, unit.fileId);
        if (diagnostics.errorCount() != errorCount)
        {
            // skip parsing when lexing reported errors
            return;
        }

        auto parseTree = parseOld(tokens, diagnostics);
        compilationContext.storeParseTree(unit, std::move(parseTree));

        // TODO remove the old parser once we use the nodebuffer for the whole pipeline
        DiagnosticsBag flatParseDiagnostics{};
        unit.nodeBuffer = parse(tokens, flatParseDiagnostics);
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

    void loadPrelude(CompilationContext& compilationContext, const std::vector<std::string>& preludeSources)
    {
        if (compilationContext.preludeWasLoaded())
        {
            std::cerr << "error: the prelude was loaded twice into one compilation\n";
            std::abort();
        }
        compilationContext.markPreludeLoaded();

        DiagnosticsBag preludeDiagnostics{};
        const auto firstPreludeUnit = compilationContext.units().size();
        const std::filesystem::path preludePath{ "<prelude>" };
        for (const auto& preludeSource : preludeSources)
        {
            compilationContext.addSource(preludeSource, preludePath, UnitOrigin::Prelude);
        }

        auto& units = compilationContext.units();
        for (auto index = firstPreludeUnit; index < units.size(); index++)
        {
            LexAndParseUnit(compilationContext, units[index], preludeDiagnostics);
        }

        if (!preludeDiagnostics.hasErrors() && units.size() > firstPreludeUnit)
        {
            Module preludeModule{};
            constexpr bool isPreludePass = true;
            TypeChecker preludeChecker{ compilationContext, isPreludePass, preludeDiagnostics, preludeModule };
            if (!preludeChecker.typeCheck() && preludeDiagnostics.diagnostics().empty())
            {
                std::cerr << "error: the prelude type checks but the compiler could not lower it\n";
                std::abort();
            }
        }

        // prelude is core library code, we will abort if there are errors
        if (!preludeDiagnostics.diagnostics().empty())
        {
            std::cerr << "error: the prelude failed to compile\n";
            writeDiagnostics(std::cerr, preludeDiagnostics);
            std::abort();
        }

        compilationContext.semanticContext().finalizePrelude(!preludeSources.empty());
    }

    void loadPrelude(CompilationContext& compilationContext, const std::filesystem::path& preludeDirectory)
    {
        loadPrelude(compilationContext, collectPreludeSources(preludeDirectory));
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

    bool typeCheck(CompilationContext& compilationContext, DiagnosticsBag& diagnostics, Module& irModule) noexcept
    {
        CARACAL_ZONE_NAMED("typeCheck");
        TypeChecker typeChecker{ compilationContext, false, diagnostics, irModule };
        return typeChecker.typeCheck();
    }

    void optimize(Module& irModule)
    {
        CARACAL_ZONE_NAMED("optimize");
        eliminateDeadCode(irModule);
    }

    ParseTreeUPtr parseOld(const TokenBuffer& tokens, DiagnosticsBag& diagnostics) noexcept
    {
        CARACAL_ZONE_NAMED("parseOld");
        ParserOld parser{ tokens, diagnostics };
        return parser.parse();
    }

    bool typeCheckOld(CompilationContext& compilationContext, DiagnosticsBag& diagnostics, bool isPreludePass) noexcept
    {
        CARACAL_ZONE_NAMED("typeCheckOld");
        TypeCheckerOld typeChecker{ compilationContext, isPreludePass, diagnostics };
        return typeChecker.typeCheck();
    }

    bool optimizeOld(const CompilationContext& compilationContext, DiagnosticsBag& diagnostics, bool isPreludePass)
    {
        CARACAL_ZONE_NAMED("optimizeOld");
        return foldConstants(compilationContext, diagnostics, isPreludePass);
    }
}
