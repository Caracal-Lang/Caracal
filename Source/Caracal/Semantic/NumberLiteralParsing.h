#pragma once

#include <Caracal/Semantic/SemanticContext.h>

#include <optional>
#include <string_view>
#include <variant>

namespace Caracal
{
    using NumberLiteralValue = std::variant<u8, u16, u32, u64, i8, i16, i32, i64, f32, f64>;

    [[nodiscard]] std::optional<i32> tryParseI32Literal(std::string_view lexeme);
    [[nodiscard]] std::optional<i32> tryParseNegatedI32Literal(std::string_view lexeme);
    [[nodiscard]] std::optional<u8> tryParseU8Literal(std::string_view lexeme);
    [[nodiscard]] std::optional<f64> tryParseF64Literal(std::string_view lexeme);
    [[nodiscard]] std::optional<f32> tryParseF32Literal(std::string_view lexeme);
    [[nodiscard]] std::optional<NumberLiteralValue> tryParseNumberLiteralValue(std::string_view lexeme, Type type, const SemanticContext& module);
}
