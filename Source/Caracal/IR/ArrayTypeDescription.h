#pragma once

#include <Caracal/Defines.h>
#include <Caracal/Semantic/Type.h>

namespace Caracal
{
    struct ArrayTypeDescription
    {
        Type elementType;
        i32 length;
    };
}
