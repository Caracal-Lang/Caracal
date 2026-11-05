#pragma once

#include <Caracal/API.h>
#include <Caracal/Project/Project.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Caracal
{
    struct CARACAL_API ProjectModeResult
    {
        std::optional<ProjectMode> mode;
        std::string error;
    };

    struct CARACAL_API TestSelection
    {
        bool allLibraries = false;
        std::vector<std::string> libraries;
    };

    struct CARACAL_API ListedLibrariesResult
    {
        std::vector<std::filesystem::path> dependencyPaths;
        std::string error;
    };

    [[nodiscard]] CARACAL_API std::vector<ProjectMode> defaultProjectModes();
    [[nodiscard]] CARACAL_API ProjectModeResult findProjectMode(const Project& project, std::optional<std::string_view> modeName);
    [[nodiscard]] CARACAL_API ProjectModeResult findProjectTestMode(const Project& project);
    [[nodiscard]] CARACAL_API ListedLibrariesResult findListedLibraries(const Project& project, const TestSelection& testSelection);
}
