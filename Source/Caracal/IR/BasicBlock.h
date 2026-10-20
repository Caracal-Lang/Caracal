#pragma once

#include <Caracal/Defines.h>
#include <Caracal/IR/BlockLabel.h>
#include <Caracal/IR/Instruction.h>

namespace Caracal
{
    struct BasicBlock
    {
        BlockId id = -1;
        BlockLabel label = BlockLabel::Entry;
        u32 instructionStart = 0;
        u32 instructionCount = 0;
    };
}
