#include <Caracal/Project/ProjectRepository.h>

#include <llvm/Support/Program.h>

#include <system_error>

namespace Caracal
{
    std::optional<std::filesystem::path> findParentGitRepository(const std::filesystem::path& directory)
    {
        std::error_code errorCode;
        auto folder = std::filesystem::absolute(directory, errorCode);
        if (errorCode)
            return std::nullopt;

        while (true)
        {
            if (std::filesystem::exists(folder / ".git", errorCode))
                return folder;

            if (folder == folder.parent_path())
                return std::nullopt;

            folder = folder.parent_path();
        }
    }

    bool initializeRepository(const std::filesystem::path& projectDirectory)
    {
        if (findParentGitRepository(projectDirectory).has_value())
            return false;

        const auto gitPath = llvm::sys::findProgramByName("git");
        if (!gitPath)
            return false;

        const auto exitCode = llvm::sys::ExecuteAndWait(gitPath.get(), { "git", "-C", projectDirectory.string(), "init", "-q" });
        return exitCode == 0;
    }
}
