#include <CaraTest.h>
#include <array>
#include <algorithm>
#include <vector>

#include <Caracal/Compilation.h>
#include <Caracal/CompilationContext.h>
#include <Caracal/Syntax/LexerSimd.h>
#include <Caracal/Syntax/Token.h>
#include <Caracal/Syntax/TokenKind.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Text/File.h>
#include <Caracal/Text/SourceText.h>
#include <iostream>

static void ExpectedTokenKind(const std::string& /*testName*/, const std::string& input, TokenKind expectedKind)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto source = std::make_shared<Caracal::SourceText>(input);
    Caracal::DiagnosticsBag diagnostics;

    const auto tokens = Caracal::lex(source, diagnostics);
    const auto& token = tokens.getToken(0);

    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      lex(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CaraTest::areEqual(expectedKind, token.kind);
    // expected token + eof token
    CaraTest::areEqual(2, tokens.size());
}

static std::vector<std::tuple<std::string, std::string, TokenKind>> Symbols_Data()
{
    return {
        std::make_tuple("Plus", "+", TokenKind::Plus),
        std::make_tuple("PercentPlus", "%+", TokenKind::PercentPlus),
        std::make_tuple("Minus", "-", TokenKind::Minus),
        std::make_tuple("PercentMinus", "%-", TokenKind::PercentMinus),
        std::make_tuple("Star", "*", TokenKind::Star),
        std::make_tuple("PercentStar", "%*", TokenKind::PercentStar),
        std::make_tuple("Slash", "/", TokenKind::Slash),

        std::make_tuple("Dot", ".", TokenKind::Dot),
        std::make_tuple("Ellipsis", "...", TokenKind::Ellipsis),
        std::make_tuple("Comma", ",", TokenKind::Comma),
        std::make_tuple("Colon", ":", TokenKind::Colon),
        std::make_tuple("Semicolon", ";", TokenKind::Semicolon),
        std::make_tuple("Underscore", "_", TokenKind::Underscore),
        std::make_tuple("SingleQuote", "'", TokenKind::SingleQuote),
        std::make_tuple("Hash", "#", TokenKind::Hash),
            
        std::make_tuple("Equal", "=", TokenKind::Equal),
        std::make_tuple("EqualEqual", "==", TokenKind::EqualEqual),
        std::make_tuple("Bang", "!", TokenKind::Bang),
        std::make_tuple("BangEqual", "!=", TokenKind::BangEqual),
        std::make_tuple("LessThan", "<", TokenKind::LessThan),
        std::make_tuple("LessThanEqual", "<=", TokenKind::LessThanEqual),
        std::make_tuple("GreaterThan", ">", TokenKind::GreaterThan),
        std::make_tuple("GreaterThanEqual", ">=", TokenKind::GreaterThanEqual),

        std::make_tuple("OpenParenthesis", "(", TokenKind::OpenParenthesis),
        std::make_tuple("CloseParenthesis", ")", TokenKind::CloseParenthesis),
        std::make_tuple("OpenBrace", "{", TokenKind::OpenBrace),
        std::make_tuple("CloseBrace", "}", TokenKind::CloseBrace),
        std::make_tuple("OpenBracket", "[", TokenKind::OpenBracket),
        std::make_tuple("CloseBracket", "]", TokenKind::CloseBracket),

        std::make_tuple("Unknown", "$", TokenKind::Unknown),
    };
}

