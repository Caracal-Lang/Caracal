#include <Caracal/Project/ProjectUpdater.h>
#include <Caracal/Text/File.h>

#include <algorithm>
#include <system_error>

namespace Caracal
{
    static ProjectUpdate Failure(std::string error)
    {
        ProjectUpdate update{};
        update.error = std::move(error);
        return update;
    }

    static std::vector<std::filesystem::path> RelativeFilePaths(const std::filesystem::path& directory, std::error_code& errorCode)
    {
        std::vector<std::filesystem::path> filePaths{};
        if (!std::filesystem::is_directory(directory))
        {
            return filePaths;
        }

        for (auto entry = std::filesystem::recursive_directory_iterator(directory, errorCode); !errorCode && entry != std::filesystem::recursive_directory_iterator(); entry.increment(errorCode))
        {
            if (entry->is_regular_file())
            {
                filePaths.push_back(entry->path().lexically_relative(directory));
            }
        }

        std::ranges::sort(filePaths);
        return filePaths;
    }

    static bool HaveSameContent(const std::filesystem::path& first, const std::filesystem::path& second)
    {
        return File::readText(first) == File::readText(second);
    }

    static void RemoveEmptyFolders(const std::filesystem::path& directory, std::error_code& errorCode)
    {
        std::vector<std::filesystem::path> folders{};
        for (auto entry = std::filesystem::recursive_directory_iterator(directory, errorCode); !errorCode && entry != std::filesystem::recursive_directory_iterator(); entry.increment(errorCode))
        {
            if (entry->is_directory())
            {
                folders.push_back(entry->path());
            }
        }

        // subfolders first
        std::ranges::sort(folders, std::greater{});
        for (const auto& folder : folders)
        {
            if (!errorCode && std::filesystem::is_empty(folder))
            {
                std::filesystem::remove(folder, errorCode);
            }
        }
    }

    ProjectUpdate updateProject(
        const Project& project,
        const std::filesystem::path& compilerDirectory,
        bool isDryRun)
    {
        const auto listsCore = std::ranges::any_of(project.dependencies, [](const ProjectDependency& dependency) { return dependency.path == "Core"; });
        if (!listsCore)
        {
            return {};
        }

        const auto compilerCore = compilerDirectory / "Core";
        if (!std::filesystem::is_directory(compilerCore))
        {
            return Failure("Couldn't update the project, there is no Core folder in " + compilerDirectory.string());
        }

        ProjectUpdate update{};
        update.coreCopy = project.folders.libraries / "Core";
        const auto projectCore = project.rootDirectory / update.coreCopy;

        std::error_code errorCode;
        const auto compilerFiles = RelativeFilePaths(compilerCore, errorCode);
        if (errorCode)
        {
            return Failure("Couldn't read " + compilerCore.string() + ", " + errorCode.message());
        }

        const auto projectFiles = RelativeFilePaths(projectCore, errorCode);
        if (errorCode)
        {
            return Failure("Couldn't read " + projectCore.string() + ", " + errorCode.message());
        }

        for (const auto& filePath : compilerFiles)
        {
            if (!std::ranges::binary_search(projectFiles, filePath))
            {
                update.addedFiles.push_back(update.coreCopy / filePath);
            }
            else if (!HaveSameContent(compilerCore / filePath, projectCore / filePath))
            {
                update.changedFiles.push_back(update.coreCopy / filePath);
            }
        }

        for (const auto& filePath : projectFiles)
        {
            if (!std::ranges::binary_search(compilerFiles, filePath))
            {
                update.removedFiles.push_back(update.coreCopy / filePath);
            }
        }

        if (isDryRun)
        {
            return update;
        }

        for (const auto& filePath : update.removedFiles)
        {
            std::filesystem::remove(project.rootDirectory / filePath, errorCode);
            if (errorCode)
            {
                return Failure("Couldn't remove " + (project.rootDirectory / filePath).string() + ", " + errorCode.message());
            }
        }

        std::filesystem::create_directories(projectCore, errorCode);
        if (!errorCode)
        {
            const auto copyOptions = std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing;
            std::filesystem::copy(compilerCore, projectCore, copyOptions, errorCode);
        }

        if (errorCode)
        {
            return Failure("Couldn't copy Core into " + projectCore.string() + ", " + errorCode.message());
        }

        RemoveEmptyFolders(projectCore, errorCode);
        if (errorCode)
        {
            return Failure("Couldn't remove the empty folders in " + projectCore.string() + ", " + errorCode.message());
        }

        return update;
    }
}
