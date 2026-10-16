#include <Caracal/IR/ArrayAddInstruction.h>

namespace Caracal
{
    ArrayAddInstruction::ArrayAddInstruction(ValueRef descriptorAddress, ValueRef value, Type arrayType, FunctionId reallocFunctionId) noexcept
        : Instruction{ InstructionKind::ArrayAdd }
        , m_descriptorAddress{ descriptorAddress }
        , m_value{ value }
        , m_arrayType{ arrayType }
        , m_reallocFunctionId{ reallocFunctionId }
    {
    }
}
