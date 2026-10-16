#include <Caracal/IR/ReturnValueTerminator.h>

namespace Caracal
{
    ReturnValueTerminator::ReturnValueTerminator(ValueRef value) noexcept
        : Terminator{ TerminatorKind::ReturnValue }
        , m_value{ value }
    {
    }
}
