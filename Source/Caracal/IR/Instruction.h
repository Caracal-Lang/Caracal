#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/Semantic/Type.h>

namespace Caracal
{
    using TemporaryId = i32;
    using LocalSlotId = i32;
    using BlockId = i32;
    using FunctionId = i32;
    using EnumId = i32;

    inline constexpr i32 TemporarySlot = 0;
    inline constexpr i32 LocalVariableSlot = -1;

    enum class InstructionKind
    {
        Parameter,
        Constant,
        AllocateLocal,
        AddressOf,
        AddressOfGlobal,
        FieldAddress,
        ElementAddress,
        MakeSlice,
        ArrayAdd,
        ArrayRemove,
        ArrayCopy,
        LoadValue,
        StoreValue,
        ValueNegation,
        IntToFloat,
        IntWiden,
        SizeOf,
        LogicalNegation,
        Call,
        CallVoid,
        Add,
        Subtract,
        Multiply,
        Divide,
        Equal,
        NotEqual,
        LessThan,
        LessOrEqual,
        GreaterThan,
        GreaterOrEqual,
        BitAnd,
        BitOr,
        BitXor,
        BitNot,
        ShiftLeft,
        ShiftRight,
        Phi,

        // dead instruction
        Nop,

        // basic block terminators
        Jump,
        Branch,
        Return,
        ReturnValue,
        Unreachable,
    };

