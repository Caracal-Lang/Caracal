#include <CaraTest.h>

#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Compiler.h>
#include <Caracal/Debug/IRPrinter.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Text/File.h>
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

    if (diagnostics.hasErrors())
    {
        Caracal::writeDiagnostics(std::cout, diagnostics);
        CaraTest::fail();
    }
    const auto preludePath = std::filesystem::path(__FILE__).parent_path() / "../../../Prelude";
    if (!Caracal::loadPrelude(compilationContext, preludePath, diagnostics))
    {
        Caracal::writeDiagnostics(std::cout, diagnostics);
        CaraTest::fail();
    }

    const auto startTime = std::chrono::high_resolution_clock::now();
    Caracal::Module module{};
    const auto wasSuccessful = Caracal::typeCheck(compilationContext, diagnostics, module);
    if (wasSuccessful)
    {
        Caracal::optimize(module);
    }
    const auto endTime = std::chrono::high_resolution_clock::now();

    std::cout << "      typeCheck() + optimize(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    if (!wasSuccessful)
    {
        std::cout << "Type checking failed!";
        CaraTest::fail();
    }

    Caracal::IRPrinter printer{ module };
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

static auto tests =
{
    CaraTest::addTest("FileTests", FileTests, FileTests_Data),
};
