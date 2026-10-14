#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>

#include <optional>
#include <variant>

namespace Caracal
{
    using FoldValue = std::variant<bool, u8, u16, u32, u64, i8, i16, i32, i64, float, f64>;

    enum class FoldResultKind
    {
        Value,
        Overflow,
        DivideByZero,
        NotFoldable,
    };

    struct FoldResult
    {
        FoldResultKind kind;
        FoldValue value;
    };

    using BinaryFoldFunction = FoldResult (*)(const FoldValue& lhs, const FoldValue& rhs);
    using UnaryFoldFunction = FoldResult (*)(const FoldValue& value);

    [[nodiscard]] CARACAL_API FoldResult foldAddition(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldSubtraction(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldMultiplication(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldFloatDivision(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldIntegerDivision(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldEqual(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldNotEqual(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldLessThan(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldLessOrEqual(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldGreaterThan(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldGreaterOrEqual(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldLogicalAnd(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldLogicalOr(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldValueNegation(const FoldValue& value);
    [[nodiscard]] CARACAL_API FoldResult foldLogicalNegation(const FoldValue& value);
    [[nodiscard]] CARACAL_API FoldResult foldBitAnd(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldBitOr(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldBitXor(const FoldValue& lhs, const FoldValue& rhs);
    [[nodiscard]] CARACAL_API FoldResult foldBitNot(const FoldValue& value);
    [[nodiscard]] CARACAL_API FoldResult foldShiftLeft(const FoldValue& value, const FoldValue& amount);
    [[nodiscard]] CARACAL_API FoldResult foldShiftRight(const FoldValue& value, const FoldValue& amount);
}
