#pragma once

#include <Caracal/API.h>
#include <Caracal/Project/Project.h>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::string projectVersion(std::chrono::year_month_day day);
    [[nodiscard]] CARACAL_API ProjectResult createProject(
        const std::filesystem::path& workingDirectory,
        const std::optional<std::string>& name,
        std::chrono::year_month_day creationDay,
        const std::filesystem::path& compilerDirectory);
}
