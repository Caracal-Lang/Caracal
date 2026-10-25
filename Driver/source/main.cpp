#include <Caracal/Compiler.h>
#include <Caracal/Project/ProjectCreator.h>
#include <Caracal/Project/ProjectLoader.h>
#include <Caracal/Project/ProjectRepository.h>
#include <Caracal/Project/ProjectUpdater.h>
#include <Caracal/Version.h>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

void printHelp()
{
    std::cout << "Caracal " << CARA_VERSION << "\n";
    std::cout << "\n";
    std::cout << "Usage:\n";
    std::cout << "  caracal <file.cara> [-ast] [-ir] [-llvm]       builds and runs the resulting <file>.exe\n";
    std::cout << "  caracal new [name] [-no-git]                   creates a project in the current folder or in [name]\n";
    std::cout << "  caracal build [path] [-mode <name>]            builds the project in the current folder, or in [path]\n";
    std::cout << "  caracal run [path] [-mode <name>]              builds and runs the project in the current folder, or in [path]\n";
    std::cout << "  caracal update [path] [-dry]                   updates the libraries of the project in the current folder, or in [path]\n";
    std::cout << "  caracal help\n";
}

std::chrono::year_month_day today()
{
    const auto now = std::time(nullptr);
    std::tm localTime{};
#ifdef _WIN32
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    return std::chrono::year(localTime.tm_year + 1900) / (localTime.tm_mon + 1) / localTime.tm_mday;
}

int createNewProject(int argc, char* argv[])
{
    auto shouldInitializeRepository = true;
    std::optional<std::string> name{};
    for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
    {
        const auto argument = std::string_view(argv[argumentIndex]);
        if (argument == "-no-git")
        {
            shouldInitializeRepository = false;
        }
        else if (argument.starts_with("-"))
        {
            std::cout << "Unknown option " << argument << ". Use \"caracal help\" to see the available options.\n";
            return 1;
        }
        else if (name.has_value())
        {
            std::cout << "\"caracal new\" takes one name but got " << name.value() << " and " << argument << ".\n";
            return 1;
        }
        else
        {
            name = std::string(argument);
        }
    }

    const auto result = Caracal::createProject(std::filesystem::current_path(), name, today(), Caracal::compilerDirectory());
    if (!result.project.has_value())
    {
        std::cout << result.error << "\n";
        return 1;
    }

    if (shouldInitializeRepository)
    {
        Caracal::initializeRepository(result.project->rootDirectory);
    }

    const auto& projectName = result.project->name;
    const auto projectDirectory = result.project->rootDirectory.string();
    std::cout << "Created the project " << projectName << " in " << projectDirectory << "\n";
    return 0;
}

struct ProjectArguments
{
    Caracal::Project project;
    std::string modeName;
};

std::optional<ProjectArguments> findProjectFromArguments(int argc, char* argv[])
{
    std::string modeName{ "Debug" };
    std::optional<std::filesystem::path> path{};
    for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
    {
        const auto argument = std::string_view(argv[argumentIndex]);
        if (argument == "-mode")
        {
            if (argumentIndex + 1 == argc)
            {
                std::cout << "-mode has to be followed by a mode from caracal.json, for example -mode Release.\n";
                return std::nullopt;
            }

            modeName = argv[++argumentIndex];
        }
        else if (argument.starts_with("-"))
        {
            std::cout << "Unknown option " << argument << ". Use \"caracal help\" to see the available options.\n";
            return std::nullopt;
        }
        else if (path.has_value())
        {
            std::cout << "\"caracal " << argv[1] << "\" takes one path but got " << path->string() << " and " << argument << ".\n";
            return std::nullopt;
        }
        else
        {
            path = argument;
        }
    }

    const auto result = Caracal::findProject(std::filesystem::current_path(), path);
    if (!result.project.has_value())
    {
        std::cout << result.error << "\n";
        return std::nullopt;
    }

    return ProjectArguments{ result.project.value(), modeName };
}

int buildProject(int argc, char* argv[])
{
    const auto projectArguments = findProjectFromArguments(argc, argv);
    if (!projectArguments.has_value())
    {
        return 1;
    }

    return Caracal::buildProject(projectArguments->project, projectArguments->modeName);
}

int runProject(int argc, char* argv[])
{
    const auto projectArguments = findProjectFromArguments(argc, argv);
    if (!projectArguments.has_value())
    {
        return 1;
    }

    return Caracal::runProject(projectArguments->project, projectArguments->modeName);
}

