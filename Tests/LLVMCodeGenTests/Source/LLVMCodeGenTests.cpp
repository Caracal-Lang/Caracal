#include <CaraTest.h>
#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Compiler.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Text/File.h>
#include <algorithm>
#include <iostream>
#include <Caracal/Semantic/TypeCheckerOptions.h>

namespace
{
    const std::string TestTargetTriple = "x86_64-unknown-none";
}

static std::filesystem::path TestDataDirectory()
{
    return std::filesystem::absolute(std::filesystem::path(__FILE__).parent_path() / "../../TestData");
}

static void CompileFixtureToModule(
    const std::filesystem::path& inputFilePath,
    Caracal::CompilationContext& compilationContext,
    Caracal::Module& module,
    const std::filesystem::path& sourceFilePath = {})
{
    if (!std::filesystem::exists(inputFilePath))
        CaraTest::fail();// ("In file missing");

    const auto input = Caracal::File::readText(inputFilePath);
    if (!input.has_value())
        CaraTest::fail();// ("Could not read input file");

    Caracal::DiagnosticsBag diagnostics;
    compilationContext.addSource(std::move(input.value()), sourceFilePath, Caracal::UnitOrigin::User);
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

    const auto wasSuccessful = Caracal::typeCheck(compilationContext, diagnostics, module);
    if (!wasSuccessful)
    {
        std::cout << "Type checking failed!";
        CaraTest::fail();
    }
    Caracal::optimize(module);
    CaraTest::isTrue(!diagnostics.hasErrors());
}

static void CheckGeneratedLLVMIR(const std::filesystem::path& outputFilePath, bool irGenerated, const std::string& output)
{
    CaraTest::isTrue(irGenerated);
    const auto outputDirectory = outputFilePath.parent_path();
    if (!std::filesystem::exists(outputDirectory))
    {
        std::filesystem::create_directories(outputDirectory);
    }

    CaraTest::equalsFile(outputFilePath, output);
}

