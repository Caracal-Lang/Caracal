#include <Caracal/Compiler.h>
#include <Caracal/Project/ProjectCreator.h>
#include <Caracal/Project/ProjectLoader.h>
#include <Caracal/Version.h>
#include <chrono>
#include <ctime>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

void printHelp()
{
    std::cout << "Caracal " << CARA_VERSION << "\n";
    std::cout << "\n";
    std::cout << "Usage:\n";
    std::cout << "  caracal <file.cara> [-ast] [-ir] [-llvm]       builds and runs the resulting <file>.exe\n";
    std::cout << "  caracal new [name]                             creates a project in the current folder or in [name] \n";
    std::cout << "  caracal build [path]                           builds the project in the current folder, or in [path]\n";
    std::cout << "  caracal run [path]                             builds and runs the project in the current folder, or in [path]\n";
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
    if (argc > 3)
    {
        std::cout << "\"caracal new\" expects a project name but got " << argc - 2 << ".\n";
        return 1;
    }

    std::optional<std::string> name{};
    if (argc == 3)
    {
        name = argv[2];
    }

    const auto result = Caracal::createProject(std::filesystem::current_path(), name, today());
    if (!result.project.has_value())
    {
        std::cout << result.error << "\n";
        return 1;
    }

    const auto& projectName = result.project->name;
    const auto projectDirectory = result.project->rootDirectory.string();
    std::cout << "Created the project " << projectName << " in " << projectDirectory << "\n";
    return 0;
}

std::optional<Caracal::Project> findProjectFromArguments(int argc, char* argv[])
{
    if (argc > 3)
    {
        std::cout << "\"caracal " << argv[1] << "\" takes a path but got " << argc - 2 << ".\n";
        return std::nullopt;
    }

    std::optional<std::filesystem::path> path{};
    if (argc == 3)
    {
        path = argv[2];
    }

    const auto result = Caracal::findProject(std::filesystem::current_path(), path);
    if (!result.project.has_value())
    {
        std::cout << result.error << "\n";
    }

    return result.project;
}

int buildProject(int argc, char* argv[])
{
    const auto project = findProjectFromArguments(argc, argv);
    if (!project.has_value())
    {
        return 1;
    }

    return Caracal::buildProject(project.value());
}

int runProject(int argc, char* argv[])
{
    const auto project = findProjectFromArguments(argc, argv);
    if (!project.has_value())
    {
        return 1;
    }

    return Caracal::runProject(project.value());
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
