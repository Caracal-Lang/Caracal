#include <CaraTest.h>
#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Debug/ParseTreePrinter.h>
#include <Caracal/Debug/ParseTreePrinterOld.h>
#include <Caracal/Semantic/TypeChecker.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Text/File.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>

using namespace CaraTest;

static void FileTestsOld(
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

    const auto& parseTrees = compilationContext.parseTrees();
    if (parseTrees.empty())
    {
        Caracal::writeDiagnostics(std::cout, diagnostics);
        CaraTest::fail();
    }
    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
    Caracal::loadPrelude(compilationContext, preludePath);

    auto startTime = std::chrono::high_resolution_clock::now();
    auto wasSuccessful = Caracal::typeCheck(compilationContext, diagnostics);
    if (wasSuccessful)
    {
        wasSuccessful = Caracal::optimize(compilationContext, diagnostics);
    }
    auto endTime = std::chrono::high_resolution_clock::now();

    std::cout << "      Type check(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    if (!wasSuccessful) {
        CaraTest::fail();// ("Type checking failed");
    }

    Caracal::ParseTreePrinterOld printer{ *parseTrees[0], &compilationContext.semanticContext()};
    const auto output = printer.prettyPrint();

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

static std::vector<std::tuple<std::string, std::filesystem::path, std::filesystem::path>> BuiltinParityTests_Data()
{
    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto currentDirectory = currentFilePath.parent_path();
    const auto inputDir = currentDirectory / "../../TestData/Diagnostics/InputBuiltin";
    const auto absolutePath = std::filesystem::absolute(inputDir);

    std::vector<std::tuple<std::string, std::filesystem::path, std::filesystem::path>> data{};

    if (!std::filesystem::exists(absolutePath))
    {
        return data;
    }

    for (const auto& entry : std::filesystem::directory_iterator(absolutePath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".cara")
        {
            const auto& inputFilePath = entry.path();
            const auto fileName = inputFilePath.stem().string();
            data.push_back(std::make_tuple(
                "Builtin/" + fileName,
                inputFilePath,
                inputFilePath));
        }
    }
    return data;
}

static void FileTestsNewChecker(
    const std::string& /*fileName*/,
    const std::filesystem::path& inputFilePath,
    const std::filesystem::path& /*outputFilePath*/)
{
    const auto input = Caracal::File::readText(inputFilePath);
    if (!input.has_value())
    {
        CaraTest::fail();
    }
    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";

    Caracal::DiagnosticsBag oldDiagnostics;
    Caracal::CompilationContext oldContext;
    oldContext.addSource(std::string(input.value()), std::filesystem::path{}, Caracal::UnitOrigin::User);
    Caracal::lexAndParse(oldContext, oldDiagnostics);
    Caracal::loadPrelude(oldContext, preludePath);
    static_cast<void>(Caracal::typeCheck(oldContext, oldDiagnostics));

    Caracal::DiagnosticsBag newDiagnostics;
    Caracal::CompilationContext newContext;
    newContext.addSource(std::string(input.value()), std::filesystem::path{}, Caracal::UnitOrigin::User);
    Caracal::lexAndParse(newContext, newDiagnostics);
    Caracal::loadPrelude(newContext, preludePath);
    Caracal::TypeChecker newChecker{ newContext, false, newDiagnostics };
    static_cast<void>(newChecker.typeCheck());

    const auto oldText = Caracal::formatDiagnostics(oldDiagnostics, {});
    const auto newText = Caracal::formatDiagnostics(newDiagnostics, {});
    if (oldText != newText)
    {
        std::cout << "      checker diagnostics mismatch in " << inputFilePath.string() << std::endl;
        std::cout << "      old checker:\n" << oldText << std::endl;
        std::cout << "      new checker:\n" << newText << std::endl;
        CaraTest::fail();
    }

    Caracal::ParseTreePrinterOld oldPrinter{ *oldContext.parseTrees()[0], &oldContext.semanticContext() };
    const auto oldPrint = oldPrinter.prettyPrint();
    auto& newUnit = newContext.units()[0];
    const auto newPrint = Caracal::prettyPrint(
        newUnit.nodeBuffer,
        newContext.tokenBuffer(newUnit.fileId),
        &newContext.semanticContext(),
        newUnit.fileId);
    if (oldPrint != newPrint)
    {
        std::cout << "      checked print mismatch in " << inputFilePath.string() << std::endl;

        // only show the first different line
        std::istringstream oldStream{ oldPrint };
        std::istringstream newStream{ newPrint };
        std::string oldLine;
        std::string newLine;
        auto lineNumber = 0;
        while (true)
        {
            const auto hasOld = static_cast<bool>(std::getline(oldStream, oldLine));
            const auto hasNew = static_cast<bool>(std::getline(newStream, newLine));
            lineNumber++;
            if (!hasOld && !hasNew)
            {
                break;
            }
            if (!hasOld || !hasNew || oldLine != newLine)
            {
                std::cout << "      first difference at line " << lineNumber << ":\n";
                std::cout << "      old: " << (hasOld ? oldLine : std::string("<end>")) << "\n";
                std::cout << "      new: " << (hasNew ? newLine : std::string("<end>")) << std::endl;
                break;
            }
        }
        CaraTest::fail();
    }
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

static void BenchmarkTypeCheckFile(const std::filesystem::path& absolutePath, const std::filesystem::path& preludePath, const char* label, bool useNewChecker)
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
        auto checkedCleanly = false;
        if (useNewChecker)
        {
            Caracal::TypeChecker checker{ context, false, diagnostics };
            checkedCleanly = checker.typeCheck();
        }
        else
        {
            checkedCleanly = Caracal::typeCheck(context, diagnostics);
        }
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

static void OneMillionLinesTypeCheckTimeOld()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    const auto preludePath = currentFilePath.parent_path() / "../../../Prelude";
    BenchmarkTypeCheckFile(std::filesystem::absolute(inputDirectory / "oneMilLines.cara"), preludePath, "oneMilLinesShort", false);
    BenchmarkTypeCheckFile(std::filesystem::absolute(inputDirectory / "oneMilLinesLongIdents.cara"), preludePath, "oneMilLinesLong ", false);
}

static void OneMillionLinesTypeCheckTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    const auto preludePath = currentFilePath.parent_path() / "../../../Prelude";
    BenchmarkTypeCheckFile(std::filesystem::absolute(inputDirectory / "oneMilLines.cara"), preludePath, "oneMilLinesShort", true);
    BenchmarkTypeCheckFile(std::filesystem::absolute(inputDirectory / "oneMilLinesLongIdents.cara"), preludePath, "oneMilLinesLong ", true);
}

static auto tests =
{
    CaraTest::addTest("FileTestsOld", FileTestsOld, FileTests_Data),
    CaraTest::addTest("UnitSemanticsResolveCrossFileNodeIndexes", UnitSemanticsResolveCrossFileNodeIndexes),
    CaraTest::addTest("FileTestsNewChecker", FileTestsNewChecker, FileTests_Data),
    CaraTest::addTest("FileTestsNewCheckerBuiltin", FileTestsNewChecker, BuiltinParityTests_Data),
    CaraTest::addTest("OneMillionLinesTypeCheckTimeOld", OneMillionLinesTypeCheckTimeOld),
    CaraTest::addTest("OneMillionLinesTypeCheckTime", OneMillionLinesTypeCheckTime),
};
