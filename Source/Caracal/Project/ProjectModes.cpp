#include <Caracal/Project/ProjectModes.h>

namespace Caracal
{
    static std::string ModeNames(const Project& project)
    {
        std::string names{};
        for (size_t index = 0; index < project.modes.size(); ++index)
        {
            if (index > 0 && index + 1 == project.modes.size())
            {
                names += " and ";
            }
            else if (index > 0)
            {
                names += ", ";
            }

            names += project.modes[index].name;
        }

        return names;
    }

    static std::string LibraryNames(const Project& project)
    {
        std::vector<std::string> libraries{};
        for (const auto& dependency : project.dependencies)
        {
            if (!dependency.isPrelude)
            {
                libraries.push_back(dependency.path.generic_string());
            }
        }

        if (libraries.empty())
        {
            return "no libraries";
        }

        std::string names{};
        for (size_t index = 0; index < libraries.size(); ++index)
        {
            if (index > 0 && index + 1 == libraries.size())
            {
                names += " and ";
            }
            else if (index > 0)
            {
                names += ", ";
            }

            names += libraries[index];
        }

        return names;
    }

    std::vector<ProjectMode> defaultProjectModes()
    {
        return {
            { .name = "Debug", .optimizationLevel = OptimizationLevel::None, .outputFolder = "Debug", .debugInfo = true, .isDefault = true },
            { .name = "Release", .optimizationLevel = OptimizationLevel::Speed, .outputFolder = "Release" },
            { .name = "Test", .optimizationLevel = OptimizationLevel::None, .outputFolder = "Test", .debugInfo = true, .test = true } };
    }

    static ProjectModeResult FindDefaultMode(const Project& project)
    {
        for (const auto& mode : project.modes)
        {
            if (mode.isDefault)
            {
                return { mode, {} };
            }
        }

        return { std::nullopt, "No mode in caracal.json has \"default\": true, pass one with -mode, the project has " + ModeNames(project) };
    }

    static ProjectModeResult FindNamedMode(const Project& project, std::string_view modeName)
    {
        for (const auto& mode : project.modes)
        {
            if (mode.name == modeName)
            {
                return { mode, {} };
            }
        }

        return { std::nullopt, "There is no mode \"" + std::string(modeName) + "\" in caracal.json, the project has " + ModeNames(project) };
    }

    ProjectModeResult findProjectMode(const Project& project, std::optional<std::string_view> modeName)
    {
        if (!modeName.has_value())
            return FindDefaultMode(project);

        return FindNamedMode(project, modeName.value());
    }

    ProjectModeResult findProjectTestMode(const Project& project)
    {
        for (const auto& mode : project.modes)
        {
            if (mode.test)
            {
                return { mode, {} };
            }
        }

        return { std::nullopt, "No mode in caracal.json has \"test\": true, the project has " + ModeNames(project) };
    }

    ListedLibrariesResult findListedLibraries(const Project& project, const TestSelection& testSelection)
    {
        if (testSelection.allLibraries && !testSelection.libraries.empty())
        {
            return { {}, "Use -all or -libs, not both" };
        }

        ListedLibrariesResult result{};
        for (const auto& library : testSelection.libraries)
        {
            auto isDependency = false;
            for (const auto& dependency : project.dependencies)
            {
                if (!dependency.isPrelude && dependency.path.generic_string() == library)
                {
                    result.dependencyPaths.push_back(dependency.path);
                    isDependency = true;
                    break;
                }
            }

            if (!isDependency)
            {
                return { {}, "\"" + library + "\" is not a library in caracal.json, the project has " + LibraryNames(project) };
            }
        }

        return result;
    }
}
