#pragma once

#include <functional>
#include <string_view>

namespace Caracal
{
    // https://www.fluentcpp.com/2017/06/09/search-set-another-type-key/
    // the is_transparent definition allows us to search a string keyed map with a string_view
    // unordered_map<std::string, T, TransparentStringHash, std::equal_to<>>.
    struct TransparentStringHash
    {
        using is_transparent = void;
        [[nodiscard]] size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
    };
}
