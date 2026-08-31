#include "Scope.h"

namespace Caracal
{
    Scope::Scope(Scope* parent, ScopeKind kind)
        : m_parent{ parent }
        , m_kind{ kind }
    {
    }

    bool Scope::hasVariableBinding(std::string_view identifier) const noexcept
    {
        if (m_variableBindings.find(identifier) != m_variableBindings.end())
            return true;
        else if (m_parent != nullptr)
            return m_parent->hasVariableBinding(identifier);
        else
            return false;
    }

    void Scope::addVariableBinding(std::string_view identifier, Type type, std::optional<SourceLocation> location, const SourceTextSharedPtr& source, VariableBindingKind kind, bool referencesConstant)
    {
        m_variableBindings.try_emplace(identifier, VariableBinding{ type, source, location, kind, false, referencesConstant });
    }

    std::optional<Type> Scope::tryGetVariableBinding(std::string_view identifier) const noexcept
    {
        if (auto search = m_variableBindings.find(identifier); search != m_variableBindings.end())
            return std::make_optional<Type>(search->second.type);
        else if (m_parent != nullptr)
            return m_parent->tryGetVariableBinding(identifier);
        else
            return std::nullopt;
    }

    SourceTextSharedPtr Scope::tryGetVariableBindingSource(std::string_view identifier) const noexcept
    {
        if (auto search = m_variableBindings.find(identifier); search != m_variableBindings.end())
            return search->second.source;
        else if (m_parent != nullptr)
            return m_parent->tryGetVariableBindingSource(identifier);
        else
            return nullptr;
    }

    std::optional<SourceLocation> Scope::tryGetVariableBindingLocation(std::string_view identifier) const noexcept
    {
        if (auto search = m_variableBindings.find(identifier); search != m_variableBindings.end())
            return search->second.location;
        else if (m_parent != nullptr)
            return m_parent->tryGetVariableBindingLocation(identifier);
        else
            return std::nullopt;
    }

    std::optional<VariableBindingKind> Scope::tryGetVariableBindingKind(std::string_view identifier) const noexcept
    {
        if (auto search = m_variableBindings.find(identifier); search != m_variableBindings.end())
            return search->second.kind;
        else if (m_parent != nullptr)
            return m_parent->tryGetVariableBindingKind(identifier);
        else
            return std::nullopt;
    }

    bool Scope::variableReferencesConstant(std::string_view identifier) const noexcept
    {
        if (auto search = m_variableBindings.find(identifier); search != m_variableBindings.end())
            return search->second.referencesConstant;
        else if (m_parent != nullptr)
            return m_parent->variableReferencesConstant(identifier);
        else
            return false;
    }

    bool Scope::markVariableBindingRead(std::string_view identifier) noexcept
    {
        if (auto search = m_variableBindings.find(identifier); search != m_variableBindings.end())
        {
            search->second.wasRead = true;
            return true;
        }
        else if (m_parent != nullptr)
        {
            return m_parent->markVariableBindingRead(identifier);
        }

        return false;
    }
}
