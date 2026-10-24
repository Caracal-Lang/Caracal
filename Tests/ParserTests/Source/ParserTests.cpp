#include <CaraTest.h>
#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Debug/ParseTreePrinter.h>
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

    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
    Caracal::CompilationContext preludeContext;
    if (!Caracal::loadPrelude(preludeContext, preludePath, diagnostics))
    {
        Caracal::writeDiagnostics(std::cout, diagnostics);
        CaraTest::fail();
    }

    // next free fileId after the prelude
    const auto fileId = static_cast<u16>(preludeContext.units().size());
    const auto tokens = Caracal::lex(source, diagnostics, fileId);

    const auto startTime = std::chrono::high_resolution_clock::now();
    const auto nodeBuffer = Caracal::parse(tokens, diagnostics);
    const auto endTime = std::chrono::high_resolution_clock::now();

    std::cout << "      parse(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    const auto output = Caracal::prettyPrint(nodeBuffer, tokens, &preludeContext.semanticContext(), fileId);

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

static auto tests =
{
    CaraTest::addTest("FileTests", FileTests, FileTests_Data),
    CaraTest::addTest("NodeBufferRoundTrips", NodeBufferRoundTrips),
    CaraTest::addTest("CapacityBoundHoldsOnDegenerateInputs", CapacityBoundHoldsOnDegenerateInputs),
    CaraTest::addTest("CapacityFloorCoversLoneDef", CapacityFloorCoversLoneDef),
};
