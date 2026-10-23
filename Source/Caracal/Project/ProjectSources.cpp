#include <Caracal/Project/ProjectSources.h>

#include <algorithm>
#include <system_error>

namespace Caracal
{
    static bool IsSkippedDirectory(const Project& project, const std::filesystem::path& directory)
    {
        if (directory == project.rootDirectory / "Temp" || directory == project.rootDirectory / "Build")
            return true;

        return std::filesystem::exists(directory / "caracal.json");
    }

    ProjectSourcesResult collectProjectSources(const Project& project)
    {
        ProjectSourcesResult result{};

        std::error_code errorCode;
        auto iterator = std::filesystem::recursive_directory_iterator(project.rootDirectory, errorCode);
        for (; !errorCode && iterator != std::filesystem::recursive_directory_iterator(); iterator.increment(errorCode))
        {
            const auto& entry = *iterator;
            if (entry.is_directory())
            {
                if (IsSkippedDirectory(project, entry.path()))
                    iterator.disable_recursion_pending();

                continue;
            }

            if (entry.is_regular_file() && entry.path().extension() == ".cara")
                result.sourceFiles.push_back(entry.path());
        }

        if (errorCode)
        {
            result.sourceFiles.clear();
            result.error = "Could not search " + project.rootDirectory.string() + " for sources, " + errorCode.message();
            return result;
        }

        std::ranges::sort(result.sourceFiles);
        return result;
    }
}
