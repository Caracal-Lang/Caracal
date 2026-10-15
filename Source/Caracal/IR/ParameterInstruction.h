#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/IRParameter.h>

namespace Caracal
{
    class ParameterInstruction final : public Instruction
    {
    public:
        ParameterInstruction(TemporaryId resultId, i32 parameterIndex, IRParameter parameter) noexcept;

        [[nodiscard]] i32 parameterIndex() const noexcept { return m_parameterIndex; }
        [[nodiscard]] const IRParameter& parameter() const noexcept { return m_parameter; }
        void remapValueIds(const ValueIdMap& remap) noexcept override;

    private:
        i32 m_parameterIndex;
        IRParameter m_parameter;
    };
}
