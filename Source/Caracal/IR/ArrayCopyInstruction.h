#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class ArrayCopyInstruction final : public Instruction
    {
    public:
        ArrayCopyInstruction(TemporaryId resultId, ValueRef sourceAddress, Type arrayType, FunctionId callocFunctionId, FunctionId memmoveFunctionId, bool reserveNulByte = false) noexcept;

        [[nodiscard]] ValueRef sourceAddress() const noexcept { return m_sourceAddress; }
        [[nodiscard]] Type arrayType() const noexcept { return m_arrayType; }
        [[nodiscard]] FunctionId callocFunctionId() const noexcept { return m_callocFunctionId; }
        [[nodiscard]] bool reserveNulByte() const noexcept { return m_reserveNulByte; }
        [[nodiscard]] FunctionId memmoveFunctionId() const noexcept { return m_memmoveFunctionId; }
        void remapValueIds(const ValueIdMap& remap) noexcept override;

    private:
        ValueRef m_sourceAddress;
        Type m_arrayType;
        FunctionId m_callocFunctionId;
        bool m_reserveNulByte;
        FunctionId m_memmoveFunctionId;
    };
}
