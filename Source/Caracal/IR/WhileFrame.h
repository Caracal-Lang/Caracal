#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/LocalsMap.h>
#include <Caracal/IR/LoopHeaderPhi.h>

#include <vector>

namespace Caracal
{
    struct WhileFrame
    {
        bool active = false;
        BlockId entryBlockId = -1;
        BlockId conditionBlockId = -1;
        BlockId loopBlockId = -1;
        BlockId continuationBlockId = -1;
        BlockId conditionExitBlockId = -1;
        LocalsMap preLoopValues;
        LocalsMap loopHeaderValues;
        std::vector<LoopHeaderPhi> loopHeaderPhis;
    };
}
