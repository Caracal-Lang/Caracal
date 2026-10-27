#pragma once

#include <Caracal/API.h>
#include <Caracal/Project/Project.h>
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

    [[nodiscard]] CARACAL_API std::vector<ProjectMode> defaultProjectModes();
    [[nodiscard]] CARACAL_API ProjectModeResult findProjectMode(const Project& project, std::optional<std::string_view> modeName);
}
