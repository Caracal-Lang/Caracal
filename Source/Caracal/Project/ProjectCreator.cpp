#include <Caracal/Project/ProjectCreator.h>
#include <Caracal/Project/ProjectLoader.h>
#include <Caracal/Text/File.h>

#include <nlohmann/json.hpp>

#include <string_view>
#include <system_error>

namespace Caracal
{
    static constexpr std::string_view MainSource =
        "def main() i32\n"
        "{\n"
        "    C.puts(\"Hello, Caracal!\");\n"
        "    return 0;\n"
        "}\n";

    static constexpr std::string_view GitIgnore =
        "Temp/\n"
        "Build/\n";

    static ProjectResult Failure(std::string error)
    {
        return { std::nullopt, std::move(error) };
    }

    static std::string CreateProjectFileText(const Project& project)
    {
        nlohmann::ordered_json projectFile;
        projectFile["name"] = project.name;
        return projectFile.dump(2, ' ', false, nlohmann::ordered_json::error_handler_t::replace) + "\n";
    }

    static bool WriteIfMissing(const std::filesystem::path& filePath, std::string_view content)
    {
        if (std::filesystem::exists(filePath))
            return true;

        return File::writeText(filePath, content);
    }

    ProjectResult createProject(
        const std::filesystem::path& workingDirectory,
        const std::optional<std::string>& name)
    {
        const auto projectName = name.value_or(workingDirectory.filename().string());
        if (const auto problem = checkProjectName(projectName))
            return Failure("\"" + projectName + "\" cannot be a project name because " + problem.value());

        auto projectDirectory = workingDirectory;
        if (name.has_value())
            projectDirectory /= projectName;

        const auto projectFilePath = projectDirectory / "caracal.json";
        if (std::filesystem::exists(projectFilePath))
            return Failure(projectDirectory.string() + " already has a caracal.json");

        std::error_code errorCode;
        std::filesystem::create_directories(projectDirectory, errorCode);
        if (errorCode)
            return Failure("Could not create " + projectDirectory.string() + ", " + errorCode.message());

        if (!WriteIfMissing(projectDirectory / "main.cara", MainSource))
            return Failure("Could not write " + (projectDirectory / "main.cara").string());

        if (!WriteIfMissing(projectDirectory / ".gitignore", GitIgnore))
            return Failure("Could not write " + (projectDirectory / ".gitignore").string());

        Project project{ projectName, projectDirectory };

        if (!File::writeText(projectFilePath, CreateProjectFileText(project)))
            return Failure("Could not write " + projectFilePath.string());

        return { std::move(project), {} };
    }
}
