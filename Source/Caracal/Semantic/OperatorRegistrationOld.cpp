#include "OperatorRegistrationOld.h"

#include <Caracal/Constants.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Optimization/OperatorFolding.h>

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
            { BuiltinDivideMethodName, false, BinaryOperatorKind::Division, UnaryOperatorKind::Invalid, &foldDivision, nullptr },
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

    void registerOperatorMethodOld(const MethodDefinitionStatement* methodStatement, TypeDefinition& typeDefinition, Type typeType, SemanticContext& module)
    {
        // only allow static equals/notEquals on types with the correct shape for operators
        if (methodStatement->modifier() != MethodModifier::Static
            || !methodStatement->methodNameNode()->hasTypeName()
            || methodStatement->methodNameNode()->typeName().value() != typeDefinition.name())
        {
            return;
        }

        const auto& methodName = methodStatement->methodNameNode()->methodName();
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

    void validateBuiltinMethodOld(const MethodDefinitionStatement* methodStatement, TypeDefinition& typeDefinition, Type typeType, const TokenBuffer& tokens, SemanticContext& module, DiagnosticsBag& diagnostics)
    {
        const auto& methodName = methodStatement->methodNameNode()->methodName();
        const auto methodLocation = tokens.getSourceLocation(methodStatement->methodNameNode()->methodNameToken());

        const auto* operatorDefinition = TryGetBuiltinOperatorDefinition(methodName);
        const auto* intrinsicDefinition = TryGetBuiltinIntrinsicDefinition(methodName);
        const auto* typeDescription = module.tryGetBuiltinTypeDescription(typeType);
        const auto isIntegerType = typeDescription != nullptr && typeDescription->kind == BuiltinTypeKind::Int;
        if (intrinsicDefinition != nullptr && !isIntegerType)
        {
            diagnostics.addBitwiseMethodOnNonIntegerTypeError(
                tokens.source(),
                methodLocation,
                methodName,
                typeDefinition.name());
            return;
        }

        if (operatorDefinition == nullptr && intrinsicDefinition == nullptr)
        {
            diagnostics.addUnknownBuiltinMethodIgnoredWarning(
                tokens.source(),
                methodLocation,
                methodName,
                typeDefinition.name());
            return;
        }

        const auto methodType = typeDefinition.tryGetMethodTypeByName(methodName);
        if (methodType == Type::Undefined())
        {
            return;
        }

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
                isValidSignature = methodStatement->modifier() == MethodModifier::Static
                    && methodStatement->methodNameNode()->hasTypeName()
                    && methodStatement->methodNameNode()->typeName().value() == typeDefinition.name();
            }

            if (isValidSignature)
            {
                switch (intrinsicDefinition->shape)
                {
                    case BuiltinIntrinsicShape::Binary:
                    {
                        isValidSignature = parameters.size() == 2
                            && parameters[0].type() == typeType
                            && parameters[1].type() == typeType;
                        break;
                    }
                    case BuiltinIntrinsicShape::Unary:
                    {
                        isValidSignature = parameters.size() == 1
                            && parameters[0].type() == typeType;
                        break;
                    }
                    case BuiltinIntrinsicShape::Shift:
                    {
                        isValidSignature = parameters.size() == 2
                            && parameters[0].type() == typeType
                            && parameters[1].type() == module.wellKnown().i32;
                        break;
                    }
                }
            }

            if (!isValidSignature)
            {
                const auto typeName = formatTypeName(module, typeType);
                auto expectedSignature = std::string{};
                switch (intrinsicDefinition->shape)
                {
                    case BuiltinIntrinsicShape::Binary:
                    {
                        expectedSignature = typeName + "." + methodDefinition.name() + "(lhs: " + typeName + ", rhs: " + typeName + ") " + typeName;
                        break;
                    }
                    case BuiltinIntrinsicShape::Unary:
                    {
                        expectedSignature = typeName + "." + methodDefinition.name() + "(value: " + typeName + ") " + typeName;
                        break;
                    }
                    case BuiltinIntrinsicShape::Shift:
                    {
                        expectedSignature = typeName + "." + methodDefinition.name() + "(value: " + typeName + ", amount: i32) " + typeName;
                        break;
                    }
                }

                diagnostics.addInvalidOperatorMethodSignatureError(
                    tokens.source(),
                    methodLocation,
                    methodName,
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

        auto isValidSignature = returnTypes.size() == 1 && returnTypes.front() != Type::Void();
        if (isValidSignature)
        {
            isValidSignature = methodStatement->modifier() == MethodModifier::Static
                && methodStatement->methodNameNode()->hasTypeName()
                && methodStatement->methodNameNode()->typeName().value() == typeDefinition.name();
        }

        if (isValidSignature)
        {
            if (operatorDefinition->isUnary)
            {
                isValidSignature = parameters.size() == 1
                    && parameters[0].type() == typeType;
            }
            else
            {
                isValidSignature = parameters.size() == 2
                    && parameters[0].type() == typeType
                    && parameters[1].type() == typeType;
            }
        }

        if (!isValidSignature)
        {
            const auto typeName = formatTypeName(module, typeType);
            auto expectedSignature = std::string{};
            if (operatorDefinition->isUnary)
            {
                expectedSignature = typeName + "." + methodDefinition.name() + "(value: " + typeName + ")";
            }
            else
            {
                expectedSignature = typeName + "." + methodDefinition.name() + "(lhs: " + typeName + ", rhs: " + typeName + ")";
            }

            diagnostics.addInvalidOperatorMethodSignatureError(
                tokens.source(),
                methodLocation,
                methodName,
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
            typeDefinition.addOperatorSignature(
                operatorDefinition->binaryOperator,
                OperatorSignature{ parameters[0].type(), parameters[1].type(), returnTypes.front(), operatorDefinition->binaryFold, nullptr });
        }
    }

}
