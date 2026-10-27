#pragma once

#include <Caracal/API.h>
#include <Caracal/Compilation/OptimizationLevel.h>
#include <Caracal/Defines.h>
#include <filesystem>
#include <optional>
#include <string>
#include <variant>
#include <vector>

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

    struct CARACAL_API ProjectDependency
    {
        std::filesystem::path path;
        bool isPrelude = false;
    };

    struct CARACAL_API ProjectConstant
    {
        std::string name;
        std::variant<bool, i32, std::string> value;
    };

    struct CARACAL_API ProjectMode
    {
        std::string name;
        OptimizationLevel optimizationLevel = OptimizationLevel::None;
        std::string outputFolder;
        bool debugInfo = false;
        bool isDefault = false;
        std::vector<ProjectConstant> constants;
    };

    struct CARACAL_API Project
    {
        std::string name;
        std::filesystem::path rootDirectory;
        ProjectType type = ProjectType::Executable;
        std::string version;
        ProjectFolders folders;
        std::vector<ProjectDependency> dependencies;
        std::vector<ProjectMode> modes;
        std::vector<ProjectConstant> constants;
    };

    struct CARACAL_API ProjectResult
    {
        std::optional<Project> project;
        std::string error;
    };
}
