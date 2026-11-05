#pragma once

#include <Caracal/Defines.h>
#include <Caracal/Semantic/Type.h>

namespace Caracal
{
    enum class BuiltinTypeKind
    {
        Int,
        Float,
        Bool,
        Pointer,
    };

    struct BuiltinTypeDescription
    {
        BuiltinTypeKind kind = BuiltinTypeKind::Int;
        i32 bits = 32;
        bool isSigned = true;
    };

    struct WellKnownTypes
    {
        Type boolean = Type::Undefined();
        Type u8 = Type::Undefined();
        Type u16 = Type::Undefined();
        Type u32 = Type::Undefined();
        Type u64 = Type::Undefined();
        Type i8 = Type::Undefined();
        Type i16 = Type::Undefined();
        Type i32 = Type::Undefined();
        Type i64 = Type::Undefined();
        Type f32 = Type::Undefined();
        Type f64 = Type::Undefined();
        Type cstring = Type::Undefined();
        Type rawptr = Type::Undefined();
        Type string = Type::Undefined();
        Type sourceLocation = Type::Undefined();
    };
}
