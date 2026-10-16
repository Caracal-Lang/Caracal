#include <Caracal/IR/AddressOfGlobalInstruction.h>

#include <utility>

namespace Caracal
{
    AddressOfGlobalInstruction::AddressOfGlobalInstruction(TemporaryId resultId, std::string name, Type type) noexcept
        : Instruction{ InstructionKind::AddressOfGlobal, resultId }
        , m_name{ std::move(name) }
        , m_type{ type }
    {
    }
}