static std::vector<std::tuple<std::string, std::string, TokenKind>> Keyword_Data()
{
    return {
        std::make_tuple("Def", "def", TokenKind::DefKeyword),
        std::make_tuple("Enum", "enum", TokenKind::EnumKeyword),
        std::make_tuple("Type", "type", TokenKind::TypeKeyword),
        std::make_tuple("If", "if", TokenKind::IfKeyword),
        std::make_tuple("Else", "else", TokenKind::ElseKeyword),
        std::make_tuple("While", "while", TokenKind::WhileKeyword),
        std::make_tuple("Break", "break", TokenKind::BreakKeyword),
        std::make_tuple("Skip", "skip", TokenKind::SkipKeyword),
        std::make_tuple("Return", "return", TokenKind::ReturnKeyword),
        std::make_tuple("True", "true", TokenKind::TrueKeyword),
        std::make_tuple("False", "false", TokenKind::FalseKeyword),
        std::make_tuple("And", "and", TokenKind::AndKeyword),
        std::make_tuple("Or", "or", TokenKind::OrKeyword),
        std::make_tuple("Ref", "ref", TokenKind::RefKeyword),
    };
}

static void IgnoresWhitespaces(const std::string& input)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto source = std::make_shared<Caracal::SourceText>(input);
    Caracal::DiagnosticsBag diagnostics;

    const auto tokens = Caracal::lex(source, diagnostics);
    const auto& token = tokens.getToken(0);

    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      lex(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CaraTest::areEqual(TokenKind::EndOfFile, token.kind);
}

static std::vector<std::tuple<std::string>> IgnoresWhitespaces_Data()
{
    return {
        std::make_tuple(""),
        std::make_tuple(" "),
        std::make_tuple("     "),
        std::make_tuple("\t"),
        std::make_tuple("\r"),
        std::make_tuple("\n"),
        std::make_tuple("\r\n"),
        std::make_tuple("\0")
    };
}

static void Identifiers(const std::string& input, const std::string_view& expectedLexeme, TokenKind expectedKind)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto source = std::make_shared<Caracal::SourceText>(input);
    Caracal::DiagnosticsBag diagnostics;

    const auto tokens = Caracal::lex(source, diagnostics);
    const auto& token = tokens.getToken(0);

    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      lex(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CaraTest::areEqual(expectedKind, token.kind);
    CaraTest::isTrue(isIdentifierKind(token.kind));
    const auto lexeme = tokens.getLexeme(token);
    CaraTest::areEqual(expectedLexeme, lexeme);
}

static std::vector<std::tuple<std::string, std::string_view, TokenKind>> Identifiers_Data()
{
    return {
        std::make_tuple("x", "x", TokenKind::Identifier),
        std::make_tuple("foo", "foo", TokenKind::Identifier),
        std::make_tuple(" bar ", "bar", TokenKind::Identifier),
        std::make_tuple("i32", "i32", TokenKind::Identifier),
        std::make_tuple("use", "use", TokenKind::Identifier),
        std::make_tuple("class", "class", TokenKind::Identifier),
        std::make_tuple("define", "define", TokenKind::Identifier),
        std::make_tuple(" _name", "_name", TokenKind::PrivateIdentifier),
        std::make_tuple("m_index", "m_index", TokenKind::Identifier),
        std::make_tuple("_10", "_10", TokenKind::PrivateIdentifier),
        std::make_tuple("\n returned", "returned", TokenKind::Identifier),
        std::make_tuple("enumeration", "enumeration", TokenKind::Identifier),
        std::make_tuple("init", "init", TokenKind::InitKeyword),
        std::make_tuple("new", "new", TokenKind::NewKeyword),
        std::make_tuple("initial", "initial", TokenKind::Identifier),
        std::make_tuple("news", "news", TokenKind::Identifier),
    };
}

static void Numbers(const std::string& input, const std::string_view& expectedLexeme)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto source = std::make_shared<Caracal::SourceText>(input);
    Caracal::DiagnosticsBag diagnostics;

    const auto tokens = Caracal::lex(source, diagnostics);
    const auto& token = tokens.getToken(0);

    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      lex(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CaraTest::areEqual(TokenKind::Number, token.kind);
    const auto lexeme = tokens.getLexeme(token);
    CaraTest::areEqual(expectedLexeme, lexeme);
}

