#include <CaraTest.h>
#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Debug/ParseTreePrinter.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/IR/Module.h>
#include <Caracal/Semantic/TypeChecker.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Text/File.h>
#include <Caracal/Text/SourceText.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <optional>
#include <vector>

static std::filesystem::path InputDirectory()
{
    return std::filesystem::absolute(std::filesystem::path(__FILE__).parent_path() / "../../TestData/Input");
}

static std::filesystem::path PreludeDirectory()
{
    return std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
}

static void PrintBenchmarkLine(const char* label, std::vector<double>& milliseconds, size_t byteCount)
{
    std::sort(milliseconds.begin(), milliseconds.end());
    const auto minimum = milliseconds.front();
    const auto median = milliseconds[milliseconds.size() / 2];
    const auto megabytesPerSecond = (static_cast<double>(byteCount) / (1024.0 * 1024.0)) / (minimum / 1000.0);
    std::cout 
        << "      " 
        << label 
        << ": min " 
        << minimum 
        << " ms, median " 
        << median
        << " ms, " 
        << megabytesPerSecond 
        << " MB/s" << std::endl;
}

[[nodiscard]] static double MillisecondsBetween(std::chrono::high_resolution_clock::time_point start, std::chrono::high_resolution_clock::time_point end)
{
    return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()) / 1000000.0;
}

[[nodiscard]] static std::string ReadBenchmarkInput(const std::filesystem::path& absolutePath)
{
    if (!std::filesystem::exists(absolutePath))
    {
        CaraTest::fail();// ("benchmark input missing");
    }

    const auto data = Caracal::File::readText(absolutePath);
    if (!data.has_value())
    {
        CaraTest::fail();// ("could not read benchmark input");
    }

    return data.value();
}

[[nodiscard]] static Caracal::TokenBuffer LexBenchmarkFile(const std::filesystem::path& absolutePath, size_t& byteCount)
{
    const auto data = ReadBenchmarkInput(absolutePath);
    byteCount = data.size();
    const auto source = std::make_shared<Caracal::SourceText>(data);
    Caracal::DiagnosticsBag lexDiagnostics;
    return Caracal::lex(source, lexDiagnostics);
}

static void BenchmarkLexFile(const std::filesystem::path& absolutePath, const char* label)
{
    const auto data = ReadBenchmarkInput(absolutePath);
    const auto source = std::make_shared<Caracal::SourceText>(data);

    constexpr int RunCount = 20;
    std::vector<double> milliseconds;
    milliseconds.reserve(RunCount);
    for (int run = 0; run < RunCount; run++)
    {
        Caracal::DiagnosticsBag diagnostics;
        const auto startTime = std::chrono::high_resolution_clock::now();
        const auto tokens = Caracal::lex(source, diagnostics);
        const auto endTime = std::chrono::high_resolution_clock::now();

        if (!diagnostics.diagnostics().empty())
        {
            CaraTest::fail();// ("benchmark input must lex without diagnostics");
        }

        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        milliseconds.push_back(static_cast<double>(nanoseconds) / 1000000.0);
    }

    PrintBenchmarkLine(label, milliseconds, data.size());
}

static void BenchmarkParseFile(const std::filesystem::path& absolutePath, const char* label)
{
    size_t byteCount = 0;
    const auto tokens = LexBenchmarkFile(absolutePath, byteCount);

    constexpr int RunCount = 10;
    std::vector<double> milliseconds;
    milliseconds.reserve(RunCount);
    for (int run = 0; run < RunCount; run++)
    {
        Caracal::DiagnosticsBag diagnostics;
        const auto startTime = std::chrono::high_resolution_clock::now();
        const auto nodeBuffer = Caracal::parse(tokens, diagnostics);
        const auto endTime = std::chrono::high_resolution_clock::now();

        if (!diagnostics.diagnostics().empty())
        {
            CaraTest::fail();// ("benchmark input must parse without diagnostics");
        }

        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        milliseconds.push_back(static_cast<double>(nanoseconds) / 1000000.0);
    }

    PrintBenchmarkLine(label, milliseconds, byteCount);
}

