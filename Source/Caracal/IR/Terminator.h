#pragma once

#include <Caracal/IR/Instruction.h>

#include <memory>

namespace Caracal
{
    class CARACAL_API Terminator
    {
    public:
        explicit Terminator(TerminatorKind kind) noexcept;
        virtual ~Terminator() = default;

        [[nodiscard]] TerminatorKind kind() const noexcept { return m_kind; }

    private:
        TerminatorKind m_kind;
    };

    using TerminatorUPtr = std::unique_ptr<Terminator>;
}