static std::vector<std::tuple<std::string, std::string_view>> Numbers_Data()
{
    return {
        std::make_tuple("0", "0"),
        std::make_tuple("  1234 ", "1234"),
        std::make_tuple("  1_234 ", "1_234"),
        std::make_tuple("12.", "12"),
        std::make_tuple("12.34", "12.34"),
        std::make_tuple("1_2.3_4", "1_2.3_4"),
        std::make_tuple("12.34_", "12.34_"),
        std::make_tuple("12.34. ", "12.34"),
        std::make_tuple(" 1234567890", "1234567890")
    };
}

static void Strings(const std::string& input, const std::string_view& expectedLexeme)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto source = std::make_shared<Caracal::SourceText>(input);
    Caracal::DiagnosticsBag diagnostics;

    const auto tokens = Caracal::lex(source, diagnostics);
    const auto& token = tokens.getToken(0);

    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      lex(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CaraTest::areEqual(TokenKind::String, token.kind);
    const auto lexeme = tokens.getLexeme(token);
    CaraTest::areEqual(expectedLexeme, lexeme);
}

static std::vector<std::tuple<std::string, std::string_view>> Strings_Data()
{
    return {
        std::make_tuple(" \"\" ", "\"\""),
        std::make_tuple("  \"1234\" ", "\"1234\""),
        std::make_tuple("\"string with whitespace\" ", "\"string with whitespace\""),
        std::make_tuple(" \"1234567890\"", "\"1234567890\"")
    };
}

static std::vector<std::tuple<std::string, std::string_view>> StringsWithEscapes_Data()
{
    return {
        std::make_tuple("\"single quote\\'\"", "\"single quote\\'\""),
        std::make_tuple("\"double quote\\\"\"", "\"double quote\\\"\""),
        std::make_tuple("\"backslash\\\\\"", "\"backslash\\\\\""),
        std::make_tuple("\"audible bell\\a\"", "\"audible bell\\a\""),
        std::make_tuple("\"backspace\\b\"", "\"backspace\\b\""),
        std::make_tuple("\"form feed\\f\"", "\"form feed\\f\""),
        std::make_tuple("\"line feed\\n\"", "\"line feed\\n\""),
        std::make_tuple("\"carriage return\\r\"", "\"carriage return\\r\""),
        std::make_tuple("\"horizonal tab\\t\"", "\"horizonal tab\\t\""),
        std::make_tuple("\"vertical tab\\v\"", "\"vertical tab\\v\""),
    };
}

static void UnterminatedStrings(const std::string& input, const std::string_view& expectedLexeme)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto source = std::make_shared<Caracal::SourceText>(input);
    Caracal::DiagnosticsBag diagnostics;

    const auto tokens = Caracal::lex(source, diagnostics);
    const auto& token = tokens.getToken(0);

    CaraTest::isTrue(!diagnostics.diagnostics().empty());

    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      lex(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CaraTest::areEqual(TokenKind::Error, token.kind);
    const auto lexeme = tokens.getLexeme(token);
    CaraTest::areEqual(expectedLexeme, lexeme);
}

static std::vector<std::tuple<std::string, std::string_view>> UnterminatedStrings_Data()
{
    return {
        std::make_tuple(" \" ", "\" "),
        std::make_tuple("  \"1234 ", "\"1234 "),
        std::make_tuple("\"string with whitespace ", "\"string with whitespace "),
        std::make_tuple(" \"1234567890", "\"1234567890"),
        std::make_tuple("\"a\\", "\"a\\"),
        std::make_tuple("\"\\", "\"\\")
    };
}

static void WhiteSpaceTrivia(const std::string& input, const std::string& expectedTrivia)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto source = std::make_shared<Caracal::SourceText>(input);
    Caracal::DiagnosticsBag diagnostics;

    const auto tokens = Caracal::lex(source, diagnostics);
    const auto& token = tokens.getToken(0);

    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      lex(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    const auto lexeme = tokens.getTrivia(token);
    CaraTest::areEqual(expectedTrivia, lexeme);
}

