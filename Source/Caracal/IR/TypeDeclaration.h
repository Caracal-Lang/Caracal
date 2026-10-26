#pragma once

#include <Caracal/IR/Instruction.h>
#include <Caracal/Semantic/Type.h>

#include <string>
#include <utility>
#include <vector>

namespace Caracal
{
    class TypeDeclaration
    {
    public:
        struct Field final
        {
            std::string name;
            Type type;
            bool isConstant{ false };
        };

        struct Method final
        {
            std::string name;
            FunctionId functionId;
        };

        TypeDeclaration() = default;
        TypeDeclaration(std::string name, Type type);

        [[nodiscard]] Type type() const noexcept { return m_type; }
        [[nodiscard]] const std::string& name() const noexcept { return m_name; }
        [[nodiscard]] const std::vector<Field>& fields() const noexcept { return m_fields; }
        [[nodiscard]] const std::vector<Method>& methods() const noexcept { return m_methods; }

        void addField(std::string name, Type type, bool isConstant);
        void addMethod(std::string name, FunctionId functionId);

    private:
        Type m_type{ Type::Undefined() };
        std::string m_name;
        std::vector<Field> m_fields;
        std::vector<Method> m_methods;
    };
}
