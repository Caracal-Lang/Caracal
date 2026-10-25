#pragma once

#include <Caracal/API.h>
#include <string>

namespace Caracal
{
    struct CARACAL_API TypeCheckerOptions
    {
        std::string defaultIntegerType{ "i32" };
        std::string defaultFloatingType{ "f32" };
        std::string defaultEnumBaseType{ "u8" };
        bool recordsSourceLocations = false;
    };
}
