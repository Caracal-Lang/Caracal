#pragma once

#include <Caracal/API.h>

#include <string>
#include <string_view>

namespace Caracal
{
    [[nodiscard]] CARACAL_API std::string replaceEscapeSequences(std::string_view input);
}
