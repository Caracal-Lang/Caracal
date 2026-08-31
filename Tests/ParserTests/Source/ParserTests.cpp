#include <CaraTest.h>
#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Debug/ParseTreePrinter.h>
#include <Caracal/Debug/ParseTreePrinterOld.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Text/SourceEncoding.h>
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
    const auto nodeBuffer = Caracal::parse(tokens, diagnostics);
    const auto endTime = std::chrono::high_resolution_clock::now();

    std::cout << "      parse(): " << CaraTest::stringify(endTime - startTime) << std::endl;
    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
    Caracal::CompilationContext preludeContext;
    Caracal::loadPrelude(preludeContext, preludePath);

    const auto output = Caracal::prettyPrint(nodeBuffer, tokens, &preludeContext.semanticContext());

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

static void FileTestsFlat(
    const std::string& /*fileName*/,
    const std::filesystem::path& inputFilePath,
    const std::filesystem::path& /*outputFilePath*/)
{
    const auto input = Caracal::File::readText(inputFilePath);
    if (!input.has_value())
    {
        CaraTest::fail();
    }

    const auto source = std::make_shared<Caracal::SourceText>(input.value());

    Caracal::DiagnosticsBag treeDiagnostics;
    const auto tokens = Caracal::lex(source, treeDiagnostics);
    const auto parseTree = Caracal::parseOld(tokens, treeDiagnostics);

    Caracal::DiagnosticsBag flatDiagnostics;
    Caracal::DiagnosticsBag flatLexDiagnostics;
    const auto flatTokens = Caracal::lex(source, flatLexDiagnostics);
    const auto flatBuffer = Caracal::parse(flatTokens, flatDiagnostics);

    const auto treeText = Caracal::formatDiagnostics(treeDiagnostics, {});
    auto combinedFlat = Caracal::formatDiagnostics(flatLexDiagnostics, {}) + Caracal::formatDiagnostics(flatDiagnostics, {});
    if (treeText != combinedFlat)
    {
        std::cout << "      diagnostics mismatch in " << inputFilePath.string() << std::endl;
        std::cout << "      tree parser:\n" << treeText << std::endl;
        std::cout << "      flat parser:\n" << combinedFlat << std::endl;
        CaraTest::fail();
    }

    CaraTest::isTrue(flatBuffer.nodeCount() >= 1);

    std::string layoutFailure;
    if (!Caracal::verifyNodeLayout(flatBuffer, layoutFailure))
    {
        std::cout << "      node layout violation in " << inputFilePath.string() << ": " << layoutFailure << std::endl;
        CaraTest::fail();
    }

    // the new printer must produce the same output as the old printer
    if (treeDiagnostics.diagnostics().empty())
    {
        Caracal::ParseTreePrinterOld classPrinter{ *parseTree, nullptr };
        const auto classText = classPrinter.prettyPrint();
        const auto flatText = Caracal::prettyPrint(flatBuffer, flatTokens, nullptr);
        if (classText != flatText)
        {
            std::cout << "      printer mismatch in " << inputFilePath.string() << std::endl;
            std::cout << "      class:\n" << classText << std::endl;
            std::cout << "      flat:\n" << flatText << std::endl;
            CaraTest::fail();
        }
    }
}

static void NodeBufferRoundTrips()
{
    Caracal::NodeBuffer buffer{ 8 };
    CaraTest::areEqual(0U, buffer.nodeCount());
    CaraTest::areEqual(8U, buffer.capacity());

    // leaf, leaf, parent
    // children sit before their parent and the subtree encapsulates them all
    buffer.append(Caracal::NodeKind::NameExpression, 0, 1);
    buffer.append(Caracal::NodeKind::NameExpression, 2, 1);
    buffer.append(Caracal::NodeKind::BinaryExpression, 1, 3);

    CaraTest::areEqual(3U, buffer.nodeCount());
    CaraTest::areEqual(Caracal::NodeKind::BinaryExpression, buffer.kind(2));
    CaraTest::areEqual(1U, buffer.mainToken(2));
    CaraTest::areEqual(3U, buffer.subtreeSize(2));

    // the last child is directly before the parent, 
    // siblings go backward by subtree size
    const auto lastChild = 2U - 1;
    CaraTest::areEqual(2U, buffer.mainToken(lastChild));
    const auto firstChild = lastChild - buffer.subtreeSize(lastChild);
    CaraTest::areEqual(0U, buffer.mainToken(firstChild));

    // nodes without a token carry the sentinel
    buffer.append(Caracal::NodeKind::ReturnTypesNode, Caracal::InvalidTokenIndex, 1);
    CaraTest::areEqual(Caracal::InvalidTokenIndex, buffer.mainToken(3));
}

