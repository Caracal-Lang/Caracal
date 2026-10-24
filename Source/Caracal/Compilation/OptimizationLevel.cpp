#include <Caracal/Compilation/OptimizationLevel.h>

namespace Caracal
{
    std::string_view optimizationLevelToString(OptimizationLevel optimizationLevel) noexcept
    {
        switch (optimizationLevel)
        {
            case OptimizationLevel::None:
            {
                return "none";
            }
            case OptimizationLevel::Speed:
            {
                return "speed";
            }
        }

        return {};
    }

    std::optional<OptimizationLevel> optimizationLevelFromString(std::string_view text) noexcept
    {
        if (text == "none")
            return OptimizationLevel::None;

        if (text == "speed")
            return OptimizationLevel::Speed;

        return std::nullopt;
    }
}
