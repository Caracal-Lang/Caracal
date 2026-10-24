#pragma once

#include <Caracal/API.h>
#include <Caracal/Project/Project.h>
#include <filesystem>
#include <string>
#include <vector>

namespace Caracal
{
    struct CARACAL_API ProjectUpdate
    {
        std::filesystem::path coreCopy;
        std::vector<std::filesystem::path> addedFiles;
        std::vector<std::filesystem::path> changedFiles;
        std::vector<std::filesystem::path> removedFiles;
        std::string error;
    };

    [[nodiscard]] CARACAL_API ProjectUpdate updateProject(
        const Project& project,
        const std::filesystem::path& compilerDirectory,
        bool isDryRun);
}
