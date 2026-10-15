#include <Caracal/IR/ArrayCopyInstruction.h>

namespace Caracal
{
    ArrayCopyInstruction::ArrayCopyInstruction(TemporaryId resultId, ValueRef sourceAddress, Type arrayType, FunctionId callocFunctionId, FunctionId memmoveFunctionId, bool reserveNulByte) noexcept
        : Instruction{ InstructionKind::ArrayCopy, resultId }
        , m_sourceAddress{ sourceAddress }
        , m_arrayType{ arrayType }
        , m_callocFunctionId{ callocFunctionId }
        , m_memmoveFunctionId{ memmoveFunctionId }
        , m_reserveNulByte{ reserveNulByte }
    {
    }

    void ArrayCopyInstruction::remapValueIds(const ValueIdMap& remap) noexcept
    {
        Instruction::remapValueIds(remap);
        m_sourceAddress = ValueRef{ remapTemporaryId(remap, m_sourceAddress.id()) };
    }
}
