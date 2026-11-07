#pragma once

#include <Caracal/Semantic/Type.h>

#include <vector>

namespace Caracal
{
    struct OverloadSet
    {
        std::vector<Type> overloads;

        [[nodiscard]] bool isOverloaded() const noexcept { return overloads.size() > 1; }
        [[nodiscard]] Type firstOverload() const noexcept { return overloads.front(); }
    };

    [[nodiscard]] inline Type tryGetOverload(const OverloadSet* overloadSet) noexcept
    {
        if (overloadSet == nullptr || overloadSet->isOverloaded())
        {
            return Type::Undefined();
        }

        return overloadSet->firstOverload();
    }
}
