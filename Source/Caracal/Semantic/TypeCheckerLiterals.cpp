#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/StringEscapes.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace Caracal
{
    [[nodiscard]] static u64 ArrayLiteralKey(u16 fileId, u32 literalIndex) noexcept
    {
        return (static_cast<u64>(fileId) << 32) | literalIndex;
    }

    [[nodiscard]] static FoldValue MakeFoldValue(const NumberLiteralValue& parsedValue) noexcept
    {
        return std::visit([](const auto value) { return FoldValue{ value }; }, parsedValue);
    }

    std::string TypeChecker::escapedStringContent(const CompilationUnit& unit, u32 literalIndex)
    {
        const auto lexeme = nameOf(unit, literalIndex);
        if (lexeme.size() < 2)
        {
            return std::string{};
        }

        return replaceEscapeSequences(lexeme.substr(1, lexeme.size() - 2));
    }

    void TypeChecker::emitCapturedArrayLiteral(const CompilationUnit& unit, u32 literalIndex, ValueRef destination, Type arrayType)
    {
        const auto captured = m_arrayLiteralElements.find(ArrayLiteralKey(unit.fileId, literalIndex));
        if (captured == m_arrayLiteralElements.end() || destination.id() < 0)
        {
            return;
        }

        const auto elementType = m_module.getArrayElementType(arrayType);
        auto elementBase = destination;
        if (arrayType.kind() == TypeKind::DynamicArray)
        {
            // dynamic literals fill the calloc'd buffer behind the descriptor
            elementBase = m_emitter.emitDynamicArrayStorage(destination, arrayType, static_cast<i32>(captured->second.size()));
            if (elementBase.id() < 0)
            {
                return;
            }
        }
        for (size_t i = 0; i < captured->second.size(); ++i)
        {
            const auto element = captured->second[i];
            const auto indexValue = m_emitter.emitConstant(ConstantValue::FromI32(static_cast<i32>(i)), m_module.wellKnown().i32);
            const auto elementAddress = m_emitter.emitElementAddress(elementBase, arrayType, indexValue, elementType);

            const auto strippedElement = NodeLayout::stripGroupings(unit.nodeBuffer, element.node);
            if (unit.nodeBuffer.kind(strippedElement) == NodeKind::ArrayLiteral)
            {
                // nested literals fill their element storage directly
                emitCapturedArrayLiteral(unit, strippedElement, elementAddress, elementType);
                continue;
            }

            auto elementValue = element.value;
            if (element.folded.has_value())
            {
                // the checked value is reused unless it has another type than the array's elements
                const auto checkedType = m_module.typeOfNode(NodeIndex{ unit.fileId, element.node }).toValue();
                if (elementValue.id() < 0 || checkedType != elementType.toValue())
                {
                    elementValue = m_emitter.emitFoldedConstant(element.folded.value(), elementType.toValue());
                }
            }
            m_emitter.emitStoreValue(elementValue, elementAddress, elementType);
        }
    }

    ValueRef TypeChecker::emitArrayLiteralToTempSlot(const CompilationUnit& unit, u32 literalIndex, Type arrayType)
    {
        const auto temp = m_emitter.allocateTempAggregateSlot("temp", arrayType);
        emitCapturedArrayLiteral(unit, literalIndex, temp, arrayType);
        return temp;
    }

    std::optional<ConstantValue> TypeChecker::tryBuildAggregateConstant(const CompilationUnit& unit, u32 literalIndex)
    {
        const auto captured = m_arrayLiteralElements.find(ArrayLiteralKey(unit.fileId, literalIndex));
        if (captured == m_arrayLiteralElements.end())
        {
            return std::nullopt;
        }

        ConstantValue::AggregateData elements{};
        elements.reserve(captured->second.size());
        for (const auto& element : captured->second)
        {
            const auto strippedElement = NodeLayout::stripGroupings(unit.nodeBuffer, element.node);
            if (unit.nodeBuffer.kind(strippedElement) == NodeKind::ArrayLiteral)
            {
                const auto nested = tryBuildAggregateConstant(unit, strippedElement);
                if (!nested.has_value())
                {
                    return std::nullopt;
                }
                elements.push_back(nested.value());
                continue;
            }

            if (!element.folded.has_value())
            {
                return std::nullopt;
            }
            elements.push_back(IREmitter::constantFromFold(element.folded.value()));
        }

        return ConstantValue::FromAggregate(std::move(elements));
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckNumberLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.numberLiteral");
        auto numberType = Type::Undefined();
        auto literalConsumedSign = false;
        auto parsedValue = std::optional<NumberLiteralValue>{};
        const auto lexeme = nameOf(unit, index);
        const auto suffix = NodeLayout::optionalChild(unit.nodeBuffer, index);
        if (suffix.hasChild)
        {
            numberType = typeCheckTypeNameNode(unit, suffix.child);
        }
        else
        {
            const auto isFloatingLiteral = lexeme.find('.') != std::string_view::npos;
            if (context.contextualNumberType.has_value())
            {
                const auto contextualType = context.contextualNumberType.value();
                const auto* contextualDescription = m_module.tryGetBuiltinTypeDescription(contextualType);
                if (!isFloatingLiteral && contextualDescription != nullptr && contextualDescription->kind == BuiltinTypeKind::Int)
                {
                    numberType = contextualType;
                }
                else if (isFloatingLiteral && contextualDescription != nullptr && contextualDescription->kind == BuiltinTypeKind::Float)
                {
                    numberType = contextualType;
                }
            }

            if (numberType == Type::Undefined() && isFloatingLiteral)
            {
                numberType = m_defaultFloatingType;
            }
            else if (numberType == Type::Undefined())
            {
                numberType = m_defaultIntegerType;
            }
        }

        const auto* numberDescription = m_module.tryGetBuiltinTypeDescription(numberType);
        if (numberType != Type::Undefined()
            && numberDescription != nullptr
            && (numberDescription->kind == BuiltinTypeKind::Int || numberDescription->kind == BuiltinTypeKind::Float))
        {
            parsedValue = tryParseNumberLiteralValue(lexeme, numberType, m_module);
            if (!parsedValue.has_value() && context.negatedLiteral
                && numberDescription->kind == BuiltinTypeKind::Int && numberDescription->isSigned && numberDescription->bits == 32)
            {
                const auto negatedValue = tryParseNegatedI32Literal(lexeme);
                if (negatedValue.has_value())
                {
                    parsedValue = NumberLiteralValue{ negatedValue.value() };
                    literalConsumedSign = true;
                }
            }

            if (!parsedValue.has_value())
            {
                m_diagnostics.addNumberLiteralOutOfRangeError(
                    unit.source,
                    nameLocation(unit, index),
                    std::string(lexeme),
                    formatTypeName(m_module, numberType));
                numberType = Type::Undefined();
            }
        }

        auto result = CheckedValue{ recordType(unit, index, numberType) };
        if (parsedValue.has_value())
        {
            result.signFolded = literalConsumedSign;
            result.folded = MakeFoldValue(parsedValue.value());
        }
        return result;
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckArrayLiteral(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto isDynamic = buffer.isDynamicArrayLiteral(tokens, index);

        const auto childrenStart = m_childrenStack.size();
        NodeLayout::appendChildren(buffer, index, m_childrenStack);
        const auto childrenEnd = m_childrenStack.size();
        const auto elementCount = childrenEnd - childrenStart;

        if (elementCount == 0)
        {
            // an empty dynamic literal has no elements to infer the type from, it
            // is only allowed where the expected type already says what it holds
            if (isDynamic && context.contextualNumberType.has_value()
                && context.contextualNumberType.value().kind() == TypeKind::DynamicArray)
            {
                const auto arrayType = context.contextualNumberType.value().toValue();
                // the empty fill still allocates storage, register the capture
                m_arrayLiteralElements[ArrayLiteralKey(unit.fileId, index)].clear();
                return recordType(unit, index, arrayType);
            }

            m_diagnostics.addEmptyArrayLiteralError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index));
            return recordType(unit, index, Type::Undefined());
        }

        // an enclosing array type provides the element type, everything else provides none
        auto contextualElementType = std::optional<Type>{};
        if (context.contextualNumberType.has_value())
        {
            const auto contextualType = context.contextualNumberType.value();
            if (contextualType.kind() == TypeKind::FixedArray
                || contextualType.kind() == TypeKind::DynamicArray
                || contextualType.kind() == TypeKind::Slice)
            {
                contextualElementType = m_module.getArrayElementType(contextualType);
            }
        }

        auto elementType = Type::Undefined();
        auto hasElementError = false;
        auto reportedElementMismatch = false;
        {
            auto elementContext = context;
            elementContext.contextualNumberType = contextualElementType;
            elementContext.wantsAddress = false;
            elementContext.receiverPosition = false;
            auto& capturedElements = m_arrayLiteralElements[ArrayLiteralKey(unit.fileId, index)];
            capturedElements.clear();
            for (auto position = childrenStart; position < childrenEnd; ++position)
            {
                const auto element = m_childrenStack[position];
                const auto checkedElement = typeCheckExpression(unit, element, elementContext);
                const auto elementValue = copyValueIfNeeded(unit, element, checkedElement.value, checkedElement.type);
                capturedElements.push_back(CapturedArrayElement{ element, elementValue, checkedElement.folded });
                const auto type = checkedElement.type;
                if (type == Type::Undefined())
                {
                    hasElementError = true;
                    continue;
                }

                if (elementType == Type::Undefined())
                {
                    elementType = type;
                }
                else if (type != elementType)
                {
                    if (!reportedElementMismatch)
                    {
                        m_diagnostics.addArrayElementTypeMismatchError(
                            unit.source,
                            ExpressionSourceLocation(m_compilationContext, unit, element),
                            formatTypeName(m_module, elementType),
                            formatTypeName(m_module, type));
                        reportedElementMismatch = true;
                    }
                    hasElementError = true;
                }
            }
        }
        m_childrenStack.resize(childrenStart);

        if (hasElementError || elementType == Type::Undefined())
        {
            return recordType(unit, index, Type::Undefined());
        }

        // ownership rule: arrays cant store references or slices
        if (elementType.isReference() || elementType.kind() == TypeKind::Slice)
        {
            m_diagnostics.addReferenceOrSliceArrayElementError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index));
        }
        else if (elementType.kind() == TypeKind::DynamicArray)
        {
            m_diagnostics.addDynamicArrayElementError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index));
        }

        auto arrayType = Type::Undefined();
        if (isDynamic)
        {
            arrayType = m_module.getOrCreateArrayType(TypeKind::DynamicArray, elementType, 0);
        }
        else
        {
            const auto length = static_cast<i32>(elementCount);
            arrayType = m_module.getOrCreateArrayType(TypeKind::FixedArray, elementType, length);
        }
        return recordType(unit, index, arrayType);
    }
}
