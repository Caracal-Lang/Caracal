#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/BinaryOperatorKind.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/UnaryOperatorKind.h>

#include <optional>
#include <string>

namespace Caracal
{
    [[nodiscard]] static UnaryOperatorKind ConvertToUnaryOperator(TokenKind kind) noexcept
    {
        switch (kind)
        {
            case TokenKind::Bang:
            {
                return UnaryOperatorKind::LogicalNegation;
            }
            case TokenKind::Minus:
            {
                return UnaryOperatorKind::ValueNegation;
            }
            case TokenKind::RefKeyword:
            {
                return UnaryOperatorKind::ReferenceOf;
            }
            default:
            {
                return UnaryOperatorKind::Invalid;
            }
        }
    }

    void TypeChecker::reportBinaryFoldFailure(const CompilationUnit& unit, FoldResultKind kind, Type operandType, u32 index, u32 rightIndex)
    {
        const auto* description = m_module.tryGetBuiltinTypeDescription(operandType);
        if (description == nullptr || description->kind != BuiltinTypeKind::Int)
        {
            return;
        }

        if (kind == FoldResultKind::DivideByZero)
        {
            m_diagnostics.addDivisionByZeroError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, rightIndex));
        }
        else if (kind == FoldResultKind::Overflow)
        {
            m_diagnostics.addConstantOverflowError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index),
                formatTypeName(m_module, operandType));
        }
    }

    void TypeChecker::reportUnaryFoldFailure(const CompilationUnit& unit, FoldResultKind kind, Type operandType, u32 index)
    {
        const auto* description = m_module.tryGetBuiltinTypeDescription(operandType);
        if (kind != FoldResultKind::Overflow || description == nullptr || description->kind != BuiltinTypeKind::Int)
        {
            return;
        }

        m_diagnostics.addConstantOverflowError(
            unit.source,
            ExpressionSourceLocation(m_compilationContext, unit, index),
            formatTypeName(m_module, operandType));
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckUnaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.unary");
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto unaryOperator = ConvertToUnaryOperator(tokens.getToken(static_cast<i32>(buffer.mainToken(index))).kind);
        switch (unaryOperator)
        {
            case UnaryOperatorKind::LogicalNegation:
            {
                const auto checkedOperand = typeCheckExpression(unit, NodeLayout::unaryOperand(index), context);
                const auto type = checkedOperand.type;
                const auto operandType = type.toValue();
                const auto* operatorSignature = m_module.tryGetOperatorSignature(operandType, UnaryOperatorKind::LogicalNegation);
                if (operandType != Type::Undefined() && operatorSignature == nullptr)
                {
                    m_diagnostics.addUnaryOperandTypeMismatchError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        "!",
                        formatTypeName(m_module, operandType),
                        "a 'bool' operand");

                    return recordType(unit, index, Type::Undefined());
                }

                auto result = CheckedValue{ recordType(unit, index, type) };
                if (operatorSignature != nullptr && checkedOperand.folded.has_value() && operatorSignature->unaryFold != nullptr)
                {
                    const auto foldResult = operatorSignature->unaryFold(checkedOperand.folded.value());
                    if (foldResult.kind == FoldResultKind::Value)
                    {
                        result.folded = foldResult.value;
                        return result;
                    }

                    reportUnaryFoldFailure(unit, foldResult.kind, operandType, index);
                }

                result.value = m_emitter.emitUnary(UnaryOperatorKind::LogicalNegation, materializeValue(checkedOperand), type);
                return result;
            }
            case UnaryOperatorKind::ValueNegation:
            {
                const auto operand = NodeLayout::unaryOperand(index);

                auto operandContext = context;
                operandContext.negatedLiteral = buffer.kind(operand) == NodeKind::NumberLiteral;
                const auto checkedOperand = typeCheckExpression(unit, operand, operandContext);
                const auto type = checkedOperand.type;
                const auto operandType = type.toValue();
                const auto* operatorSignature = m_module.tryGetOperatorSignature(operandType, UnaryOperatorKind::ValueNegation);
                if (operandType != Type::Undefined() && operatorSignature == nullptr)
                {
                    m_diagnostics.addUnaryOperandTypeMismatchError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        "-",
                        formatTypeName(m_module, operandType),
                        "a numeric operand");

                    return recordType(unit, index, Type::Undefined());
                }

                auto result = CheckedValue{ recordType(unit, index, type) };
                if (checkedOperand.signFolded)
                {
                    // the literal already parsed negative, nothing to negate
                    result.folded = checkedOperand.folded;
                    result.value = checkedOperand.value;
                    return result;
                }

                if (operatorSignature != nullptr && checkedOperand.folded.has_value() && operatorSignature->unaryFold != nullptr)
                {
                    const auto foldResult = operatorSignature->unaryFold(checkedOperand.folded.value());
                    if (foldResult.kind == FoldResultKind::Value)
                    {
                        result.folded = foldResult.value;
                        return result;
                    }

                    reportUnaryFoldFailure(unit, foldResult.kind, operandType, index);
                }

                result.value = m_emitter.emitUnary(UnaryOperatorKind::ValueNegation, materializeValue(checkedOperand), type);
                return result;
            }
            case UnaryOperatorKind::ReferenceOf:
            {
                const auto operand = NodeLayout::unaryOperand(index);
                auto operandContext = context;
                operandContext.wantsAddress = true;
                auto type = typeCheckExpression(unit, operand, operandContext).type;
                if (type.isReference())
                {
                    m_diagnostics.addAlreadyReferenceError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index));
                    return Type::Undefined();
                }

                const auto strippedOperand = NodeLayout::stripGroupings(buffer, operand);
                auto operandIsMemberAccess = false;
                if (buffer.kind(strippedOperand) == NodeKind::BinaryExpression)
                {
                    const auto operandOperator = ConvertToBinaryOperator(tokens.getToken(static_cast<i32>(buffer.mainToken(strippedOperand))).kind);
                    const auto operandLayout = NodeLayout::binary(buffer, strippedOperand);
                    operandIsMemberAccess = operandOperator == BinaryOperatorKind::MemberAccess
                        && buffer.kind(operandLayout.right) == NodeKind::NameExpression;
                }
                if (buffer.kind(strippedOperand) != NodeKind::NameExpression && !operandIsMemberAccess)
                {
                    // cant return references to temporaries
                    m_diagnostics.addReferenceToNonVariableError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index));
                    return Type::Undefined();
                }

                if (operandIsMemberAccess)
                {
                    auto rootName = std::string{};
                    auto rootReferencesConstant = false;
                    if (receiverChainIsConstant(unit, strippedOperand, rootName, rootReferencesConstant))
                    {
                        // TODO set const ref flag
                    }
                }
                else if (const auto* operandBinding = tryGetBinding(nameIdOf(unit, strippedOperand)); operandBinding != nullptr && operandBinding->kind == VariableBindingKind::LocalConstant)
                {
                    // TODO set const ref flag
                }

                auto referenceType = type.toReference();
                auto result = CheckedValue{ recordType(unit, index, referenceType) };
                if (!context.wantsAddress && !operandIsMemberAccess)
                {
                    // a ref used as a value loads the value of its variable
                    // where an address is wanted, the consumer takes it from the node itself
                    result.value = m_emitter.emitRefValue(nameIdOf(unit, strippedOperand), type);
                    if (result.value.id() < 0)
                    {
                        result.value = m_emitter.readGlobal(nameOf(unit, strippedOperand));
                    }
                }
                return result;
            }
            default:
            {
                TODO("Missing UnaryOperatorKind!!");
                return Type::Undefined();
            }
        }
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckBinaryExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.binary");
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto binaryOperator = ConvertToBinaryOperator(tokens.getToken(static_cast<i32>(buffer.mainToken(index))).kind);
        const auto binaryLayout = NodeLayout::binary(buffer, index);
        switch (binaryOperator)
        {
            case BinaryOperatorKind::MemberAccess:
            {
                auto receiverContext = context;
                receiverContext.wantsAddress = true;
                receiverContext.receiverPosition = !context.wantsAddress || context.receiverPosition;
                const auto checkedLeft = typeCheckExpression(unit, binaryLayout.left, receiverContext);
                auto leftType = checkedLeft.type;
                if (leftType.kind() == TypeKind::Enum)
                {
                    auto& enumDefinition = m_module.getEnumDefinition(leftType);
                    if (buffer.kind(binaryLayout.right) == NodeKind::NameExpression)
                    {
                        const auto fieldName = nameOf(unit, binaryLayout.right);
                        const auto* field = enumDefinition.tryGetFieldByName(fieldName);
                        if (field == nullptr)
                        {
                            m_diagnostics.addUnknownEnumFieldError(
                                unit.source,
                                nameLocation(unit, binaryLayout.right),
                                enumDefinition.name(),
                                std::string(fieldName));
                            return Type::Undefined();
                        }

                        recordType(unit, index, leftType);
                        recordType(unit, binaryLayout.right, leftType);
                        auto result = CheckedValue{ leftType };
                        if (field->hasConstantValue())
                        {
                            if (const auto constant = m_emitter.makeEnumConstantValue(enumDefinition.baseType(), field->value()); constant.has_value())
                            {
                                result.value = m_emitter.emitEnumMemberConstant(leftType, enumDefinition.name(), std::string(fieldName), constant.value());
                            }
                        }
                        else if (const auto* fieldConstant = m_emitter.tryGetEnumFieldConstant(leftType.id(), std::string(fieldName)); fieldConstant != nullptr)
                        {
                            result.value = m_emitter.emitEnumMemberConstant(leftType, enumDefinition.name(), std::string(fieldName), fieldConstant->constant);
                        }
                        return result;
                    }

                    m_diagnostics.addInvalidEnumMemberAccessError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                        enumDefinition.name());
                    return Type::Undefined();
                }
                else if (leftType.kind() == TypeKind::Type)
                {
                    auto& typeDefinition = m_module.getTypeDefinition(leftType);

                    if (buffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression)
                    {
                        const auto nameExpressionIndex = NodeLayout::subtreeBegin(buffer, binaryLayout.right);
                        const auto name = std::string(nameOf(unit, nameExpressionIndex));

                        const auto* overloadSet = typeDefinition.tryGetMethodOverloadSet(name);
                        auto methodType = Type::Undefined();
                        if (overloadSet != nullptr && !overloadSet->overloads.empty())
                        {
                            methodType = resolveMethodCallee(unit, binaryLayout.right, nameExpressionIndex, *overloadSet, isTypeNameExpression(unit, binaryLayout.left));
                            if (methodType == Type::Undefined())
                            {
                                return Type::Undefined();
                            }
                        }

                        if (methodType != Type::Undefined())
                        {
                            auto& methodDefinition = m_module.getFunctionDefinition(methodType);
                            if (methodDefinition.symbolName().has_value())
                            {
                                m_module.markExternRequired(methodType);
                            }

                            if (!m_isPreludePass)
                            {
                                m_module.markPreludeTypeRequired(leftType);
                                for (const auto callReturnType : methodDefinition.returnTypes())
                                {
                                    m_module.markPreludeTypeRequired(callReturnType);
                                }
                            }

                            if (methodDefinition.functionType() == FunctionType::PrivateMethod && m_currentType != leftType)
                            {
                                m_diagnostics.addPrivateMethodCallOutsideTypeError(
                                    unit.source,
                                    ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                                    name,
                                    typeDefinition.name());
                                return Type::Undefined();
                            }

                            if (methodDefinition.functionType() == FunctionType::StaticMethod && !isTypeNameExpression(unit, binaryLayout.left))
                            {
                                m_diagnostics.addStaticMethodCalledWithoutTypeNameError(
                                    unit.source,
                                    ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                                    name,
                                    typeDefinition.name());
                                return Type::Undefined();
                            }

                            auto implicitReceiver = std::optional<ValueRef>{};
                            if (methodDefinition.functionType() == FunctionType::PublicMethod
                                || methodDefinition.functionType() == FunctionType::PrivateMethod
                                || (methodDefinition.functionType() == FunctionType::Intrinsic
                                    && !methodDefinition.parameters().empty()
                                    && methodDefinition.parameters().front().name() == ImplicitThisName))
                            {
                                auto receiverAddress = memberReceiverAddress(unit, binaryLayout.left);
                                if (receiverAddress.id() < 0 && memberLeftYieldsAddress(unit, binaryLayout.left))
                                {
                                    // the receiver was checked in address context, its value already is the address
                                    receiverAddress = checkedLeft.value;
                                }
                                if (receiverAddress.id() < 0)
                                {
                                    receiverAddress = m_emitter.spillToTempSlot(materializeValue(checkedLeft), leftType.toValue());
                                }
                                implicitReceiver = receiverAddress;
                            }

                            auto methodCallValue = ValueRef{};
                            auto methodCallFold = std::optional<FoldValue>{};
                            if (!typeCheckCallArguments(unit, binaryLayout.right, methodType, context, methodCallValue, methodCallFold, implicitReceiver))
                            {
                                return Type::Undefined();
                            }

                            // the argument walk can move the definition store, resolve again
                            const auto& checkedMethodDefinition = m_module.getFunctionDefinition(methodType);
                            const auto& returnTypes = checkedMethodDefinition.returnTypes();
                            Type returnType = Type::Void();
                            if (returnTypes.size() == 1)
                            {
                                returnType = returnTypes[0];
                            }
                            else if (returnTypes.size() > 1)
                            {
                                TODO("Handle multiple return types");
                            }

                            if (checkedMethodDefinition.functionType() == FunctionType::SynthesizedConstructor)
                            {
                                recordType(unit, index, leftType);
                                recordType(unit, binaryLayout.right, leftType);
                                recordType(unit, nameExpressionIndex, methodType);
                                auto constructed = CheckedValue{ leftType };
                                constructed.value = methodCallValue;
                                return constructed;
                            }

                            recordType(unit, index, returnType);
                            recordType(unit, binaryLayout.right, returnType);
                            recordType(unit, nameExpressionIndex, methodType);
                            auto result = CheckedValue{ returnType };
                            result.value = methodCallValue;
                            result.folded = methodCallFold;
                            return result;
                        }
                        else
                        {
                            m_diagnostics.addUnknownMethodError(
                                unit.source,
                                ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                                typeDefinition.name(),
                                name);
                            return Type::Undefined();
                        }
                    }
                    else if (buffer.kind(binaryLayout.right) == NodeKind::NameExpression)
                    {
                        const auto fieldName = nameOf(unit, binaryLayout.right);
                        const auto& fieldDefinition = typeDefinition.tryGetFieldByName(fieldName);
                        if (fieldDefinition.type() == Type::Undefined())
                        {
                            m_diagnostics.addUnknownFieldError(
                                unit.source,
                                nameLocation(unit, binaryLayout.right),
                                typeDefinition.name(),
                                std::string(fieldName));
                            return Type::Undefined();
                        }

                        // trying to access private fields isnt allowed
                        if (!fieldName.empty() && fieldName.front() == '_' && m_currentType != leftType)
                        {
                            m_diagnostics.addPrivateFieldAccessOutsideTypeError(
                                unit.source,
                                nameLocation(unit, binaryLayout.right),
                                std::string(fieldName),
                                typeDefinition.name());
                            return Type::Undefined();
                        }

                        auto fieldType = fieldDefinition.type();
                        recordType(unit, binaryLayout.right, fieldType);
                        recordType(unit, index, fieldType);
                        auto result = CheckedValue{ fieldType };
                        if (context.receiverPosition || !context.wantsAddress)
                        {
                            auto receiverAddress = memberReceiverAddress(unit, binaryLayout.left);
                            if (receiverAddress.id() < 0 && memberLeftYieldsAddress(unit, binaryLayout.left))
                            {
                                receiverAddress = checkedLeft.value;
                            }
                            if (receiverAddress.id() < 0)
                            {
                                // a temporary receiver spills so the field has an address
                                receiverAddress = m_emitter.spillToTempSlot(materializeValue(checkedLeft), leftType.toValue());
                            }
                            if (receiverAddress.id() >= 0)
                            {
                                const auto fieldAddress = m_emitter.emitFieldAddress(receiverAddress, leftType.toValue(), fieldDefinition.index(), fieldType);
                                if (context.receiverPosition)
                                {
                                    // a field used as a receiver returns its address, not its value
                                    result.value = fieldAddress;
                                }
                                else
                                {
                                    result.value = m_emitter.emitLoad(fieldAddress, fieldType);
                                }
                            }
                        }
                        return result;
                    }
                }
                else if (leftType.kind() == TypeKind::FixedArray
                    || leftType.kind() == TypeKind::DynamicArray
                    || leftType.kind() == TypeKind::Slice)
                {
                    const auto receiverType = leftType.toValue();
                    if (buffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression)
                    {
                        const auto nameExpressionIndex = NodeLayout::subtreeBegin(buffer, binaryLayout.right);
                        const auto name = std::string(nameOf(unit, nameExpressionIndex));

                        auto methodType = m_module.tryGetOrCreateArrayIntrinsic(receiverType, name);
                        if (methodType == Type::Undefined())
                        {
                            if (name == "add" || name == "remove")
                            {
                                const auto elementType = m_module.getArrayElementType(receiverType);
                                const auto dynamicTypeName = "[" + formatTypeName(m_module, elementType) + "; _]";
                                m_diagnostics.addMethodRequiresDynamicArrayError(
                                    unit.source,
                                    ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                                    formatTypeName(m_module, receiverType),
                                    dynamicTypeName);
                            }
                            else
                            {
                                m_diagnostics.addUnknownMethodError(
                                    unit.source,
                                    ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                                    formatTypeName(m_module, receiverType),
                                    name);
                            }
                            return Type::Undefined();
                        }

                        auto& methodDefinition = m_module.getFunctionDefinition(methodType);
                        if ((methodDefinition.intrinsicKind() == IntrinsicKind::ArraySet
                            || methodDefinition.intrinsicKind() == IntrinsicKind::ArrayAdd
                            || methodDefinition.intrinsicKind() == IntrinsicKind::ArrayRemove))
                        {
                            if (m_module.isImmutableSlice(receiverType))
                            {
                                m_diagnostics.addMutatingMethodOnImmutableSliceError(
                                    unit.source,
                                    ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.left),
                                    name);
                                return Type::Undefined();
                            }

                            auto rootName = std::string{};
                            auto rootReferencesConstant = false;
                            if (receiverChainIsConstant(unit, binaryLayout.left, rootName, rootReferencesConstant))
                            {
                                if (rootReferencesConstant)
                                {
                                    m_diagnostics.addMutatingMethodThroughConstantReferenceError(
                                        unit.source,
                                        ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.left),
                                        rootName,
                                        name);
                                }
                                else
                                {
                                    m_diagnostics.addMutatingMethodOnConstantError(
                                        unit.source,
                                        ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.left),
                                        rootName,
                                        name);
                                }
                            }
                        }

                        const auto arrayReceiver = arrayReceiverAddress(unit, binaryLayout.left, checkedLeft, receiverType);
                        return finishResolvedMethodCall(unit, index, binaryLayout.right, nameExpressionIndex, methodType, context, arrayReceiver);
                    }
                    else if (buffer.kind(binaryLayout.right) == NodeKind::NameExpression)
                    {
                        const auto memberName = nameOf(unit, binaryLayout.right);
                        if (memberName == ArrayLengthMemberName)
                        {
                            const auto lengthType = m_module.wellKnown().i32;
                            recordType(unit, binaryLayout.right, lengthType);
                            recordType(unit, index, lengthType);
                            auto result = CheckedValue{ lengthType };
                            if (receiverType.kind() == TypeKind::FixedArray && !context.wantsAddress)
                            {
                                // a fixed length is a compile time constant
                                const auto length = m_module.getArrayLength(receiverType);
                                result.folded = FoldValue{ length };
                                result.value = m_emitter.emitConstant(ConstantValue::FromI32(length), lengthType);
                            }
                            else if (!context.wantsAddress)
                            {
                                // slice and dynamic lengths load from the descriptor
                                const auto receiverAddress = arrayReceiverAddress(unit, binaryLayout.left, checkedLeft, receiverType);
                                if (receiverAddress.id() >= 0)
                                {
                                    const auto lengthAddress = m_emitter.emitFieldAddress(receiverAddress, receiverType, 1, lengthType);
                                    result.value = m_emitter.emitLoad(lengthAddress, lengthType);
                                }
                            }
                            return result;
                        }

                        m_diagnostics.addUnknownFieldError(
                            unit.source,
                            nameLocation(unit, binaryLayout.right),
                            formatTypeName(m_module, receiverType),
                            std::string(memberName));
                        return Type::Undefined();
                    }
                }
                else if (leftType.kind() == TypeKind::Builtin
                    && buffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression)
                {
                    const auto nameExpressionIndex = NodeLayout::subtreeBegin(buffer, binaryLayout.right);
                    const auto name = std::string(nameOf(unit, nameExpressionIndex));

                    // only intrinsic methods are callable on builtin types, operator declarations are just signatures
                    auto& typeDefinition = m_module.getTypeDefinition(leftType);
                    const auto methodType = tryGetOverload(typeDefinition.tryGetMethodOverloadSet(name));
                    auto isIntrinsic = false;
                    if (methodType != Type::Undefined())
                    {
                        isIntrinsic = m_module.getFunctionDefinition(methodType).functionType() == FunctionType::Intrinsic;
                    }

                    if (!isIntrinsic)
                    {
                        m_diagnostics.addUnknownMethodError(
                            unit.source,
                            ExpressionSourceLocation(m_compilationContext, unit, binaryLayout.right),
                            formatTypeName(m_module, leftType.toValue()),
                            name);
                        return Type::Undefined();
                    }

                    return finishResolvedMethodCall(unit, index, binaryLayout.right, nameExpressionIndex, methodType, context);
                }

                if (leftType != Type::Undefined())
                {
                    m_diagnostics.addInvalidMemberAccessReceiverError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        formatTypeName(m_module, leftType));
                }
                return Type::Undefined();
            }
            case BinaryOperatorKind::AdditionWrapping:
            case BinaryOperatorKind::SubtractionWrapping:
            case BinaryOperatorKind::MultiplicationWrapping:
            case BinaryOperatorKind::Division:
            {
                const auto checkedLeft = typeCheckExpression(unit, binaryLayout.left, context);
                auto leftType = checkedLeft.type;
                if (leftType.isReference())
                {
                    leftType = leftType.toValue();
                }
                const auto checkedRight = typeCheckExpression(unit, binaryLayout.right, context);
                auto rightType = checkedRight.type;
                if (rightType.isReference())
                {
                    rightType = rightType.toValue();
                }

                // TODO we need to be able look up the resulting type for a binary expression,
                // for now we'll just make sure left and right have the same type and use that one
                if (leftType != rightType)
                {
                    if (leftType != Type::Undefined() && rightType != Type::Undefined())
                    {
                        m_diagnostics.addArithmeticOperandTypeMismatchError(
                            unit.source,
                            nameLocation(unit, index),
                            formatBinaryOperator(binaryOperator),
                            formatTypeName(m_module, leftType),
                            formatTypeName(m_module, rightType));
                    }

                    return recordType(unit, index, Type::Undefined());
                }

                const auto* operatorSignature = m_module.tryGetOperatorSignature(leftType, binaryOperator);
                if (operatorSignature == nullptr)
                {
                    m_diagnostics.addBinaryOperandTypeMismatchError(
                        unit.source,
                        nameLocation(unit, index),
                        formatBinaryOperator(binaryOperator),
                        formatTypeName(m_module, leftType),
                        "numeric operands");

                    return recordType(unit, index, Type::Undefined());
                }

                auto result = CheckedValue{ recordType(unit, index, operatorSignature->resultType) };
                if (checkedLeft.folded.has_value() && checkedRight.folded.has_value() && operatorSignature->binaryFold != nullptr)
                {
                    const auto foldResult = operatorSignature->binaryFold(checkedLeft.folded.value(), checkedRight.folded.value());
                    if (foldResult.kind == FoldResultKind::Value)
                    {
                        result.folded = foldResult.value;
                        return result;
                    }

                    reportBinaryFoldFailure(unit, foldResult.kind, leftType, index, binaryLayout.right);
                }

                auto leftValue = materializeValue(checkedLeft);
                auto rightValue = materializeValue(checkedRight);
                if (operatorSignature->callee != Type::Undefined())
                {
                    // runtime operators call their backing function by value
                    m_emitter.recordCalledPreludeFunction(operatorSignature->callee);
                    if (leftValue.id() >= 0 && rightValue.id() >= 0)
                    {
                        const ValueRef operands[] = { leftValue, rightValue };
                        result.value = m_emitter.emitCall(operatorSignature->callee.id(), operands, operatorSignature->resultType);
                    }
                    return result;
                }

                if (binaryOperator == BinaryOperatorKind::Division && leftValue.id() >= 0 && rightValue.id() >= 0)
                {
                    // convert the int operands to float for the float return type
                    const auto* operandDescription = m_module.tryGetBuiltinTypeDescription(leftType);
                    const auto* resultDescription = m_module.tryGetBuiltinTypeDescription(operatorSignature->resultType);
                    if (operandDescription != nullptr && operandDescription->kind == BuiltinTypeKind::Int
                        && resultDescription != nullptr && resultDescription->kind == BuiltinTypeKind::Float)
                    {
                        leftValue = m_emitter.emitIntToFloat(leftValue, leftType, operatorSignature->resultType);
                        rightValue = m_emitter.emitIntToFloat(rightValue, rightType, operatorSignature->resultType);
                    }
                }
                result.value = m_emitter.emitBinary(binaryOperator, leftValue, rightValue, operatorSignature->resultType, leftType);
                return result;
            }
            case BinaryOperatorKind::Equal:
            case BinaryOperatorKind::NotEqual:
            case BinaryOperatorKind::LessThan:
            case BinaryOperatorKind::LessOrEqual:
            case BinaryOperatorKind::GreaterThan:
            case BinaryOperatorKind::GreaterOrEqual:
            case BinaryOperatorKind::LogicalAnd:
            case BinaryOperatorKind::LogicalOr:
            {
                const auto checkedLeft = typeCheckExpression(unit, binaryLayout.left, context);
                auto leftType = checkedLeft.type;
                if (leftType.isReference())
                {
                    leftType = leftType.toValue();
                }

                // and/or open the block for the right operand before it is checked
                // whether the whole expression folds and needs no blocks is only known afterwards
                auto shortCircuit = ShortCircuitFrame{};
                const auto isShortCircuitOperator = binaryOperator == BinaryOperatorKind::LogicalAnd || binaryOperator == BinaryOperatorKind::LogicalOr;
                if (isShortCircuitOperator && leftType == m_module.wellKnown().boolean)
                {
                    shortCircuit = m_emitter.beginShortCircuit(binaryOperator == BinaryOperatorKind::LogicalAnd, materializeValue(checkedLeft));
                }

                const auto checkedRight = typeCheckExpression(unit, binaryLayout.right, context);
                auto rightType = checkedRight.type;
                if (rightType.isReference())
                {
                    rightType = rightType.toValue();
                }

                if (!areComparableTypes(leftType, rightType))
                {
                    if (leftType != Type::Undefined() && rightType != Type::Undefined())
                    {
                        m_diagnostics.addComparisonOperandTypeMismatchError(
                            unit.source,
                            nameLocation(unit, index),
                            formatBinaryOperator(binaryOperator),
                            formatTypeName(m_module, leftType),
                            formatTypeName(m_module, rightType));
                    }

                    return recordType(unit, index, Type::Undefined());
                }

                const auto operation = binaryOperator;

                auto lookupType = leftType;
                if (lookupType.kind() == TypeKind::Enum)
                {
                    lookupType = m_module.getEnumDefinition(lookupType).baseType();
                }

                const auto* operatorSignature = m_module.tryGetOperatorSignature(lookupType, operation);
                if (operatorSignature == nullptr)
                {
                    const auto* expectedOperands = "numeric operands";
                    if (operation == BinaryOperatorKind::LogicalAnd || operation == BinaryOperatorKind::LogicalOr)
                    {
                        expectedOperands = "'bool' operands";
                    }
                    else if (operation == BinaryOperatorKind::Equal || operation == BinaryOperatorKind::NotEqual)
                    {
                        expectedOperands = "operands that can be compared for equality";
                    }

                    m_diagnostics.addBinaryOperandTypeMismatchError(
                        unit.source,
                        nameLocation(unit, index),
                        formatBinaryOperator(operation),
                        formatTypeName(m_module, leftType),
                        expectedOperands);

                    return recordType(unit, index, Type::Undefined());
                }

                auto result = CheckedValue{ recordType(unit, index, operatorSignature->resultType) };
                if (checkedLeft.folded.has_value() && checkedRight.folded.has_value() && operatorSignature->binaryFold != nullptr)
                {
                    const auto foldResult = operatorSignature->binaryFold(checkedLeft.folded.value(), checkedRight.folded.value());
                    if (foldResult.kind == FoldResultKind::Value)
                    {
                        m_emitter.discardShortCircuit(shortCircuit);
                        result.folded = foldResult.value;
                        return result;
                    }

                    reportBinaryFoldFailure(unit, foldResult.kind, lookupType, index, binaryLayout.right);
                }

                if (shortCircuit.active)
                {
                    result.value = m_emitter.endShortCircuit(shortCircuit, materializeValue(checkedRight));
                    return result;
                }

                auto leftValue = ValueRef{};
                auto rightValue = ValueRef{};
                if (!isShortCircuitOperator)
                {
                    leftValue = materializeValue(checkedLeft);
                    rightValue = materializeValue(checkedRight);
                }

                if (!isShortCircuitOperator && operatorSignature->callee != Type::Undefined())
                {
                    // runtime operators call their backing function by value
                    m_emitter.recordCalledPreludeFunction(operatorSignature->callee);
                    if (leftValue.id() >= 0 && rightValue.id() >= 0)
                    {
                        const ValueRef operands[] = { leftValue, rightValue };
                        result.value = m_emitter.emitCall(operatorSignature->callee.id(), operands, operatorSignature->resultType);
                    }
                }
                else if (!isShortCircuitOperator && leftType == m_module.wellKnown().cstring
                    && leftType != Type::Undefined())
                {
                    result.value = m_emitter.emitCStringEquality(operation, leftValue, rightValue);
                }
                else if (!isShortCircuitOperator)
                {
                    result.value = m_emitter.emitBinary(operation, leftValue, rightValue, operatorSignature->resultType, leftType);
                }
                return result;
            }
            default:
            {
                TODO("Missing BinaryOperatornKind!!");
                return Type::Undefined();
            }
        }

        return Type::Undefined();
    }

    bool TypeChecker::areComparableTypes(Type leftType, Type rightType)
    {
        if (leftType == rightType)
        {
            return true;
        }

        if (leftType.kind() == TypeKind::Enum)
        {
            auto& enumDefinition = m_module.getEnumDefinition(leftType);
            if (enumDefinition.baseType() == rightType)
            {
                return true;
            }
        }

        if (rightType.kind() == TypeKind::Enum)
        {
            auto& enumDefinition = m_module.getEnumDefinition(rightType);
            if (enumDefinition.baseType() == leftType)
            {
                return true;
            }
        }

        return false;
    }
}
