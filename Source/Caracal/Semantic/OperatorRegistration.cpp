#include <Caracal/Semantic/OperatorRegistration.h>

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Optimization/OperatorFolding.h>
#include <Caracal/Syntax/MethodModifier.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <string>
#include <string_view>

namespace Caracal
{
    struct BuiltinOperatorDefinition
    {
        std::string_view methodName;
        bool isUnary;
        BinaryOperatorKind binaryOperator;
        UnaryOperatorKind unaryOperator;
        BinaryFoldFunction binaryFold;
        UnaryFoldFunction unaryFold;
    };

    static const BuiltinOperatorDefinition* TryGetBuiltinOperatorDefinition(std::string_view methodName)
    {
        static constexpr BuiltinOperatorDefinition Definitions[] = {
            { BuiltinAddMethodName, false, BinaryOperatorKind::AdditionWrapping, UnaryOperatorKind::Invalid, &foldAddition, nullptr },
            { BuiltinSubtractMethodName, false, BinaryOperatorKind::SubtractionWrapping, UnaryOperatorKind::Invalid, &foldSubtraction, nullptr },
            { BuiltinMultiplyMethodName, false, BinaryOperatorKind::MultiplicationWrapping, UnaryOperatorKind::Invalid, &foldMultiplication, nullptr },
            { BuiltinDivideMethodName, false, BinaryOperatorKind::Division, UnaryOperatorKind::Invalid, &foldFloatDivision, nullptr },
            { BuiltinEqualsMethodName, false, BinaryOperatorKind::Equal, UnaryOperatorKind::Invalid, &foldEqual, nullptr },
            { BuiltinNotEqualsMethodName, false, BinaryOperatorKind::NotEqual, UnaryOperatorKind::Invalid, &foldNotEqual, nullptr },
            { BuiltinLessThanMethodName, false, BinaryOperatorKind::LessThan, UnaryOperatorKind::Invalid, &foldLessThan, nullptr },
            { BuiltinLessOrEqualMethodName, false, BinaryOperatorKind::LessOrEqual, UnaryOperatorKind::Invalid, &foldLessOrEqual, nullptr },
            { BuiltinGreaterThanMethodName, false, BinaryOperatorKind::GreaterThan, UnaryOperatorKind::Invalid, &foldGreaterThan, nullptr },
            { BuiltinGreaterOrEqualMethodName, false, BinaryOperatorKind::GreaterOrEqual, UnaryOperatorKind::Invalid, &foldGreaterOrEqual, nullptr },
            { BuiltinLogicalAndMethodName, false, BinaryOperatorKind::LogicalAnd, UnaryOperatorKind::Invalid, &foldLogicalAnd, nullptr },
            { BuiltinLogicalOrMethodName, false, BinaryOperatorKind::LogicalOr, UnaryOperatorKind::Invalid, &foldLogicalOr, nullptr },
            { BuiltinNegateMethodName, true, BinaryOperatorKind::Invalid, UnaryOperatorKind::ValueNegation, nullptr, &foldValueNegation },
            { BuiltinLogicalNegateMethodName, true, BinaryOperatorKind::Invalid, UnaryOperatorKind::LogicalNegation, nullptr, &foldLogicalNegation },
        };

        for (const auto& definition : Definitions)
        {
            if (definition.methodName == methodName)
            {
                return &definition;
            }
        }

        return nullptr;
    }

    enum class BuiltinIntrinsicShape
    {
        Binary,
        Unary,
        Shift,
    };

    struct BuiltinIntrinsicDefinition
    {
        std::string_view methodName;
        BuiltinIntrinsicShape shape;
        IntrinsicKind kind;
    };

    static const BuiltinIntrinsicDefinition* TryGetBuiltinIntrinsicDefinition(std::string_view methodName)
    {
        static constexpr BuiltinIntrinsicDefinition Definitions[] = {
            { BuiltinBitAndMethodName, BuiltinIntrinsicShape::Binary, IntrinsicKind::BitAnd },
            { BuiltinBitOrMethodName, BuiltinIntrinsicShape::Binary, IntrinsicKind::BitOr },
            { BuiltinBitXorMethodName, BuiltinIntrinsicShape::Binary, IntrinsicKind::BitXor },
            { BuiltinBitNotMethodName, BuiltinIntrinsicShape::Unary, IntrinsicKind::BitNot },
            { BuiltinShiftLeftMethodName, BuiltinIntrinsicShape::Shift, IntrinsicKind::ShiftLeft },
            { BuiltinShiftRightMethodName, BuiltinIntrinsicShape::Shift, IntrinsicKind::ShiftRight },
        };

        for (const auto& definition : Definitions)
        {
            if (definition.methodName == methodName)
            {
                return &definition;
            }
        }

        return nullptr;
    }