static std::vector<std::tuple<std::string, std::string>> WhiteSpaceTrivia_Data()
{
    return {
        std::make_tuple(std::string(""), std::string("")),
        std::make_tuple(std::string(" 1234567890"), std::string(" ")),
        std::make_tuple(std::string(" \"hello\""), std::string(" ")),
        std::make_tuple(std::string(" bool"), std::string(" ")),
        std::make_tuple(std::string("   // 1234567890"), std::string("   // 1234567890")),
        std::make_tuple(std::string("// hello\n123"), std::string("// hello\n")),
        std::make_tuple(std::string(" /* block comment */ 123"), std::string(" /* block comment */ ")),
        std::make_tuple(std::string(" 1  id   \"hi\"    "), std::string(" ")),
    };
}

static void WholeInput(const std::string& input, i32 tokenCount)
{
    const auto startTime = std::chrono::high_resolution_clock::now();

    const auto source = std::make_shared<Caracal::SourceText>(input);
    Caracal::DiagnosticsBag diagnostics;

    const auto tokens = Caracal::lex(source, diagnostics);

    const auto endTime = std::chrono::high_resolution_clock::now();
    std::cout << "      lex(): " << CaraTest::stringify(endTime - startTime) << std::endl;

    CaraTest::isTrue(diagnostics.diagnostics().empty());
    CaraTest::areEqual(tokenCount, tokens.size());
}

static std::vector<std::tuple<std::string, i32>> WholeInput_Data()
{
    return {
        std::make_tuple("", 1),
        std::make_tuple("name", 2),
        std::make_tuple("use name", 3),
        std::make_tuple("return (x, y)", 7),
        //std::make_tuple("a = () => 3", 8),
        std::make_tuple("enum Value { A B = 5 C D }", 11),
        std::make_tuple("define sum(a int, b int) { return a + b }", 16),
        std::make_tuple("1%+2", 4),
        std::make_tuple("a %- b", 4),
        std::make_tuple("x%*y", 4)
    };
}

template<typename Primitive>
static void CheckScanCases(Primitive primitive, const std::vector<std::pair<std::string, i32>>& cases)
{
    for (const auto& [input, expected] : cases)
    {
        const auto source = std::make_shared<Caracal::SourceText>(input);
        CaraTest::areEqual(expected, primitive(source->text.data()));
    }
}

