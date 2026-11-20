#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <functional>

namespace Caracal
{
    enum class TypeKind
    {
        Invalid,
        Builtin,
        Enum,
        Type,
        TypeParameter,
        Function,
        Method,
        Constructor,
        FixedArray,
        DynamicArray,
        Slice
    };

    class CARACAL_API Type
    {
    public:
        Type(i32 id, TypeKind kind);

        [[nodiscard]] i32 id() const noexcept { return m_id; }
        [[nodiscard]] TypeKind kind() const noexcept { return m_kind; }
        [[nodiscard]] bool isReference() const noexcept { return m_id >= 0 && m_id % 2 == 1; }
        [[nodiscard]] bool isOptional() const noexcept { return m_id >= 0 && m_id % 4 >= 2; }
        [[nodiscard]] bool isBaseType() const noexcept { return !isReference() && !isOptional(); }

        [[nodiscard]] Type toBaseType() const noexcept { return m_id >= 0 ? Type(m_id - (m_id % 4), m_kind) : *this; }
        [[nodiscard]] Type toReference() const noexcept { return (isReference() || m_id < 0) ? *this : Type(m_id + 1, m_kind); }
        [[nodiscard]] Type toValue() const noexcept { return isReference() ? Type(m_id - 1, m_kind) : *this; }
        [[nodiscard]] Type toOptional() const noexcept { return (isOptional() || m_id < 0) ? *this : Type(m_id + 2, m_kind); }
        [[nodiscard]] Type toNonOptional() const noexcept { return isOptional() ? Type(m_id - 2, m_kind) : *this; }

        [[nodiscard]] static Type CVariadic() noexcept { return Type(-5, TypeKind::Builtin); }
        [[nodiscard]] static Type Function() noexcept { return Type(-4, TypeKind::Builtin); }
        [[nodiscard]] static Type Discard() noexcept { return Type(-3, TypeKind::Builtin); }
        [[nodiscard]] static Type Undefined() noexcept { return Type(-2, TypeKind::Builtin); }
        [[nodiscard]] static Type Void() noexcept { return Type(-1, TypeKind::Builtin); }

    private:
        i32 m_id;
        TypeKind m_kind;
    };

    CARACAL_API bool operator==(Type lhs, Type rhs) noexcept;
    CARACAL_API bool operator!=(Type lhs, Type rhs) noexcept;
}

namespace std {
    template<>
    struct hash<Caracal::Type>
    {
        std::size_t operator()(const Caracal::Type& t) const noexcept
        {
            return std::hash<int>()(t.id());
        }
    };
}
