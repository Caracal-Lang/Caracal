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

    std::vector<ProjectMode> defaultProjectModes()
    {
        return {
            { "Debug", OptimizationLevel::None, "Debug", true, true },
            { "Release", OptimizationLevel::Speed, "Release", false, false } };
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
}
