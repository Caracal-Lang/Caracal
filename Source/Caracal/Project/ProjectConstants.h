#pragma once

#include <Caracal/API.h>
#include <Caracal/Project/Project.h>
#include <string>
#include <string_view>
#include <vector>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::string_view projectConstantTypeName(const ProjectConstant& constant);
    [[nodiscard]] CARACAL_API std::vector<ProjectConstant> projectConstantsForMode(const Project& project, const ProjectMode& mode);
    [[nodiscard]] CARACAL_API std::string createProjectConstantsSource(const std::vector<ProjectConstant>& constants);
}