    [[nodiscard]] constexpr bool isTerminatorKind(InstructionKind kind) noexcept
    {
        switch (kind)
        {
            case InstructionKind::Jump:
            case InstructionKind::Branch:
            case InstructionKind::Return:
            case InstructionKind::ReturnValue:
            case InstructionKind::Unreachable:
            {
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    enum class ConstantForm : i32
    {
        Scalar,
        String,
        Pooled,
    };

    // one instruction or terminator of a Function, the kind decides what a, b, c and d hold:
    // Parameter          a parameter index
    // Constant           a ConstantForm, Scalar: b literal alternative, c d bits
    //                    String: c d offset and length in ConstantPool, Pooled: b index
    // AllocateLocal      a slot id, b name id, c the variable it holds, no result
    // AddressOf          a slot id
    // AddressOfGlobal    a name id
    // FieldAddress       a object address, b field index, c d object type
    // ElementAddress     a base address, b index, c d array type
    // MakeSlice          a base address, b length
    // ArrayAdd           a descriptor address, b value, c realloc function, type is the array type
    // ArrayRemove        a descriptor address, b index, c memmove function, type is the array type
    // ArrayCopy          a source address, b calloc function, c memmove function, d 1 to reserve
    //                    a nul byte, type is the array type
    // LoadValue          a address
    // StoreValue         a value, b address, no result
    // IntToFloat         a operand, c d source type, also IntWiden
    // SizeOf             c d measured type
    // ValueNegation      a operand, also LogicalNegation and BitNot
    // Call               a function id, b c start and count in the operand pool, also CallVoid
    // Add                a left, b right, also Subtract to BitXor and ShiftLeft, ShiftRight
    // Equal              a left, b right, c d operand type, also the other comparisons
    // Phi                a b start and count of its inputs
    // Jump               a target block id
    // Branch             a condition, b true block id, c false block id
    // ReturnValue        a value
    struct Instruction
    {
        Instruction(InstructionKind instructionKind, TemporaryId instructionResultId, Type instructionType) noexcept
            : kind{ instructionKind }
            , resultId{ instructionResultId }
            , type{ instructionType }
        {
        }

        [[nodiscard]] static Instruction CreateParameter(TemporaryId result, Type resultType, i32 parameterIndex) noexcept
        {
            auto instruction = Instruction{ InstructionKind::Parameter, result, resultType };
            instruction.a = parameterIndex;
            return instruction;
        }

        [[nodiscard]] static Instruction CreateScalarConstant(TemporaryId result, Type resultType, u32 alternative, u64 bits) noexcept
        {
            auto instruction = Instruction{ InstructionKind::Constant, result, resultType };
            instruction.a = static_cast<i32>(ConstantForm::Scalar);
            instruction.b = static_cast<i32>(alternative);
            instruction.setConstantBits(bits);
            return instruction;
        }

        [[nodiscard]] static Instruction CreateStringConstant(TemporaryId result, Type resultType, u32 offset, u32 length) noexcept
        {
            auto instruction = Instruction{ InstructionKind::Constant, result, resultType };
            instruction.a = static_cast<i32>(ConstantForm::String);
            instruction.c = static_cast<i32>(offset);
            instruction.d = static_cast<i32>(length);
            return instruction;
        }

        [[nodiscard]] static Instruction CreatePooledConstant(TemporaryId result, Type resultType, u32 constantIndex) noexcept
        {
            auto instruction = Instruction{ InstructionKind::Constant, result, resultType };
            instruction.a = static_cast<i32>(ConstantForm::Pooled);
            instruction.b = static_cast<i32>(constantIndex);
            return instruction;
        }

        [[nodiscard]] static Instruction CreateAllocateLocal(Type valueType, LocalSlotId slot, u32 nameId, i32 variable) noexcept
        {
            auto instruction = Instruction{ InstructionKind::AllocateLocal, -1, valueType };
            instruction.a = slot;
            instruction.b = static_cast<i32>(nameId);
            instruction.c = variable;
            return instruction;
        }

        [[nodiscard]] static Instruction CreateAddressOf(TemporaryId result, Type resultType, LocalSlotId slot) noexcept
        {
            auto instruction = Instruction{ InstructionKind::AddressOf, result, resultType };
            instruction.a = slot;
            return instruction;
        }

        [[nodiscard]] static Instruction CreateAddressOfGlobal(TemporaryId result, Type resultType, u32 nameId) noexcept
        {
            auto instruction = Instruction{ InstructionKind::AddressOfGlobal, result, resultType };
            instruction.a = static_cast<i32>(nameId);
            return instruction;
        }

        [[nodiscard]] static Instruction CreateFieldAddress(TemporaryId result, Type resultType, TemporaryId objectAddress, i32 fieldIndex, Type objectType) noexcept
        {
            auto instruction = Instruction{ InstructionKind::FieldAddress, result, resultType };
            instruction.a = objectAddress;
            instruction.b = fieldIndex;
            instruction.setSecondType(objectType);
            return instruction;
        }

        [[nodiscard]] static Instruction CreateElementAddress(TemporaryId result, Type resultType, TemporaryId baseAddress, TemporaryId index, Type arrayType) noexcept
        {
            auto instruction = Instruction{ InstructionKind::ElementAddress, result, resultType };
            instruction.a = baseAddress;
            instruction.b = index;
            instruction.setSecondType(arrayType);
            return instruction;
        }

        [[nodiscard]] static Instruction CreateArrayAdd(Type arrayType, TemporaryId descriptorAddress, TemporaryId value, FunctionId reallocFunction) noexcept
        {
            auto instruction = Instruction{ InstructionKind::ArrayAdd, -1, arrayType };
            instruction.a = descriptorAddress;
            instruction.b = value;
            instruction.c = reallocFunction;
            return instruction;
        }

        [[nodiscard]] static Instruction CreateArrayRemove(Type arrayType, TemporaryId descriptorAddress, TemporaryId index, FunctionId memmoveFunction) noexcept
        {
            auto instruction = Instruction{ InstructionKind::ArrayRemove, -1, arrayType };
            instruction.a = descriptorAddress;
            instruction.b = index;
            instruction.c = memmoveFunction;
            return instruction;
        }

        [[nodiscard]] static Instruction CreateArrayCopy(TemporaryId result, Type arrayType, TemporaryId sourceAddress, FunctionId callocFunction, FunctionId memmoveFunction, bool reserveNulByte) noexcept
        {
            auto instruction = Instruction{ InstructionKind::ArrayCopy, result, arrayType };
            instruction.a = sourceAddress;
            instruction.b = callocFunction;
            instruction.c = memmoveFunction;
            instruction.d = static_cast<i32>(reserveNulByte);
            return instruction;
        }

        [[nodiscard]] static Instruction CreateStoreValue(Type valueType, TemporaryId value, TemporaryId address) noexcept
        {
            auto instruction = Instruction{ InstructionKind::StoreValue, -1, valueType };
            instruction.a = value;
            instruction.b = address;
            return instruction;
        }

        // IntToFloat or IntWiden
        [[nodiscard]] static Instruction CreateConversion(InstructionKind conversionKind, TemporaryId result, Type resultType, TemporaryId operand, Type sourceType) noexcept
        {
            auto instruction = Instruction{ conversionKind, result, resultType };
            instruction.a = operand;
            instruction.setSecondType(sourceType);
            return instruction;
        }

        [[nodiscard]] static Instruction CreateSizeOf(TemporaryId result, Type resultType, Type measuredType) noexcept
        {
            auto instruction = Instruction{ InstructionKind::SizeOf, result, resultType };
            instruction.setSecondType(measuredType);
            return instruction;
        }

        // LoadValue, ValueNegation, LogicalNegation or BitNot
        [[nodiscard]] static Instruction CreateUnary(InstructionKind unaryKind, TemporaryId result, Type resultType, TemporaryId operand) noexcept
        {
            auto instruction = Instruction{ unaryKind, result, resultType };
            instruction.a = operand;
            return instruction;
        }

        [[nodiscard]] static Instruction CreateCall(TemporaryId result, Type resultType, FunctionId functionId, u32 operandStart, u32 operandCount) noexcept
        {
            auto instruction = Instruction{ InstructionKind::Call, result, resultType };
            instruction.a = functionId;
            instruction.b = static_cast<i32>(operandStart);
            instruction.c = static_cast<i32>(operandCount);
            return instruction;
        }

        [[nodiscard]] static Instruction CreateCallVoid(FunctionId functionId, u32 operandStart, u32 operandCount) noexcept
        {
            auto instruction = Instruction{ InstructionKind::CallVoid, -1, Type::Void() };
            instruction.a = functionId;
            instruction.b = static_cast<i32>(operandStart);
            instruction.c = static_cast<i32>(operandCount);
            return instruction;
        }

        // Add to BitXor, ShiftLeft, ShiftRight or MakeSlice
        [[nodiscard]] static Instruction CreateBinary(InstructionKind binaryKind, TemporaryId result, Type resultType, TemporaryId left, TemporaryId right) noexcept
        {
            auto instruction = Instruction{ binaryKind, result, resultType };
            instruction.a = left;
            instruction.b = right;
            return instruction;
        }

        // Equal to GreaterOrEqual
        [[nodiscard]] static Instruction CreateComparison(InstructionKind comparisonKind, TemporaryId result, Type resultType, TemporaryId left, TemporaryId right, Type operandType) noexcept
        {
            auto instruction = Instruction{ comparisonKind, result, resultType };
            instruction.a = left;
            instruction.b = right;
            instruction.setSecondType(operandType);
            return instruction;
        }

        [[nodiscard]] static Instruction CreatePhi(TemporaryId result, Type resultType) noexcept
        {
            return Instruction{ InstructionKind::Phi, result, resultType };
        }

        [[nodiscard]] static Instruction CreateJump(BlockId target) noexcept
        {
            auto instruction = Instruction{ InstructionKind::Jump, -1, Type::Void() };
            instruction.a = target;
            return instruction;
        }

        [[nodiscard]] static Instruction CreateBranch(TemporaryId condition, BlockId trueBlock, BlockId falseBlock) noexcept
        {
            auto instruction = Instruction{ InstructionKind::Branch, -1, Type::Void() };
            instruction.a = condition;
            instruction.b = trueBlock;
            instruction.c = falseBlock;
            return instruction;
        }

        [[nodiscard]] static Instruction CreateReturn() noexcept
        {
            return Instruction{ InstructionKind::Return, -1, Type::Void() };
        }

        [[nodiscard]] static Instruction CreateReturnValue(TemporaryId value) noexcept
        {
            auto instruction = Instruction{ InstructionKind::ReturnValue, -1, Type::Void() };
            instruction.a = value;
            return instruction;
        }

        [[nodiscard]] static Instruction CreateUnreachable() noexcept
        {
            return Instruction{ InstructionKind::Unreachable, -1, Type::Void() };
        }

        // stores a second type in c and d, for example a comparison's operand type
        void setSecondType(Type secondType) noexcept
        {
            c = secondType.id();
            d = static_cast<i32>(secondType.kind());
        }

        [[nodiscard]] Type secondType() const noexcept
        {
            return Type{ c, static_cast<TypeKind>(d) };
        }

        // a scalar constant keeps its bits in c and d, its type says how to read them
        void setConstantBits(u64 bits) noexcept
        {
            c = static_cast<i32>(static_cast<u32>(bits));
            d = static_cast<i32>(static_cast<u32>(bits >> 32));
        }

        [[nodiscard]] u64 constantBits() const noexcept
        {
            return static_cast<u64>(static_cast<u32>(c)) | (static_cast<u64>(static_cast<u32>(d)) << 32);
        }

        InstructionKind kind;
        TemporaryId resultId;
        Type type;
        i32 a = 0;
        i32 b = 0;
        i32 c = 0;
        i32 d = 0;
    };

    static_assert(sizeof(Instruction) == 32, "Instruction must stay 32 bytes");
}
