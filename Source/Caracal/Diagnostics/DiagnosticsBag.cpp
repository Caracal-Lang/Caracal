#include <Caracal/Diagnostics/DiagnosticsBag.h>

#include <unordered_map>

namespace Caracal
{
    static bool IsCrossFileDuplicate(
        const SourceTextSharedPtr& source,
        const SourceTextSharedPtr& otherSource)
    {
        if (!source || !otherSource)
        {
            return false;
        }

        return source->filePath != otherSource->filePath;
    }

    static void AddPreviousDeclarationLabel(
        Diagnostic& diagnostic,
        const SourceTextSharedPtr& source,
        const SourceTextSharedPtr& otherSource,
        const std::optional<SourceLocation>& otherLocation,
        const std::string& relatedMessage,
        const std::string& relatedLabel,
        const std::string& secondaryLabel)
    {
        if (!otherLocation.has_value())
        {
            return;
        }

        if (IsCrossFileDuplicate(source, otherSource))
        {
            diagnostic.addRelatedPrimaryLabel(
                otherSource,
                otherLocation.value(),
                relatedMessage,
                relatedLabel);
        }
        else
        {
            diagnostic.addSecondaryLabel(otherLocation.value(), secondaryLabel);
        }
    }

    static std::string ToTokenSource(TokenKind kind)
    {
        const std::unordered_map<TokenKind, std::string_view> tokenSource{
            { TokenKind::Error,             std::string_view("error token") },

            { TokenKind::Plus,              std::string_view("'+'") },
            { TokenKind::PercentPlus,       std::string_view("'%+'") },
            { TokenKind::Minus,             std::string_view("'-'") },
            { TokenKind::PercentMinus,      std::string_view("'%-'") },
            { TokenKind::Star,              std::string_view("'*'") },
            { TokenKind::PercentStar,       std::string_view("'%*'") },
            { TokenKind::Slash,             std::string_view("'/'") },
            { TokenKind::Dot,               std::string_view("'.'") },
            { TokenKind::Ellipsis,          std::string_view("'...'") },
            { TokenKind::Comma,             std::string_view("','") },
            { TokenKind::Colon,             std::string_view("':'") },
            { TokenKind::Semicolon,         std::string_view("';'") },
            { TokenKind::Underscore,        std::string_view("'_'") },
            { TokenKind::SingleQuote,       std::string_view("\"'\"") },
            { TokenKind::Hash,              std::string_view("'#'") },

            { TokenKind::Equal,             std::string_view("'='") },
            { TokenKind::EqualEqual,        std::string_view("'=='") },
            { TokenKind::Bang,              std::string_view("'!'") },
            { TokenKind::BangEqual,         std::string_view("'!='") },
            { TokenKind::LessThan,          std::string_view("'<'") },
            { TokenKind::LessThanEqual,     std::string_view("'<='") },
            { TokenKind::GreaterThan,       std::string_view("'>'") },
            { TokenKind::GreaterThanEqual,  std::string_view("'>='") },

            { TokenKind::OpenParenthesis,   std::string_view("'('") },
            { TokenKind::CloseParenthesis,  std::string_view("')'") },
            { TokenKind::OpenBrace,       std::string_view("'{'") },
            { TokenKind::CloseBrace,      std::string_view("'}'") },
            { TokenKind::OpenBracket,     std::string_view("'['") },
            { TokenKind::CloseBracket,    std::string_view("']'") },

            { TokenKind::Identifier,        std::string_view("identifier") },
            { TokenKind::Number,            std::string_view("number") },
            { TokenKind::String,            std::string_view("string") },

            { TokenKind::DefKeyword,        std::string_view("'def'") },
            { TokenKind::EnumKeyword,       std::string_view("'enum'") },
            { TokenKind::TypeKeyword,       std::string_view("'type'") },
            { TokenKind::IfKeyword,         std::string_view("'if'") },
            { TokenKind::ElseKeyword,       std::string_view("'else'") },
            { TokenKind::WhileKeyword,      std::string_view("'while'") },
            { TokenKind::BreakKeyword,      std::string_view("'break'") },
            { TokenKind::SkipKeyword,       std::string_view("'skip'") },
            { TokenKind::ReturnKeyword,     std::string_view("'return'") },
            { TokenKind::TrueKeyword,       std::string_view("'true'") },
            { TokenKind::FalseKeyword,      std::string_view("'false'") },
            { TokenKind::AndKeyword,        std::string_view("'and'") },
            { TokenKind::OrKeyword,         std::string_view("'or'") },
            { TokenKind::RefKeyword,        std::string_view("'ref'") },

            { TokenKind::EndOfFile,         std::string_view("end of file") },
        };

        const auto it = tokenSource.find(kind);
        if (it != tokenSource.end())
            return std::string(it->second);

        return stringify(kind);
    }

    static std::string FormatByteValue(u8 value)
    {
        constexpr const char* HexDigits = "0123456789ABCDEF";
        auto result = std::string("0x");
        result += HexDigits[(value >> 4) & 0xF];
        result += HexDigits[value & 0xF];
        return result;
    }

