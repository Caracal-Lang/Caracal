#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/LocalsMap.h>

namespace Caracal
{
    struct IfFrame
    {
        bool active = false;
        bool hasElse = false;
        BlockId conditionExitBlockId = -1;
        BlockId trueBlockId = -1;
        BlockId falseBlockId = -1;
        BlockId continuationBlockId = -1;
        LocalsMap preBranchValues;
        BlockId trueExitBlockId = -1;
        bool trueFallsThrough = false;
        LocalsMap trueExitValues;
    };
}
