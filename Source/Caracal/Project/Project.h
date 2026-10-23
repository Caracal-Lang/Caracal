#pragma once

#include <Caracal/API.h>
#include <filesystem>
#include <optional>
#include <string>

namespace Caracal
{
    enum class ProjectType
    {
        Executable,
    };

    struct CARACAL_API Project
    {
        std::string name;
        std::filesystem::path rootDirectory;
        ProjectType type = ProjectType::Executable;
        std::string version;
    };

    struct CARACAL_API ProjectResult
    {
        std::optional<Project> project;
        std::string error;
    };
}
