#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>

#include <string_view>

namespace Caracal
{
    enum class BlockLabel : u8
    {
        Entry,
        IfTrue,
        IfFalse,
        IfContinuation,
        WhileCondition,
        WhileBody,
        WhileContinuation,
        AndRhs,
        AndMerge,
        OrRhs,
        OrMerge,
    };

    [[nodiscard]] CARACAL_API std::string_view blockLabelText(BlockLabel label) noexcept;
}
