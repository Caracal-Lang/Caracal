#pragma once

#include <Caracal/API.h>

#include <string>

namespace Caracal
{
    CARACAL_API void replaceNonAsciiCharacters(std::string& text);
}
