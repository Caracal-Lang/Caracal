#pragma once

#include <Caracal/Defines.h>
#include <Caracal/IR/Function.h>
#include <Caracal/Semantic/Type.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace Caracal
{
    struct PreludeFunctionBody
    {
        Function body;
        std::vector<Type> calledPreludeFunctions;
    };

    class PreludeFunctionPool
    {
    public:
        void add(PreludeFunctionBody body)
        {
            const auto id = body.body.id();
            m_bodies.insert_or_assign(id, std::move(body));
        }

        [[nodiscard]] std::optional<PreludeFunctionBody> take(FunctionId id)
        {
            const auto found = m_bodies.find(id);
            if (found == m_bodies.end())
            {
                return std::nullopt;
            }

            auto taken = std::move(found->second);
            m_bodies.erase(found);
            return taken;
        }

        [[nodiscard]] size_t size() const noexcept { return m_bodies.size(); }

    private:
        std::unordered_map<FunctionId, PreludeFunctionBody> m_bodies;
    };
}
