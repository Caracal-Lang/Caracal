#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <algorithm>

namespace Caracal
{
    struct AnnotationDefinition
    {
        AnnotationKind kind;
        std::string_view name;
        TokenKind targetKind;
        i32 requiredArgumentCount;
        bool requiresIntegerArgument;
        std::string_view namedStringArgument;
    };

    static const AnnotationDefinition* GetAnnotationDefinition(AnnotationKind kind)
    {
        static const AnnotationDefinition Definitions[] = {
            { AnnotationKind::Extern, ExternAnnotationName, TokenKind::DefKeyword, 0, false, SymbolAnnotationArgumentName },
            { AnnotationKind::Flag, FlagAnnotationName, TokenKind::EnumKeyword, 0, false, "" },
            { AnnotationKind::Step, StepAnnotationName, TokenKind::EnumKeyword, 1, true, "" },
            { AnnotationKind::Builtin, BuiltinAnnotationName, TokenKind::TypeKeyword, 0, false, "" },
        };

        for (const auto& definition : Definitions)
        {
            if (definition.kind == kind)
            {
                return &definition;
            }
        }

        return nullptr;
    }

    static AnnotationKind ParseAnnotationKind(std::string_view name)
    {
        if (name == ExternAnnotationName)
        {
            return AnnotationKind::Extern;
        }

        if (name == FlagAnnotationName)
        {
            return AnnotationKind::Flag;
        }

        if (name == StepAnnotationName)
        {
            return AnnotationKind::Step;
        }

        if (name == BuiltinAnnotationName)
        {
            return AnnotationKind::Builtin;
        }

        return AnnotationKind::Error;
    }

    static std::string ReplaceEscapeSequences(std::string_view input)
    {
        std::string result;
        result.reserve(input.size());

        for (size_t index = 0; index < input.size(); index++)
        {
            const auto character = input[index];
            if (character != '\\' || index + 1 == input.size())
            {
                result.push_back(character);
                continue;
            }

            index++;
            const auto escapedCharacter = input[index];
            switch (escapedCharacter)
            {
                case '\'':
                {
                    result.push_back('\'');
                    break;
                }
                case '\"':
                {
                    result.push_back('\"');
                    break;
                }
                case 'a':
                {
                    result.push_back('\a');
                    break;
                }
                case 'b':
                {
                    result.push_back('\b');
                    break;
                }
                case 'f':
                {
                    result.push_back('\f');
                    break;
                }
                case 'n':
                {
                    result.push_back('\n');
                    break;
                }
                case 'r':
                {
                    result.push_back('\r');
                    break;
                }
                case 't':
                {
                    result.push_back('\t');
                    break;
                }
                case 'v':
                {
                    result.push_back('\v');
                    break;
                }
                case '\\':
                {
                    result.push_back('\\');
                    break;
                }
                default:
                {
                    // unknown escapes stay verbatim
                    result.push_back('\\');
                    result.push_back(escapedCharacter);
                    break;
                }
            }
        }

        return result;
    }

