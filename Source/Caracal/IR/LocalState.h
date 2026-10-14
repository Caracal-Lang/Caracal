#pragma once

#include <Caracal/IR/LocalStorageKind.h>
#include <Caracal/IR/ValueRef.h>
#include <Caracal/Semantic/Type.h>

namespace Caracal
{
    struct LocalState
    {
        ValueRef value;
        Type type;
        LocalStorageKind storageKind = LocalStorageKind::Value;
    };
}
