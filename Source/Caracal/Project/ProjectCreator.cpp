#include <Caracal/Compilation/OptimizationLevel.h>
#include <Caracal/Defines.h>
#include <Caracal/Project/ProjectCreator.h>
#include <Caracal/Project/ProjectLoader.h>
#include <Caracal/Project/ProjectModes.h>
#include <Caracal/Project/ProjectRepository.h>
#include <Caracal/Text/File.h>

#include <nlohmann/json.hpp>

#include <format>
#include <string_view>
#include <system_error>

namespace Caracal
{
    static constexpr std::string_view MainSource =
        "def main() i32\n"
        "{\n"
        "    printLine(\"Hello, Caracal!\");\n"
        "    return 0;\n"
        "}\n";

    static constexpr std::string_view GitIgnore =
        "Temp/\n"
        "Output/\n";

    static constexpr std::string_view EditorConfigSettings =
        "[*.cara]\n"
        "charset = utf-8\n"
        "end_of_line = lf\n"
        "insert_final_newline = true\n"
        "indent_style = space\n"
        "indent_size = 4\n";

    static std::string EditorConfigText(const std::filesystem::path& projectDirectory)
    {
        if (findParentGitRepository(projectDirectory).has_value())
            return std::string(EditorConfigSettings);

        return "root = true\n\n" + std::string(EditorConfigSettings);
    }

    static std::string GitAttributesText(const Project& project)
    {
        // linguist-vendored leaves the dependencies out of the GitHub language statistics
        return "*.cara text eol=lf\n" + project.folders.libraries.generic_string() + "/** linguist-vendored\n";
    }

    static std::string ReadmeText(const Project& project)
    {
        return std::format(
            "# {0}\n"
            "\n"
            "## Build\n"
            "\n"
            "```\n"
            "caracal help                   # lists the commands\n"
            "caracal build                  # builds into {1}/Debug\n"
            "caracal run                    # builds and runs\n"
            "caracal build -mode Release    # builds into {1}/Release\n"
            "caracal update                 # updates {2}/Core from the compiler\n"
            "```\n",
            project.name,
            project.folders.output.generic_string(),
            project.folders.libraries.generic_string());
    }

    static std::string VisualStudioCodeTasksText()
    {
        nlohmann::ordered_json errorLine;
        errorLine["regexp"] = R"(^\s*(Error|Warning)\s+(\S+)\s+(.+?)\s*$)";
        errorLine["severity"] = 1;
        errorLine["code"] = 2;
        errorLine["message"] = 3;

        // the line after the error ends in "[C:/Hello/main.cara:3:12]"
        nlohmann::ordered_json locationLine;
        locationLine["regexp"] = R"(^\s*\S+\[(.+):(\d+):(\d+)\])";
        locationLine["file"] = 1;
        locationLine["line"] = 2;
        locationLine["column"] = 3;

        nlohmann::ordered_json buildTask;
        buildTask["label"] = "caracal build";
        buildTask["type"] = "shell";
        buildTask["command"] = "caracal";
        buildTask["args"] = nlohmann::ordered_json::array({ "build" });
        buildTask["group"]["kind"] = "build";
        buildTask["group"]["isDefault"] = true;
        buildTask["problemMatcher"]["owner"] = "caracal";
        buildTask["problemMatcher"]["fileLocation"] = "absolute";
        buildTask["problemMatcher"]["pattern"] = nlohmann::ordered_json::array({ errorLine, locationLine });

        nlohmann::ordered_json tasksFile;
        tasksFile["version"] = "2.0.0";
        tasksFile["tasks"] = nlohmann::ordered_json::array({ buildTask });

        return tasksFile.dump(2) + "\n";
    }

    static std::string VisualStudioCodeExtensionsText()
    {
        nlohmann::ordered_json extensionsFile;
        extensionsFile["recommendations"] = nlohmann::ordered_json::array({ "EditorConfig.EditorConfig" });

        return extensionsFile.dump(2) + "\n";
    }

    static ProjectResult Failure(std::string error)
    {
        return { std::nullopt, std::move(error) };
    }

    static std::string_view ProjectTypeText(ProjectType type)
    {
        switch (type)
        {
            case ProjectType::Executable:
            {
                return "exe";
            }
        }

        return {};
    }

