#pragma once

#include <Caracal/API.h>
#include <filesystem>
#include <optional>
#include <string>

namespace Caracal
{
    struct CARACAL_API Project
    {
        std::string name;
        std::filesystem::path rootDirectory;
    };

    struct CARACAL_API ProjectResult
    {
        std::optional<Project> project;
        std::string error;
    };
}
