#include <Caracal/Optimization/OperatorFolding.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace Caracal
{
    namespace
    {
        template <typename TPayload>
        constexpr bool IsSubWordInteger = std::is_integral_v<TPayload> && !std::is_same_v<TPayload, bool>
            && !std::is_same_v<TPayload, i64> && !std::is_same_v<TPayload, u64>;

        template <typename TInteger>
        FoldResult MakeIntegerResult(std::int64_t result)
        {
            if (result >= static_cast<std::int64_t>(std::numeric_limits<TInteger>::min())
                && result <= static_cast<std::int64_t>(std::numeric_limits<TInteger>::max()))
            {
                return FoldResult{ FoldResultKind::Value, FoldValue{ static_cast<TInteger>(result) } };
            }

            return FoldResult{ FoldResultKind::Overflow, FoldValue{ static_cast<TInteger>(result) } };
        }

        enum class ArithmeticOperation
        {
            Addition,
            Subtraction,
            Multiplication,
        };

        FoldResult FoldArithmetic(ArithmeticOperation operation, const FoldValue& lhs, const FoldValue& rhs)
        {
            return std::visit(
                [operation](const auto& left, const auto& right) -> FoldResult
                {
                    using Left = std::decay_t<decltype(left)>;
                    using Right = std::decay_t<decltype(right)>;

                    if constexpr (!std::is_same_v<Left, Right>)
                    {
                        return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                    }
                    else if constexpr (std::is_same_v<Left, float> || std::is_same_v<Left, f64>)
                    {
                        auto result = Left{ 0 };
                        switch (operation)
                        {
                            case ArithmeticOperation::Addition:
                            {
                                result = left + right;
                                break;
                            }
                            case ArithmeticOperation::Subtraction:
                            {
                                result = left - right;
                                break;
                            }
                            case ArithmeticOperation::Multiplication:
                            {
                                result = left * right;
                                break;
                            }
                        }

                        return FoldResult{ FoldResultKind::Value, FoldValue{ result } };
                    }
                    else if constexpr (IsSubWordInteger<Left>)
                    {
                        const auto leftValue = static_cast<std::int64_t>(left);
                        const auto rightValue = static_cast<std::int64_t>(right);
                        std::int64_t result = 0;
                        switch (operation)
                        {
                            case ArithmeticOperation::Addition:
                            {
                                result = leftValue + rightValue;
                                break;
                            }
                            case ArithmeticOperation::Subtraction:
                            {
                                result = leftValue - rightValue;
                                break;
                            }
                            case ArithmeticOperation::Multiplication:
                            {
                                result = leftValue * rightValue;
                                break;
                            }
                        }

                        return MakeIntegerResult<Left>(result);
                    }
                    else if constexpr (std::is_same_v<Left, i64>)
                    {
                        // 64 bit operands cannot borrow a wider space, compute unsigned for defined wrap and detect overflow explicitly
                        const auto leftValue = static_cast<u64>(left);
                        const auto rightValue = static_cast<u64>(right);
                        auto wrapped = u64{ 0 };
                        auto overflowed = false;
                        switch (operation)
                        {
                            case ArithmeticOperation::Addition:
                            {
                                wrapped = leftValue + rightValue;
                                overflowed = ((static_cast<i64>(wrapped) ^ left) & (static_cast<i64>(wrapped) ^ right)) < 0;
                                break;
                            }
                            case ArithmeticOperation::Subtraction:
                            {
                                wrapped = leftValue - rightValue;
                                overflowed = ((left ^ right) & (left ^ static_cast<i64>(wrapped))) < 0;
                                break;
                            }
                            case ArithmeticOperation::Multiplication:
                            {
                                wrapped = leftValue * rightValue;
                                if (left == -1)
                                {
                                    overflowed = right == std::numeric_limits<i64>::min();
                                }
                                else if (left != 0)
                                {
                                    overflowed = static_cast<i64>(wrapped) / left != right;
                                }
                                break;
                            }
                        }

                        if (overflowed)
                        {
                            return FoldResult{ FoldResultKind::Overflow, FoldValue{ static_cast<i64>(wrapped) } };
                        }

                        return FoldResult{ FoldResultKind::Value, FoldValue{ static_cast<i64>(wrapped) } };
                    }
                    else if constexpr (std::is_same_v<Left, u64>)
                    {
                        auto wrapped = u64{ 0 };
                        auto overflowed = false;
                        switch (operation)
                        {
                            case ArithmeticOperation::Addition:
                            {
                                wrapped = left + right;
                                overflowed = wrapped < left;
                                break;
                            }
                            case ArithmeticOperation::Subtraction:
                            {
                                wrapped = left - right;
                                overflowed = left < right;
                                break;
                            }
                            case ArithmeticOperation::Multiplication:
                            {
                                wrapped = left * right;
                                overflowed = right != 0 && wrapped / right != left;
                                break;
                            }
                        }

                        if (overflowed)
                        {
                            return FoldResult{ FoldResultKind::Overflow, FoldValue{ wrapped } };
                        }

                        return FoldResult{ FoldResultKind::Value, FoldValue{ wrapped } };
                    }
                    else
                    {
                        return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                    }
                },
                lhs,
                rhs);
        }

        enum class ComparisonOperation
        {
            Equal,
            NotEqual,
            LessThan,
            LessOrEqual,
            GreaterThan,
            GreaterOrEqual,
        };

        // IEEE float uses ordered semantics for comparisons, which means NaN compares are false
        // except for not equals but we dont define those in the prelude for floats
        FoldResult FoldComparison(ComparisonOperation operation, const FoldValue& lhs, const FoldValue& rhs)
        {
            return std::visit(
                [operation](const auto& left, const auto& right) -> FoldResult
                {
                    using Left = std::decay_t<decltype(left)>;
                    using Right = std::decay_t<decltype(right)>;

                    if constexpr (!std::is_same_v<Left, Right>)
                    {
                        return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                    }
                    else if constexpr (std::is_same_v<Left, bool>)
                    {
                        switch (operation)
                        {
                            case ComparisonOperation::Equal:
                            {
                                return FoldResult{ FoldResultKind::Value, FoldValue{ left == right } };
                            }
                            case ComparisonOperation::NotEqual:
                            {
                                return FoldResult{ FoldResultKind::Value, FoldValue{ left != right } };
                            }
                            default:
                            {
                                return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                            }
                        }
                    }
                    else
                    {
                        auto result = false;
                        switch (operation)
                        {
                            case ComparisonOperation::Equal:
                            {
                                result = left == right;
                                break;
                            }
                            case ComparisonOperation::NotEqual:
                            {
                                result = left != right;
                                break;
                            }
                            case ComparisonOperation::LessThan:
                            {
                                result = left < right;
                                break;
                            }
                            case ComparisonOperation::LessOrEqual:
                            {
                                result = left <= right;
                                break;
                            }
                            case ComparisonOperation::GreaterThan:
                            {
                                result = left > right;
                                break;
                            }
                            case ComparisonOperation::GreaterOrEqual:
                            {
                                result = left >= right;
                                break;
                            }
                        }

                        return FoldResult{ FoldResultKind::Value, FoldValue{ result } };
                    }
                },
                lhs,
                rhs);
        }

        FoldResult FoldLogical(bool isAnd, const FoldValue& lhs, const FoldValue& rhs)
        {
            return std::visit(
                [isAnd](const auto& left, const auto& right) -> FoldResult
                {
                    using Left = std::decay_t<decltype(left)>;
                    using Right = std::decay_t<decltype(right)>;

                    if constexpr (std::is_same_v<Left, bool> && std::is_same_v<Right, bool>)
                    {
                        if (isAnd)
                        {
                            return FoldResult{ FoldResultKind::Value, FoldValue{ left && right } };
                        }

                        return FoldResult{ FoldResultKind::Value, FoldValue{ left || right } };
                    }
                    else
                    {
                        return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                    }
                },
                lhs,
                rhs);
        }
    }

    FoldResult foldAddition(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldArithmetic(ArithmeticOperation::Addition, lhs, rhs);
    }

    FoldResult foldSubtraction(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldArithmetic(ArithmeticOperation::Subtraction, lhs, rhs);
    }

    FoldResult foldMultiplication(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldArithmetic(ArithmeticOperation::Multiplication, lhs, rhs);
    }

    FoldResult foldFloatDivision(const FoldValue& lhs, const FoldValue& rhs)
    {
        return std::visit(
            [](const auto& left, const auto& right) -> FoldResult
            {
                using Left = std::decay_t<decltype(left)>;
                using Right = std::decay_t<decltype(right)>;

                if constexpr (!std::is_same_v<Left, Right>)
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
                else if constexpr (std::is_same_v<Left, float> || std::is_same_v<Left, f64>)
                {
                    if (right == Left{ 0 })
                    {
                        return FoldResult{ FoldResultKind::DivideByZero, FoldValue{} };
                    }

                    return FoldResult{ FoldResultKind::Value, FoldValue{ left / right } };
                }
                else if constexpr (IsSubWordInteger<Left>)
                {
                    if (right == 0)
                    {
                        return FoldResult{ FoldResultKind::DivideByZero, FoldValue{} };
                    }

                    return FoldResult{ FoldResultKind::Value, FoldValue{ static_cast<float>(left) / static_cast<float>(right) } };
                }
                else if constexpr (std::is_same_v<Left, i64> || std::is_same_v<Left, u64>)
                {
                    // 64 bit division produces a double
                    if (right == 0)
                    {
                        return FoldResult{ FoldResultKind::DivideByZero, FoldValue{} };
                    }

                    return FoldResult{ FoldResultKind::Value, FoldValue{ static_cast<f64>(left) / static_cast<f64>(right) } };
                }
                else
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
            },
            lhs,
            rhs);
    }

    FoldResult foldIntegerDivision(const FoldValue& lhs, const FoldValue& rhs)
    {
        return std::visit(
            [](const auto& left, const auto& right) -> FoldResult
            {
                using Left = std::decay_t<decltype(left)>;
                using Right = std::decay_t<decltype(right)>;

                if constexpr (!std::is_same_v<Left, Right> || !std::is_integral_v<Left> || std::is_same_v<Left, bool>)
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
                else
                {
                    if (right == 0)
                    {
                        return FoldResult{ FoldResultKind::DivideByZero, FoldValue{} };
                    }

                    // the smallest signed value divided by -1 does not fit the type
                    if constexpr (std::is_signed_v<Left>)
                    {
                        if (left == std::numeric_limits<Left>::min() && right == Left{ -1 })
                        {
                            return FoldResult{ FoldResultKind::Overflow, FoldValue{} };
                        }
                    }

                    // truncates toward zero like C
                    return FoldResult{ FoldResultKind::Value, FoldValue{ static_cast<Left>(left / right) } };
                }
            },
            lhs,
            rhs);
    }

    FoldResult foldEqual(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldComparison(ComparisonOperation::Equal, lhs, rhs);
    }

    FoldResult foldNotEqual(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldComparison(ComparisonOperation::NotEqual, lhs, rhs);
    }

    FoldResult foldLessThan(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldComparison(ComparisonOperation::LessThan, lhs, rhs);
    }

    FoldResult foldLessOrEqual(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldComparison(ComparisonOperation::LessOrEqual, lhs, rhs);
    }

    FoldResult foldGreaterThan(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldComparison(ComparisonOperation::GreaterThan, lhs, rhs);
    }

    FoldResult foldGreaterOrEqual(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldComparison(ComparisonOperation::GreaterOrEqual, lhs, rhs);
    }

    FoldResult foldLogicalAnd(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldLogical(true, lhs, rhs);
    }

    FoldResult foldLogicalOr(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldLogical(false, lhs, rhs);
    }

    FoldResult foldValueNegation(const FoldValue& value)
    {
        return std::visit(
            [](const auto& payload) -> FoldResult
            {
                using Payload = std::decay_t<decltype(payload)>;

                if constexpr (std::is_same_v<Payload, float> || std::is_same_v<Payload, f64>)
                {
                    return FoldResult{ FoldResultKind::Value, FoldValue{ -payload } };
                }
                else if constexpr (IsSubWordInteger<Payload>)
                {
                    return MakeIntegerResult<Payload>(-static_cast<std::int64_t>(payload));
                }
                else if constexpr (std::is_same_v<Payload, i64>)
                {
                    // negating the minimum wraps back onto itself, that counts as overflow
                    if (payload == std::numeric_limits<i64>::min())
                    {
                        return FoldResult{ FoldResultKind::Overflow, FoldValue{ payload } };
                    }

                    return FoldResult{ FoldResultKind::Value, FoldValue{ -payload } };
                }
                else
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
            },
            value);
    }

    FoldResult foldLogicalNegation(const FoldValue& value)
    {
        return std::visit(
            [](const auto& payload) -> FoldResult
            {
                using Payload = std::decay_t<decltype(payload)>;

                if constexpr (std::is_same_v<Payload, bool>)
                {
                    return FoldResult{ FoldResultKind::Value, FoldValue{ !payload } };
                }
                else
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
            },
            value);
    }

    template <typename TOperation>
    [[nodiscard]] static FoldResult FoldBitwiseBinary(const FoldValue& lhs, const FoldValue& rhs, TOperation operation)
    {
        return std::visit(
            [&operation](const auto& left, const auto& right) -> FoldResult
            {
                using Left = std::decay_t<decltype(left)>;
                using Right = std::decay_t<decltype(right)>;

                if constexpr (!std::is_same_v<Left, Right>)
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
                else if constexpr (std::is_integral_v<Left> && !std::is_same_v<Left, bool>)
                {
                    return FoldResult{ FoldResultKind::Value, FoldValue{ static_cast<Left>(operation(left, right)) } };
                }
                else
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
            },
            lhs,
            rhs);
    }

    FoldResult foldBitAnd(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldBitwiseBinary(lhs, rhs, [](auto left, auto right) { return left & right; });
    }

    FoldResult foldBitOr(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldBitwiseBinary(lhs, rhs, [](auto left, auto right) { return left | right; });
    }

    FoldResult foldBitXor(const FoldValue& lhs, const FoldValue& rhs)
    {
        return FoldBitwiseBinary(lhs, rhs, [](auto left, auto right) { return left ^ right; });
    }

    FoldResult foldBitNot(const FoldValue& value)
    {
        return std::visit(
            [](const auto& payload) -> FoldResult
            {
                using Payload = std::decay_t<decltype(payload)>;

                if constexpr (std::is_integral_v<Payload> && !std::is_same_v<Payload, bool>)
                {
                    return FoldResult{ FoldResultKind::Value, FoldValue{ static_cast<Payload>(~payload) } };
                }
                else
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
            },
            value);
    }

    template <typename TOperation>
    [[nodiscard]] static FoldResult FoldShift(const FoldValue& value, const FoldValue& amount, TOperation operation)
    {
        return std::visit(
            [&operation](const auto& left, const auto& right) -> FoldResult
            {
                using Left = std::decay_t<decltype(left)>;
                using Right = std::decay_t<decltype(right)>;

                // the amount is always an i32, masked to the operand width exactly like the runtime
                if constexpr (!std::is_same_v<Right, i32>)
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
                else if constexpr (std::is_integral_v<Left> && !std::is_same_v<Left, bool>)
                {
                    constexpr auto widthMask = static_cast<i32>(sizeof(Left) * 8 - 1);
                    return FoldResult{ FoldResultKind::Value, FoldValue{ static_cast<Left>(operation(left, right & widthMask)) } };
                }
                else
                {
                    return FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
                }
            },
            value,
            amount);
    }

    FoldResult foldShiftLeft(const FoldValue& value, const FoldValue& amount)
    {
        // shifting in unsigned space matches the runtime wraparound for signed values
        return FoldShift(value, amount, [](auto left, i32 maskedAmount) 
            { 
                using Unsigned = std::make_unsigned_t<decltype(left)>; 
                return static_cast<Unsigned>(left) << maskedAmount; 
            });
    }

    FoldResult foldShiftRight(const FoldValue& value, const FoldValue& amount)
    {
        // signed values shift arithmetic and unsigned values shift logical, matching the emitted instructions
        return FoldShift(value, amount, [](auto left, i32 maskedAmount) 
            { 
                return left >> maskedAmount; 
            });
    }
}
