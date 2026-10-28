#include <CaraTest.h>

#include <Caracal/Semantic/BuiltinTypeDescription.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Semantic/SemanticContext.h>

#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

using Caracal::BuiltinTypeDescription;
using Caracal::BuiltinTypeKind;
using Caracal::NumberLiteralValue;

static void I32LiteralsParse(const std::string& lexeme, i32 expected)
{
    CaraTest::isTrue(Caracal::tryParseI32Literal(lexeme) == expected);
}

static std::vector<std::tuple<std::string, i32>> I32LiteralsParse_Data()
{
    return {
        std::make_tuple("0", 0),
        std::make_tuple("2147483647", 2147483647),
    };
}

static void I32LiteralsNotParse(const std::string& lexeme)
{
    CaraTest::isFalse(Caracal::tryParseI32Literal(lexeme).has_value());
}

static std::vector<std::tuple<std::string>> I32LiteralsNotParse_Data()
{
    return {
        std::make_tuple("2147483648"),
        std::make_tuple("1.5"),
        std::make_tuple("12a"),
        std::make_tuple(""),
    };
}

static void NegatedI32LiteralsParse(const std::string& lexeme, i32 expected)
{
    CaraTest::isTrue(Caracal::tryParseNegatedI32Literal(lexeme) == expected);
}

static std::vector<std::tuple<std::string, i32>> NegatedI32LiteralsParse_Data()
{
    return {
        std::make_tuple("5", -5),
        std::make_tuple("0", 0),
        std::make_tuple("2147483648", std::numeric_limits<i32>::min()),
    };
}

static void NegatedI32LiteralsNotParse(const std::string& lexeme)
{
    CaraTest::isFalse(Caracal::tryParseNegatedI32Literal(lexeme).has_value());
}

static std::vector<std::tuple<std::string>> NegatedI32LiteralsNotParse_Data()
{
    return {
        std::make_tuple("2147483649"),
        std::make_tuple("1.5"),
        std::make_tuple(""),
    };
}

static void U8LiteralsParse(const std::string& lexeme, u8 expected)
{
    CaraTest::isTrue(Caracal::tryParseU8Literal(lexeme) == expected);
}

static std::vector<std::tuple<std::string, u8>> U8LiteralsParse_Data()
{
    return {
        std::make_tuple("0", u8{ 0 }),
        std::make_tuple("255", u8{ 255 }),
    };
}

static void U8LiteralsNotParse(const std::string& lexeme)
{
    CaraTest::isFalse(Caracal::tryParseU8Literal(lexeme).has_value());
}

static std::vector<std::tuple<std::string>> U8LiteralsNotParse_Data()
{
    return {
        std::make_tuple("256"),
        std::make_tuple("-1"),
        std::make_tuple(""),
    };
}

static void F64LiteralsParse(const std::string& lexeme, f64 expected)
{
    CaraTest::isTrue(Caracal::tryParseF64Literal(lexeme) == expected);
}

static std::vector<std::tuple<std::string, f64>> F64LiteralsParse_Data()
{
    return {
        std::make_tuple("1.5", 1.5),
        std::make_tuple("0.1", 0.1),
        std::make_tuple("1", 1.0),
    };
}

static void F64LiteralsNotParse(const std::string& lexeme)
{
    CaraTest::isFalse(Caracal::tryParseF64Literal(lexeme).has_value());
}

static std::vector<std::tuple<std::string>> F64LiteralsNotParse_Data()
{
    return {
        std::make_tuple("1" + std::string(309, '0')),
        std::make_tuple("1.5x"),
        std::make_tuple(""),
    };
}

static void F32LiteralsParse(const std::string& lexeme, f32 expected)
{
    CaraTest::isTrue(Caracal::tryParseF32Literal(lexeme) == expected);
}

static std::vector<std::tuple<std::string, f32>> F32LiteralsParse_Data()
{
    return {
        std::make_tuple("1.5", 1.5f),
        std::make_tuple("0.1", 0.1f),
        std::make_tuple("340282346638528859811704183484516925440", std::numeric_limits<f32>::max()),
    };
}

static void F32LiteralsNotParse(const std::string& lexeme)
{
    CaraTest::isFalse(Caracal::tryParseF32Literal(lexeme).has_value());
}

static std::vector<std::tuple<std::string>> F32LiteralsNotParse_Data()
{
    return {
        std::make_tuple("350000000000000000000000000000000000000"),
        std::make_tuple("1.5x"),
        std::make_tuple(""),
    };
}

