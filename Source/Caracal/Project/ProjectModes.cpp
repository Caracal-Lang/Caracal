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
            { "Debug", OptimizationLevel::None, "Debug", true },
            { "Release", OptimizationLevel::Speed, "Release", false } };
    }

    ProjectModeResult findProjectMode(const Project& project, std::string_view modeName)
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
}