static void CapacityBoundHoldsOnDegenerateInputs()
{
    const std::vector<std::string> inputs
    {
        "",
        "def",
        "def def",
        "def (",
        "def f",
        "# def",
        "# # def",
        "x def",
        "type",
        "enum",
        "if",
        "return",
        "def f(){ x }",
        "def f(){ if if if",
        "def f(){ while while while",
        "def f(){ ((((",
        "def f(){ [[[[",
        "def f(...,...,...){}",
    };

    for (const auto& input : inputs)
    {
        const auto source = std::make_shared<Caracal::SourceText>(input);
        Caracal::DiagnosticsBag diagnostics;
        const auto tokens = Caracal::lex(source, diagnostics);
        const auto nodeBuffer = Caracal::parse(tokens, diagnostics);
        CaraTest::isTrue(nodeBuffer.nodeCount() <= nodeBuffer.capacity());

        std::string layoutFailure;
        CaraTest::isTrue(Caracal::verifyNodeLayout(nodeBuffer, layoutFailure));
    }
}

static void CapacityFloorCoversLoneDef()
{
    const auto source = std::make_shared<Caracal::SourceText>(std::string{ "def" });
    Caracal::DiagnosticsBag diagnostics;
    const auto tokens = Caracal::lex(source, diagnostics);
    const auto nodeBuffer = Caracal::parse(tokens, diagnostics);
    CaraTest::areEqual(2U, tokens.size());
    CaraTest::areEqual(5U, nodeBuffer.nodeCount());
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

[[nodiscard]] static Caracal::TokenBuffer LexBenchmarkFile(const std::filesystem::path& absolutePath, size_t& byteCount)
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

    byteCount = data.value().size();
    const auto source = std::make_shared<Caracal::SourceText>(data.value());
    Caracal::DiagnosticsBag lexDiagnostics;
    return Caracal::lex(source, lexDiagnostics);
}

static void BenchmarkParseFileOld(const std::filesystem::path& absolutePath, const char* label)
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
        const auto parseTree = Caracal::parseOld(tokens, diagnostics);
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

static void OneMillionLinesParseTimeOld()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    BenchmarkParseFileOld(std::filesystem::absolute(inputDirectory / "oneMilLines.cara"), "oneMilLinesShort");
    BenchmarkParseFileOld(std::filesystem::absolute(inputDirectory / "oneMilLinesLongIdents.cara"), "oneMilLinesLong ");
}

static void OneMillionLinesParseTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    BenchmarkParseFile(std::filesystem::absolute(inputDirectory / "oneMilLines.cara"), "oneMilLinesShort");
    BenchmarkParseFile(std::filesystem::absolute(inputDirectory / "oneMilLinesLongIdents.cara"), "oneMilLinesLong ");
}

static void OneMillionLinesPrintTimeOld()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    const auto absolutePath = std::filesystem::absolute(inputDirectory / "oneMilLines.cara");

    size_t byteCount = 0;
    const auto tokens = LexBenchmarkFile(absolutePath, byteCount);
    Caracal::DiagnosticsBag diagnostics;
    const auto parseTree = Caracal::parseOld(tokens, diagnostics);
    CaraTest::isTrue(diagnostics.diagnostics().empty());

    Caracal::CompilationContext preludeContext;
    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
    Caracal::loadPrelude(preludeContext, preludePath);
    auto* module = &preludeContext.semanticContext();

    constexpr int RunCount = 3;
    std::vector<double> milliseconds;
    milliseconds.reserve(RunCount);
    size_t outputBytes = 0;
    for (int run = 0; run < RunCount; run++)
    {
        const auto startTime = std::chrono::high_resolution_clock::now();
        Caracal::ParseTreePrinterOld classPrinter{ *parseTree, module };
        const auto output = classPrinter.prettyPrint();
        const auto endTime = std::chrono::high_resolution_clock::now();
        outputBytes = output.size();
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        milliseconds.push_back(static_cast<double>(nanoseconds) / 1000000.0);
    }

    std::cout << "      print output " << (outputBytes / (1024.0 * 1024.0)) << " MB" << std::endl;
    PrintBenchmarkLine("oneMilLinesShort", milliseconds, outputBytes);
}

static void OneMillionLinesPrintTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    const auto absolutePath = std::filesystem::absolute(inputDirectory / "oneMilLines.cara");

    size_t byteCount = 0;
    const auto tokens = LexBenchmarkFile(absolutePath, byteCount);
    Caracal::DiagnosticsBag diagnostics;
    const auto nodeBuffer = Caracal::parse(tokens, diagnostics);
    CaraTest::isTrue(diagnostics.diagnostics().empty());

    Caracal::CompilationContext preludeContext;
    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
    Caracal::loadPrelude(preludeContext, preludePath);
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

static auto tests =
{
    CaraTest::addTest("FileTests", FileTests, FileTests_Data),
    CaraTest::addTest("FileTestsFlat", FileTestsFlat, FileTests_Data),
    CaraTest::addTest("NodeBufferRoundTrips", NodeBufferRoundTrips),
    CaraTest::addTest("CapacityBoundHoldsOnDegenerateInputs", CapacityBoundHoldsOnDegenerateInputs),
    CaraTest::addTest("CapacityFloorCoversLoneDef", CapacityFloorCoversLoneDef),
    CaraTest::addTest("OneMillionLinesParseTimeOld", OneMillionLinesParseTimeOld),
    CaraTest::addTest("OneMillionLinesParseTime", OneMillionLinesParseTime),
    CaraTest::addTest("OneMillionLinesPrintTimeOld", OneMillionLinesPrintTimeOld),
    CaraTest::addTest("OneMillionLinesPrintTime", OneMillionLinesPrintTime),
};