    void validateBuiltinMethod(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 methodIndex,
        TypeDefinition& typeDefinition,
        Type typeType,
        SemanticContext& module,
        DiagnosticsBag& diagnostics)
    {
        const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, methodIndex);
        const auto methodName = context.getLexeme(unit.fileId, unit.nodeBuffer.mainToken(methodLayout.methodName));
        const auto methodLocation = context.sourceLocation(unit.fileId, unit.nodeBuffer.mainToken(methodLayout.methodName));

        const auto* operatorDefinition = TryGetBuiltinOperatorDefinition(methodName);
        const auto* intrinsicDefinition = TryGetBuiltinIntrinsicDefinition(methodName);
        const auto* typeDescription = module.tryGetBuiltinTypeDescription(typeType);
        const auto isIntegerType = typeDescription != nullptr && typeDescription->kind == BuiltinTypeKind::Int;
        if (intrinsicDefinition != nullptr && !isIntegerType)
        {
            diagnostics.addBitwiseMethodOnNonIntegerTypeError(
                unit.source,
                methodLocation,
                std::string(methodName),
                typeDefinition.name());
            return;
        }

        if (operatorDefinition == nullptr && intrinsicDefinition == nullptr)
        {
            diagnostics.addUnknownBuiltinMethodIgnoredWarning(
                unit.source,
                methodLocation,
                std::string(methodName),
                typeDefinition.name());
            return;
        }

        const auto methodType = typeDefinition.tryGetMethodTypeByName(methodName);
        if (methodType == Type::Undefined())
        {
            return;
        }

        const auto& tokens = context.tokenBuffer(unit.fileId);
        const auto isStatic = unit.nodeBuffer.methodModifier(tokens, methodLayout.methodName) == MethodModifier::Static;
        const auto hasTypeName = unit.nodeBuffer.methodHasTypeName(tokens, methodLayout.methodName);
        auto typeName = std::string_view{};
        if (hasTypeName)
        {
            typeName = context.getLexeme(unit.fileId, unit.nodeBuffer.methodTypeNameToken(tokens, methodLayout.methodName));
        }
        const auto hasOperatorShape = isStatic && hasTypeName && typeName == typeDefinition.name();

        if (intrinsicDefinition != nullptr)
        {
            auto& methodDefinition = module.getFunctionDefinition(methodType);
            const auto& parameters = methodDefinition.parameters();
            const auto& returnTypes = methodDefinition.returnTypes();

            for (const auto& parameter : parameters)
            {
                if (parameter.type() == Type::Undefined())
                {
                    return;
                }
            }

            auto isValidSignature = returnTypes.size() == 1 && returnTypes.front() == typeType;
            if (isValidSignature)
            {
                isValidSignature = hasOperatorShape;
            }

            if (isValidSignature)
            {
                switch (intrinsicDefinition->shape)
                {
                    case BuiltinIntrinsicShape::Binary:
                    {
                        isValidSignature = (parameters.size() == 2
                            && parameters[0].type() == typeType
                            && parameters[1].type() == typeType);
                        break;
                    }
                    case BuiltinIntrinsicShape::Unary:
                    {
                        isValidSignature = (parameters.size() == 1
                            && parameters[0].type() == typeType);
                        break;
                    }
                    case BuiltinIntrinsicShape::Shift:
                    {
                        isValidSignature = (parameters.size() == 2
                            && parameters[0].type() == typeType
                            && parameters[1].type() == module.wellKnown().i32);
                        break;
                    }
                }
            }

            if (!isValidSignature)
            {
                const auto formattedTypeName = formatTypeName(module, typeType);
                auto expectedSignature = std::string{};
                switch (intrinsicDefinition->shape)
                {
                    case BuiltinIntrinsicShape::Binary:
                    {
                        expectedSignature = formattedTypeName + "." + methodDefinition.name() + "(lhs: " + formattedTypeName + ", rhs: " + formattedTypeName + ") " + formattedTypeName;
                        break;
                    }
                    case BuiltinIntrinsicShape::Unary:
                    {
                        expectedSignature = formattedTypeName + "." + methodDefinition.name() + "(value: " + formattedTypeName + ") " + formattedTypeName;
                        break;
                    }
                    case BuiltinIntrinsicShape::Shift:
                    {
                        expectedSignature = formattedTypeName + "." + methodDefinition.name() + "(value: " + formattedTypeName + ", amount: i32) " + formattedTypeName;
                        break;
                    }
                }

                diagnostics.addInvalidOperatorMethodSignatureError(
                    unit.source,
                    methodLocation,
                    std::string(methodName),
                    expectedSignature);
                return;
            }

            // the lowerer is gonna emit instructions instead of calls for intrinsics
            methodDefinition.setFunctionType(FunctionType::Intrinsic);
            methodDefinition.setIntrinsicKind(intrinsicDefinition->kind);
            return;
        }

