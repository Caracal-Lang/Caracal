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

    struct CARACAL_API ProjectFolders
    {
        std::filesystem::path libraries{ "Libraries" };
        std::filesystem::path temp{ "Temp" };
        std::filesystem::path output{ "Output" };
    };

    struct CARACAL_API Project
    {
        std::string name;
        std::filesystem::path rootDirectory;
        ProjectType type = ProjectType::Executable;
        std::string version;
        ProjectFolders folders;
    };

    struct CARACAL_API ProjectResult
    {
        std::optional<Project> project;
        std::string error;
    };
}
