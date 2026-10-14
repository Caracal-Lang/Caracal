#pragma once

#include <Caracal/API.h>

#include <string>

namespace Caracal
{
    enum class MethodModifier
    {
        None,
        Public,
        Private,
        Static
    };

    [[nodiscard]] CARACAL_API std::string stringify(MethodModifier modifier);
}