static const BuiltinTypeDescription I8Description{ BuiltinTypeKind::Int, 8, true };
static const BuiltinTypeDescription I16Description{ BuiltinTypeKind::Int, 16, true };
static const BuiltinTypeDescription I32Description{ BuiltinTypeKind::Int, 32, true };
static const BuiltinTypeDescription I64Description{ BuiltinTypeKind::Int, 64, true };
static const BuiltinTypeDescription U8Description{ BuiltinTypeKind::Int, 8, false };
static const BuiltinTypeDescription U16Description{ BuiltinTypeKind::Int, 16, false };
static const BuiltinTypeDescription U32Description{ BuiltinTypeKind::Int, 32, false };
static const BuiltinTypeDescription U64Description{ BuiltinTypeKind::Int, 64, false };

static void IntegerLiteralsParse(const std::string& lexeme, BuiltinTypeDescription description, NumberLiteralValue expected)
{
    CaraTest::isTrue(Caracal::tryParseIntegerLiteral(lexeme, description) == expected);
}

static std::vector<std::tuple<std::string, BuiltinTypeDescription, NumberLiteralValue>> IntegerLiteralsParse_Data()
{
    return {
        std::make_tuple("127", I8Description, NumberLiteralValue{ i8{ 127 } }),
        std::make_tuple("32767", I16Description, NumberLiteralValue{ i16{ 32767 } }),
        std::make_tuple("2147483647", I32Description, NumberLiteralValue{ i32{ 2147483647 } }),
        std::make_tuple("9223372036854775807", I64Description, NumberLiteralValue{ std::numeric_limits<i64>::max() }),
        std::make_tuple("255", U8Description, NumberLiteralValue{ u8{ 255 } }),
        std::make_tuple("65535", U16Description, NumberLiteralValue{ u16{ 65535 } }),
        std::make_tuple("4294967295", U32Description, NumberLiteralValue{ u32{ 4294967295 } }),
        std::make_tuple("18446744073709551615", U64Description, NumberLiteralValue{ std::numeric_limits<u64>::max() }),
        std::make_tuple("0", U16Description, NumberLiteralValue{ u16{ 0 } }),
    };
}

static void IntegerLiteralsNotParse(const std::string& lexeme, BuiltinTypeDescription description)
{
    CaraTest::isFalse(Caracal::tryParseIntegerLiteral(lexeme, description).has_value());
}

static std::vector<std::tuple<std::string, BuiltinTypeDescription>> IntegerLiteralsNotParse_Data()
{
    return {
        std::make_tuple("128", I8Description),
        std::make_tuple("32768", I16Description),
        std::make_tuple("2147483648", I32Description),
        std::make_tuple("9223372036854775808", I64Description),
        std::make_tuple("256", U8Description),
        std::make_tuple("65536", U16Description),
        std::make_tuple("4294967296", U32Description),
        std::make_tuple("18446744073709551616", U64Description),
        std::make_tuple("-1", I32Description),
        std::make_tuple("1.5", I32Description),
        std::make_tuple("", I32Description),
    };
}

static void NegatedIntegerLiteralsParse(const std::string& lexeme, BuiltinTypeDescription description, NumberLiteralValue expected)
{
    CaraTest::isTrue(Caracal::tryParseNegatedIntegerLiteral(lexeme, description) == expected);
}

static std::vector<std::tuple<std::string, BuiltinTypeDescription, NumberLiteralValue>> NegatedIntegerLiteralsParse_Data()
{
    return {
        std::make_tuple("128", I8Description, NumberLiteralValue{ std::numeric_limits<i8>::min() }),
        std::make_tuple("32768", I16Description, NumberLiteralValue{ std::numeric_limits<i16>::min() }),
        std::make_tuple("2147483648", I32Description, NumberLiteralValue{ std::numeric_limits<i32>::min() }),
        std::make_tuple("9223372036854775808", I64Description, NumberLiteralValue{ std::numeric_limits<i64>::min() }),
        std::make_tuple("5", I8Description, NumberLiteralValue{ i8{ -5 } }),
        std::make_tuple("0", I32Description, NumberLiteralValue{ i32{ 0 } }),
        std::make_tuple("0", U8Description, NumberLiteralValue{ u8{ 0 } }),
        std::make_tuple("0", U64Description, NumberLiteralValue{ u64{ 0 } }),
    };
}

static void NegatedIntegerLiteralsNotParse(const std::string& lexeme, BuiltinTypeDescription description)
{
    CaraTest::isFalse(Caracal::tryParseNegatedIntegerLiteral(lexeme, description).has_value());
}

static std::vector<std::tuple<std::string, BuiltinTypeDescription>> NegatedIntegerLiteralsNotParse_Data()
{
    return {
        std::make_tuple("129", I8Description),
        std::make_tuple("32769", I16Description),
        std::make_tuple("2147483649", I32Description),
        std::make_tuple("9223372036854775809", I64Description),
        std::make_tuple("1", U8Description),
        std::make_tuple("1", U64Description),
        std::make_tuple("-1", I32Description),
        std::make_tuple("1.5", I32Description),
        std::make_tuple("", I32Description),
    };
}