void printUpdatedFiles(std::string_view label, const std::vector<std::filesystem::path>& filePaths)
{
    for (const auto& filePath : filePaths)
    {
        std::cout << "  " << label << " " << filePath.generic_string() << "\n";
    }
}

int updateProject(int argc, char* argv[])
{
    auto isDryRun = false;
    std::optional<std::filesystem::path> path{};
    for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
    {
        const auto argument = std::string_view(argv[argumentIndex]);
        if (argument == "-dry")
        {
            isDryRun = true;
        }
        else if (argument.starts_with("-"))
        {
            std::cout << "Unknown option " << argument << ". Use \"caracal help\" to see the available options.\n";
            return 1;
        }
        else if (path.has_value())
        {
            std::cout << "\"caracal update\" takes one path but got " << path->string() << " and " << argument << ".\n";
            return 1;
        }
        else
        {
            path = argument;
        }
    }

    const auto result = Caracal::findProject(std::filesystem::current_path(), path);
    if (!result.project.has_value())
    {
        std::cout << result.error << "\n";
        return 1;
    }

    const auto& project = result.project.value();
    const auto update = Caracal::updateProject(project, Caracal::compilerDirectory(), isDryRun);
    if (!update.error.empty())
    {
        std::cout << update.error << "\n";
        return 1;
    }

    for (const auto& dependency : project.dependencies)
    {
        if (dependency.isPrelude)
        {
            std::cout << "Skipped " << (project.folders.libraries / dependency.path).generic_string() << ".\n";
        }
    }

    if (update.coreCopy.empty())
    {
        std::cout << "Nothing to update, \"dependencies\" in caracal.json has no Core.\n";
        return 0;
    }

    const auto coreCopy = update.coreCopy.generic_string();
    if (update.addedFiles.empty() && update.changedFiles.empty() && update.removedFiles.empty())
    {
        std::cout << coreCopy << " is up to date.\n";
        return 0;
    }

    if (isDryRun)
    {
        std::cout << "Would update " << coreCopy << ":\n";
    }
    else
    {
        std::cout << "Updated " << coreCopy << ":\n";
    }

    printUpdatedFiles("added  ", update.addedFiles);
    printUpdatedFiles("changed", update.changedFiles);
    printUpdatedFiles("removed", update.removedFiles);
    return 0;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "Caracal " << CARA_VERSION << "\n";
        std::cout << "Use \"caracal help\" to see the available commands.\n";
        return 1;
    }

    if (std::string_view(argv[1]) == "help")
    {
        printHelp();
        return 0;
    }

    if (std::string_view(argv[1]) == "new")
    {
        return createNewProject(argc, argv);
    }

    if (std::string_view(argv[1]) == "build")
    {
        return buildProject(argc, argv);
    }

    if (std::string_view(argv[1]) == "run")
    {
        return runProject(argc, argv);
    }

    if (std::string_view(argv[1]) == "update")
    {
        return updateProject(argc, argv);
    }

    std::filesystem::path parameterFilePath{};
    Caracal::PrintOptions printOptions{};
    for (int argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
    {
        const auto argument = std::string_view(argv[argumentIndex]);
        if (argument == "-ast")
        {
            printOptions.printParseTree = true;
        }
        else if (argument == "-ir")
        {
            printOptions.printIR = true;
        }
        else if (argument == "-llvm")
        {
            printOptions.printLLVMIR = true;
        }
        else if (argument.starts_with("-"))
        {
            std::cout << "Unknown option " << argument << ". Use \"caracal help\" to see the available options.\n";
            return 1;
        }
        else if (!parameterFilePath.empty())
        {
            std::cout << "Only one file can be compiled at a time, got " << parameterFilePath.string() << " and " << argument << ".\n";
            return 1;
        }
        else
        {
            parameterFilePath = argument;
        }
    }

    if (parameterFilePath.empty())
    {
        std::cout << "No file to compile. Use \"caracal help\" to see the available commands.\n";
        return 1;
    }

    const auto absolutePath = std::filesystem::absolute(parameterFilePath);
    if (!std::filesystem::exists(absolutePath))
    {
        std::cout << "The parameter is not a valid file.\n";
        return 1;
    }

    return Caracal::compileFile(absolutePath, Caracal::DiagnosticOptions{}, printOptions);
}
