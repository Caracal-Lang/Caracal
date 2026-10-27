#pragma once

#include <Caracal/API.h>
#include <Caracal/Compilation/BuildCommand.h>
#include <Caracal/Compilation/OptimizationLevel.h>
#include <Caracal/Compilation/PrintOptions.h>
#include <Caracal/Diagnostics/DiagnosticOptions.h>
#include <Caracal/IR/Module.h>
#include <Caracal/Project/Project.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::filesystem::path compilerDirectory();

    [[nodiscard]] CARACAL_API BuildCommand createFileBuildCommand(
        const std::filesystem::path& filePath,
        std::chrono::year_month_day buildDay,
        const DiagnosticOptions& diagnosticOptions = {},
        const PrintOptions& printOptions = {});

    CARACAL_API int compileFile(
        const std::filesystem::path& filePath,
        std::chrono::year_month_day buildDay,
        const DiagnosticOptions& diagnosticOptions = {},
        const PrintOptions& printOptions = {});

    [[nodiscard]] CARACAL_API std::optional<BuildCommand> createProjectBuildCommand(
        const Project& project,
        std::optional<std::string_view> modeName = std::nullopt,
        const DiagnosticOptions& diagnosticOptions = {},
        bool shouldRunAfterBuild = false);

    CARACAL_API int buildProject(
        const Project& project,
        std::optional<std::string_view> modeName = std::nullopt,
        const DiagnosticOptions& diagnosticOptions = {});

    CARACAL_API int runProject(
        const Project& project,
        std::optional<std::string_view> modeName = std::nullopt,
        const DiagnosticOptions& diagnosticOptions = {});

    CARACAL_API std::pair<bool, std::string> generateLLVMIRFromModule(
        Module& irModule,
        std::string targetTriple,
        OptimizationLevel optimizationLevel = OptimizationLevel::None,
        bool debugInfo = false);
};
