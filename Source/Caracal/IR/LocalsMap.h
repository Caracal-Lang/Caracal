#pragma once

#include <Caracal/Defines.h>
#include <Caracal/IR/LocalState.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace Caracal
{
    class LocalsMap
    {
    public:
        using Entry = std::pair<u32, LocalState>;

        void clear() noexcept { m_entries.clear(); }
        [[nodiscard]] size_t size() const noexcept { return m_entries.size(); }
        [[nodiscard]] auto begin() const noexcept { return m_entries.begin(); }
        [[nodiscard]] auto end() const noexcept { return m_entries.end(); }

        [[nodiscard]] const LocalState* tryGet(u32 nameId) const noexcept
        {
            const auto entry = lowerBound(nameId);
            if (entry == m_entries.end() || entry->first != nameId)
            {
                return nullptr;
            }
            return &entry->second;
        }

        [[nodiscard]] LocalState* tryGetMutable(u32 nameId) noexcept
        {
            const auto entry = lowerBound(nameId);
            if (entry == m_entries.end() || entry->first != nameId)
            {
                return nullptr;
            }
            return &m_entries[static_cast<size_t>(entry - m_entries.begin())].second;
        }

        [[nodiscard]] bool contains(u32 nameId) const noexcept { return tryGet(nameId) != nullptr; }

        [[nodiscard]] const LocalState& get(u32 nameId) const noexcept
        {
            static const LocalState MissingLocal{ ValueRef{}, Type::Undefined(), LocalStorageKind::Value };
            const auto* state = tryGet(nameId);
            if (state == nullptr)
            {
                return MissingLocal;
            }
            return *state;
        }

        void set(u32 nameId, LocalState state)
        {
            const auto entry = lowerBound(nameId);
            if (entry != m_entries.end() && entry->first == nameId)
            {
                m_entries[static_cast<size_t>(entry - m_entries.begin())].second = state;
                return;
            }
            m_entries.insert(entry, Entry{ nameId, state });
        }

        void reserve(size_t count) { m_entries.reserve(count); }
        void append(u32 nameId, LocalState state) { m_entries.emplace_back(nameId, state); }

        void finishBuild()
        {
            std::sort(m_entries.begin(), m_entries.end(),
                [](const Entry& left, const Entry& right) { return left.first < right.first; });
        }

    private:
        [[nodiscard]] std::vector<Entry>::const_iterator lowerBound(u32 nameId) const noexcept
        {
            return std::lower_bound(m_entries.begin(), m_entries.end(), nameId,
                [](const Entry& entry, u32 id) { return entry.first < id; });
        }

        std::vector<Entry> m_entries;
    };
}