static void ScanPrimitives()
{
    namespace Scan = Caracal::LexerScan;

    const std::vector<std::pair<std::string, i32>> identifierCases = {
        { "", 0 },
        { "+abc", 0 },
        { "a+", 1 },
        { "abc_123", 7 },
        { std::string(15, 'a') + "+", 15 },
        { std::string(16, 'a') + "+", 16 },
        { std::string(40, 'a'), 40 },
    };
    CheckScanCases(Scan::Scalar::identifierRunLength, identifierCases);

    const std::vector<std::pair<std::string, i32>> digitCases = {
        { "", 0 },
        { "a1", 0 },
        { "1a", 1 },
        { "1234567890", 10 },
        { std::string(15, '7') + "_", 15 },
        { std::string(16, '7') + "_", 16 },
        { std::string(40, '7'), 40 },
    };
    CheckScanCases(Scan::Scalar::digitRunLength, digitCases);

    const std::vector<std::pair<std::string, i32>> whitespaceCases = {
        { "", 0 },
        { "a ", 0 },
        { " \t\r\nx", 4 },
        { "\v", 0 },
        { std::string(15, ' ') + "x", 15 },
        { std::string(16, ' ') + "x", 16 },
        { std::string(40, ' '), 40 },
    };
    CheckScanCases(Scan::Scalar::whitespaceRunLength, whitespaceCases);

    const std::vector<std::pair<std::string, i32>> stringBodyCases = {
        { "", 0 },
        { "abc", 3 },
        { "\"tail", 0 },
        { "ab\\cd\"", 2 },
        { "ab\ncd", 2 },
        { std::string(15, 'a') + "\"", 15 },
        { std::string(16, 'a') + "\"", 16 },
        { std::string(40, 'a'), 40 },
    };
    CheckScanCases(Scan::Scalar::findAnyOf<'"', '\\', '\r', '\n', '\0'>, stringBodyCases);

    const std::vector<std::pair<std::string, i32>> starSlashCases = {
        { "", 0 },
        { "*/", 0 },
        { "**/", 1 },
        { "* /", 3 },
        { "***", 3 },
        { "*a*/", 2 },
        { std::string(15, 'a') + "*/", 15 },
        { std::string(16, 'a') + "*/", 16 },
    };
    CheckScanCases(Scan::Scalar::findStarSlashOrEOF, starSlashCases);

#if defined(CARACAL_LEXER_SSE2)
    CheckScanCases(Scan::Sse2::identifierRunLength, identifierCases);
    CheckScanCases(Scan::Sse2::digitRunLength, digitCases);
    CheckScanCases(Scan::Sse2::whitespaceRunLength, whitespaceCases);
    CheckScanCases(Scan::Sse2::findAnyOf<'"', '\\', '\r', '\n', '\0'>, stringBodyCases);
    CheckScanCases(Scan::Sse2::findStarSlashOrEOF, starSlashCases);
#endif
}

#if defined(CARACAL_LEXER_SSE2)
template<typename ScalarPrimitive, typename SimdPrimitive>
static i32 CountJumpWalkMismatches(const char* base, i32 size, ScalarPrimitive scalarPrimitive, SimdPrimitive simdPrimitive)
{
    i32 mismatches = 0;
    i32 position = 0;
    while (position <= size)
    {
        const auto scalarResult = scalarPrimitive(base + position);
        const auto simdResult = simdPrimitive(base + position);
        if (scalarResult != simdResult)
        {
            mismatches++;
        }

        position += scalarResult + 1;
    }

    return mismatches;
}

static void CheckFileDifferential(const std::filesystem::path& absolutePath)
{
    namespace Scan = Caracal::LexerScan;

    const auto data = Caracal::File::readText(absolutePath);
    if (!data.has_value())
    {
        CaraTest::fail();
    }

    const auto source = std::make_shared<Caracal::SourceText>(data.value());
    const char* base = source->text.data();
    const auto size = static_cast<i32>(source->text.size());

    i32 mismatches = 0;
    mismatches += CountJumpWalkMismatches(base, size, Scan::Scalar::identifierRunLength, Scan::Sse2::identifierRunLength);
    mismatches += CountJumpWalkMismatches(base, size, Scan::Scalar::digitRunLength, Scan::Sse2::digitRunLength);
    mismatches += CountJumpWalkMismatches(base, size, Scan::Scalar::whitespaceRunLength, Scan::Sse2::whitespaceRunLength);
    mismatches += CountJumpWalkMismatches(base, size,
        Scan::Scalar::findAnyOf<'"', '\\', '\r', '\n', '\0'>, Scan::Sse2::findAnyOf<'"', '\\', '\r', '\n', '\0'>);
    mismatches += CountJumpWalkMismatches(base, size,
        Scan::Scalar::findAnyOf<'\n', '\0'>, Scan::Sse2::findAnyOf<'\n', '\0'>);
    mismatches += CountJumpWalkMismatches(base, size, Scan::Scalar::findStarSlashOrEOF, Scan::Sse2::findStarSlashOrEOF);

    CaraTest::areEqual(0, mismatches);
}
#endif

