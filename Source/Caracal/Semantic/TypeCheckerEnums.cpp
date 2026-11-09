#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace Caracal
{
    [[nodiscard]] static std::optional<i64> TryGetFoldedEnumInteger(const FoldValue& folded)
    {
        return std::visit([](const auto value) -> std::optional<i64>
            {
                using Payload = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Payload, u64>)
                {
                    if (value > static_cast<u64>(std::numeric_limits<i64>::max()))
                    {
                        return std::nullopt;
                    }

                    return static_cast<i64>(value);
                }
                else if constexpr (std::is_integral_v<Payload> && !std::is_same_v<Payload, bool>)
                {
                    return static_cast<i64>(value);
                }
                else
                {
                    return std::nullopt;
                }
            }, folded);
    }

    [[nodiscard]] static std::optional<i64> TryAddEnumStep(std::optional<i64> value, i64 step) noexcept
    {
        if (!value.has_value()
            || (step > 0 && value.value() > std::numeric_limits<i64>::max() - step)
            || (step < 0 && value.value() < std::numeric_limits<i64>::min() - step))
        {
            return std::nullopt;
        }

        return value.value() + step;
    }

    [[nodiscard]] static bool IsIntegerType(const SemanticContext& module, Type type) noexcept
    {
        const auto* description = module.tryGetBuiltinTypeDescription(type);
        return description != nullptr && description->kind == BuiltinTypeKind::Int;
    }

    // the bits of a value, without the sign
    [[nodiscard]] static i32 EnumValueBits(const BuiltinTypeDescription& description) noexcept
    {
        if (description.isSigned)
        {
            return description.bits - 1;
        }

        return description.bits;
    }

    [[nodiscard]] static bool EnumValueFits(const SemanticContext& module, Type baseType, i64 value) noexcept
    {
        if (!IsIntegerType(module, baseType))
        {
            return true;
        }

        const auto& description = *module.tryGetBuiltinTypeDescription(baseType);
        if (description.bits >= 64)
        {
            return description.isSigned || value >= 0;
        }

        const auto top = (i64{ 1 } << EnumValueBits(description)) - 1;
        if (description.isSigned)
        {
            return value >= -top - 1 && value <= top;
        }

        return value >= 0 && value <= top;
    }

    [[nodiscard]] static bool EnumFlagFits(const SemanticContext& module, Type baseType, size_t flagIndex) noexcept
    {
        return !IsIntegerType(module, baseType)
            || flagIndex < static_cast<size_t>(EnumValueBits(*module.tryGetBuiltinTypeDescription(baseType)));
    }

    void TypeChecker::typeCheckEnumDefinitions()
    {
        CARACAL_ZONE_NAMED("check.enumDefinitions");
        for (const auto declaration : m_enumDeclarations)
        {
            const auto& unit = unitFor(declaration);
            typeCheckEnumDefinitionStatement(unit, declaration.index);
        }
    }

    void TypeChecker::typeCheckEnumDefinitionStatement(const CompilationUnit& unit, u32 index)
    {
        auto baseType = Type::Undefined();
        const auto defaultBaseType = m_defaultEnumBaseType;
        const auto enumType = m_module.tryGetTypeByName(nameOf(unit, index));
        if (enumType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& enumDefinition = m_module.getEnumDefinition(enumType);
        auto isFlag = false;
        auto stepValue = std::optional<i32>{};
        std::vector<u32> annotations;
        NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
        validateEnumAnnotation(unit, index, annotations, isFlag, stepValue);
        auto currentFieldValue = std::optional<i64>{ 0 };
        auto flagIndex = size_t{ 0 };
        const auto step = stepValue.value_or(1);

        std::vector<u32> children;
        NodeLayout::collectChildren(unit.nodeBuffer, index, children);
        auto firstField = size_t{ 0 };
        if (!children.empty() && unit.nodeBuffer.kind(children.front()) != NodeKind::EnumFieldDeclaration)
        {
            baseType = typeCheckTypeNameNode(unit, children.front());
            enumDefinition.setBaseType(baseType);
            firstField = 1;
            if (baseType != Type::Undefined() && !canBeEnumBaseType(baseType))
            {
                m_diagnostics.addEnumBaseTypeCannotBeBuiltError(
                    unit.source,
                    ExpressionSourceLocation(m_compilationContext, unit, children.front()),
                    formatTypeName(m_module, baseType));
            }
        }

        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        for (auto fieldPosition = firstField; fieldPosition < children.size(); ++fieldPosition)
        {
            const auto fieldNode = children[fieldPosition];
            const auto fieldName = nameOf(unit, fieldNode);
            if (const auto* otherField = enumDefinition.tryGetFieldByName(fieldName); otherField != nullptr)
            {
                m_diagnostics.addDuplicateEnumFieldDeclarationError(
                    unit.source,
                    nameLocation(unit, fieldNode),
                    std::string(fieldName),
                    otherField->location());
                continue;
            }

            const auto fieldLocation = nameLocation(unit, fieldNode);
            const auto valueExpression = NodeLayout::optionalChild(unit.nodeBuffer, fieldNode);
            if (isFlag)
            {
                if (valueExpression.hasChild)
                {
                    m_diagnostics.addFlagEnumExplicitValueError(
                        unit.source,
                        NodeSpans::expressionLocation(unit.nodeBuffer, tokens, valueExpression.child),
                        std::string(fieldName));
                }

                // EnumIntegerLiteral casts the value back to u64, so bit 63 is safe in an i64
                auto flagValue = i64{ 0 };
                if (flagIndex < 64)
                {
                    flagValue = static_cast<i64>(u64{ 1 } << flagIndex);
                }
                auto flagBaseType = baseType;
                if (flagBaseType == Type::Undefined())
                {
                    flagBaseType = defaultBaseType;
                }
                if (!EnumFlagFits(m_module, flagBaseType, flagIndex))
                {
                    auto flagValueText = "2^" + std::to_string(flagIndex);
                    if (flagIndex < 64)
                    {
                        flagValueText = std::to_string(u64{ 1 } << flagIndex);
                    }
                    m_diagnostics.addEnumValueDoesNotFitError(unit.source, fieldLocation, std::string(fieldName), flagValueText, formatTypeName(m_module, flagBaseType));
                }

                enumDefinition.addField(fieldName, flagValue, fieldLocation);
                ++flagIndex;
            }
            else if (valueExpression.hasChild)
            {
                const auto expression = valueExpression.child;
                auto contextualType = defaultBaseType;
                if (baseType != Type::Undefined())
                {
                    contextualType = baseType;
                }
                else if (unit.nodeBuffer.kind(expression) == NodeKind::StringLiteral)
                {
                    // TODO enum string values are cstrings for now until enums can handle any types
                    contextualType = m_module.wellKnown().cstring;
                }

                const auto diagnosticsBeforeValue = m_diagnostics.diagnostics().size();
                auto checkedFieldValue = CheckedValue{ Type::Undefined() };
                {
                    auto context = ExpressionContext{};
                    context.contextualNumberType = contextualType;
                    checkedFieldValue = typeCheckExpression(unit, expression, context);
                }
                const auto valueHasError = m_diagnostics.diagnostics().size() != diagnosticsBeforeValue;
                const auto fieldValueType = checkedFieldValue.type;
                if (baseType == Type::Undefined())
                {
                    baseType = fieldValueType;
                    enumDefinition.setBaseType(baseType);
                    if (baseType != Type::Undefined() && !canBeEnumBaseType(baseType))
                    {
                        m_diagnostics.addEnumBaseTypeCannotBeBuiltError(
                            unit.source,
                            NodeSpans::expressionLocation(unit.nodeBuffer, tokens, expression),
                            formatTypeName(m_module, baseType));
                    }
                }
                else if (fieldValueType != baseType)
                {
                    if (fieldValueType != Type::Undefined())
                    {
                        m_diagnostics.addEnumFieldValueTypeMismatchError(
                            unit.source,
                            NodeSpans::expressionLocation(unit.nodeBuffer, tokens, expression),
                            formatTypeName(m_module, baseType),
                            formatTypeName(m_module, fieldValueType));
                    }
                }

                auto integerValue = std::optional<i64>{};
                if (checkedFieldValue.folded.has_value())
                {
                    integerValue = TryGetFoldedEnumInteger(checkedFieldValue.folded.value());
                }

                if (checkedFieldValue.folded.has_value() && IsIntegerType(m_module, baseType))
                {
                    currentFieldValue = TryAddEnumStep(integerValue, step);
                }

                if (integerValue.has_value() && unit.nodeBuffer.kind(expression) == NodeKind::NumberLiteral)
                {
                    enumDefinition.addField(fieldName, integerValue.value(), fieldLocation);
                }
                else
                {
                    enumDefinition.addField(fieldName, fieldLocation);
                    if (!tryRecordExpressionEnumFieldConstant(unit, enumType, fieldName, expression, checkedFieldValue)
                        && !valueHasError
                        && fieldValueType == baseType
                        && canBeEnumBaseType(baseType))
                    {
                        m_diagnostics.addNonConstantEnumValueError(
                            unit.source,
                            NodeSpans::expressionLocation(unit.nodeBuffer, tokens, expression),
                            ContainsRuntimeCall(m_module, unit, tokens, expression));
                    }
                }
            }
            else
            {
                auto fieldBaseType = baseType;
                if (fieldBaseType == Type::Undefined())
                {
                    fieldBaseType = defaultBaseType;
                }
                if (!currentFieldValue.has_value())
                {
                    // currentFieldValue is empty when the next value does not fit an i64
                    m_diagnostics.addEnumValueDoesNotFitError(unit.source, fieldLocation, std::string(fieldName), std::string{}, formatTypeName(m_module, fieldBaseType));
                }
                else if (!EnumValueFits(m_module, fieldBaseType, currentFieldValue.value()))
                {
                    m_diagnostics.addEnumValueDoesNotFitError(unit.source, fieldLocation, std::string(fieldName), std::to_string(currentFieldValue.value()), formatTypeName(m_module, fieldBaseType));
                }

                enumDefinition.addField(fieldName, currentFieldValue.value_or(0), fieldLocation);
                currentFieldValue = TryAddEnumStep(currentFieldValue, step);
            }
        }

        if (enumDefinition.baseType() == Type::Undefined())
        {
            enumDefinition.setBaseType(defaultBaseType);
        }

        if (enumDefinition.baseType() == m_module.wellKnown().cstring)
        {
            for (const auto& field : enumDefinition.fields())
            {
                if (field.hasConstantValue())
                {
                    m_diagnostics.addEnumFieldWithoutValueError(unit.source, field.location(), std::string(field.name()));
                }
            }
        }
    }

    bool TypeChecker::canBeEnumBaseType(Type type) const noexcept
    {
        if (type == m_module.wellKnown().cstring)
        {
            return true;
        }

        const auto* description = m_module.tryGetBuiltinTypeDescription(type);
        return type.isBaseType() && description != nullptr && description->kind != BuiltinTypeKind::Pointer;
    }

    bool TypeChecker::tryRecordExpressionEnumFieldConstant(const CompilationUnit& unit, Type enumType, std::string_view fieldName, u32 expressionIndex, const CheckedValue& checkedFieldValue)
    {
        if (checkedFieldValue.folded.has_value())
        {
            m_emitter.recordEnumFieldConstant(enumType.id(), std::string(fieldName), IREmitter::constantFromFold(checkedFieldValue.folded.value()), checkedFieldValue.folded);
            return true;
        }

        // cstring valued fields store their literal bytes
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, expressionIndex);
        if (unit.nodeBuffer.kind(stripped) == NodeKind::StringLiteral
            && checkedFieldValue.type == m_module.wellKnown().cstring)
        {
            m_emitter.recordEnumFieldConstant(enumType.id(), std::string(fieldName), ConstantValue::FromString(escapedStringContent(unit, stripped)), std::nullopt);
            return true;
        }

        return false;
    }
}
