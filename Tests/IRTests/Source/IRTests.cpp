#include <CaraTest.h>

#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Compiler.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Text/File.h>
#include <Caracal/IR/IRLowerer.h>
#include <Caracal/IR/Module.h>
#include <Caracal/Optimization/DeadCodeElimination.h>

#include <algorithm>

#include <iostream>

static void FileTests(
    const std::string& /*fileName*/,
    const std::filesystem::path& inputFilePath,
    const std::filesystem::path& outputFilePath)
{
    if (!std::filesystem::exists(inputFilePath))
        CaraTest::fail();
    if (!std::filesystem::exists(outputFilePath))
        CaraTest::skip();

    const auto input = Caracal::File::readText(inputFilePath);
    if (!input.has_value())
        CaraTest::fail();

    Caracal::DiagnosticsBag diagnostics;
    Caracal::CompilationContext compilationContext;
    compilationContext.addSource(std::move(input.value()), std::filesystem::path{}, Caracal::UnitOrigin::User);
    Caracal::lexAndParse(compilationContext, diagnostics);

    const auto& parseTrees = compilationContext.parseTrees();
    if (parseTrees.empty())
    {
        Caracal::writeDiagnostics(std::cout, diagnostics);
        CaraTest::fail();
    }
    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
    Caracal::loadPrelude(compilationContext, preludePath);

    auto wasSuccessful = Caracal::typeCheckOld(compilationContext, diagnostics);
    if (wasSuccessful)
    {
        wasSuccessful = Caracal::optimizeOld(compilationContext, diagnostics);
    }

    if (!wasSuccessful)
    {
        std::cout << "Type checking failed!";
        CaraTest::fail();
    }

    const auto startTime = std::chrono::high_resolution_clock::now();
    const auto [irGenerated, output] = Caracal::generateIRText(compilationContext.semanticContext());
    const auto endTime = std::chrono::high_resolution_clock::now();

    std::cout << "      generateIRText(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CaraTest::isTrue(irGenerated);
    CaraTest::isTrue(!diagnostics.hasErrors());
    const auto outputDirectory = std::filesystem::path(outputFilePath).parent_path();
    if (!std::filesystem::exists(outputDirectory))
    {
        std::filesystem::create_directories(outputDirectory);
    }

    CaraTest::equalsFile(std::filesystem::path(outputFilePath), output);
}

static std::vector<std::tuple<std::string, std::filesystem::path, std::filesystem::path>> FileTests_Data()
{
    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto currentDirectory = currentFilePath.parent_path();
    const auto testDataDir = currentDirectory / "../../TestData";
    const auto inputDir = testDataDir / "Input";
    const auto absolutePath = std::filesystem::absolute(inputDir);

    std::vector<std::tuple<std::string, std::filesystem::path, std::filesystem::path>> data{};

    if (!std::filesystem::exists(absolutePath))
    {
        return data;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(absolutePath))
    {
        if (entry.is_regular_file() 
            && entry.path().extension() == ".cara" 
            && entry.path().filename() != "oneMilLines.cara"
            && entry.path().filename() != "oneMilLinesLongIdents.cara")
        {
            const auto& inputFilePath = entry.path();
            const auto inputDirName = inputFilePath.parent_path().filename();

            const auto outputParseDirectoryPath = testDataDir / "OutputIR";

            const auto fileName = inputFilePath.stem().string();
            const auto outputFileName = fileName + ".txt";
            const auto outputParsePath = std::filesystem::absolute(outputParseDirectoryPath / inputDirName / outputFileName);

            const auto testName = inputDirName.string() + "/" + fileName;
            data.push_back(std::make_tuple(testName, inputFilePath, outputParsePath));
        }
    }

    return data;
}

static void PrintBenchmarkLine(const char* label, std::vector<double>& milliseconds, size_t byteCount)
{
    std::sort(milliseconds.begin(), milliseconds.end());
    const auto megabytes = static_cast<double>(byteCount) / (1024.0 * 1024.0);
    const auto median = milliseconds[milliseconds.size() / 2];
    std::cout << "      " << label << ": min " << milliseconds.front() << " ms, median " << median
        << " ms, " << (megabytes / (milliseconds.front() / 1000.0)) << " MB/s" << std::endl;
}

static void BenchmarkLowerFileOld(const std::filesystem::path& absolutePath, const std::filesystem::path& preludePath, const char* label)
{
    const auto data = Caracal::File::readText(absolutePath);
    if (!data.has_value())
    {
        CaraTest::fail();
    }
    const auto byteCount = data.value().size();

    constexpr int RunCount = 5;
    std::vector<double> milliseconds;
    milliseconds.reserve(RunCount);
    for (int run = 0; run < RunCount; run++)
    {
        Caracal::CompilationContext context;
        Caracal::loadPrelude(context, preludePath);

        context.addSource(data.value(), absolutePath, Caracal::UnitOrigin::User);
        Caracal::DiagnosticsBag diagnostics;
        Caracal::lexAndParse(context, diagnostics);
        if (!Caracal::typeCheckOld(context, diagnostics) || !diagnostics.diagnostics().empty())
        {
            CaraTest::fail();
        }

        const auto startTime = std::chrono::high_resolution_clock::now();
        if (!Caracal::optimizeOld(context, diagnostics))
        {
            CaraTest::fail();
        }
        Caracal::Module module{};
        Caracal::IRLowerer lowerer{ context.semanticContext() };
        if (!lowerer.lower(module))
        {
            CaraTest::fail();
        }
        Caracal::eliminateDeadCode(module);
        const auto endTime = std::chrono::high_resolution_clock::now();

        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        milliseconds.push_back(static_cast<double>(nanoseconds) / 1000000.0);
    }

    PrintBenchmarkLine(label, milliseconds, byteCount);
}

static void OneMillionLinesLowerTimeOld()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    const auto preludePath = currentFilePath.parent_path() / "../../../Prelude";
    BenchmarkLowerFileOld(std::filesystem::absolute(inputDirectory / "oneMilLines.cara"), preludePath, "oneMilLowerShort");
    BenchmarkLowerFileOld(std::filesystem::absolute(inputDirectory / "oneMilLinesLongIdents.cara"), preludePath, "oneMilLowerLong ");
}

static auto tests =
{
    CaraTest::addTest("FileTests", FileTests, FileTests_Data),
    CaraTest::addTest("OneMillionLinesLowerTimeOld", OneMillionLinesLowerTimeOld),
};
