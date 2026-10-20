#pragma once

#include <Caracal/IR/Instruction.h>

namespace Caracal
{
    struct PhiInput
    {
        BlockId blockId = -1;
        TemporaryId value = -1;
    };
}