    bool TypeChecker::validateNamedAnnotationArguments(const CompilationUnit& unit, u32 annotationIndex, std::string_view namedStringArgument, std::optional<std::string>* stringArgumentValue, bool* requiredValue)
    {
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto annotationName = nameOf(unit, annotationIndex);
        const auto annotationKind = ParseAnnotationKind(annotationName);

        std::vector<u32> arguments;
        NodeLayout::collectChildren(buffer, annotationIndex, arguments);
        std::vector<std::string_view> seenArgumentNames;
        for (const auto argumentIndex : arguments)
        {
            if (buffer.kind(argumentIndex) != NodeKind::NamedArgument)
            {
                continue;
            }

            const auto argumentName = nameOf(unit, argumentIndex);
            const auto& argumentNameLocation = nameLocation(unit, argumentIndex);
            if (std::find(seenArgumentNames.begin(), seenArgumentNames.end(), argumentName) != seenArgumentNames.end())
            {
                m_diagnostics.addDuplicateAnnotationArgumentError(unit.source, argumentNameLocation, std::string(annotationName), std::string(argumentName));
                return false;
            }
            seenArgumentNames.push_back(argumentName);

            const auto valueIndex = NodeLayout::namedArgumentValue(argumentIndex);

            // the extern annotation optionally marks bindings the compiler itself emits calls to
            if (annotationKind == AnnotationKind::Extern && argumentName == "required")
            {
                if (buffer.kind(valueIndex) != NodeKind::BoolLiteral)
                {
                    ExpressionContext argumentContext{};
                    argumentContext.contextualNumberType = m_module.wellKnown().cstring;
                    const auto requiredArgumentType = typeCheckExpression(unit, valueIndex, argumentContext);
                    m_diagnostics.addAnnotationArgumentTypeMismatchError(
                        unit.source,
                        NodeSpans::expressionSourceLocation(buffer, tokens, valueIndex),
                        annotationKind,
                        std::string(annotationName),
                        "a bool literal for 'required'",
                        "an expression of type '" + formatTypeName(m_module, requiredArgumentType) + "'");
                    return false;
                }

                if (requiredValue != nullptr)
                {
                    *requiredValue = tokens.getToken(static_cast<i32>(buffer.mainToken(valueIndex))).kind == TokenKind::TrueKeyword;
                }
                continue;
            }

            if (namedStringArgument.empty() || argumentName != namedStringArgument)
            {
                m_diagnostics.addUnexpectedAnnotationArgumentError(unit.source, argumentNameLocation, std::string(annotationName), std::string(argumentName));
                return false;
            }

            auto argumentType = Type::Undefined();
            {
                ExpressionContext argumentContext{};
                argumentContext.contextualNumberType = m_module.wellKnown().cstring;
                argumentType = typeCheckExpression(unit, valueIndex, argumentContext);
            }
            if (argumentType == Type::Undefined())
            {
                return false;
            }

            if (buffer.kind(valueIndex) != NodeKind::StringLiteral)
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    unit.source,
                    NodeSpans::expressionSourceLocation(buffer, tokens, valueIndex),
                    annotationKind,
                    std::string(annotationName),
                    "a string literal for '" + std::string(namedStringArgument) + "'",
                    "an expression of type '" + formatTypeName(m_module, argumentType) + "'");
                return false;
            }

