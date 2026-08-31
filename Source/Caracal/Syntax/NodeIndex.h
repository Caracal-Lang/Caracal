#pragma once

#include <Caracal/Defines.h>

namespace Caracal
{
    // node index 0 is valid, so we need another value
    inline constexpr u32 InvalidNodeIndex = 0xFFFFFFFF;

    struct NodeIndex
    {
        u16 fileId = 0;
        u32 index = InvalidNodeIndex;
    };

    [[nodiscard]] inline bool operator==(NodeIndex lhs, NodeIndex rhs) noexcept
    {
        return lhs.fileId == rhs.fileId && lhs.index == rhs.index;
    }
}
