#pragma once

#include <Caracal/API.h>
#include <Caracal/Compilation/BuildCommand.h>
#include <Caracal/Compilation/PrintOptions.h>
#include <Caracal/Diagnostics/DiagnosticOptions.h>
#include <Caracal/IR/Module.h>
#include <Caracal/Project/Project.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <filesystem>
#include <optional>
#include <string>

namespace Caracal
{
    CARACAL_API int compileFile(
        const std::filesystem::path& filePath,
        const DiagnosticOptions& diagnosticOptions = {},
        const PrintOptions& printOptions = {});

    [[nodiscard]] CARACAL_API std::optional<BuildCommand> createProjectBuildCommand(
        const Project& project,
        const DiagnosticOptions& diagnosticOptions = {},
        bool shouldRunAfterBuild = false);

    CARACAL_API int buildProject(
        const Project& project,
        const DiagnosticOptions& diagnosticOptions = {});

    CARACAL_API int runProject(
        const Project& project,
        const DiagnosticOptions& diagnosticOptions = {});

    CARACAL_API std::pair<bool, std::string> generateLLVMIRFromModule(Module& irModule, std::string targetTriple);
};
