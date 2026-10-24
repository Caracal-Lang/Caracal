#pragma once

#include <Caracal/API.h>
#include <Caracal/Compilation/PrintOptions.h>
#include <Caracal/Diagnostics/DiagnosticOptions.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <filesystem>
#include <vector>

namespace Caracal
{
    struct CARACAL_API BuildCommand
    {
        std::vector<std::filesystem::path> sourceFiles;
        std::filesystem::path preludeDirectory;
        std::vector<std::filesystem::path> libraryDirectories;
        TypeCheckerOptions typeCheckerOptions;
        DiagnosticOptions diagnosticOptions;
        PrintOptions printOptions;
        std::filesystem::path objectFilePath;
        std::filesystem::path executablePath;
        bool shouldRunAfterBuild = false;
    };
}
