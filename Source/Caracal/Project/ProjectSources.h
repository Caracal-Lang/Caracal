#pragma once

#include <Caracal/API.h>
#include <Caracal/Project/Project.h>
#include <filesystem>
#include <string>
#include <vector>

namespace Caracal
{
    struct CARACAL_API ProjectSourcesResult
    {
        std::vector<std::filesystem::path> sourceFiles;
        std::string error;
    };

    // skips the project's libraries, temp and output folders and any subfolder with a caracal.json
    [[nodiscard]] CARACAL_API ProjectSourcesResult collectProjectSources(const Project& project);
}
