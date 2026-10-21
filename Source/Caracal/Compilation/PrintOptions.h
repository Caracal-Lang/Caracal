#pragma once

#include <Caracal/API.h>

namespace Caracal
{
    struct CARACAL_API PrintOptions
    {
        bool printParseTree = false;
        bool printIR = false;
        bool printLLVMIR = false;
    };
}
