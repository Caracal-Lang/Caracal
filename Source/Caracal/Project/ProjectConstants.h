#pragma once

#include <Caracal/API.h>
#include <Caracal/Project/Project.h>
#include <string>
#include <vector>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::string createProjectConstantsSource(const std::vector<ProjectConstant>& constants);
}
