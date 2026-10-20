#include <Caracal/IR/ConstantPool.h>

#include <bit>
#include <type_traits>
#include <utility>
#include <variant>

namespace Caracal
{
    template<typename TValue>
    [[nodiscard]] static u64 ScalarBits(const TValue& value) noexcept
    {
        if constexpr (std::is_same_v<TValue, f32>)
        {
            return std::bit_cast<u32>(value);
        }
        else if constexpr (std::is_same_v<TValue, f64>)
        {
            return std::bit_cast<u64>(value);
        }
        else if constexpr (std::is_same_v<TValue, std::string>)
        {
            return 0;
        }
        else if constexpr (std::is_signed_v<TValue>)
        {
            return static_cast<u64>(static_cast<i64>(value));
        }
        else
        {
            return static_cast<u64>(value);
        }
    }

    template<size_t... TIndices>
    [[nodiscard]] static constexpr bool FoldValueMatchesLiteralData(std::index_sequence<TIndices...>) noexcept
    {
        return (std::is_same_v<std::variant_alternative_t<TIndices, FoldValue>, std::variant_alternative_t<TIndices, ConstantValue::LiteralData>> && ...);
    }

    static_assert(FoldValueMatchesLiteralData(std::make_index_sequence<std::variant_size_v<FoldValue>>{}),
        "LiteralData must start with the types of FoldValue, in the same order");

    u64 scalarConstantBits(const ConstantValue::LiteralData& literal) noexcept
    {
        return std::visit([](const auto& value) { return ScalarBits(value); }, literal);
    }

    u64 scalarConstantBits(const FoldValue& value) noexcept
    {
        return std::visit([](const auto& payload) { return ScalarBits(payload); }, value);
    }

    ConstantValue::LiteralData scalarConstantFromBits(u32 alternative, u64 bits) noexcept
    {
        using LiteralData = ConstantValue::LiteralData;
        switch (alternative)
        {
            case 0:
            {
                return LiteralData{ std::in_place_index<0>, bits != 0 };
            }
            case 1:
            {
                return LiteralData{ std::in_place_index<1>, static_cast<u8>(bits) };
            }
            case 2:
            {
                return LiteralData{ std::in_place_index<2>, static_cast<u16>(bits) };
            }
            case 3:
            {
                return LiteralData{ std::in_place_index<3>, static_cast<u32>(bits) };
            }
            case 4:
            {
                return LiteralData{ std::in_place_index<4>, bits };
            }
            case 5:
            {
                return LiteralData{ std::in_place_index<5>, static_cast<i8>(bits) };
            }
            case 6:
            {
                return LiteralData{ std::in_place_index<6>, static_cast<i16>(bits) };
            }
            case 7:
            {
                return LiteralData{ std::in_place_index<7>, static_cast<i32>(bits) };
            }
            case 8:
            {
                return LiteralData{ std::in_place_index<8>, static_cast<i64>(bits) };
            }
            case 9:
            {
                return LiteralData{ std::in_place_index<9>, std::bit_cast<f32>(static_cast<u32>(bits)) };
            }
            case 10:
            {
                return LiteralData{ std::in_place_index<10>, std::bit_cast<f64>(bits) };
            }
            default:
            {
                return LiteralData{ std::in_place_index<0>, false };
            }
        }
    }

    StringRange ConstantPool::addString(std::string_view text)
    {
        const auto range = StringRange{ static_cast<u32>(m_characters.size()), static_cast<u32>(text.size()) };
        m_characters.append(text);
        return range;
    }

    std::string_view ConstantPool::string(StringRange range) const noexcept
    {
        return std::string_view{ m_characters }.substr(range.offset, range.length);
    }

    u32 ConstantPool::addConstant(ConstantValue constant)
    {
        m_constants.push_back(std::move(constant));
        return static_cast<u32>(m_constants.size() - 1);
    }
}
