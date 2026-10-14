#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    struct ShortCircuitFrame
    {
        BlockId conditionBlockId = -1;
        BlockId rightBlockId = -1;
        ValueRef leftValue{};
        bool isLogicalAnd = false;
        bool active = false;
    };
}