static void DebugFileTests(
    const std::string& /*fileName*/,
    const std::filesystem::path& inputFilePath,
    const std::filesystem::path& relativeOutputPath)
{
    const auto outputFilePath = TestDataDirectory() / "OutputLLVMDebug" / relativeOutputPath;
    if (!std::filesystem::exists(outputFilePath))
        CaraTest::skip();// ("Out file missing");

    Caracal::CompilationContext compilationContext;
    Caracal::Module module{};
    CompileFixtureToModule(inputFilePath, compilationContext, module);

    const auto startTime = std::chrono::high_resolution_clock::now();
    const auto [irGenerated, output] = Caracal::generateLLVMIRFromModule(module, TestTargetTriple);
    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      generateLLVMIRFromModule(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CheckGeneratedLLVMIR(outputFilePath, irGenerated, output);
}

static void ReleaseFileTests(
    const std::string& /*fileName*/,
    const std::filesystem::path& inputFilePath,
    const std::filesystem::path& relativeOutputPath)
{
    const auto outputFilePath = TestDataDirectory() / "OutputLLVMRelease" / relativeOutputPath;
    if (!std::filesystem::exists(outputFilePath))
        CaraTest::skip();// ("Out file missing");

    Caracal::CompilationContext compilationContext;
    Caracal::Module module{};
    CompileFixtureToModule(inputFilePath, compilationContext, module);

    const auto startTime = std::chrono::high_resolution_clock::now();
    const auto [irGenerated, output] = Caracal::generateLLVMIRFromModule(module, TestTargetTriple, Caracal::OptimizationLevel::Speed);
    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      generateLLVMIRFromModule(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CheckGeneratedLLVMIR(outputFilePath, irGenerated, output);
}

static std::vector<std::tuple<std::string, std::filesystem::path, std::filesystem::path>> FileTests_Data()
{
    const auto inputDirectory = TestDataDirectory() / "Input";

    std::vector<std::tuple<std::string, std::filesystem::path, std::filesystem::path>> data{};

    if (!std::filesystem::exists(inputDirectory))
    {
        return data;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(inputDirectory))
    {
        if (entry.is_regular_file()
            && entry.path().extension() == ".cara"
            && entry.path().filename() != "oneMilLines.cara"
            && entry.path().filename() != "oneMilLinesLongIdents.cara")
        {
            const auto& inputFilePath = entry.path();
            const auto inputDirName = inputFilePath.parent_path().filename();
            const auto fileName = inputFilePath.stem().string();

            const auto testName = inputDirName.string() + "/" + fileName;
            data.push_back(std::make_tuple(
                testName,
                inputFilePath,
                inputDirName / (fileName + ".txt")));
        }
    }
    return data;
}

static void DebugInfoFileTests(
    const std::string& /*fileName*/,
    const std::filesystem::path& inputFilePath,
    const std::filesystem::path& relativeOutputPath)
{
    const auto outputFilePath = TestDataDirectory() / "OutputLLVMDebugInfo" / relativeOutputPath;
    if (!std::filesystem::exists(outputFilePath))
        CaraTest::skip();// ("Out file missing");

    Caracal::CompilationContext compilationContext{ Caracal::TypeCheckerOptions{ .recordsSourceLocations = true } };
    Caracal::Module module{};
    const auto sourceFilePath = inputFilePath.lexically_relative(TestDataDirectory());
    CompileFixtureToModule(inputFilePath, compilationContext, module, sourceFilePath);

    const auto [irGenerated, output] = Caracal::generateLLVMIRFromModule(module, TestTargetTriple, Caracal::OptimizationLevel::None, true);

    CheckGeneratedLLVMIR(outputFilePath, irGenerated, output);
}

static std::vector<std::tuple<std::string, std::filesystem::path, std::filesystem::path>> DebugInfoFileTests_Data()
{
    const auto inputDirectory = TestDataDirectory() / "InputDebugInfo";

    std::vector<std::tuple<std::string, std::filesystem::path, std::filesystem::path>> data{};
    for (const auto& entry : std::filesystem::directory_iterator(inputDirectory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".cara")
        {
            const auto fileName = entry.path().stem().string();
            data.push_back(std::make_tuple(fileName, entry.path(), std::filesystem::path(fileName + ".txt")));
        }
    }

    std::ranges::sort(data);
    return data;
}

static void FunctionWithoutSourceLineGetsNoDebugInformation()
{
    const auto inputFilePath = TestDataDirectory() / "InputDebugInfo" / "twoFunctions.cara";
    Caracal::CompilationContext compilationContext{ Caracal::TypeCheckerOptions{ .recordsSourceLocations = true } };
    Caracal::Module module{};
    CompileFixtureToModule(inputFilePath, compilationContext, module, inputFilePath.lexically_relative(TestDataDirectory()));
    for (auto& function : module.functions())
    {
        if (function.name() == "square")
            function.setSourceLine({});
    }

    const auto [irGenerated, output] = Caracal::generateLLVMIRFromModule(module, TestTargetTriple, Caracal::OptimizationLevel::None, true);

    CaraTest::isTrue(irGenerated);
    CaraTest::isTrue(output.find("define i32 @square(i32 %0) {") != std::string::npos);
    CaraTest::isTrue(output.find("name: \"square\"") == std::string::npos);
    CaraTest::isTrue(output.find("name: \"main\"") != std::string::npos);
}

static void CoreEntryPointAndUserMainGetDebugInformation()
{
    const auto coreFilePath = TestDataDirectory() / "../../Core/Entrypoint.cara";
    auto coreSource = Caracal::File::readText(coreFilePath);
    if (!coreSource.has_value())
        CaraTest::fail();

    Caracal::CompilationContext compilationContext{ Caracal::TypeCheckerOptions{ .recordsSourceLocations = true } };
    compilationContext.addSource(std::move(coreSource.value()), "Core/Entrypoint.cara", Caracal::UnitOrigin::Core);
    Caracal::Module module{};
    CompileFixtureToModule(TestDataDirectory() / "InputDebugInfoCore" / "entryPoint.cara", compilationContext, module, "InputDebugInfoCore/entryPoint.cara");

    const auto [irGenerated, output] = Caracal::generateLLVMIRFromModule(module, TestTargetTriple, Caracal::OptimizationLevel::None, true);

    CheckGeneratedLLVMIR(TestDataDirectory() / "OutputLLVMDebugInfo" / "entryPoint.txt", irGenerated, output);
}

static auto tests =
{
    CaraTest::addTest("DebugFileTests", DebugFileTests, FileTests_Data),
    CaraTest::addTest("ReleaseFileTests", ReleaseFileTests, FileTests_Data),
    CaraTest::addTest("DebugInfoFileTests", DebugInfoFileTests, DebugInfoFileTests_Data),
    CaraTest::addTest("a function without a source line gets no debug information", FunctionWithoutSourceLineGetsNoDebugInformation),
    CaraTest::addTest("the Core entry point and the user's main get debug information", CoreEntryPointAndUserMainGetDebugInformation),
};