static void ScanPrimitivesDifferential()
{
#if !defined(CARACAL_LEXER_SSE2)
    CaraTest::skip();
#else
    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = std::filesystem::absolute(currentFilePath.parent_path() / "../../TestData/Input");

    for (const auto& entry : std::filesystem::recursive_directory_iterator(inputDirectory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".cara")
        {
            continue;
        }
        if (entry.path().filename() == "oneMilLines.cara")
        {
            continue;
        }

        CheckFileDifferential(entry.path());
    }

    CheckFileDifferential(inputDirectory / "oneMilLines.cara");
    CheckFileDifferential(inputDirectory / "oneMilLinesOld.txt");
#endif
}

static void CheckBlockEdge(const std::string& input, TokenKind expectedKind, const std::string& expectedLexeme)
{
    const auto source = std::make_shared<Caracal::SourceText>(input);
    Caracal::DiagnosticsBag diagnostics;
    const auto tokens = Caracal::lex(source, diagnostics);
    const auto token = tokens.getToken(0);

    CaraTest::areEqual(expectedKind, token.kind);
    CaraTest::areEqual(std::string_view(expectedLexeme), tokens.getLexeme(token));
    CaraTest::isTrue(diagnostics.diagnostics().empty());
}

static void LongRunBoundaries()
{
    // long whitespace run before a token
    CheckBlockEdge(std::string(70, ' ') + "x", TokenKind::Identifier, "x");

    // whitespace run ending right before a chunk boundary
    CheckBlockEdge(std::string(63, ' ') + "ab", TokenKind::Identifier, "ab");

    // identifier filling exactly four chunks
    CheckBlockEdge(std::string(64, 'a'), TokenKind::Identifier, std::string(64, 'a'));

    // identifier spanning many chunks
    CheckBlockEdge(std::string(130, 'a'), TokenKind::Identifier, std::string(130, 'a'));

    // long gap before a number run
    CheckBlockEdge(std::string(62, ' ') + "1234", TokenKind::Number, "1234");

    // a long string followed by another token
    const auto stringInput = "\"" + std::string(80, 's') + "\" name";
    const auto source = std::make_shared<Caracal::SourceText>(stringInput);
    Caracal::DiagnosticsBag diagnostics;
    const auto tokens = Caracal::lex(source, diagnostics);
    CaraTest::areEqual(TokenKind::String, tokens.getToken(0).kind);
    CaraTest::areEqual(TokenKind::Identifier, tokens.getToken(1).kind);
    CaraTest::areEqual(std::string_view("name"), tokens.getLexeme(tokens.getToken(1)));
}

static void BenchmarkLexFile(const std::filesystem::path& absolutePath, const char* label)
{
    if (!std::filesystem::exists(absolutePath))
    {
        CaraTest::fail();// ("benchmark input missing");
    }

    const auto data = Caracal::File::readText(absolutePath);
    if (!data.has_value())
    {
        CaraTest::fail();// ("could not read benchmark input");
    }

    const auto source = std::make_shared<Caracal::SourceText>(data.value());

    constexpr int RunCount = 20;
    std::vector<double> milliseconds;
    milliseconds.reserve(RunCount);
    for (int run = 0; run < RunCount; run++)
    {
        Caracal::DiagnosticsBag diagnostics;
        const auto startTime = std::chrono::high_resolution_clock::now();
        const auto tokens = Caracal::lex(source, diagnostics);
        const auto endTime = std::chrono::high_resolution_clock::now();

        if (!diagnostics.diagnostics().empty())
        {
            CaraTest::fail();// ("benchmark input must lex without diagnostics");
        }

        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        milliseconds.push_back(static_cast<double>(nanoseconds) / 1000000.0);
    }

    std::sort(milliseconds.begin(), milliseconds.end());
    const auto minimum = milliseconds.front();
    const auto median = milliseconds[milliseconds.size() / 2];
    const auto megabytesPerSecond = (static_cast<double>(data.value().size()) / (1024.0 * 1024.0)) / (minimum / 1000.0);
    std::cout << "      " << label << ": min " << minimum << " ms, median " << median
        << " ms, " << megabytesPerSecond << " MB/s" << std::endl;
}

