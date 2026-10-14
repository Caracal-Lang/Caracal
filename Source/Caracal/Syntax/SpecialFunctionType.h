#pragma once

#include <Caracal/API.h>

#include <string>

namespace Caracal
{
    enum class SpecialFunctionType
    {
        None,
        Constructor,
        Destructor,
    };

    [[nodiscard]] CARACAL_API std::string stringify(SpecialFunctionType specialFunctionType);
}
