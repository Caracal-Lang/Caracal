#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/ValueRef.h>

#include <vector>

namespace Caracal
{
    class CallVoidInstruction final : public Instruction
    {
    public:
        CallVoidInstruction(FunctionId functionId, std::vector<ValueRef> arguments) noexcept;

        [[nodiscard]] FunctionId functionId() const noexcept { return m_functionId; }
        [[nodiscard]] const std::vector<ValueRef>& arguments() const noexcept { return m_arguments; }

    private:
        FunctionId m_functionId;
        std::vector<ValueRef> m_arguments;
    };
}
