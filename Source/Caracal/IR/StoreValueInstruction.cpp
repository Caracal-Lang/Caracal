#include <Caracal/IR/StoreValueInstruction.h>

namespace Caracal
{
    StoreValueInstruction::StoreValueInstruction(ValueRef value, ValueRef address, Type type) noexcept
        : Instruction{ InstructionKind::StoreValue }
        , m_value{ value }
        , m_address{ address }
        , m_type{ type }
    {
    }
}
