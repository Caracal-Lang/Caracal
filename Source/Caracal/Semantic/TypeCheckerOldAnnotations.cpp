#include "TypeCheckerOld.h"

#include <Caracal/Constants.h>
#include <Caracal/ScopedValue.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Syntax/BoolLiteral.h>
#include <Caracal/Syntax/StringLiteral.h>

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

    bool TypeCheckerOld::validateBuiltinAnnotationArguments(const AnnotationNode* annotation, const TokenBuffer& tokens)
    {
        // builtins need to at least define their kind
        if (annotation->arguments().empty())
        {
            m_diagnostics.addAnnotationMissingArgumentsError(
                tokens.source(),
                annotation->argumentsLocation(tokens),
                annotation->kind(),
                annotation->name(),
                KindAnnotationArgumentName);
            return false;
        }

        const Argument* kindArgument = nullptr;
        const Argument* bitsArgument = nullptr;
        const Argument* signedArgument = nullptr;
        std::vector<std::string_view> seenArgumentNames;
        for (const auto& argument : annotation->arguments())
        {
            if (!argument.isNamed())
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    tokens.source(),
                    argument.value()->sourceLocation(tokens),
                    annotation->kind(),
                    annotation->name(),
                    "named arguments like 'kind = int'",
                    "a positional argument");
                return false;
            }

            const auto nameLocation = tokens.getSourceLocation(argument.nameToken().value());
            if (std::find(seenArgumentNames.begin(), seenArgumentNames.end(), argument.name()) != seenArgumentNames.end())
            {
                m_diagnostics.addDuplicateAnnotationArgumentError(tokens.source(), nameLocation, annotation->name(), argument.name());
                return false;
            }
            seenArgumentNames.push_back(argument.name());

            if (argument.name() == KindAnnotationArgumentName)
            {
                kindArgument = &argument;
            }
            else if (argument.name() == BitsAnnotationArgumentName)
            {
                bitsArgument = &argument;
            }
            else if (argument.name() == SignedAnnotationArgumentName)
            {
                signedArgument = &argument;
            }
            else
            {
                m_diagnostics.addUnexpectedAnnotationArgumentError(tokens.source(), nameLocation, annotation->name(), argument.name());
                return false;
            }
        }

        if (kindArgument == nullptr)
        {
            m_diagnostics.addAnnotationMissingArgumentsError(
                tokens.source(),
                annotation->argumentsLocation(tokens),
                annotation->kind(),
                annotation->name(),
                KindAnnotationArgumentName);
            return false;
        }

        auto kindName = std::string_view();
        const auto* kindValue = kindArgument->value().get();
        if (kindValue->kind() == NodeKind::NameExpression)
        {
            kindName = static_cast<const NameExpression*>(kindValue)->name();
        }

        const auto isInt = kindName == BuiltinKindIntName;
        const auto isFloat = kindName == BuiltinKindFloatName;
        const auto isBool = kindName == BuiltinKindBoolName;
        const auto isPointer = kindName == BuiltinKindPointerName;
        if (!isInt && !isFloat && !isBool && !isPointer)
        {
            m_diagnostics.addAnnotationArgumentTypeMismatchError(
                tokens.source(),
                kindValue->sourceLocation(tokens),
                annotation->kind(),
                annotation->name(),
                "one of 'int', 'float', 'bool' or 'pointer' for 'kind'",
                "'" + formatAnnotationArgumentValue(kindValue, tokens) + "'");
            return false;
        }

        if (isInt || isFloat)
        {
            if (bitsArgument == nullptr)
            {
                m_diagnostics.addAnnotationMissingArgumentsError(
                    tokens.source(),
                    annotation->argumentsLocation(tokens),
                    annotation->kind(),
                    annotation->name(),
                    BitsAnnotationArgumentName);
                return false;
            }

            auto* bitsValue = bitsArgument->value().get();
            auto bits = 0;
            if (bitsValue->kind() == NodeKind::NumberLiteral)
            {
                const auto parsed = tryParseI32Literal(static_cast<const NumberLiteral*>(bitsValue)->literalLexeme());
                if (parsed.has_value())
                {
                    bits = parsed.value();
                }
            }

            // llvm supports integers up to 65k bits
            if (isInt && !(bits >= 1 && bits <= 65535))
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    tokens.source(),
                    bitsValue->sourceLocation(tokens),
                    annotation->kind(),
                    annotation->name(),
                    "a number between 1 and 65535 for 'bits'",
                    "'" + formatAnnotationArgumentValue(bitsValue, tokens) + "'");
                return false;
            }

            if (isFloat && !(bits == 32 || bits == 64))
            {
                m_diagnostics.addUnsupportedFloatBitsError(
                    tokens.source(),
                    bitsValue->sourceLocation(tokens));
                return false;
            }
        }
        else if (bitsArgument != nullptr)
        {
            m_diagnostics.addUnsupportedAnnotationArgumentError(
                tokens.source(),
                tokens.getSourceLocation(bitsArgument->nameToken().value()),
                std::string(bitsArgument->name()),
                std::string(kindName));
            return false;
        }

        if (isInt)
        {
            if (signedArgument == nullptr)
            {
                m_diagnostics.addAnnotationMissingArgumentsError(
                    tokens.source(),
                    annotation->argumentsLocation(tokens),
                    annotation->kind(),
                    annotation->name(),
                    SignedAnnotationArgumentName);
                return false;
            }

            if (signedArgument->value()->kind() != NodeKind::BoolLiteral)
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    tokens.source(),
                    signedArgument->value()->sourceLocation(tokens),
                    annotation->kind(),
                    annotation->name(),
                    "'true' or 'false' for 'signed'",
                    "'" + formatAnnotationArgumentValue(signedArgument->value().get(), tokens) + "'");
                return false;
            }
        }
        else if (signedArgument != nullptr)
        {
            m_diagnostics.addUnsupportedAnnotationArgumentError(
                tokens.source(),
                tokens.getSourceLocation(signedArgument->nameToken().value()),
                std::string(signedArgument->name()),
                std::string(kindName));
            return false;
        }

        return true;
    }

    bool TypeCheckerOld::validateNamedAnnotationArguments(const AnnotationNode* annotation, std::string_view namedStringArgument, const TokenBuffer& tokens, std::optional<std::string>* stringArgumentValue, bool* requiredValue)
    {
        std::vector<std::string_view> seenArgumentNames;
        for (const auto& argument : annotation->arguments())
        {
            if (!argument.isNamed())
            {
                continue;
            }

            const auto nameLocation = tokens.getSourceLocation(argument.nameToken().value());
            if (std::find(seenArgumentNames.begin(), seenArgumentNames.end(), argument.name()) != seenArgumentNames.end())
            {
                m_diagnostics.addDuplicateAnnotationArgumentError(tokens.source(), nameLocation, annotation->name(), argument.name());
                return false;
            }
            seenArgumentNames.push_back(argument.name());

            // the extern annotation optionally marks bindings the compiler itself emits calls to
            if (annotation->kind() == AnnotationKind::Extern && argument.name() == "required")
            {
                auto* requiredExpression = argument.value().get();
                if (requiredExpression->kind() != NodeKind::BoolLiteral)
                {
                    // annotation arguments are compiler metadata, string literals are cstrings
                    const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, m_module.wellKnown().cstring };
                    const auto requiredArgumentType = typeCheckExpression(requiredExpression, tokens);
                    m_diagnostics.addAnnotationArgumentTypeMismatchError(
                        tokens.source(),
                        requiredExpression->sourceLocation(tokens),
                        annotation->kind(),
                        annotation->name(),
                        "a bool literal for 'required'",
                        "an expression of type '" + formatTypeName(m_module, requiredArgumentType) + "'");
                    return false;
                }

                if (requiredValue != nullptr)
                {
                    *requiredValue = static_cast<const BoolLiteral*>(requiredExpression)->value();
                }
                continue;
            }

            if (namedStringArgument.empty() || argument.name() != namedStringArgument)
            {
                m_diagnostics.addUnexpectedAnnotationArgumentError(tokens.source(), nameLocation, annotation->name(), argument.name());
                return false;
            }

            auto* value = argument.value().get();
            auto argumentType = Type::Undefined();
            {
                // annotation arguments are compiler metadata, string literals are cstrings
                const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, m_module.wellKnown().cstring };
                argumentType = typeCheckExpression(value, tokens);
            }
            if (argumentType == Type::Undefined())
            {
                return false;
            }

            if (value->kind() != NodeKind::StringLiteral)
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    tokens.source(),
                    value->sourceLocation(tokens),
                    annotation->kind(),
                    annotation->name(),
                    "a string literal for '" + std::string(namedStringArgument) + "'",
                    "an expression of type '" + formatTypeName(m_module, argumentType) + "'");
                return false;
            }

            if (stringArgumentValue != nullptr)
            {
                *stringArgumentValue = static_cast<const StringLiteral*>(value)->escapedContent();
            }
        }

        return true;
    }

    bool TypeCheckerOld::validateAnnotation(const AnnotationNode* annotation, TokenKind targetKind, const TokenBuffer& tokens, std::optional<i32>* i32ArgumentValue, std::optional<std::string>* stringArgumentValue, bool* requiredValue)
    {
        const auto annotationLocation = annotation->sourceLocation(tokens);
        const auto* definition = GetAnnotationDefinition(annotation->kind());
        if (definition == nullptr)
        {
            m_diagnostics.addUnknownAnnotationError(tokens.source(), annotationLocation, annotation->name(), targetKind);
            return false;
        }

        if (definition->targetKind != targetKind)
        {
            m_diagnostics.addUnexpectedAnnotationTargetError(tokens.source(), annotationLocation);
            return false;
        }

        if (annotation->kind() == AnnotationKind::Builtin)
        {
            return validateBuiltinAnnotationArguments(annotation, tokens);
        }

        if (!validateNamedAnnotationArguments(annotation, definition->namedStringArgument, tokens, stringArgumentValue, requiredValue))
        {
            return false;
        }

        Expression* firstPositionalArgument = nullptr;
        i32 actualCount = 0;
        for (const auto& argument : annotation->arguments())
        {
            if (argument.isNamed())
            {
                continue;
            }

            if (firstPositionalArgument == nullptr)
            {
                firstPositionalArgument = argument.value().get();
            }
            ++actualCount;
        }

        if (!definition->namedStringArgument.empty() && actualCount > 0
            && firstPositionalArgument->kind() == NodeKind::StringLiteral)
        {
            m_diagnostics.addAnnotationMissingArgumentsError(
                tokens.source(),
                annotation->argumentsLocation(tokens),
                annotation->kind(),
                annotation->name(),
                std::string(definition->namedStringArgument));
            return false;
        }

        if (definition->requiredArgumentCount > 0 && !annotation->hasParentheses())
        {
            m_diagnostics.addAnnotationMissingArgumentsError(tokens.source(), annotationLocation, annotation->kind(), annotation->name());
            return false;
        }

        if (actualCount != definition->requiredArgumentCount)
        {
            m_diagnostics.addAnnotationWrongNumberOfArgumentsError(
                tokens.source(),
                annotation->argumentsLocation(tokens),
                annotation->kind(),
                annotation->name(),
                definition->requiredArgumentCount,
                actualCount);
            return false;
        }

        if (definition->requiresIntegerArgument)
        {
            const auto expectedType = m_module.wellKnown().i32;
            auto* argument = firstPositionalArgument;
            auto argumentType = Type::Undefined();
            {
                // annotation arguments are compiler metadata, string literals are cstrings
                const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, m_module.wellKnown().cstring };
                argumentType = typeCheckExpression(argument, tokens);
            }
            if (argumentType == Type::Undefined())
            {
                return false;
            }

            if (argument->kind() != NodeKind::NumberLiteral)
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    tokens.source(),
                    argument->sourceLocation(tokens),
                    annotation->kind(),
                    annotation->name(),
                    "a " + formatTypeName(m_module, expectedType) + " number literal argument",
                    "an expression of type '" + formatTypeName(m_module, argumentType) + "'");
                return false;
            }

            if (argumentType != expectedType)
            {
                m_diagnostics.addAnnotationArgumentTypeMismatchError(
                    tokens.source(),
                    argument->sourceLocation(tokens),
                    annotation->kind(),
                    annotation->name(),
                    "a " + formatTypeName(m_module, expectedType) + " number literal argument",
                    "a number literal of type '" + formatTypeName(m_module, argumentType) + "'");
                return false;
            }

            if (i32ArgumentValue != nullptr)
            {
                *i32ArgumentValue = convertToI32(static_cast<NumberLiteral*>(argument), tokens);
            }
        }

        return true;
    }

    bool TypeCheckerOld::validateCallableAnnotations(const std::vector<AnnotationNodeUPtr>& annotations, const TokenBuffer& tokens, std::optional<std::string>& symbolName, bool& isRequired)
    {
        auto isExtern = false;
        for (const auto& annotationNode : annotations)
        {
            const auto* annotation = annotationNode.get();
            std::optional<std::string> annotationSymbolName;
            if (!validateAnnotation(annotation, TokenKind::DefKeyword, tokens, nullptr, &annotationSymbolName, &isRequired))
            {
                continue;
            }

            if (annotation->kind() == AnnotationKind::Extern)
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

    void TypeCheckerOld::validateEnumAnnotation(const EnumDefinitionStatement* statement, const TokenBuffer& tokens, bool& isFlag, std::optional<i32>& stepValue)
    {
        isFlag = false;
        stepValue = std::nullopt;

        const AnnotationNode* flagAnnotation = nullptr;
        const AnnotationNode* stepAnnotation = nullptr;

        for (const auto& annotationNode : statement->annotations())
        {
            const auto* annotation = annotationNode.get();
            auto i32ArgumentValue = std::optional<i32>{};
            if (!validateAnnotation(annotation, TokenKind::EnumKeyword, tokens, &i32ArgumentValue))
            {
                continue;
            }

            switch (annotation->kind())
            {
                case AnnotationKind::Flag:
                    if (stepAnnotation != nullptr)
                    {
                        m_diagnostics.addConflictingEnumAnnotationsError(
                            tokens.source(),
                            annotation->sourceLocation(tokens),
                            stepAnnotation->sourceLocation(tokens),
                            annotation->name(),
                            stepAnnotation->name());
                        break;
                    }

                    isFlag = true;
                    flagAnnotation = annotation;
                    break;
                case AnnotationKind::Step:
                    if (flagAnnotation != nullptr)
                    {
                        m_diagnostics.addConflictingEnumAnnotationsError(
                            tokens.source(),
                            annotation->sourceLocation(tokens),
                            flagAnnotation->sourceLocation(tokens),
                            annotation->name(),
                            flagAnnotation->name());
                        break;
                    }

                    stepValue = i32ArgumentValue;
                    stepAnnotation = annotation;
                    break;
                default:
                    break;
            }
        }
    }

    void TypeCheckerOld::validateTypeAnnotation(const TypeDefinitionStatement* statement, const TokenBuffer& tokens)
    {
        for (const auto& annotationNode : statement->annotations())
        {
            static_cast<void>(validateAnnotation(annotationNode.get(), TokenKind::TypeKeyword, tokens));
        }
    }

}
