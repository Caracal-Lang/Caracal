#include <Caracal/IR/TypeDeclaration.h>

namespace Caracal
{
    TypeDeclaration::TypeDeclaration(std::string name, Type type)
        : m_type{ type }
        , m_name{ std::move(name) }
    {
    }

    void TypeDeclaration::addField(std::string name, Type type, bool isConstant)
    {
        m_fields.push_back(Field{ std::move(name), type, isConstant });
    }

    void TypeDeclaration::addMethod(std::string name, FunctionId functionId)
    {
        m_methods.push_back(Method{ std::move(name), functionId });
    }
}