static void OneMillionLinesTime()
{
#ifdef _DEBUG
    CaraTest::skip();
#endif

    const auto currentFilePath = std::filesystem::path(__FILE__);
    const auto inputDirectory = currentFilePath.parent_path() / "../../TestData/Input";
    BenchmarkLexFile(std::filesystem::absolute(inputDirectory / "oneMilLinesOld.txt"), "oneMilLinesOld  ");
    BenchmarkLexFile(std::filesystem::absolute(inputDirectory / "oneMilLines.cara"), "oneMilLinesShort");
    BenchmarkLexFile(std::filesystem::absolute(inputDirectory / "oneMilLinesLongIdents.cara"), "oneMilLinesLong ");
}

static void TokenResolutionAcrossUnits()
{
    Caracal::DiagnosticsBag diagnostics;
    Caracal::CompilationContext compilationContext;
    compilationContext.addSource("one :: 1;", "one.cara", Caracal::UnitOrigin::User);
    compilationContext.addSource("\ntwotwo :: 22;", "twotwo.cara", Caracal::UnitOrigin::User);
    Caracal::lexAndParse(compilationContext, diagnostics);

    CaraTest::isTrue(!diagnostics.hasErrors());

    const auto& units = compilationContext.units();
    const auto firstToken = units[0].tokenBuffer.value().getToken(0);
    const auto secondToken = units[1].tokenBuffer.value().getToken(0);

    CaraTest::areEqual(u16{ 0 }, firstToken.fileId);
    CaraTest::areEqual(u16{ 1 }, secondToken.fileId);

    CaraTest::isTrue(compilationContext.getLexeme(firstToken) == "one");
    CaraTest::isTrue(compilationContext.getLexeme(secondToken) == "twotwo");

    const auto& firstLocation = compilationContext.sourceLocation(firstToken);
    CaraTest::areEqual(0, firstLocation.startIndex);
    CaraTest::areEqual(3, firstLocation.endIndex);

    const auto& secondLocation = compilationContext.sourceLocation(secondToken);
    CaraTest::areEqual(1, secondLocation.startIndex);
    CaraTest::areEqual(7, secondLocation.endIndex);

    CaraTest::isTrue(compilationContext.source(firstToken)->filePath == "one.cara");
    CaraTest::isTrue(compilationContext.source(secondToken)->filePath == "twotwo.cara");
}

static const auto tests =
{
    CaraTest::addTest("SingleCharacter", ExpectedTokenKind, Symbols_Data),
    CaraTest::addTest("IgnoresWhitespaces", IgnoresWhitespaces, IgnoresWhitespaces_Data),
    CaraTest::addTest("Identifiers", Identifiers, Identifiers_Data),
    CaraTest::addTest("Numbers", Numbers, Numbers_Data),
    CaraTest::addTest("Strings", Strings, Strings_Data),
    CaraTest::addTest("StringsWithEscapes", Strings, StringsWithEscapes_Data),
    CaraTest::addTest("UnterminatedStrings", UnterminatedStrings, UnterminatedStrings_Data),
    CaraTest::addTest("Keywords", ExpectedTokenKind, Keyword_Data),
    CaraTest::addTest("WhiteSpaceTrivia", WhiteSpaceTrivia, WhiteSpaceTrivia_Data),
    CaraTest::addTest("WholeInput", WholeInput, WholeInput_Data),
    CaraTest::addTest("ScanPrimitives", ScanPrimitives),
    CaraTest::addTest("ScanPrimitivesDifferential", ScanPrimitivesDifferential),
    CaraTest::addTest("LongRunBoundaries", LongRunBoundaries),
    CaraTest::addTest("TokenResolutionAcrossUnits", TokenResolutionAcrossUnits),
    CaraTest::addTest("OneMillionLinesTime", OneMillionLinesTime),
};
