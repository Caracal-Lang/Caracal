#pragma once

#include <Caracal/Defines.h>

#include <limits>
#include <vector>

namespace Caracal
{
    struct DefinitionUseTable
    {
        static constexpr u32 NoDefinitionInstruction = std::numeric_limits<u32>::max();

        std::vector<i32> useCounts;
        std::vector<u32> definitionInstructions;
    };
}