    static std::string ProjectVersion(std::chrono::year_month_day day)
    {
        return std::format(
            "{}.{}.{}",
            static_cast<i32>(day.year()),
            static_cast<u32>(day.month()),
            static_cast<u32>(day.day()));
    }

    static std::string CreateProjectFileText(const Project& project)
    {
        nlohmann::ordered_json projectFile;
        projectFile["name"] = project.name;
        projectFile["type"] = ProjectTypeText(project.type);
        projectFile["version"] = project.version;
        projectFile["folders"]["libraries"] = project.folders.libraries.generic_string();
        projectFile["folders"]["temp"] = project.folders.temp.generic_string();
        projectFile["folders"]["output"] = project.folders.output.generic_string();

        auto dependencies = nlohmann::ordered_json::array();
        for (const auto& dependency : project.dependencies)
        {
            nlohmann::ordered_json entry;
            entry["path"] = dependency.path.generic_string();
            if (dependency.isPrelude)
                entry["prelude"] = true;

            dependencies.push_back(std::move(entry));
        }
        projectFile["dependencies"] = std::move(dependencies);

        for (const auto& mode : project.modes)
        {
            if (mode.isDefault)
                projectFile["modes"][mode.name]["default"] = true;

            projectFile["modes"][mode.name]["optimize"] = optimizationLevelToString(mode.optimizationLevel);
            projectFile["modes"][mode.name]["debugInfo"] = mode.debugInfo;
            projectFile["modes"][mode.name]["output"] = mode.outputFolder;
        }

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
        const std::optional<std::string>& name,
        std::chrono::year_month_day creationDay,
        const std::filesystem::path& compilerDirectory)
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

        const auto compilerCore = compilerDirectory / "Core";
        if (!std::filesystem::is_directory(compilerCore))
            return Failure("Couldn't create the project, the Core folder in " + compilerDirectory.string() + " is missing");

        std::error_code errorCode;
        std::filesystem::create_directories(projectDirectory, errorCode);
        if (errorCode)
            return Failure("Couldn't create " + projectDirectory.string() + ", " + errorCode.message());

        if (!WriteIfMissing(projectDirectory / "main.cara", MainSource))
            return Failure("Couldn't write " + (projectDirectory / "main.cara").string());

        if (!WriteIfMissing(projectDirectory / ".gitignore", GitIgnore))
            return Failure("Couldn't write " + (projectDirectory / ".gitignore").string());

        if (!WriteIfMissing(projectDirectory / ".editorconfig", EditorConfigText(projectDirectory)))
            return Failure("Couldn't write " + (projectDirectory / ".editorconfig").string());

        Project project{ projectName, projectDirectory, ProjectType::Executable, ProjectVersion(creationDay), {}, { { "Core", false } }, defaultProjectModes() };

        if (!WriteIfMissing(projectDirectory / ".gitattributes", GitAttributesText(project)))
            return Failure("Couldn't write " + (projectDirectory / ".gitattributes").string());

        if (!WriteIfMissing(projectDirectory / "README.md", ReadmeText(project)))
            return Failure("Couldn't write " + (projectDirectory / "README.md").string());

        const auto visualStudioCodeDirectory = projectDirectory / ".vscode";
        std::filesystem::create_directories(visualStudioCodeDirectory, errorCode);
        if (errorCode)
            return Failure("Couldn't create " + visualStudioCodeDirectory.string() + ", " + errorCode.message());

        if (!WriteIfMissing(visualStudioCodeDirectory / "tasks.json", VisualStudioCodeTasksText()))
            return Failure("Couldn't write " + (visualStudioCodeDirectory / "tasks.json").string());

        if (!WriteIfMissing(visualStudioCodeDirectory / "extensions.json", VisualStudioCodeExtensionsText()))
            return Failure("Couldn't write " + (visualStudioCodeDirectory / "extensions.json").string());

        const auto librariesDirectory = projectDirectory / project.folders.libraries;
        std::filesystem::create_directories(librariesDirectory, errorCode);
        if (!errorCode)
        {
            const auto copyOptions = std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing;
            std::filesystem::copy(compilerCore, librariesDirectory / "Core", copyOptions, errorCode);
        }

        if (errorCode)
            return Failure("Couldn't copy Core into " + librariesDirectory.string() + ", " + errorCode.message());

        if (!File::writeText(projectFilePath, CreateProjectFileText(project)))
            return Failure("Couldn't write " + projectFilePath.string());

        return { std::move(project), {} };
    }
}
