#include <Caracal/IR/Instruction.h>

namespace Caracal
{
    Instruction::Instruction(InstructionKind kind) noexcept
        : m_kind{ kind }
        , m_resultId{ -1 }
    {
    }

    Instruction::Instruction(InstructionKind kind, TemporaryId resultId) noexcept
        : m_kind{ kind }
        , m_resultId{ resultId }
    {
    }

    void Instruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        m_resultId = remapTemporaryId(remap, m_resultId);
    }

    TemporaryId remapTemporaryId(const ValueIdMap& remap, TemporaryId id) noexcept
    {
        const auto foundId = remap.find(id);
        if (foundId != remap.end())
        {
            return foundId->second;
        }

        return id;
    }
}
