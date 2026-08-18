#include <CaraTest.h>
#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Text/SourceEncoding.h>
#include <Caracal/Debug/ParseTreePrinter.h>
#include <Caracal/Text/File.h>
#include <algorithm>
#include <iostream>
#include <vector>

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
    if(!input.has_value())
        CaraTest::fail();// ("Could not read input file");

    const auto source = std::make_shared<Caracal::SourceText>(input.value());
    Caracal::DiagnosticsBag diagnostics;

    if (!Caracal::validateSourceEncoding(source, diagnostics))
        CaraTest::fail();

    const auto tokens = Caracal::lex(source, diagnostics);

    const auto startTime = std::chrono::high_resolution_clock::now();
    const auto parseTree = Caracal::parse(tokens, diagnostics);
    const auto endTime = std::chrono::high_resolution_clock::now();

    std::cout << "      parse(): " << CaraTest::stringify(endTime - startTime) << std::endl;
    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
    Caracal::CompilationContext preludeContext;
    Caracal::loadPrelude(preludeContext, preludePath);
    
    Caracal::ParseTreePrinter printer{ *parseTree, &preludeContext.semanticContext() };
    const auto output = printer.prettyPrint();

    CaraTest::isTrue(diagnostics.diagnostics().empty());
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

            const auto outputParseDirectoryPath = testDataDir / "OutputParse";

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

static void BenchmarkParseFile(const std::filesystem::path& absolutePath, const char* label)
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

    const auto source = std::make_shared<Caracal::SourceText>(data.value());
    Caracal::DiagnosticsBag lexDiagnostics;
    const auto tokens = Caracal::lex(source, lexDiagnostics);

    constexpr int RunCount = 10;
    std::vector<double> milliseconds;
    milliseconds.reserve(RunCount);
    for (int run = 0; run < RunCount; run++)
    {
        Caracal::DiagnosticsBag diagnostics;
        const auto startTime = std::chrono::high_resolution_clock::now();
        const auto parseTree = Caracal::parse(tokens, diagnostics);
        const auto endTime = std::chrono::high_resolution_clock::now();

        if (!diagnostics.diagnostics().empty())
        {
            CaraTest::fail();// ("benchmark input must parse without diagnostics");
        }

        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        milliseconds.push_back(static_cast<double>(nanoseconds) / 1000000.0);
    }

    std::sort(milliseconds.begin(), milliseconds.end());
    const auto minimum = milliseconds.front();
    const auto median = milliseconds[milliseconds.size() / 2];
    const auto megabytesPerSecond = (static_cast<double>(data.value().size()) / (1024.0 * 1024.0)) / (minimum / 1000.0);
    std::cout << "      " << label << ": min " << minimum << " ms, median " << median
        << " ms, " << megabytesPerSecond << " MB/s" << std::endl;
}

static void OneMillionLinesParseTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    // the old-syntax benchmark file only lexes, the current grammar cannot parse it
    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    BenchmarkParseFile(std::filesystem::absolute(inputDirectory / "oneMilLines.cara"), "oneMilLinesShort");
    BenchmarkParseFile(std::filesystem::absolute(inputDirectory / "oneMilLinesLongIdents.cara"), "oneMilLinesLong ");
}

static auto tests =
{
    CaraTest::addTest("FileTests", FileTests, FileTests_Data),
    CaraTest::addTest("OneMillionLinesParseTime", OneMillionLinesParseTime),
};
