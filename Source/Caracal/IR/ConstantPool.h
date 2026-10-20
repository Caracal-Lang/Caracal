#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/IR/ConstantValue.h>
#include <Caracal/Optimization/OperatorFolding.h>

#include <string>
#include <string_view>
#include <vector>

namespace Caracal
{
    struct StringRange
    {
        u32 offset = 0;
        u32 length = 0;
    };

    [[nodiscard]] CARACAL_API u64 scalarConstantBits(const ConstantValue::LiteralData& literal) noexcept;
    [[nodiscard]] CARACAL_API u64 scalarConstantBits(const FoldValue& value) noexcept;
    [[nodiscard]] CARACAL_API ConstantValue::LiteralData scalarConstantFromBits(u32 alternative, u64 bits) noexcept;

    class CARACAL_API ConstantPool
    {
    public:
        [[nodiscard]] StringRange addString(std::string_view text);
        [[nodiscard]] std::string_view string(StringRange range) const noexcept;
        [[nodiscard]] u32 addConstant(ConstantValue constant);
        [[nodiscard]] const ConstantValue& constant(u32 index) const noexcept { return m_constants[index]; }

    private:
        std::string m_characters;
        std::vector<ConstantValue> m_constants;
    };
}
