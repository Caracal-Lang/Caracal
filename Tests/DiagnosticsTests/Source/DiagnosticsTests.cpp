#include <CaraTest.h>

#include <Caracal/Compilation.h>
#include <Caracal/IR/Module.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Diagnostics/DiagnosticPrinter.h>
#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Semantic/Type.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Text/File.h>
#include <Caracal/Text/SourceText.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace
{
    using DiagnosticsTestData = std::tuple<std::string, std::filesystem::path, std::filesystem::path>;

    std::filesystem::path CurrentDirectory()
    {
        return std::filesystem::path(__FILE__).parent_path();
    }

    std::filesystem::path DiagnosticsDataDirectory()
    {
        return std::filesystem::absolute(CurrentDirectory() / "../../TestData/Diagnostics");
    }

    std::filesystem::path TestDataDirectory()
    {
        return std::filesystem::absolute(CurrentDirectory() / "../../TestData");
    }

    std::filesystem::path DiagnosticsInputDirectory()
    {
        return DiagnosticsDataDirectory() / "Input";
    }

    std::filesystem::path DiagnosticsOutputDirectory()
    {
        return DiagnosticsDataDirectory() / "Output";
    }

    std::filesystem::path DiagnosticsBuiltinInputDirectory()
    {
        return DiagnosticsDataDirectory() / "InputBuiltin";
    }

    std::filesystem::path DiagnosticsBuiltinOutputDirectory()
    {
        return DiagnosticsDataDirectory() / "OutputBuiltin";
    }

    std::filesystem::path RepositoryRootDirectory()
    {
        return std::filesystem::absolute(CurrentDirectory() / "../../..");
    }

    std::filesystem::path MakeRepositoryRelativePath(const std::filesystem::path& filePath)
    {
        const auto absoluteFilePath = std::filesystem::absolute(filePath);
        auto relativePath = std::filesystem::relative(absoluteFilePath, TestDataDirectory());
        if (relativePath.empty())
        {
            relativePath = std::filesystem::relative(absoluteFilePath, RepositoryRootDirectory());
        }

        if (relativePath.empty())
            return filePath;

        return relativePath;
    }

    std::string RenderDiagnosticsForSource(const std::string& input, const std::filesystem::path& filePath, bool withPrelude)
    {
        Caracal::DiagnosticsBag diagnostics;
        Caracal::CompilationContext compilationContext;
        compilationContext.addSource(input, MakeRepositoryRelativePath(filePath), Caracal::UnitOrigin::User);
        Caracal::lexAndParse(compilationContext, diagnostics);

        const Caracal::DiagnosticOptions diagnosticOptions{
            .contextLines = 3,
            .enableColors = false,
            .enableUnicode = false
        };

        if (!diagnostics.diagnostics().empty())
            return Caracal::formatDiagnostics(diagnostics, diagnosticOptions);

        auto preludeSources = std::vector<std::string>{};
        if (withPrelude)
        {
            preludeSources = Caracal::collectPreludeSources(RepositoryRootDirectory() / "Prelude");
        }
        Caracal::loadPrelude(compilationContext, preludeSources);

        Caracal::Module irModule{};
        const auto wasSuccessful = Caracal::typeCheck(compilationContext, diagnostics, irModule);
        if (!wasSuccessful || !diagnostics.diagnostics().empty())
            return Caracal::formatDiagnostics(diagnostics, diagnosticOptions);

        return {};
    }

    std::string RenderDiagnosticsForFile(const std::filesystem::path& inputFilePath, bool withPrelude)
    {
        const auto input = Caracal::File::readText(inputFilePath);
        if (!input.has_value())
            throw std::runtime_error("Could not read input file: " + inputFilePath.string());

        return RenderDiagnosticsForSource(input.value(), inputFilePath, withPrelude);
    }

    void WriteTextFile(const std::filesystem::path& filePath, const std::string& content)
    {
        std::filesystem::create_directories(filePath.parent_path());

        std::ofstream outFile(filePath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!outFile.is_open())
            throw std::runtime_error("Could not write output file: " + filePath.string());

        outFile << content;
    }

    static void FileTests(
        const std::string& fileName,
        const std::filesystem::path& inputFilePath,
        const std::filesystem::path& outputFilePath)
    {
        static_cast<void>(fileName);

        if (!std::filesystem::exists(inputFilePath))
            CaraTest::fail();
        if (!std::filesystem::exists(outputFilePath))
            CaraTest::skip();

        const auto output = RenderDiagnosticsForFile(inputFilePath, true);
        if (output.empty())
            CaraTest::fail();

        CaraTest::equalsFile(outputFilePath, output);
    }

    static void BuiltinFileTests(
        const std::string& fileName,
        const std::filesystem::path& inputFilePath,
        const std::filesystem::path& outputFilePath)
    {
        static_cast<void>(fileName);

        if (!std::filesystem::exists(inputFilePath))
            CaraTest::fail();
        if (!std::filesystem::exists(outputFilePath))
            CaraTest::skip();

        const auto output = RenderDiagnosticsForFile(inputFilePath, false);
        if (output.empty())
            CaraTest::fail();

        CaraTest::equalsFile(outputFilePath, output);
    }

    static std::vector<DiagnosticsTestData> CollectFileTestData(
        const std::filesystem::path& inputDirectory,
        const std::filesystem::path& outputDirectory)
    {
        std::vector<DiagnosticsTestData> data{};
        if (!std::filesystem::exists(inputDirectory))
            return data;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(inputDirectory))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".cara")
                continue;

            auto relativeInputPath = std::filesystem::relative(entry.path(), inputDirectory);
            const auto fileName = entry.path().stem().string();
            auto outputFilePath = outputDirectory / relativeInputPath;
            outputFilePath.replace_extension(".txt");

            relativeInputPath.replace_extension();
            const auto testName = relativeInputPath.generic_string();

            data.push_back(std::make_tuple(
                testName,
                entry.path(),
                std::filesystem::absolute(outputFilePath)));
        }

        return data;
    }

    static std::vector<DiagnosticsTestData> FileTests_Data()
    {
        return CollectFileTestData(DiagnosticsInputDirectory(), DiagnosticsOutputDirectory());
    }

    static std::vector<DiagnosticsTestData> BuiltinFileTests_Data()
    {
        return CollectFileTestData(DiagnosticsBuiltinInputDirectory(), DiagnosticsBuiltinOutputDirectory());
    }
}

static auto tests =
{
    CaraTest::addTest("FileTests", FileTests, FileTests_Data),
    CaraTest::addTest("BuiltinFileTests", BuiltinFileTests, BuiltinFileTests_Data),
};
