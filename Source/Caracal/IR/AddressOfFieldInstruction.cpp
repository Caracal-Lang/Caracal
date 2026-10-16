#include <Caracal/IR/AddressOfFieldInstruction.h>

namespace Caracal
{
    AddressOfFieldInstruction::AddressOfFieldInstruction(
        TemporaryId resultId, 
        ValueRef objectAddress, 
        Type objectType, 
        std::string fieldName,
        i32 fieldIndex,
        Type type) noexcept
        : Instruction{ InstructionKind::FieldAddress, resultId }
        , m_objectAddress{ objectAddress }
        , m_objectType{ objectType }
        , m_fieldName{ std::move(fieldName) }
        , m_fieldIndex{ fieldIndex }
        , m_type{ type }
    {
    }
}
