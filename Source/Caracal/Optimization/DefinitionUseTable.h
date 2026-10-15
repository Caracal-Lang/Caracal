#pragma once

#include <Caracal/Defines.h>
#include <Caracal/IR/Instruction.h>

#include <vector>

namespace Caracal
{
    struct DefinitionUseTable
    {
        std::vector<i32> useCounts;
        std::vector<const Instruction*> definitions;
        std::vector<size_t> definitionBlockIndexes;
    };
}
