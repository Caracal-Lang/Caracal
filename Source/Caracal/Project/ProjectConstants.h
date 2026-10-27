#pragma once

#include <Caracal/API.h>
#include <Caracal/Project/Project.h>
#include <string>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::string createProjectConstantsSource(const Project& project);
}