        const auto& methodDefinition = module.getFunctionDefinition(methodType);
        const auto& parameters = methodDefinition.parameters();
        const auto& returnTypes = methodDefinition.returnTypes();

        for (const auto& parameter : parameters)
        {
            if (parameter.type() == Type::Undefined())
            {
                return;
            }
        }

        auto isValidSignature = (returnTypes.size() == 1 && returnTypes.front() != Type::Void());
        if (isValidSignature)
        {
            isValidSignature = hasOperatorShape;
        }

        if (isValidSignature)
        {
            if (operatorDefinition->isUnary)
            {
                isValidSignature = (parameters.size() == 1
                    && parameters[0].type() == typeType);
            }
            else
            {
                isValidSignature = (parameters.size() == 2
                    && parameters[0].type() == typeType
                    && parameters[1].type() == typeType);
            }
        }

        if (!isValidSignature)
        {
            const auto formattedTypeName = formatTypeName(module, typeType);
            auto expectedSignature = std::string{};
            if (operatorDefinition->isUnary)
            {
                expectedSignature = formattedTypeName + "." + methodDefinition.name() + "(value: " + formattedTypeName + ")";
            }
            else
            {
                expectedSignature = formattedTypeName + "." + methodDefinition.name() + "(lhs: " + formattedTypeName + ", rhs: " + formattedTypeName + ")";
            }

            diagnostics.addInvalidOperatorMethodSignatureError(
                unit.source,
                methodLocation,
                std::string(methodName),
                expectedSignature);
            return;
        }

        if (operatorDefinition->isUnary)
        {
            typeDefinition.addOperatorSignature(
                operatorDefinition->unaryOperator,
                OperatorSignature{ parameters[0].type(), Type::Undefined(), returnTypes.front(), nullptr, operatorDefinition->unaryFold });
        }
        else
        {
            auto binaryFold = operatorDefinition->binaryFold;
            if (operatorDefinition->binaryOperator == BinaryOperatorKind::Division)
            {
                // the default operator definition table expects float result, we need to replace it if the prelude differs
                const auto* resultDescription = module.tryGetBuiltinTypeDescription(returnTypes.front());
                if (resultDescription != nullptr && resultDescription->kind == BuiltinTypeKind::Int)
                {
                    binaryFold = &foldIntegerDivision;
                }
            }

            typeDefinition.addOperatorSignature(
                operatorDefinition->binaryOperator,
                OperatorSignature{ parameters[0].type(), parameters[1].type(), returnTypes.front(), binaryFold, nullptr });
        }
    }

    // only allow static equals/notEquals on types with the correct shape for operators
    void registerOperatorMethod(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 methodIndex,
        TypeDefinition& typeDefinition,
        Type typeType,
        SemanticContext& module)
    {
        const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, methodIndex);
        const auto& tokens = context.tokenBuffer(unit.fileId);
        const auto modifier = unit.nodeBuffer.methodModifier(tokens, methodLayout.methodName);
        const auto hasTypeName = unit.nodeBuffer.methodHasTypeName(tokens, methodLayout.methodName);
        auto typeName = std::string_view{};
        if (hasTypeName)
        {
            typeName = context.getLexeme(unit.fileId, unit.nodeBuffer.methodTypeNameToken(tokens, methodLayout.methodName));
        }

        if (modifier != MethodModifier::Static
            || !hasTypeName
            || typeName != typeDefinition.name())
        {
            return;
        }

        const auto methodName = context.getLexeme(unit.fileId, unit.nodeBuffer.mainToken(methodLayout.methodName));
        auto binaryOperator = BinaryOperatorKind::Invalid;
        if (methodName == BuiltinEqualsMethodName)
        {
            binaryOperator = BinaryOperatorKind::Equal;
        }
        else if (methodName == BuiltinNotEqualsMethodName)
        {
            binaryOperator = BinaryOperatorKind::NotEqual;
        }
        else
        {
            return;
        }

        const auto methodType = typeDefinition.tryGetMethodTypeByName(methodName);
        if (methodType == Type::Undefined())
        {
            return;
        }

        const auto& methodDefinition = module.getFunctionDefinition(methodType);
        const auto& parameters = methodDefinition.parameters();
        const auto& returnTypes = methodDefinition.returnTypes();
        if (parameters.size() != 2
            || parameters[0].type() != typeType
            || parameters[1].type() != typeType
            || returnTypes.size() != 1
            || returnTypes.front() != module.wellKnown().boolean)
        {
            return;
        }

        typeDefinition.addOperatorSignature(
            binaryOperator,
            OperatorSignature{ typeType, typeType, returnTypes.front(), nullptr, nullptr, methodType });
    }
}