            if (stringArgumentValue != nullptr)
            {
                // the string token's lexeme still carries its quotes and escapes
                const auto lexeme = nameOf(unit, valueIndex);
                const auto withoutQuotes = lexeme.substr(1, lexeme.size() - 2);
                *stringArgumentValue = ReplaceEscapeSequences(withoutQuotes);
            }
        }

        return true;
    }

    static std::string FormatAnnotationArgumentValue(const NodeBuffer& buffer, const TokenBuffer& tokens, const SourceTextSharedPtr& source, u32 valueIndex)
    {
        const auto location = NodeSpans::expressionSourceLocation(buffer, tokens, valueIndex);
        const auto& text = source->text;
        if (location.startIndex < 0 || location.endIndex > static_cast<i32>(text.size()) || location.endIndex <= location.startIndex)
        {
            return {};
        }

        return text.substr(location.startIndex, location.endIndex - location.startIndex);
    }

    bool TypeChecker::validateBuiltinAnnotationArguments(const CompilationUnit& unit, u32 annotationIndex)
    {
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto annotationName = nameOf(unit, annotationIndex);
        const auto annotationKind = ParseAnnotationKind(annotationName);
        const auto location = annotationLocation(unit, annotationIndex);

        std::vector<u32> arguments;
        NodeLayout::collectChildren(buffer, annotationIndex, arguments);

        // builtins need to at least define their kind
        if (arguments.empty())
        {
            m_diagnostics.addAnnotationMissingArgumentsError(
                unit.source,
                NodeSpans::annotationArgumentsLocation(buffer, tokens, annotationIndex, location),
                annotationKind,
                std::string(annotationName),
                KindAnnotationArgumentName);
            return false;
        }

        auto kindArgument = InvalidNodeIndex;
        auto bitsArgument = InvalidNodeIndex;
        auto signedArgument = InvalidNodeIndex;
        std::vector<std::string_view> seenArgumentNames;
        for (const auto argumentIndex : arguments)
        {
            if (buffer.kind(argumentIndex) != NodeKind::NamedArgument)
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    unit.source,
                    NodeSpans::expressionSourceLocation(buffer, tokens, argumentIndex),
                    annotationKind,
                    std::string(annotationName),
                    "named arguments like 'kind = int'",
                    "a positional argument");
                return false;
            }

            const auto argumentName = nameOf(unit, argumentIndex);
            const auto& argumentNameLocation = nameLocation(unit, argumentIndex);
            if (std::find(seenArgumentNames.begin(), seenArgumentNames.end(), argumentName) != seenArgumentNames.end())
            {
                m_diagnostics.addDuplicateAnnotationArgumentError(unit.source, argumentNameLocation, std::string(annotationName), std::string(argumentName));
                return false;
            }
            seenArgumentNames.push_back(argumentName);

            if (argumentName == KindAnnotationArgumentName)
            {
                kindArgument = argumentIndex;
            }
            else if (argumentName == BitsAnnotationArgumentName)
            {
                bitsArgument = argumentIndex;
            }
            else if (argumentName == SignedAnnotationArgumentName)
            {
                signedArgument = argumentIndex;
            }
            else
            {
                m_diagnostics.addUnexpectedAnnotationArgumentError(unit.source, argumentNameLocation, std::string(annotationName), std::string(argumentName));
                return false;
            }
        }

        if (kindArgument == InvalidNodeIndex)
        {
            m_diagnostics.addAnnotationMissingArgumentsError(
                unit.source,
                NodeSpans::annotationArgumentsLocation(buffer, tokens, annotationIndex, location),
                annotationKind,
                std::string(annotationName),
                KindAnnotationArgumentName);
            return false;
        }

        auto kindName = std::string_view();
        const auto kindValue = NodeLayout::namedArgumentValue(kindArgument);
        if (buffer.kind(kindValue) == NodeKind::NameExpression)
        {
            kindName = nameOf(unit, kindValue);
        }

        const auto isInt = kindName == BuiltinKindIntName;
        const auto isFloat = kindName == BuiltinKindFloatName;
        const auto isBool = kindName == BuiltinKindBoolName;
        const auto isPointer = kindName == BuiltinKindPointerName;
        if (!isInt && !isFloat && !isBool && !isPointer)
        {
            m_diagnostics.addAnnotationArgumentTypeMismatchError(
                unit.source,
                NodeSpans::expressionSourceLocation(buffer, tokens, kindValue),
                annotationKind,
                std::string(annotationName),
                "one of 'int', 'float', 'bool' or 'pointer' for 'kind'",
                "'" + FormatAnnotationArgumentValue(buffer, tokens, unit.source, kindValue) + "'");
            return false;
        }

        if (isInt || isFloat)
        {
            if (bitsArgument == InvalidNodeIndex)
            {
                m_diagnostics.addAnnotationMissingArgumentsError(
                    unit.source,
                    NodeSpans::annotationArgumentsLocation(buffer, tokens, annotationIndex, location),
                    annotationKind,
                    std::string(annotationName),
                    BitsAnnotationArgumentName);
                return false;
            }

            const auto bitsValue = NodeLayout::namedArgumentValue(bitsArgument);
            auto bits = 0;
            if (buffer.kind(bitsValue) == NodeKind::NumberLiteral)
            {
                const auto parsed = tryParseI32Literal(nameOf(unit, bitsValue));
                if (parsed.has_value())
                {
                    bits = parsed.value();
                }
            }

            // llvm supports integers up to 65k bits
            if (isInt && !(bits >= 1 && bits <= 65535))
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    unit.source,
                    NodeSpans::expressionSourceLocation(buffer, tokens, bitsValue),
                    annotationKind,
                    std::string(annotationName),
                    "a number between 1 and 65535 for 'bits'",
                    "'" + FormatAnnotationArgumentValue(buffer, tokens, unit.source, bitsValue) + "'");
                return false;
            }

            if (isFloat && !(bits == 32 || bits == 64))
            {
                m_diagnostics.addUnsupportedFloatBitsError(
                    unit.source,
                    NodeSpans::expressionSourceLocation(buffer, tokens, bitsValue));
                return false;
            }
        }
        else if (bitsArgument != InvalidNodeIndex)
        {
            m_diagnostics.addUnsupportedAnnotationArgumentError(
                unit.source,
                nameLocation(unit, bitsArgument),
                std::string(nameOf(unit, bitsArgument)),
                std::string(kindName));
            return false;
        }

        if (isInt)
        {
            if (signedArgument == InvalidNodeIndex)
            {
                m_diagnostics.addAnnotationMissingArgumentsError(
                    unit.source,
                    NodeSpans::annotationArgumentsLocation(buffer, tokens, annotationIndex, location),
                    annotationKind,
                    std::string(annotationName),
                    SignedAnnotationArgumentName);
                return false;
            }

            const auto signedValue = NodeLayout::namedArgumentValue(signedArgument);
            if (buffer.kind(signedValue) != NodeKind::BoolLiteral)
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    unit.source,
                    NodeSpans::expressionSourceLocation(buffer, tokens, signedValue),
                    annotationKind,
                    std::string(annotationName),
                    "'true' or 'false' for 'signed'",
                    "'" + FormatAnnotationArgumentValue(buffer, tokens, unit.source, signedValue) + "'");
                return false;
            }
        }
        else if (signedArgument != InvalidNodeIndex)
        {
            m_diagnostics.addUnsupportedAnnotationArgumentError(
                unit.source,
                nameLocation(unit, signedArgument),
                std::string(nameOf(unit, signedArgument)),
                std::string(kindName));
            return false;
        }

        return true;
    }

    bool TypeChecker::validateAnnotation(const CompilationUnit& unit, u32 annotationIndex, TokenKind targetKind, std::optional<i32>* i32ArgumentValue, std::optional<std::string>* stringArgumentValue, bool* requiredValue)
    {
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto annotationName = nameOf(unit, annotationIndex);
        const auto annotationKind = ParseAnnotationKind(annotationName);
        const auto location = annotationLocation(unit, annotationIndex);
        const auto* definition = GetAnnotationDefinition(annotationKind);
        if (definition == nullptr)
        {
            m_diagnostics.addUnknownAnnotationError(unit.source, location, std::string(annotationName), targetKind);
            return false;
        }

        if (definition->targetKind != targetKind)
        {
            m_diagnostics.addUnexpectedAnnotationTargetError(unit.source, location);
            return false;
        }

        if (annotationKind == AnnotationKind::Builtin)
        {
            return validateBuiltinAnnotationArguments(unit, annotationIndex);
        }

        if (!validateNamedAnnotationArguments(unit, annotationIndex, definition->namedStringArgument, stringArgumentValue, requiredValue))
        {
            return false;
        }

        std::vector<u32> arguments;
        NodeLayout::collectChildren(buffer, annotationIndex, arguments);

        auto firstPositionalArgument = InvalidNodeIndex;
        i32 actualCount = 0;
        for (const auto argumentIndex : arguments)
        {
            if (buffer.kind(argumentIndex) == NodeKind::NamedArgument)
            {
                continue;
            }

            if (firstPositionalArgument == InvalidNodeIndex)
            {
                firstPositionalArgument = argumentIndex;
            }
            ++actualCount;
        }

        if (!definition->namedStringArgument.empty() && actualCount > 0
            && buffer.kind(firstPositionalArgument) == NodeKind::StringLiteral)
        {
            m_diagnostics.addAnnotationMissingArgumentsError(
                unit.source,
                NodeSpans::annotationArgumentsLocation(buffer, tokens, annotationIndex, location),
                annotationKind,
                std::string(annotationName),
                std::string(definition->namedStringArgument));
            return false;
        }

        if (definition->requiredArgumentCount > 0 && !buffer.annotationHasParentheses(tokens, annotationIndex))
        {
            m_diagnostics.addAnnotationMissingArgumentsError(unit.source, location, annotationKind, std::string(annotationName));
            return false;
        }

        if (actualCount != definition->requiredArgumentCount)
        {
            m_diagnostics.addAnnotationWrongNumberOfArgumentsError(
                unit.source,
                NodeSpans::annotationArgumentsLocation(buffer, tokens, annotationIndex, location),
                annotationKind,
                std::string(annotationName),
                definition->requiredArgumentCount,
                actualCount);
            return false;
        }

        if (definition->requiresIntegerArgument)
        {
            const auto expectedType = m_module.wellKnown().i32;
            const auto argumentIndex = firstPositionalArgument;
            auto argumentType = Type::Undefined();
            {
                ExpressionContext argumentContext{};
                argumentContext.contextualNumberType = m_module.wellKnown().cstring;
                argumentType = typeCheckExpression(unit, argumentIndex, argumentContext);
            }
            if (argumentType == Type::Undefined())
            {
                return false;
            }

            if (buffer.kind(argumentIndex) != NodeKind::NumberLiteral)
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    unit.source,
                    NodeSpans::expressionSourceLocation(buffer, tokens, argumentIndex),
                    annotationKind,
                    std::string(annotationName),
                    "a " + formatTypeName(m_module, expectedType) + " number literal argument",
                    "an expression of type '" + formatTypeName(m_module, argumentType) + "'");
                return false;
            }

            if (argumentType != expectedType)
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    unit.source,
                    NodeSpans::expressionSourceLocation(buffer, tokens, argumentIndex),
                    annotationKind,
                    std::string(annotationName),
                    "a " + formatTypeName(m_module, expectedType) + " number literal argument",
                    "a number literal of type '" + formatTypeName(m_module, argumentType) + "'");
                return false;
            }

            if (i32ArgumentValue != nullptr)
            {
                *i32ArgumentValue = convertToI32(unit, argumentIndex);
            }
        }

        return true;
    }

    bool TypeChecker::validateCallableAnnotations(const CompilationUnit& unit, const std::vector<u32>& annotations, std::optional<std::string>& symbolName, bool& isRequired)
    {
        auto isExtern = false;
        for (const auto annotationIndex : annotations)
        {
            std::optional<std::string> annotationSymbolName;
            if (!validateAnnotation(unit, annotationIndex, TokenKind::DefKeyword, nullptr, &annotationSymbolName, &isRequired))
            {
                continue;
            }

            if (ParseAnnotationKind(nameOf(unit, annotationIndex)) == AnnotationKind::Extern)
            {
                isExtern = true;
                if (annotationSymbolName.has_value())
                {
                    symbolName = std::move(annotationSymbolName);
                }
            }
        }

        return isExtern;
    }

    void TypeChecker::validateEnumAnnotation(const CompilationUnit& unit, u32 enumIndex, const std::vector<u32>& annotations, bool& isFlag, std::optional<i32>& stepValue)
    {
        static_cast<void>(enumIndex);

        isFlag = false;
        stepValue = std::nullopt;

        auto flagAnnotation = InvalidNodeIndex;
        auto stepAnnotation = InvalidNodeIndex;

        for (const auto annotationIndex : annotations)
        {
            auto i32ArgumentValue = std::optional<i32>{};
            if (!validateAnnotation(unit, annotationIndex, TokenKind::EnumKeyword, &i32ArgumentValue))
            {
                continue;
            }

            switch (ParseAnnotationKind(nameOf(unit, annotationIndex)))
            {
                case AnnotationKind::Flag:
                {
                    if (stepAnnotation != InvalidNodeIndex)
                    {
                        m_diagnostics.addConflictingEnumAnnotationsError(
                            unit.source,
                            annotationLocation(unit, annotationIndex),
                            annotationLocation(unit, stepAnnotation),
                            std::string(nameOf(unit, annotationIndex)),
                            std::string(nameOf(unit, stepAnnotation)));
                        break;
                    }

                    isFlag = true;
                    flagAnnotation = annotationIndex;
                    break;
                }
                case AnnotationKind::Step:
                {
                    if (flagAnnotation != InvalidNodeIndex)
                    {
                        m_diagnostics.addConflictingEnumAnnotationsError(
                            unit.source,
                            annotationLocation(unit, annotationIndex),
                            annotationLocation(unit, flagAnnotation),
                            std::string(nameOf(unit, annotationIndex)),
                            std::string(nameOf(unit, flagAnnotation)));
                        break;
                    }

                    stepValue = i32ArgumentValue;
                    stepAnnotation = annotationIndex;
                    break;
                }
                default:
                {
                    break;
                }
            }
        }
    }

    void TypeChecker::validateTypeAnnotation(const CompilationUnit& unit, u32 typeIndex, const std::vector<u32>& annotations)
    {
        static_cast<void>(typeIndex);

        for (const auto annotationIndex : annotations)
        {
            static_cast<void>(validateAnnotation(unit, annotationIndex, TokenKind::TypeKeyword));
        }
    }
}
