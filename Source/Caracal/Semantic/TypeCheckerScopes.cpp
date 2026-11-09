#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <optional>
#include <string>
#include <string_view>

namespace Caracal
{
    static bool ShouldIgnoreUnusedVariableWarning(std::string_view name)
    {
        return name == "_" || name == ImplicitThisName;
    }

    u32 TypeChecker::internName(std::string_view lexeme)
    {
        const auto [entry, inserted] = m_nameIds.try_emplace(lexeme, static_cast<u32>(m_nameLexemes.size()));
        if (inserted)
        {
            m_nameLexemes.push_back(lexeme);
        }
        return entry->second;
    }

    void TypeChecker::internUnitIdentifiers(const CompilationUnit& unit)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto tokenCount = static_cast<i32>(tokens.size());
        if (m_tokenNameIds.size() <= unit.fileId)
        {
            m_tokenNameIds.resize(static_cast<size_t>(unit.fileId) + 1);
        }
        auto& ids = m_tokenNameIds[unit.fileId];
        ids.assign(tokenCount, InvalidNameId);
        for (i32 position = 0; position < tokenCount; ++position)
        {
            const auto token = tokens.getToken(position);
            if (isIdentifierKind(token.kind))
            {
                ids[position] = internName(tokens.getLexeme(token));
            }
        }
    }

    void TypeChecker::addVariableBinding(u32 nameId, Type type, std::optional<SourceLocation> location, const SourceTextSharedPtr& source, VariableBindingKind kind, bool referencesConstant)
    {
        if (m_bindingsByName.size() <= nameId)
        {
            m_bindingsByName.resize(static_cast<size_t>(nameId) + 1);
        }

        auto& bindings = m_bindingsByName[nameId];
        const auto scopeIndex = static_cast<u32>(m_scopeFrames.size() - 1);
        if (!bindings.empty() && bindings.back().scopeIndex == scopeIndex)
        {
            // the first binding in a scope wins
            return;
        }

        bindings.push_back(LexicalBinding{ type, source, location, kind, false, referencesConstant, scopeIndex });
        m_boundNames.push_back(nameId);
    }

    bool TypeChecker::isTypeNameExpression(const CompilationUnit& unit, u32 expression) const noexcept
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, expression);
        return unit.nodeBuffer.kind(stripped) == NodeKind::NameExpression
            && tryGetBinding(nameIdOf(unit, stripped)) == nullptr
            && m_module.tryGetTypeByName(nameOf(unit, stripped)) != Type::Undefined();
    }

    const TypeChecker::LexicalBinding* TypeChecker::tryGetConflictingBinding(u32 nameId) const noexcept
    {
        const auto* existing = tryGetBinding(nameId);
        if (existing != nullptr && existing->scopeIndex == 0 && !currentScopeIsGlobal())
        {
            return nullptr;
        }

        return existing;
    }

    ValueRef TypeChecker::addressOfName(const CompilationUnit& unit, u32 nameNode)
    {
        const auto nameId = nameIdOf(unit, nameNode);
        const auto* binding = tryGetBinding(nameId);
        if (binding == nullptr)
        {
            return ValueRef{};
        }

        if (binding->scopeIndex == 0)
        {
            return m_emitter.emitGlobalAddress(nameOf(unit, nameNode));
        }

        return m_emitter.addressOfLocal(nameId);
    }

    bool TypeChecker::nameIsInitConstant(const CompilationUnit& unit, u32 nameNode) const
    {
        const auto* binding = tryGetBinding(nameIdOf(unit, nameNode));
        return binding != nullptr && binding->scopeIndex == 0 && m_initConstants.contains(std::string(nameOf(unit, nameNode)));
    }

    void TypeChecker::pushScope(ScopeKind kind)
    {
        m_scopeFrames.push_back(ScopeFrame{ static_cast<u32>(m_boundNames.size()), kind });
    }

    void TypeChecker::popScope(bool emitUnusedWarnings)
    {
        const auto boundNamesStart = m_scopeFrames.back().boundNamesStart;
        if (emitUnusedWarnings)
        {
            emitUnusedVariableWarnings(m_scopeFrames.back());
        }

        for (auto position = static_cast<size_t>(boundNamesStart); position < m_boundNames.size(); ++position)
        {
            m_bindingsByName[m_boundNames[position]].pop_back();
        }
        m_boundNames.resize(boundNamesStart);
        m_scopeFrames.pop_back();
        if (m_scopeFrames.size() == 0)
        {
            TODO("Popped too many scopes");
        }
    }

    ScopeKind TypeChecker::currentScopeKind() const noexcept
    {
        return m_scopeFrames.back().kind;
    }

    bool TypeChecker::currentScopeIsGlobal() const noexcept
    {
        return m_scopeFrames.size() == 1;
    }

    void TypeChecker::emitUnusedVariableWarnings(const ScopeFrame& frame)
    {
        if (m_diagnostics.hasErrors())
        {
            return;
        }

        for (auto position = static_cast<size_t>(frame.boundNamesStart); position < m_boundNames.size(); ++position)
        {
            const auto nameId = m_boundNames[position];
            const auto& binding = m_bindingsByName[nameId].back();
            const auto name = m_nameLexemes[nameId];
            if (binding.wasRead || ShouldIgnoreUnusedVariableWarning(name) || !binding.location.has_value())
            {
                continue;
            }

            if (binding.kind != VariableBindingKind::LocalVariable && binding.kind != VariableBindingKind::LocalConstant && binding.kind != VariableBindingKind::Parameter)
            {
                continue;
            }

            if (binding.kind == VariableBindingKind::Parameter)
            {
                m_diagnostics.addUnusedParameterWarning(binding.source, binding.location.value(), std::string(name));
            }
            else if (binding.kind == VariableBindingKind::LocalConstant)
            {
                m_diagnostics.addUnusedLocalConstantWarning(binding.source, binding.location.value(), std::string(name));
            }
            else
            {
                m_diagnostics.addUnusedLocalVariableWarning(binding.source, binding.location.value(), std::string(name));
            }
        }
    }
}