static Caracal::SemanticContext CreateNumberTypes()
{
    auto context = Caracal::SemanticContext::WithBuiltins();
    static_cast<void>(context.createBuiltinTypeFromDescription("i8", { BuiltinTypeKind::Int, 8, true }));
    static_cast<void>(context.createBuiltinTypeFromDescription("u64", { BuiltinTypeKind::Int, 64, false }));
    static_cast<void>(context.createBuiltinTypeFromDescription("f32", { BuiltinTypeKind::Float, 32, true }));
    static_cast<void>(context.createBuiltinTypeFromDescription("f64", { BuiltinTypeKind::Float, 64, true }));
    static_cast<void>(context.createBuiltinTypeFromDescription("bool", { BuiltinTypeKind::Bool, 1, false }));
    return context;
}

static void NumberLiteralValuesParse(const std::string& typeName, const std::string& lexeme, NumberLiteralValue expected)
{
    const auto context = CreateNumberTypes();
    const auto type = context.tryGetTypeByName(typeName);
    CaraTest::isTrue(Caracal::tryParseNumberLiteralValue(lexeme, type, context) == expected);
}

static std::vector<std::tuple<std::string, std::string, NumberLiteralValue>> NumberLiteralValuesParse_Data()
{
    return {
        std::make_tuple("i8", "127", NumberLiteralValue{ i8{ 127 } }),
        std::make_tuple("u64", "18446744073709551615", NumberLiteralValue{ std::numeric_limits<u64>::max() }),
        std::make_tuple("f32", "1.5", NumberLiteralValue{ 1.5f }),
        std::make_tuple("f32", "2", NumberLiteralValue{ 2.0f }),
        std::make_tuple("f64", "1.5", NumberLiteralValue{ 1.5 }),
    };
}

static void NumberLiteralValuesNotParse(const std::string& typeName, const std::string& lexeme)
{
    const auto context = CreateNumberTypes();
    const auto type = context.tryGetTypeByName(typeName);
    CaraTest::isFalse(Caracal::tryParseNumberLiteralValue(lexeme, type, context).has_value());
}

static std::vector<std::tuple<std::string, std::string>> NumberLiteralValuesNotParse_Data()
{
    return {
        std::make_tuple("i8", "128"),
        std::make_tuple("f32", "350000000000000000000000000000000000000"),
        std::make_tuple("i8", "1.5"),
        std::make_tuple("bool", "1"),
        std::make_tuple("void", "1"),
    };
}

static const auto tests =
{
    CaraTest::addTest("I32LiteralsParse", I32LiteralsParse, I32LiteralsParse_Data),
    CaraTest::addTest("I32LiteralsNotParse", I32LiteralsNotParse, I32LiteralsNotParse_Data),
    CaraTest::addTest("NegatedI32LiteralsParse", NegatedI32LiteralsParse, NegatedI32LiteralsParse_Data),
    CaraTest::addTest("NegatedI32LiteralsNotParse", NegatedI32LiteralsNotParse, NegatedI32LiteralsNotParse_Data),
    CaraTest::addTest("U8LiteralsParse", U8LiteralsParse, U8LiteralsParse_Data),
    CaraTest::addTest("U8LiteralsNotParse", U8LiteralsNotParse, U8LiteralsNotParse_Data),
    CaraTest::addTest("F64LiteralsParse", F64LiteralsParse, F64LiteralsParse_Data),
    CaraTest::addTest("F64LiteralsNotParse", F64LiteralsNotParse, F64LiteralsNotParse_Data),
    CaraTest::addTest("F32LiteralsParse", F32LiteralsParse, F32LiteralsParse_Data),
    CaraTest::addTest("F32LiteralsNotParse", F32LiteralsNotParse, F32LiteralsNotParse_Data),
    CaraTest::addTest("IntegerLiteralsParse", IntegerLiteralsParse, IntegerLiteralsParse_Data),
    CaraTest::addTest("IntegerLiteralsNotParse", IntegerLiteralsNotParse, IntegerLiteralsNotParse_Data),
    CaraTest::addTest("NegatedIntegerLiteralsParse", NegatedIntegerLiteralsParse, NegatedIntegerLiteralsParse_Data),
    CaraTest::addTest("NegatedIntegerLiteralsNotParse", NegatedIntegerLiteralsNotParse, NegatedIntegerLiteralsNotParse_Data),
    CaraTest::addTest("NumberLiteralValuesParse", NumberLiteralValuesParse, NumberLiteralValuesParse_Data),
    CaraTest::addTest("NumberLiteralValuesNotParse", NumberLiteralValuesNotParse, NumberLiteralValuesNotParse_Data),
};
