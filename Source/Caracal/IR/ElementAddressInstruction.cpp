#include <Caracal/IR/ElementAddressInstruction.h>

namespace Caracal
{
    ElementAddressInstruction::ElementAddressInstruction(
        TemporaryId resultId,
        ValueRef baseAddress,
        Type arrayType,
        ValueRef index,
        Type type) noexcept
        : Instruction{ InstructionKind::ElementAddress, resultId }
        , m_baseAddress{ baseAddress }
        , m_arrayType{ arrayType }
        , m_index{ index }
        , m_type{ type }
    {
    }
}
