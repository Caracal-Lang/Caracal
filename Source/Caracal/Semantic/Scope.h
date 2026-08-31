#pragma once

#include <Caracal/API.h>
#include <Caracal/Semantic/Type.h>
#include <Caracal/Text/SourceLocation.h>
#include <Caracal/Text/SourceText.h>

#include <string_view>
#include <unordered_map>
#include <optional>

namespace Caracal 
{
    enum class ScopeKind
    {
        Invalid,
        Global,
        Function,
        Type,
        Method,
        StaticMethod,
    };

    enum class VariableBindingKind
    {
        LocalVariable,
        LocalConstant,
        Parameter,
        Field,
    };

    struct VariableBinding
    {
        Type type;
        SourceTextSharedPtr source;
        std::optional<SourceLocation> location;
        VariableBindingKind kind = VariableBindingKind::LocalVariable;
        bool wasRead = false;
        bool referencesConstant = false;
    };

    class CARACAL_API Scope
    {
    public:
        Scope(Scope* parent, ScopeKind kind);

        void reset(Scope* parent, ScopeKind kind) noexcept
        {
            m_parent = parent;
            m_kind = kind;
            m_variableBindings.clear();
            m_functionBindings.clear();
        }

        [[nodiscard]] ScopeKind kind() const noexcept { return m_kind; }

        bool hasVariableBinding(std::string_view identifier) const noexcept;

        void addVariableBinding(std::string_view identifier, Type node, std::optional<SourceLocation> location = std::nullopt, const SourceTextSharedPtr& source = nullptr, VariableBindingKind kind = VariableBindingKind::LocalVariable, bool referencesConstant = false);
        [[nodiscard]] std::optional<Type> tryGetVariableBinding(std::string_view identifier) const noexcept;
        [[nodiscard]] SourceTextSharedPtr tryGetVariableBindingSource(std::string_view identifier) const noexcept;
        [[nodiscard]] std::optional<SourceLocation> tryGetVariableBindingLocation(std::string_view identifier) const noexcept;
        [[nodiscard]] std::optional<VariableBindingKind> tryGetVariableBindingKind(std::string_view identifier) const noexcept;
        [[nodiscard]] bool variableReferencesConstant(std::string_view identifier) const noexcept;
        bool markVariableBindingRead(std::string_view identifier) noexcept;
        [[nodiscard]] const std::unordered_map<std::string_view, VariableBinding>& variableBindings() const noexcept { return m_variableBindings; }

    private:
        Scope* m_parent;
        ScopeKind m_kind;
        std::unordered_map<std::string_view, VariableBinding> m_variableBindings;
        std::unordered_map<std::string_view, Type> m_functionBindings;
    };
}