    void DiagnosticsBag::addIllegalCharacterError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::L0001_IllegalCharacter,
            source,
            location,
            "Remove the unsupported character.");
        diagnostic.addPrimaryLabel(location, "This character is not part of the Caracal syntax.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addInvalidUtf8SourceError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        u8 byteValue)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::L0003_InvalidUtf8Source,
            source,
            location,
            "Save the file with UTF-8 encoding.");
        diagnostic.addPrimaryLabel(location, "The byte " + FormatByteValue(byteValue) + " is not valid UTF-8.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnsupportedSourceEncodingError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& encodingName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::L0004_UnsupportedSourceEncoding,
            source,
            location,
            "Save the file with UTF-8 encoding.");
        diagnostic.addPrimaryLabel(location, "This file is " + encodingName + " encoded, Caracal reads UTF-8 only.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnterminatedStringError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::L0002_UnterminatedString,
            source,
            location,
            "Add a closing quote to terminate the string.");
        diagnostic.addPrimaryLabel(location, "String literal is missing a closing quote.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addExpectedTokenError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        TokenKind expectedKind,
        TokenKind actualKind)
    {
        auto fix = std::string{};
        if (actualKind == TokenKind::EndOfFile)
        {
            fix = "Add a " + ToTokenSource(expectedKind) + " before the end of file.";
        }
        else if (expectedKind == TokenKind::Semicolon)
        {
            fix = "Add a ';' at the end of the previous statement.";
        }
        else
        {
            fix = "Replace " + ToTokenSource(actualKind) + " with " + ToTokenSource(expectedKind) + ".";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0001_UnexpectedToken,
            source,
            location,
            fix);
        diagnostic.addPrimaryLabel(location, "Expected " + ToTokenSource(expectedKind) + " but got " + ToTokenSource(actualKind));

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addExpectedTokenError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        TokenKind expectedKind1,
        TokenKind expectedKind2,
        TokenKind actualKind)
    {
        auto fix = std::string{};
        if (actualKind == TokenKind::EndOfFile)
        {
            fix = "Add a " + ToTokenSource(expectedKind1) + " or " + ToTokenSource(expectedKind2) + " before the end of file.";
        }
        else
        {
            fix = "Replace "
                + ToTokenSource(actualKind)
                + " with "
                + ToTokenSource(expectedKind1)
                + " or "
                + ToTokenSource(expectedKind2)
                + ".";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0001_UnexpectedToken,
            source,
            location,
            fix);
        diagnostic.addPrimaryLabel(
            location,
            "Expected "
            + ToTokenSource(expectedKind1)
            + " or "
            + ToTokenSource(expectedKind2)
            + " but got "
            + ToTokenSource(actualKind));

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addExtraTokensRemainingError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0002_UnexpectedTrailingTokens,
            source,
            location);
        diagnostic.addPrimaryLabel(location, "Input should end before these trailing tokens.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addExpectedEnumFieldError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind)
    {
        auto fix = std::string{};
        if (actualKind == TokenKind::Number)
        {
            fix = "Start the enum field name with an identifier, for example 'FieldName' or 'FieldName :: 1'.";
        }
        else if (actualKind == TokenKind::CloseBrace || actualKind == TokenKind::EndOfFile)
        {
            fix = "Add an enum field name before the end of the enum body.";
        }
        else
        {
            fix = "Replace " + ToTokenSource(actualKind) + " with an enum field name such as 'FieldName'.";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0004_InvalidEnumField,
            source,
            location,
            fix);
        diagnostic.addPrimaryLabel(location, "Expected an enum field name, but got a " + ToTokenSource(actualKind));

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUninitializedTypeFieldError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& fieldName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0008_UninitializedField,
            source,
            location,
            "Add '= <value>' for a mutable field or ': <value>' for a constant field.");
        diagnostic.addPrimaryLabel(location, "Type field '" + fieldName + "' must be initialized");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnexpectedTopLevelTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind)
    {
        auto fix = std::string{};
        if (actualKind == TokenKind::CloseBrace)
        {
            fix = "Remove the stray '}' or move it inside a declaration body.";
        }
        else
        {
            fix = "Remove the unexpected top-level token or start a declaration with 'def', 'enum', or 'type'.";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0003_UnexpectedTopLevelToken,
            source,
            location,
            fix);
        diagnostic.addPrimaryLabel(location, "Unexpected top-level token " + ToTokenSource(actualKind));

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnexpectedStatementTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0005_InvalidStatement,
            source,
            location,
            "Remove this token or rewrite the statement so it starts with a valid statement token.");
        diagnostic.addPrimaryLabel(location, "Unexpected token " + ToTokenSource(actualKind) + " in statement");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnexpectedExpressionTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0006_InvalidExpression,
            source,
            location,
            "Remove this token or replace it with a valid expression.");
        diagnostic.addPrimaryLabel(location, "Unexpected token " + ToTokenSource(actualKind) + " in expression");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnexpectedParameterTokenError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind actualKind)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0011_InvalidParameter,
            source,
            location,
            "Start each parameter with a name and ': type', or use '...' for variadics.");
        diagnostic.addPrimaryLabel(location, "Unexpected token " + ToTokenSource(actualKind) + " in parameter list");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addPositionalArgumentAfterNamedError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0009_PositionalArgumentAfterNamed,
            source,
            location,
            "Move positional arguments before the named arguments.");
        diagnostic.addPrimaryLabel(location, "A positional argument cannot follow a named argument.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addAnnotationNotAllowedHereError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0010_AnnotationNotAllowedHere,
            source,
            location,
            "Annotations are only allowed on top-level and type-member declarations.");
        diagnostic.addPrimaryLabel(location, "An annotation is not allowed in this scope.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addExplicitTypeOnInitConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName, const std::string& initTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0012_ExplicitTypeOnInitConstant,
            source,
            location,
            "Write '" + constantName + " :: init " + initTypeName + ";' instead.");
        diagnostic.addPrimaryLabel(location, "Explicit type is not allowed for init constants");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addPrivateStaticMethodError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0013_PrivateStaticMethod,
            source,
            location,
            "Remove the leading '_' from the method name.");
        diagnostic.addPrimaryLabel(location, "Static methods cannot be private");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addPrivateFreeFunctionError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0014_PrivateFreeFunction,
            source,
            location,
            "Remove the leading '_' from the function name.");
        diagnostic.addPrimaryLabel(location, "Free functions cannot be private");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addExpectedArrayLengthError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0015_ExpectedArrayLength,
            source,
            location,
            "Write an integer length like '[i32; 4]'.");
        diagnostic.addPrimaryLabel(location, "Expected an array length");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addReservedOperatorError(const SourceTextSharedPtr& source, const SourceLocation& location, TokenKind reservedOperator)
    {
        auto wrappingOperator = std::string();
        switch (reservedOperator)
        {
            case TokenKind::Plus:
            {
                wrappingOperator = "'%+'";
                break;
            }
            case TokenKind::Minus:
            {
                wrappingOperator = "'%-'";
                break;
            }
            case TokenKind::Star:
            {
                wrappingOperator = "'%*'";
                break;
            }
            default:
            {
                wrappingOperator = "a wrapping operator";
                break;
            }
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0016_ReservedOperator,
            source,
            location,
            "Use the wrapping operator " + wrappingOperator + " instead.");
        diagnostic.addPrimaryLabel(location, ToTokenSource(reservedOperator) + " is reserved for checked arithmetic");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDanglingAnnotationError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::P0007_DanglingAnnotation,
            source,
            location,
            "Remove the extra annotation or attach it to the next supported declaration.");
        diagnostic.addPrimaryLabel(location, "This annotation is not attached to any declaration.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnexpectedAnnotationTargetError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0002_UnexpectedAnnotationTarget,
            source,
            location,
            "Remove the annotation or move it to a supported declaration kind.");
        diagnostic.addPrimaryLabel(location, "This annotation is not supported on this declaration.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnknownAnnotationError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& annotationName, TokenKind targetKind)
    {
        auto fix = std::string{};
        switch (targetKind)
        {
            case TokenKind::DefKeyword:
                fix = "Rename this annotation to #extern, or remove it from the function.";
                break;
            case TokenKind::EnumKeyword:
                fix = "Rename this annotation to #flag or #step, or remove it from the enum.";
                break;
            default:
                fix = "Rename this annotation to a supported one, or remove it.";
                break;
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0001_UnknownAnnotation,
            source,
            location,
            fix);
        diagnostic.addPrimaryLabel(location, "Unknown annotation '#" + annotationName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addAnnotationMissingArgumentsError(const SourceTextSharedPtr& source, const SourceLocation& location, AnnotationKind annotationKind, const std::string& annotationName, const std::string& argumentName)
    {
        auto fix = std::string{};
        auto label = std::string{};
        if (!argumentName.empty())
        {
            fix = "Provide the argument as " + argumentName + " = \"...\".";
            label = "Annotation '#" + annotationName + "' is missing the '" + argumentName + "' argument.";
        }
        else if (annotationKind == AnnotationKind::Step)
        {
            fix = "Add the required annotation argument, for example #step(10).";
            label = "Annotation '#" + annotationName + "' requires one argument.";
        }
        else
        {
            fix = "Add the required annotation argument.";
            label = "Annotation '#" + annotationName + "' requires one argument.";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0003_MissingAnnotationArguments,
            source,
            location,
            fix);
        diagnostic.addPrimaryLabel(location, label);

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addAnnotationWrongNumberOfArgumentsError(const SourceTextSharedPtr& source, const SourceLocation& location, AnnotationKind annotationKind, const std::string& annotationName, i32 expectedCount, i32 actualCount)
    {
        auto fix = std::string{};
        if (annotationKind == AnnotationKind::Step && expectedCount == 1)
        {
            if (actualCount == 0)
            {
                fix = "Add the missing annotation argument, for example #step(10).";
            }
            else
            {
                fix = "Reduce the annotation to one argument, for example #step(10).";
            }
        }
        else if (expectedCount == 0)
        {
            fix = "Remove the unexpected annotation arguments.";
        }
        else
        {
            fix = "Adjust the annotation argument count.";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0004_WrongNumberOfAnnotationArguments,
            source,
            location,
            fix);
        diagnostic.addPrimaryLabel(location, "Annotation '#" + annotationName + "' expects " + std::to_string(expectedCount) + " argument(s), but got " + std::to_string(actualCount) + ".");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addAnnotationArgumentTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, AnnotationKind annotationKind, const std::string& annotationName, const std::string& expectedDescription, const std::string& actualDescription)
    {
        auto fix = std::string{};
        if (annotationKind == AnnotationKind::Step)
        {
            fix = "Change the annotation argument to an i32 literal, for example #step(10).";
        }
        else
        {
            fix = "Change the annotation argument to the expected type.";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0005_AnnotationArgumentTypeMismatch,
            source,
            location,
            fix);
        diagnostic.addPrimaryLabel(location, "Annotation '#" + annotationName + "' expects " + expectedDescription + ", but got " + actualDescription + ".");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnsupportedAnnotationArgumentError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& argumentName, const std::string& kindName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0045_UnexpectedAnnotationArgument,
            source,
            location,
            "Remove the '" + argumentName + "' argument.");
        diagnostic.addPrimaryLabel(location, "'" + argumentName + "' is not supported for '" + kindName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnsupportedFloatBitsError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0089_UnsupportedFloatBits,
            source,
            location,
            "Change 'bits' to 32 or 64.");
        diagnostic.addPrimaryLabel(location, "Floats only support 32 or 64 bits");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnexpectedAnnotationArgumentError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& annotationName, const std::string& argumentName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0045_UnexpectedAnnotationArgument,
            source,
            location,
            "Remove the argument, or use one that the '#" + annotationName + "' annotation accepts.");
        diagnostic.addPrimaryLabel(location, "Annotation '#" + annotationName + "' does not accept an argument named '" + argumentName + "'.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateAnnotationArgumentError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& annotationName, const std::string& argumentName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0046_DuplicateAnnotationArgument,
            source,
            location,
            "Remove the duplicate argument.");
        diagnostic.addPrimaryLabel(location, "Annotation '#" + annotationName + "' already has an argument named '" + argumentName + "'.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addExternMethodRequiresSymbolError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0047_ExternMethodRequiresSymbol,
            source,
            location,
            "Add the external symbol, for example #extern(symbol = \"printf\").");
        diagnostic.addPrimaryLabel(location, "Method '" + methodName + "' is extern but has no symbol, a method needs to specify the external symbol.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addAssignmentToInitConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0053_AssignmentToInitConstant,
            source,
            location,
            "An 'init' constant can only be assigned inside 'main'.");
        diagnostic.addPrimaryLabel(location, "'" + constantName + "' is an 'init' constant and cannot be assigned here.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addInitConstantAlreadyInitializedError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName, const std::optional<SourceLocation>& previousLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0054_InitConstantAlreadyInitialized,
            source,
            location,
            "An 'init' constant may be assigned only once, remove the extra assignment.");
        diagnostic.addPrimaryLabel(location, "'" + constantName + "' has already been initialized.");
        if (previousLocation.has_value())
        {
            diagnostic.addSecondaryLabel(previousLocation.value(), "'" + constantName + "' was first initialized here.");
        }

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUninitializedInitConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0055_UninitializedInitConstant,
            source,
            location,
            "Assign '" + constantName + "' once inside 'main'.");
        diagnostic.addPrimaryLabel(location, "'" + constantName + "' is an 'init' constant but is never initialized.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addNonGlobalInitConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0056_NonGlobalInitConstant,
            source,
            location,
            "An 'init' constant can only be declared at global scope.");
        diagnostic.addPrimaryLabel(location, "'" + constantName + "' is an 'init' constant and cannot be declared here.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addInitReferenceWithoutRefError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0100_InitReferenceWithoutRef,
            source,
            location,
            "Initialize '" + constantName + "' with 'ref' and a variable, like '" + constantName + " = ref value;'.");
        diagnostic.addPrimaryLabel(location, "'" + constantName + "' is a reference and needs the address of a variable.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addAssignmentToConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0057_AssignmentToConstant,
            source,
            location,
            "Declare '" + constantName + "' as a variable if it needs to change, or remove the assignment.");
        diagnostic.addPrimaryLabel(location, "'" + constantName + "' is a constant and cannot be reassigned.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addMutatingMethodOnConstantError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& constantName, const std::string& methodName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0087_MutatingMethodOnConstant,
            source,
            location,
            "Declare '" + constantName + "' as a variable if it needs to change, or remove the '" + methodName + "' call.");
        diagnostic.addPrimaryLabel(location, "'" + constantName + "' is a constant and cannot be modified.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addMutatingMethodThroughConstantReferenceError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& referenceName, const std::string& methodName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0088_MutatingMethodThroughConstantReference,
            source,
            location,
            "Reference a variable instead if it needs to change, or remove the '" + methodName + "' call.");
        diagnostic.addPrimaryLabel(location, "'" + referenceName + "' references a constant and cannot be written through.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addAssignmentThroughConstantReferenceError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& referenceName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0058_AssignmentThroughConstantReference,
            source,
            location,
            "Reference a variable instead if it needs to change, or remove the assignment.");
        diagnostic.addPrimaryLabel(location, "'" + referenceName + "' references a constant and cannot be written through.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnreachableCodeWarning(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Warning,
            DiagnosticKind::T0059_UnreachableCode,
            source,
            location,
            "This code can never run, remove it.");
        diagnostic.addPrimaryLabel(location, "Unreachable code after a 'return', 'break', or 'skip'.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addMissingReturnError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName, const std::string& returnTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0060_MissingReturn,
            source,
            location,
            "Add a 'return' so every path returns a '" + returnTypeName + "'.");
        diagnostic.addPrimaryLabel(location, "'" + functionName + "' does not return a value on all paths.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDivisionByZeroError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0061_DivisionByZero,
            source,
            location,
            "Divide by a non-zero value.");
        diagnostic.addPrimaryLabel(location, "Integer division by zero.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addConstantOverflowError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0062_ConstantOverflow,
            source,
            location,
            "Use a wider type, or values that fit in '" + typeName + "'.");
        diagnostic.addPrimaryLabel(location, "This constant expression does not fit in '" + typeName + "'.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnaryOperandTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& operatorName, const std::string& operandTypeName, const std::string& expectedDescription)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0063_UnaryOperandTypeMismatch,
            source,
            location,
            "Operator '" + operatorName + "' expects " + expectedDescription + ".");
        diagnostic.addPrimaryLabel(location, "Operator '" + operatorName + "' cannot be applied to '" + operandTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addBinaryOperandTypeMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& operatorName, const std::string& operandTypeName, const std::string& expectedDescription)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0065_BinaryOperandTypeMismatch,
            source,
            location,
            "Operator '" + operatorName + "' expects " + expectedDescription + ".");
        diagnostic.addPrimaryLabel(location, "Operator '" + operatorName + "' cannot be applied to '" + operandTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addNotABuiltinTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0066_NotABuiltinType,
            source,
            location,
            "Apply #builtin only to declarations of builtin type names, or remove the annotation.");
        diagnostic.addPrimaryLabel(location, "'" + typeName + "' is not a builtin type");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateBuiltinTypeBindingError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName, const std::optional<SourceLocation>& otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0067_DuplicateBuiltinTypeBinding,
            source,
            location,
            "Merge the members into the existing #builtin declaration.");
        diagnostic.addPrimaryLabel(location, "Builtin type '" + typeName + "' is already bound to a declaration");
        if (otherLocation.has_value())
        {
            diagnostic.addSecondaryLabel(otherLocation.value(), "Previous declaration is here");
        }

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addBuiltinTypeFieldIgnoredWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& fieldName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Warning,
            DiagnosticKind::T0068_BuiltinTypeFieldIgnored,
            source,
            location,
            "Remove the field, builtin types have no fields.");
        diagnostic.addPrimaryLabel(location, "Field '" + fieldName + "' in a #builtin type is ignored");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addBuiltinTypeConstructorIgnoredWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Warning,
            DiagnosticKind::T0069_BuiltinTypeConstructorIgnored,
            source,
            location,
            "Remove the constructor parameters, builtin types are not constructible.");
        diagnostic.addPrimaryLabel(location, "Constructor on builtin type '" + typeName + "' is ignored");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addBuiltinMethodBodyIgnoredWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Warning,
            DiagnosticKind::T0070_BuiltinMethodBodyIgnored,
            source,
            location,
            "Leave the body empty, #builtin methods are signatures only.");
        diagnostic.addPrimaryLabel(location, "Body of builtin method '" + methodName + "' is ignored");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnknownBuiltinMethodIgnoredWarning(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Warning,
            DiagnosticKind::T0071_UnknownBuiltinMethodIgnored,
            source,
            location,
            "Use one of the fixed operator method names, or remove the method.");
        diagnostic.addPrimaryLabel(location, "'" + methodName + "' is not a known operator of builtin type '" + typeName + "' and is ignored");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addInvalidOperatorMethodSignatureError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& expectedSignature)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0072_InvalidOperatorMethodSignature,
            source,
            location,
            "Declare the operator as '" + expectedSignature + "'.");
        diagnostic.addPrimaryLabel(location, "Operator method '" + methodName + "' does not match the required signature");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addBitwiseMethodOnNonIntegerTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0083_BitwiseMethodOnNonIntegerType,
            source,
            location,
            "Declare '" + methodName + "' on 'i32' or 'u8' instead.");
        diagnostic.addPrimaryLabel(location, "'" + methodName + "' cannot be declared on builtin type '" + typeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addFieldAccessMissingDotError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& fieldName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0096_FieldAccessMissingDot,
            source,
            location,
            "Write '." + fieldName + "' to access the field.");
        diagnostic.addPrimaryLabel(location, "'" + fieldName + "' is a field, not a local variable");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addMethodCallMissingDotError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0073_MethodCallMissingDot,
            source,
            location,
            "Write '." + methodName + "()' to call the method.");
        diagnostic.addPrimaryLabel(location, "'" + methodName + "' is a method of '" + typeName + "', not a free function");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addMemberAccessInDefaultParameterError(const SourceTextSharedPtr& source, const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0076_MemberAccessInDefaultParameter,
            source,
            location,
            "Use a default value without member access, or pass the argument at the call site.");
        diagnostic.addPrimaryLabel(location, "Member access is not allowed for default parameters");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addPrivateMethodCallOutsideTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& methodName, const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0077_PrivateMethodCallOutsideType,
            source,
            location,
            "Call it from a method of '" + typeName + "', or remove the leading '_' to make it public.");
        diagnostic.addPrimaryLabel(location, "'" + methodName + "' is private to type '" + typeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addStaticMethodTypeNameMismatchError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& declaredTypeName, const std::string& enclosingTypeName, const std::string& methodName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0074_StaticMethodTypeNameMismatch,
            source,
            location,
            "Write '" + enclosingTypeName + "." + methodName + "', or move the method into type '" + declaredTypeName + "'.");
        diagnostic.addPrimaryLabel(location, "'" + declaredTypeName + "' does not match the enclosing type '" + enclosingTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addVoidParameterTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& parameterName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0064_VoidParameterType,
            source,
            location,
            "Give the parameter a concrete type, or remove it.");
        diagnostic.addPrimaryLabel(location, "Parameter '" + parameterName + "' cannot have type 'void'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addConflictingEnumAnnotationsError(const SourceTextSharedPtr& source, const SourceLocation& location, const SourceLocation& otherLocation, const std::string& annotationName, const std::string& otherAnnotationName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0006_ConflictingEnumAnnotations,
            source,
            location,
            "Remove one of the conflicting enum annotations.");
        diagnostic.addPrimaryLabel(location, "Annotation '#" + annotationName + "' cannot be used together with '#" + otherAnnotationName + "' on the same enum.");
        diagnostic.addSecondaryLabel(otherLocation, "Conflicting annotation '#" + otherAnnotationName + "' was already used here.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnknownNameError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& name)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0007_UnknownName,
            source,
            location,
            "Declare this name before using it, or rename it to an existing symbol.");
        diagnostic.addPrimaryLabel(location, "Unknown name '" + name + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnknownFunctionError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& functionName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0008_UnknownFunction,
            source,
            location,
            "Declare this function before calling it, or rename the call to an existing function.");
        diagnostic.addPrimaryLabel(location, "Unknown function '" + functionName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnknownTypeError(const SourceTextSharedPtr& source, const SourceLocation& location, const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0009_UnknownType,
            source,
            location,
            "Declare this type before using it, or rename it to an existing type.");
        diagnostic.addPrimaryLabel(location, "Unknown type '" + typeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnknownMethodError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& receiverTypeName,
        const std::string& methodName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0010_UnknownMethod,
            source,
            location,
            "Add this method to the type, or rename the call to an existing method.");
        diagnostic.addPrimaryLabel(location, "Type '" + receiverTypeName + "' has no method '" + methodName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnknownFieldError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& receiverTypeName,
        const std::string& fieldName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0011_UnknownField,
            source,
            location,
            "Add this field to the type, or rename the access to an existing field.");
        diagnostic.addPrimaryLabel(location, "Type '" + receiverTypeName + "' has no field '" + fieldName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnknownEnumFieldError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& enumName,
        const std::string& fieldName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0012_UnknownEnumField,
            source,
            location,
            "Use an existing enum field name, or rename this member access.");
        diagnostic.addPrimaryLabel(location, "Enum '" + enumName + "' has no field '" + fieldName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addInvalidEnumMemberAccessError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& enumName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0013_InvalidEnumMemberAccess,
            source,
            location,
            "Access an enum field as '" + enumName + ".FieldName' without calling it.");
        diagnostic.addPrimaryLabel(location, "Enum '" + enumName + "' member access must be a field name");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addInvalidMemberAccessReceiverError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& receiverTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0014_InvalidMemberAccessReceiver,
            source,
            location,
            "Access members only on types and enum values that support member lookup.");
        diagnostic.addPrimaryLabel(location, "Type '" + receiverTypeName + "' does not support member access");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addArgumentCountMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& functionName,
        i32 expectedCount,
        i32 actualCount,
        bool isVariadic)
    {
        auto fix = std::string{};
        if (isVariadic)
        {
            fix = "Add the missing arguments to this call.";
        }
        else
        {
            fix = "Adjust this call to match the required argument count.";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0015_ArgumentCountMismatch,
            source,
            location,
            fix);

        if (isVariadic)
        {
            diagnostic.addPrimaryLabel(location, "Function '" + functionName + "' expects at least " + std::to_string(expectedCount) + " argument(s), but got " + std::to_string(actualCount));
        }
        else
        {
            diagnostic.addPrimaryLabel(location, "Function '" + functionName + "' expects " + std::to_string(expectedCount) + " argument(s), but got " + std::to_string(actualCount));
        }

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addArgumentTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& functionName,
        const std::vector<ArgumentTypeMismatchInfo>& mismatches)
    {
        auto fix = std::string{};
        if (mismatches.size() == 1)
        {
            fix = "Change the mismatched argument to match the parameter type.";
        }
        else
        {
            fix = "Change the mismatched arguments to match the parameter types.";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0016_ArgumentTypeMismatch,
            source,
            location,
            fix);

        for (const auto& mismatch : mismatches)
        {
            diagnostic.addPrimaryLabel(
                mismatch.location,
                "Argument " + std::to_string(mismatch.argumentIndex) + " of '" + functionName + "' expects '" + mismatch.expectedTypeName + "', but got '" + mismatch.actualTypeName + "'");
        }

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addInvalidVariadicArgumentTypeError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& functionName,
        i32 argumentIndex,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0017_InvalidVariadicArgumentType,
            source,
            location,
            "Pass a non-void value as this variadic argument.");
        diagnostic.addPrimaryLabel(location, "Variadic argument " + std::to_string(argumentIndex) + " of '" + functionName + "' cannot have type '" + actualTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnknownArgumentNameError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& functionName,
        const std::string& argumentName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0048_UnknownArgumentName,
            source,
            location,
            "Use the name of one of the function's parameters.");
        diagnostic.addPrimaryLabel(location, "Function '" + functionName + "' has no parameter named '" + argumentName + "'.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateArgumentBindingError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& functionName,
        const std::string& parameterName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0049_DuplicateArgumentBinding,
            source,
            location,
            "Pass the argument for '" + parameterName + "' only once.");
        diagnostic.addPrimaryLabel(location, "Parameter '" + parameterName + "' of '" + functionName + "' is already bound by an earlier argument.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addMissingRequiredArgumentError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& functionName,
        const std::string& parameterName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0050_MissingRequiredArgument,
            source,
            location,
            "Pass an argument for '" + parameterName + "', or give it a default value.");
        diagnostic.addPrimaryLabel(location, "Call to '" + functionName + "' is missing a required argument for '" + parameterName + "'.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDefaultParameterTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& expectedTypeName,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0051_DefaultParameterTypeMismatch,
            source,
            location,
            "Change the default value to match the parameter type.");
        diagnostic.addPrimaryLabel(location, "Default value of type '" + actualTypeName + "' does not match parameter type '" + expectedTypeName + "'.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addNonTrailingDefaultParameterError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& parameterName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0052_NonTrailingDefaultParameter,
            source,
            location,
            "Move parameters with default values to the end of the parameter list.");
        diagnostic.addPrimaryLabel(location, "Parameter '" + parameterName + "' has no default value but follows one that does.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addNonBoolIfConditionError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0018_NonBoolIfCondition,
            source,
            location,
            "Change the if condition to a bool expression.");
        diagnostic.addPrimaryLabel(location, "If condition must have type 'bool', but got '" + actualTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addNonBoolWhileConditionError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0019_NonBoolWhileCondition,
            source,
            location,
            "Change the while condition to a bool expression.");
        diagnostic.addPrimaryLabel(location, "While condition must have type 'bool', but got '" + actualTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addReturnTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& expectedTypeName,
        const std::string& actualTypeName)
    {
        auto fix = std::string{};
        if (expectedTypeName == "void")
        {
            fix = "Remove the returned value from this return statement.";
        }
        else
        {
            fix = "Return a value of type '" + expectedTypeName + "' or change the function's return type.";
        }

        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0020_ReturnTypeMismatch,
            source,
            location,
            fix);
        diagnostic.addPrimaryLabel(location, "Cannot return '" + actualTypeName + "' from a function with return type '" + expectedTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addAssignmentTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& expectedTypeName,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0021_AssignmentTypeMismatch,
            source,
            location,
            "Assign a value of type '" + expectedTypeName + "' or change the assignment target type.");
        diagnostic.addPrimaryLabel(location, "Cannot assign '" + actualTypeName + "' to a value of type '" + expectedTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addExplicitConstantTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& expectedTypeName,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0022_ExplicitConstantTypeMismatch,
            source,
            location,
            "Initialize this constant with a value of type '" + expectedTypeName + "' or change its explicit type.");
        diagnostic.addPrimaryLabel(location, "Constant is declared as '" + expectedTypeName + "', but its initializer has type '" + actualTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addExplicitVariableTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& expectedTypeName,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0023_ExplicitVariableTypeMismatch,
            source,
            location,
            "Initialize this variable with a value of type '" + expectedTypeName + "' or change its explicit type.");
        diagnostic.addPrimaryLabel(location, "Variable is declared as '" + expectedTypeName + "', but its initializer has type '" + actualTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addTypeFieldInitializerMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& expectedTypeName,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0024_TypeFieldInitializerMismatch,
            source,
            location,
            "Initialize this field with a value of type '" + expectedTypeName + "' or change the field type.");
        diagnostic.addPrimaryLabel(location, "Field is declared as '" + expectedTypeName + "', but its initializer has type '" + actualTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addArithmeticOperandTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& operatorName,
        const std::string& leftTypeName,
        const std::string& rightTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0025_ArithmeticOperandTypeMismatch,
            source,
            location,
            "Use matching operand types for this arithmetic operation.");
        diagnostic.addPrimaryLabel(location, "Operator '" + operatorName + "' cannot be applied to '" + leftTypeName + "' and '" + rightTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addComparisonOperandTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& operatorName,
        const std::string& leftTypeName,
        const std::string& rightTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0026_ComparisonOperandTypeMismatch,
            source,
            location,
            "Use comparable operand types for this comparison.");
        diagnostic.addPrimaryLabel(location, "Operator '" + operatorName + "' cannot compare '" + leftTypeName + "' and '" + rightTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addEnumFieldValueTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& expectedTypeName,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0027_EnumFieldValueTypeMismatch,
            source,
            location,
            "Use a value of the enum base type for this field.");
        diagnostic.addPrimaryLabel(location, "Enum base type is '" + expectedTypeName + "', but this field's initializer has type '" + actualTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addArrayElementTypeMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& expectedTypeName,
        const std::string& actualTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0078_ArrayElementTypeMismatch,
            source,
            location,
            "Use elements of a single type in the array literal.");
        diagnostic.addPrimaryLabel(location, "This element has type '" + actualTypeName + "', but the previous elements have type '" + expectedTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addArrayLengthMismatchError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& expectedTypeName,
        i32 expectedLength,
        i32 actualLength)
    {
        const auto expected = std::to_string(expectedLength);
        const auto actual = std::to_string(actualLength);
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0080_ArrayLengthMismatch,
            source,
            location,
            "Use an array of length " + expected + " or change the expected length to " + actual + ".");
        diagnostic.addPrimaryLabel(location, "This array has length " + actual + ", but the expected type '" + expectedTypeName + "' has length " + expected);

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addNonExternVariadicFunctionError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& functionName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0028_NonExternVariadicFunction,
            source,
            location,
            "Add #extern to this function, or remove the variadic parameter.");
        diagnostic.addPrimaryLabel(location, "Function '" + functionName + "' uses a variadic parameter but is not marked #extern");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addFlagEnumExplicitValueError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& fieldName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0029_FlagEnumExplicitValue,
            source,
            location,
            "Remove the explicit value from this flag enum field.");
        diagnostic.addPrimaryLabel(location, "Flag enum field '" + fieldName + "' cannot declare an explicit value");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addTypeDotNewDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const SourceLocation& otherLocation,
        const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0030_TypeDotNewDeclaration,
            source,
            location,
            "Remove the " + typeName + ".new declaration and declare constructor parameters on type instead.");
        diagnostic.addPrimaryLabel(location, "Type.new declarations are not allowed");
        diagnostic.addSecondaryLabel(otherLocation, "Constructor parameters are declared here on the type.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addReferenceOrSliceReturnError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0031_ReferenceOrSliceReturn,
            source,
            location,
            "Return an owning copy of the value instead.");
        diagnostic.addPrimaryLabel(location, "Returning references or slices is not allowed");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addReferenceOrSliceFieldError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0085_ReferenceOrSliceField,
            source,
            location,
            "Store an owning copy of the value instead.");
        diagnostic.addPrimaryLabel(location, "Storing references or slices is not allowed");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addReferenceOrSliceArrayElementError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0086_ReferenceOrSliceArrayElement,
            source,
            location,
            "Store owning copies of the values instead.");
        diagnostic.addPrimaryLabel(location, "Storing references or slices is not allowed");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDynamicArrayElementError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0091_DynamicArrayElement,
            source,
            location,
            "Store fixed arrays for now, nesting arrives with the copy and destruction design.");
        diagnostic.addPrimaryLabel(location, "Storing dynamic arrays inside arrays is not supported");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addMutatingMethodOnImmutableSliceError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& methodName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0095_MutatingMethodOnImmutableSlice,
            source,
            location,
            "Immutable slices provide read-only access. Copy the bytes into a new array to modify them.");
        diagnostic.addPrimaryLabel(location, "'" + methodName + "' mutates an immutable slice");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addPrivateFieldAccessOutsideTypeError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& fieldName,
        const std::string& typeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0094_PrivateFieldAccessOutsideType,
            source,
            location,
            "Use a method of '" + typeName + "' to work with the field, or remove the leading underscore to make it public.");
        diagnostic.addPrimaryLabel(location, "'" + fieldName + "' is private to '" + typeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addMemberAccessInStaticMethodError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& methodName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0093_MemberAccessInStaticMethod,
            source,
            location,
            "Make '" + methodName + "' an instance method if it needs member access.");
        diagnostic.addPrimaryLabel(location, "'" + methodName + "' is static and has no instance");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addReferenceToNonVariableError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0092_ReferenceToNonVariable,
            source,
            location,
            "Bind the value to a local variable first.");
        diagnostic.addPrimaryLabel(location, "'ref' expects a variable name");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addMethodRequiresDynamicArrayError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& receiverTypeName,
        const std::string& dynamicTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0090_MethodRequiresDynamicArray,
            source,
            location,
            "Change the type to a dynamic array like '" + dynamicTypeName + "' if it needs to grow or shrink.");
        diagnostic.addPrimaryLabel(location, "Type '" + receiverTypeName + "' cannot change its length");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addAlreadyReferenceError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0032_AlreadyReference,
            source,
            location,
            "Remove the extra 'ref' operator.");
        diagnostic.addPrimaryLabel(location, "This expression is already a reference");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addCallReturnsNoValueError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0081_CallReturnsNoValue,
            source,
            location,
            "Call it as its own statement or discard the result with '_'.");
        diagnostic.addPrimaryLabel(location, "'" + name + "' would store nothing because this call does not return a value");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addGlobalConstantWithCallError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0082_GlobalConstantWithCall,
            source,
            location,
            "Initialize '" + name + "' with a constant expression or a constructor call.");
        diagnostic.addPrimaryLabel(location, "This initializer calls a function, which cannot run before the program starts");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addGlobalConstantNotComputableError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0084_GlobalConstantNotComputable,
            source,
            location,
            "Initialize '" + name + "' with values that are known at compile time.");
        diagnostic.addPrimaryLabel(location, "This initializer uses values that are not known until the program runs");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name,
        const SourceTextSharedPtr& otherSource,
        std::optional<SourceLocation> otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0033_DuplicateDeclaration,
            source,
            location,
            "Rename this declaration, or remove the duplicate.");
        diagnostic.addPrimaryLabel(location, "Duplicate declaration of '" + name + "'");
        AddPreviousDeclarationLabel(
            diagnostic,
            source,
            otherSource,
            otherLocation,
            "Previous declaration",
            "Previous declaration of '" + name + "' is here.",
            "Previous declaration is here.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateConstantDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name,
        const SourceTextSharedPtr& otherSource,
        std::optional<SourceLocation> otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0034_DuplicateConstantDeclaration,
            source,
            location,
            "Rename this constant, or remove the duplicate declaration.");
        diagnostic.addPrimaryLabel(location, "Constant '" + name + "' is already declared");
        AddPreviousDeclarationLabel(
            diagnostic,
            source,
            otherSource,
            otherLocation,
            "Previous constant declaration",
            "Previous constant declaration of '" + name + "' is here.",
            "Previous constant declaration is here.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateVariableDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name,
        const SourceTextSharedPtr& otherSource,
        std::optional<SourceLocation> otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0035_DuplicateVariableDeclaration,
            source,
            location,
            "Rename this variable, or remove the duplicate declaration.");
        diagnostic.addPrimaryLabel(location, "Variable '" + name + "' is already declared");
        AddPreviousDeclarationLabel(
            diagnostic,
            source,
            otherSource,
            otherLocation,
            "Previous variable declaration",
            "Previous variable declaration of '" + name + "' is here.",
            "Previous variable declaration is here.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateParameterDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name,
        const SourceTextSharedPtr& otherSource,
        std::optional<SourceLocation> otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0036_DuplicateParameterDeclaration,
            source,
            location,
            "Rename this parameter, or remove the duplicate declaration.");
        diagnostic.addPrimaryLabel(location, "Parameter '" + name + "' is already declared");
        AddPreviousDeclarationLabel(
            diagnostic,
            source,
            otherSource,
            otherLocation,
            "Previous parameter declaration",
            "Previous parameter declaration of '" + name + "' is here.",
            "Previous parameter declaration is here.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateEnumFieldDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name,
        std::optional<SourceLocation> otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0037_DuplicateEnumFieldDeclaration,
            source,
            location,
            "Rename this enum field, or remove the duplicate declaration.");
        diagnostic.addPrimaryLabel(location, "Enum field '" + name + "' is already declared");
        if (otherLocation.has_value())
        {
            diagnostic.addSecondaryLabel(otherLocation.value(), "Previous enum field declaration is here.");
        }

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateTypeFieldDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name,
        std::optional<SourceLocation> otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0038_DuplicateTypeFieldDeclaration,
            source,
            location,
            "Rename this type field, or remove the duplicate declaration.");
        diagnostic.addPrimaryLabel(location, "Type field '" + name + "' is already declared");
        if (otherLocation.has_value())
        {
            diagnostic.addSecondaryLabel(otherLocation.value(), "Previous type field declaration is here.");
        }

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateFunctionDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& functionName,
        const SourceTextSharedPtr& otherSource,
        std::optional<SourceLocation> otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0039_DuplicateFunctionDeclaration,
            source,
            location,
            "Rename this function, or remove the duplicate declaration.");
        diagnostic.addPrimaryLabel(location, "Function '" + functionName + "' is already declared");
        AddPreviousDeclarationLabel(
            diagnostic,
            source,
            otherSource,
            otherLocation,
            "Previous function declaration",
            "Previous function declaration of '" + functionName + "' is here.",
            "Previous function declaration is here.");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateTypeDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& typeName,
        const SourceTextSharedPtr& otherSource,
        std::optional<SourceLocation> otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0040_DuplicateTypeDeclaration,
            source,
            location,
            "Rename this type declaration, or remove the duplicate.");
        diagnostic.addPrimaryLabel(location, "Type '" + typeName + "' is already declared");
        if (otherLocation.has_value())
        {
            if (IsCrossFileDuplicate(source, otherSource))
            {
                diagnostic.addRelatedPrimaryLabel(
                    otherSource,
                    otherLocation.value(),
                    "Previous type declaration",
                    "Previous type declaration of '" + typeName + "' is here.");
            }
            else
            {
                diagnostic.addSecondaryLabel(otherLocation.value(), "Previous type declaration is here.");
            }
        }

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addDuplicateMethodDeclarationError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& methodName,
        const std::string& typeName,
        std::optional<SourceLocation> otherLocation)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0075_DuplicateMethodDeclaration,
            source,
            location,
            "Rename this method, or remove the duplicate declaration.");
        diagnostic.addPrimaryLabel(location, "Method '" + methodName + "' is already declared in type '" + typeName + "'");
        if (otherLocation.has_value())
        {
            diagnostic.addSecondaryLabel(otherLocation.value(), "Previous method declaration is here.");
        }

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addNumberLiteralOutOfRangeError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& literalText,
        const std::string& targetTypeName)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0041_NumberLiteralOutOfRange,
            source,
            location,
            "Use a smaller literal that fits in type '" + targetTypeName + "', or change the target type.");
        diagnostic.addPrimaryLabel(location, "Literal '" + literalText + "' does not fit in type '" + targetTypeName + "'");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addEmptyArrayLiteralError(
        const SourceTextSharedPtr& source,
        const SourceLocation& location)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Error,
            DiagnosticKind::T0079_EmptyArrayLiteral,
            source,
            location,
            "Add at least one element to the array literal.");
        diagnostic.addPrimaryLabel(location, "Empty arrays are not allowed");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnusedLocalVariableWarning(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Warning,
            DiagnosticKind::T0042_UnusedLocalVariable,
            source,
            location,
            "Remove this variable, rename it to '_', or use its value.");
        diagnostic.addPrimaryLabel(location, "Local variable '" + name + "' is never used");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnusedParameterWarning(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Warning,
            DiagnosticKind::T0044_UnusedParameter,
            source,
            location,
            "Remove this parameter, rename it to '_', or use its value.");
        diagnostic.addPrimaryLabel(location, "Parameter '" + name + "' is never used");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addUnusedLocalConstantWarning(
        const SourceTextSharedPtr& source,
        const SourceLocation& location,
        const std::string& name)
    {
        auto diagnostic = Diagnostic(
            DiagnosticLevel::Warning,
            DiagnosticKind::T0043_UnusedLocalConstant,
            source,
            location,
            "Remove this constant, rename it to '_', or use its value.");
        diagnostic.addPrimaryLabel(location, "Local constant '" + name + "' is never used");

        m_diagnostics.push_back(std::move(diagnostic));
    }

    void DiagnosticsBag::addPreludeTypeMissingError(const std::string& typeName)
    {
        m_diagnostics.emplace_back(
            DiagnosticLevel::Error,
            DiagnosticKind::T0097_PreludeTypeMissing,
            "Declare the type '" + typeName + "' in the prelude.");
    }

    void DiagnosticsBag::addPreludeCFunctionMissingError(const std::string& symbolName, const std::string& signature)
    {
        m_diagnostics.emplace_back(
            DiagnosticLevel::Error,
            DiagnosticKind::T0098_PreludeCFunctionMissing,
            "Declare " + signature + " in the prelude with #extern(symbol = \"" + symbolName + "\", required = true).");
    }

    void DiagnosticsBag::addPreludeCFunctionMismatchError(const std::string& functionName, const std::string& signature)
    {
        m_diagnostics.emplace_back(
            DiagnosticLevel::Error,
            DiagnosticKind::T0099_PreludeCFunctionMismatch,
            "Change the declaration of " + functionName + " to " + signature + ".");
    }

    void DiagnosticsBag::addPreludeCFunctionWithoutRequiredError(const std::string& functionName)
    {
        m_diagnostics.emplace_back(
            DiagnosticLevel::Error,
            DiagnosticKind::T0099_PreludeCFunctionMismatch,
            "Add required = true to the #extern annotation of " + functionName + ".");
    }

    const std::vector<Diagnostic>& DiagnosticsBag::diagnostics() const
    {
        return m_diagnostics;
    }

    int DiagnosticsBag::errorCount() const noexcept
    {
        int count = 0;
        for (const auto& diagnostic : m_diagnostics)
        {
            if (diagnostic.level() == DiagnosticLevel::Error)
            {
                count++;
            }
        }

        return count;
    }

    bool DiagnosticsBag::hasErrors() const noexcept
    {
        for (const auto& diagnostic : m_diagnostics)
        {
            if (diagnostic.level() == DiagnosticLevel::Error)
            {
                return true;
            }
        }

        return false;
    }

    bool DiagnosticsBag::hasWarnings() const noexcept
    {
        for (const auto& diagnostic : m_diagnostics)
        {
            if (diagnostic.level() == DiagnosticLevel::Warning)
            {
                return true;
            }
        }

        return false;
    }
}
