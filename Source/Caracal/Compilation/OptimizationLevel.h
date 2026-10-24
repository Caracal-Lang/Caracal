#pragma once

#include <Caracal/API.h>

#include <optional>
#include <string_view>

namespace Caracal
{
    enum class OptimizationLevel
    {
        None,
        Speed,
    };

    [[nodiscard]] CARACAL_API std::string_view optimizationLevelToString(OptimizationLevel optimizationLevel) noexcept;
    [[nodiscard]] CARACAL_API std::optional<OptimizationLevel> optimizationLevelFromString(std::string_view text) noexcept;
}