static void BenchmarkTypeCheckFile(const std::filesystem::path& absolutePath, const std::filesystem::path& preludePath, const char* label)
{
    const auto data = ReadBenchmarkInput(absolutePath);
    const auto byteCount = data.size();

    constexpr int RunCount = 5;
    std::vector<double> checkMilliseconds;
    std::vector<double> eliminationMilliseconds;
    std::vector<double> destroyMilliseconds;
    for (int run = 0; run < RunCount; run++)
    {
        Caracal::CompilationContext context;
        Caracal::loadPrelude(context, preludePath);

        context.addSource(data, absolutePath, Caracal::UnitOrigin::User);
        Caracal::DiagnosticsBag diagnostics;
        Caracal::lexAndParse(context, diagnostics);
        if (!diagnostics.diagnostics().empty())
        {
            CaraTest::fail();// ("benchmark input must parse without diagnostics");
        }

        // type checking includes the IR emission
        std::optional<Caracal::Module> irModule{ std::in_place };
        auto checkedCleanly = false;
        const auto checkStart = std::chrono::high_resolution_clock::now();
        auto checkEnd = checkStart;
        {
            Caracal::TypeChecker checker{ context, false, diagnostics, irModule.value() };
            checkedCleanly = checker.typeCheck();
            checkEnd = std::chrono::high_resolution_clock::now();
        }

        if (!checkedCleanly || !diagnostics.diagnostics().empty())
        {
            std::cout << Caracal::formatDiagnostics(diagnostics, {}) << std::endl;
            CaraTest::fail();// ("benchmark input must check without diagnostics");
        }

        const auto eliminationStart = std::chrono::high_resolution_clock::now();
        Caracal::optimize(irModule.value());
        const auto eliminationEnd = std::chrono::high_resolution_clock::now();

        const auto destroyStart = std::chrono::high_resolution_clock::now();
        irModule.reset();
        const auto destroyEnd = std::chrono::high_resolution_clock::now();

        checkMilliseconds.push_back(MillisecondsBetween(checkStart, checkEnd));
        eliminationMilliseconds.push_back(MillisecondsBetween(eliminationStart, eliminationEnd));
        destroyMilliseconds.push_back(MillisecondsBetween(destroyStart, destroyEnd));
    }

    PrintBenchmarkLine((std::string("typecheck ") + label).c_str(), checkMilliseconds, byteCount);
    PrintBenchmarkLine((std::string("DCE       ") + label).c_str(), eliminationMilliseconds, byteCount);
    PrintBenchmarkLine((std::string("destroy   ") + label).c_str(), destroyMilliseconds, byteCount);
}

static void OneMillionLinesLexTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto inputDirectory = InputDirectory();
    BenchmarkLexFile(inputDirectory / "oneMilLinesOld.txt", "oneMilLinesOld  ");
    BenchmarkLexFile(inputDirectory / "oneMilLines.cara", "oneMilLinesShort");
    BenchmarkLexFile(inputDirectory / "oneMilLinesLongIdents.cara", "oneMilLinesLong ");
}

static void OneMillionLinesParseTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto inputDirectory = InputDirectory();
    BenchmarkParseFile(inputDirectory / "oneMilLines.cara", "oneMilLinesShort");
    BenchmarkParseFile(inputDirectory / "oneMilLinesLongIdents.cara", "oneMilLinesLong ");
}

static void OneMillionLinesPrintTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    size_t byteCount = 0;
    const auto tokens = LexBenchmarkFile(InputDirectory() / "oneMilLines.cara", byteCount);
    Caracal::DiagnosticsBag diagnostics;
    const auto nodeBuffer = Caracal::parse(tokens, diagnostics);
    CaraTest::isTrue(diagnostics.diagnostics().empty());

    Caracal::CompilationContext preludeContext;
    Caracal::loadPrelude(preludeContext, PreludeDirectory());
    auto* module = &preludeContext.semanticContext();

    constexpr int RunCount = 3;
    std::vector<double> milliseconds;
    milliseconds.reserve(RunCount);
    size_t outputBytes = 0;
    for (int run = 0; run < RunCount; run++)
    {
        const auto startTime = std::chrono::high_resolution_clock::now();
        const auto output = Caracal::prettyPrint(nodeBuffer, tokens, module);
        const auto endTime = std::chrono::high_resolution_clock::now();
        outputBytes = output.size();
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        milliseconds.push_back(static_cast<double>(nanoseconds) / 1000000.0);
    }

    std::cout << "      print output " << (outputBytes / (1024.0 * 1024.0)) << " MB" << std::endl;
    PrintBenchmarkLine("oneMilLinesShort", milliseconds, outputBytes);
}

static void OneMillionLinesTypeCheckTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto inputDirectory = InputDirectory();
    const auto preludePath = PreludeDirectory();
    BenchmarkTypeCheckFile(inputDirectory / "oneMilLines.cara", preludePath, "oneMilLinesShort");
    BenchmarkTypeCheckFile(inputDirectory / "oneMilLinesLongIdents.cara", preludePath, "oneMilLinesLong ");
}

static auto tests =
{
    CaraTest::addTest("OneMillionLinesLexTime", OneMillionLinesLexTime),
    CaraTest::addTest("OneMillionLinesParseTime", OneMillionLinesParseTime),
    CaraTest::addTest("OneMillionLinesTypeCheckTime", OneMillionLinesTypeCheckTime),
    CaraTest::addTest("OneMillionLinesPrintTime", OneMillionLinesPrintTime),
};
