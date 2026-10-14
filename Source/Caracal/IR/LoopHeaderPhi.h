#pragma once

#include <Caracal/Defines.h>
#include <Caracal/IR/PhiInstruction.h>

namespace Caracal
{
    struct LoopHeaderPhi
    {
        u32 nameId;
        PhiInstruction* instruction;
    };
}
