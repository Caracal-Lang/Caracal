#include "FunctionDefinition.h"

#include <Caracal/Constants.h>

namespace Caracal
{
    FunctionDefinition::FunctionDefinition(
        Type type,
        Type parentType,
        FunctionType functionType,
        std::string name,
        std::string fullName,
        bool isVariadic,
        std::vector<Parameter> parameters,
        std::vector<Type> returnTypes)
        : m_type{ type }
        , m_parentType{ parentType }
        , m_functionType{ functionType }
        , m_name{ std::move(name) }
        , m_fullName{ std::move(fullName) }
        , m_isVariadic{ isVariadic }
        , m_parameters{ std::move(parameters) }
        , m_returnTypes{ std::move(returnTypes) }
    {
    }

    void FunctionDefinition::setParameters(std::vector<Parameter> parameters) noexcept
    {
        m_parameters = std::move(parameters);
    }

    void FunctionDefinition::setReturnTypes(std::vector<Type> returnTypes) noexcept
    {
        m_returnTypes = std::move(returnTypes);
    }
    
    void FunctionDefinition::setIsVariadic(bool isVariadic) noexcept
    {
        m_isVariadic = isVariadic;
    }

    bool FunctionDefinition::hasImplicitThis() const noexcept
    {
        if (m_functionType == FunctionType::SynthesizedConstructor
            || m_functionType == FunctionType::PublicMethod
            || m_functionType == FunctionType::PrivateMethod)
        {
            return true;
        }

        return m_functionType == FunctionType::Intrinsic
            && !m_parameters.empty()
            && m_parameters.front().name() == ImplicitThisName;
    }

    bool isBitwiseIntrinsicKind(IntrinsicKind kind) noexcept
    {
        return kind == IntrinsicKind::BitAnd
            || kind == IntrinsicKind::BitOr
            || kind == IntrinsicKind::BitXor
            || kind == IntrinsicKind::BitNot
            || kind == IntrinsicKind::ShiftLeft
            || kind == IntrinsicKind::ShiftRight;
    }
}
