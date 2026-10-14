#include "NumberLiteralParsing.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace Caracal
{
    std::optional<i32> tryParseI32Literal(std::string_view lexeme)
    {
        i32 value = 0;
        const auto* begin = lexeme.data();
        const auto* end = begin + lexeme.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end)
        {
            return std::nullopt;
        }

        return value;
    }

    std::optional<i32> tryParseNegatedI32Literal(std::string_view lexeme)
    {
        std::int64_t magnitude = 0;
        const auto* begin = lexeme.data();
        const auto* end = begin + lexeme.size();
        const auto result = std::from_chars(begin, end, magnitude);
        if (result.ec != std::errc{} || result.ptr != end)
        {
            return std::nullopt;
        }

        const auto negated = -magnitude;
        if (negated < static_cast<std::int64_t>(std::numeric_limits<i32>::min())
            || negated > static_cast<std::int64_t>(std::numeric_limits<i32>::max()))
        {
            return std::nullopt;
        }

        return static_cast<i32>(negated);
    }

    std::optional<u8> tryParseU8Literal(std::string_view lexeme)
    {
        unsigned int value = 0;
        const auto* begin = lexeme.data();
        const auto* end = begin + lexeme.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end || value > std::numeric_limits<u8>::max())
        {
            return std::nullopt;
        }

        return static_cast<u8>(value);
    }

    std::optional<f64> tryParseF64Literal(std::string_view lexeme)
    {
        f64 value = 0.0;
        const auto* begin = lexeme.data();
        const auto* end = begin + lexeme.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
        {
            return std::nullopt;
        }

        return value;
    }

    std::optional<f32> tryParseF32Literal(std::string_view lexeme)
    {
        f32 value = 0.0f;
        const auto* begin = lexeme.data();
        const auto* end = begin + lexeme.size();
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
        {
            return std::nullopt;
        }

        return value;
    }

    std::optional<NumberLiteralValue> tryParseNumberLiteralValue(std::string_view lexeme, Type type, const SemanticContext& module)
    {
        const auto* description = module.tryGetBuiltinTypeDescription(type);
        if (description == nullptr)
        {
            return std::nullopt;
        }

        if (description->kind == BuiltinTypeKind::Float)
        {
            if (description->bits == 32)
            {
                const auto value = tryParseF32Literal(lexeme);
                if (!value.has_value())
                {
                    return std::nullopt;
                }

                return NumberLiteralValue{ value.value() };
            }

            const auto value = tryParseF64Literal(lexeme);
            if (!value.has_value())
            {
                return std::nullopt;
            }

            return NumberLiteralValue{ value.value() };
        }

        if (description->kind != BuiltinTypeKind::Int)
        {
            return std::nullopt;
        }

        u64 magnitude = 0;
        const auto* begin = lexeme.data();
        const auto* end = begin + lexeme.size();
        const auto result = std::from_chars(begin, end, magnitude);
        if (result.ec != std::errc{} || result.ptr != end)
        {
            return std::nullopt;
        }

        auto maxValue = u64{ 0 };
        if (description->isSigned)
        {
            maxValue = (u64{ 1 } << (description->bits - 1)) - 1;
        }
        else if (description->bits == 64)
        {
            maxValue = std::numeric_limits<u64>::max();
        }
        else
        {
            maxValue = (u64{ 1 } << description->bits) - 1;
        }

        if (magnitude > maxValue)
        {
            return std::nullopt;
        }

        if (description->isSigned)
        {
            if (description->bits <= 8)
            {
                return NumberLiteralValue{ static_cast<i8>(magnitude) };
            }
            if (description->bits <= 16)
            {
                return NumberLiteralValue{ static_cast<i16>(magnitude) };
            }
            if (description->bits <= 32)
            {
                return NumberLiteralValue{ static_cast<i32>(magnitude) };
            }
            return NumberLiteralValue{ static_cast<i64>(magnitude) };
        }

        if (description->bits <= 8)
        {
            return NumberLiteralValue{ static_cast<u8>(magnitude) };
        }
        if (description->bits <= 16)
        {
            return NumberLiteralValue{ static_cast<u16>(magnitude) };
        }
        if (description->bits <= 32)
        {
            return NumberLiteralValue{ static_cast<u32>(magnitude) };
        }
        return NumberLiteralValue{ static_cast<u64>(magnitude) };
    }
}
