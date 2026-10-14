#include <CaraTest.h>
#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Debug/ParseTreePrinter.h>
#include <Caracal/Semantic/TypeChecker.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Text/File.h>
#include <algorithm>
#include <iostream>
#include <vector>

using namespace CaraTest;

static void FileTests(
    const std::string& fileName,
    const std::filesystem::path& inputFilePath,
    const std::filesystem::path& outputFilePath)
{
    if (!std::filesystem::exists(inputFilePath))
        CaraTest::fail();// ("In file missing");
    if (!std::filesystem::exists(outputFilePath))
        CaraTest::skip();// ("Out file missing");

    const auto input = Caracal::File::readText(inputFilePath);
    if (!input.has_value())
        CaraTest::fail();// ("Could not read input file");

    Caracal::DiagnosticsBag diagnostics;
    Caracal::CompilationContext compilationContext;
    compilationContext.addSource(std::move(input.value()), std::filesystem::path{}, Caracal::UnitOrigin::User);
    Caracal::lexAndParse(compilationContext, diagnostics);

    if (diagnostics.hasErrors())
    {
        Caracal::writeDiagnostics(std::cout, diagnostics);
        CaraTest::fail();
    }
    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
    Caracal::loadPrelude(compilationContext, preludePath);

    auto startTime = std::chrono::high_resolution_clock::now();
    Caracal::Module module{};
    const auto wasSuccessful = Caracal::typeCheck(compilationContext, diagnostics, module);
    auto endTime = std::chrono::high_resolution_clock::now();

    std::cout << "      Type check(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    if (!wasSuccessful) {
        CaraTest::fail();// ("Type checking failed");
    }

    const auto& unit = compilationContext.units()[0];
    const auto output = Caracal::prettyPrint(
        unit.nodeBuffer,
        compilationContext.tokenBuffer(unit.fileId),
        &compilationContext.semanticContext(),
        unit.fileId);

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

            const auto outputParseDirectoryPath = testDataDir / "OutputType";

            const auto fileName = inputFilePath.stem().string();
            const auto outputFileName = fileName + ".txt";

            const auto putputParsePath = std::filesystem::absolute(outputParseDirectoryPath / inputDirName / outputFileName);

            const auto testName = inputDirName.string() + "/" + fileName;
            data.push_back(std::make_tuple(
                testName,
                inputFilePath,
                putputParsePath));
        }
    }
    return data;
}

static void UnitSemanticsResolveCrossFileNodeIndexes()
{
    Caracal::SemanticContext context;

    const auto& first = context.createUnitSemantics(1, 8);
    const auto& second = context.createUnitSemantics(2, 4);
    CaraTest::areEqual(size_t{ 8 }, first.nodeTypes.size());
    CaraTest::areEqual(size_t{ 4 }, second.nodeTypes.size());

    // everything starts undefined
    CaraTest::isTrue(context.typeOfNode(Caracal::NodeIndex{ 1, 5 }) == Caracal::Type::Undefined());

    // a checker processing one unit resolves a reference into another unit
    context.setNodeType(Caracal::NodeIndex{ 2, 3 }, Caracal::Type::Void());
    CaraTest::isTrue(context.typeOfNode(Caracal::NodeIndex{ 2, 3 }) == Caracal::Type::Void());
    CaraTest::isTrue(context.typeOfNode(Caracal::NodeIndex{ 2, 2 }) == Caracal::Type::Undefined());

    // an unknown unit or an index past the table stays undefined
    CaraTest::isTrue(context.typeOfNode(Caracal::NodeIndex{ 9, 0 }) == Caracal::Type::Undefined());
    CaraTest::isTrue(context.typeOfNode(Caracal::NodeIndex{ 2, 400 }) == Caracal::Type::Undefined());
}

static void PrintBenchmarkLine(const char* label, std::vector<double>& milliseconds, size_t byteCount)
{
    std::sort(milliseconds.begin(), milliseconds.end());
    const auto minimum = milliseconds.front();
    const auto median = milliseconds[milliseconds.size() / 2];
    const auto megabytesPerSecond = (static_cast<double>(byteCount) / (1024.0 * 1024.0)) / (minimum / 1000.0);
    std::cout << "      " << label << ": min " << minimum << " ms, median " << median
        << " ms, " << megabytesPerSecond << " MB/s" << std::endl;
}

static void BenchmarkTypeCheckFile(const std::filesystem::path& absolutePath, const std::filesystem::path& preludePath, const char* label)
{
    const auto data = Caracal::File::readText(absolutePath);
    if (!data.has_value())
    {
        CaraTest::fail();// ("benchmark input missing");
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
        if (!diagnostics.diagnostics().empty())
        {
            CaraTest::fail();// ("benchmark input must parse without diagnostics");
        }

        const auto startTime = std::chrono::high_resolution_clock::now();
        Caracal::Module irModule{};
        Caracal::TypeChecker checker{ context, false, diagnostics, irModule };
        const auto checkedCleanly = checker.typeCheck();
        Caracal::optimize(irModule);
        const auto endTime = std::chrono::high_resolution_clock::now();

        if (!checkedCleanly || !diagnostics.diagnostics().empty())
        {
            std::cout << Caracal::formatDiagnostics(diagnostics, {}) << std::endl;
            CaraTest::fail();// ("benchmark input must check without diagnostics");
        }

        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        milliseconds.push_back(static_cast<double>(nanoseconds) / 1000000.0);
    }

    PrintBenchmarkLine(label, milliseconds, byteCount);
}

static void OneMillionLinesTypeCheckTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    const auto preludePath = currentFilePath.parent_path() / "../../../Prelude";
    BenchmarkTypeCheckFile(std::filesystem::absolute(inputDirectory / "oneMilLines.cara"), preludePath, "oneMilLinesShort");
    BenchmarkTypeCheckFile(std::filesystem::absolute(inputDirectory / "oneMilLinesLongIdents.cara"), preludePath, "oneMilLinesLong ");
}

static auto tests =
{
    CaraTest::addTest("FileTests", FileTests, FileTests_Data),
    CaraTest::addTest("UnitSemanticsResolveCrossFileNodeIndexes", UnitSemanticsResolveCrossFileNodeIndexes),
    CaraTest::addTest("OneMillionLinesTypeCheckTime", OneMillionLinesTypeCheckTime),
};
