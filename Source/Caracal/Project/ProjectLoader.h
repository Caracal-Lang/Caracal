#pragma once

#include <Caracal/API.h>
#include <Caracal/Project/Project.h>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::optional<std::string> checkProjectName(std::string_view name);
    [[nodiscard]] CARACAL_API ProjectResult loadProject(const std::filesystem::path& projectFilePath);
    [[nodiscard]] CARACAL_API ProjectResult findProject(
        const std::filesystem::path& workingDirectory, 
        const std::optional<std::filesystem::path>& path);
}
