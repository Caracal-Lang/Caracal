#pragma once

#include <Caracal/IR/Terminator.h>
#include <Caracal/IR/ValueRef.h>

namespace Caracal
{
    class BranchIfTerminator final : public Terminator
    {
    public:
        BranchIfTerminator(ValueRef condition, BlockId trueBlockId, BlockId falseBlockId) noexcept;

        [[nodiscard]] ValueRef condition() const noexcept { return m_condition; }
        [[nodiscard]] BlockId trueBlockId() const noexcept { return m_trueBlockId; }
        [[nodiscard]] BlockId falseBlockId() const noexcept { return m_falseBlockId; }

    private:
        ValueRef m_condition;
        BlockId m_trueBlockId;
        BlockId m_falseBlockId;
    };
}
