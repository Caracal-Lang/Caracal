#include <Caracal/IR/ArrayRemoveInstruction.h>

namespace Caracal
{
    ArrayRemoveInstruction::ArrayRemoveInstruction(ValueRef descriptorAddress, ValueRef index, Type arrayType, FunctionId memmoveFunctionId) noexcept
        : Instruction{ InstructionKind::ArrayRemove }
        , m_descriptorAddress{ descriptorAddress }
        , m_index{ index }
        , m_arrayType{ arrayType }
        , m_memmoveFunctionId{ memmoveFunctionId }
    {
    }
}
