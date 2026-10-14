#pragma once

#include <Caracal/Semantic/Type.h>
#include <Caracal/Text/SourceLocation.h>
#include <Caracal/Defines.h>

#include <string>
#include <string_view>

namespace Caracal
{
    class EnumField
    {
    public:
        EnumField(std::string_view name, SourceLocation location);
        EnumField(std::string_view name, i32 value, SourceLocation location);

        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        [[nodiscard]] i32 value() const noexcept { return m_value; }
        [[nodiscard]] bool hasConstantValue() const noexcept { return m_hasConstantValue; }
        [[nodiscard]] const SourceLocation& location() const noexcept { return m_location; }

    private:
        std::string m_name;
        i32 m_value;
        bool m_hasConstantValue;
        SourceLocation m_location;
    };
}
