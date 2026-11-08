#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Profiling.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Caracal
{
    [[nodiscard]] static bool IsCompilerCalledFunctionName(std::string_view functionName) noexcept
    {
        return functionName == UserMainFunctionName || functionName == EntryPointFunctionName || functionName == TestsStubName;
    }

    [[nodiscard]] static bool HasUndefinedParameter(const FunctionDefinition& definition) noexcept
    {
        for (const auto& parameter : definition.parameters())
        {
            if (parameter.type() == Type::Undefined())
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] static bool HaveSameParameterTypes(const FunctionDefinition& left, const FunctionDefinition& right) noexcept
    {
        const auto& leftParameters = left.parameters();
        const auto& rightParameters = right.parameters();
        if (leftParameters.size() != rightParameters.size())
        {
            return false;
        }

        for (size_t index = 0; index < leftParameters.size(); ++index)
        {
            if (leftParameters[index].type() != rightParameters[index].type())
            {
                return false;
            }
        }

        return true;
    }

    // an extern without a symbol argument uses its name as the C symbol
    [[nodiscard]] static std::optional<std::string> ExternSymbol(const FunctionDefinition& definition)
    {
        if (!definition.isExtern())
        {
            return std::nullopt;
        }

        if (definition.symbolName().has_value())
        {
            return definition.symbolName();
        }

        return definition.fullName();
    }

    void TypeChecker::reportInvalidOverloadSets()
    {
        CARACAL_ZONE_NAMED("check.overloadSets");
        auto acceptedDeclarations = std::vector<NodeIndex>{};
        acceptedDeclarations.reserve(m_functionDeclarations.size());
        for (const auto declaration : m_functionDeclarations)
        {
            if (!rejectInvalidOverload(declaration))
            {
                acceptedDeclarations.push_back(declaration);
            }
        }

        m_functionDeclarations = std::move(acceptedDeclarations);
    }

    bool TypeChecker::rejectInvalidOverload(NodeIndex declaration)
    {
        const auto functionType = m_module.typeOfNode(declaration);
        const auto& definition = m_module.getFunctionDefinition(functionType);
        if (HasUndefinedParameter(definition))
        {
            return false;
        }

        const auto* overloadSet = m_module.tryGetOverloadSet(definition.fullName());
        auto earlierDefinitions = std::vector<const FunctionDefinition*>{};
        for (const auto memberType : overloadSet->overloads)
        {
            if (memberType == functionType)
            {
                break;
            }

            const auto& memberDefinition = m_module.getFunctionDefinition(memberType);
            if (!HasUndefinedParameter(memberDefinition))
            {
                earlierDefinitions.push_back(&memberDefinition);
            }
        }

        if (earlierDefinitions.empty())
        {
            return false;
        }

        const auto& unit = unitFor(declaration);
        auto otherSource = SourceTextSharedPtr{};
        auto otherLocation = std::optional<SourceLocation>{};
        auto isRejected = false;
        for (const auto* earlierDefinition : earlierDefinitions)
        {
            if (HaveSameParameterTypes(definition, *earlierDefinition))
            {
                locateDefinitionName(earlierDefinition->definitionNode(), otherSource, otherLocation);
                m_diagnostics.addDuplicateFunctionDeclarationError(
                    unit.source,
                    nameLocation(unit, declaration.index),
                    definition.fullName(),
                    otherSource,
                    otherLocation);
                isRejected = true;
                break;
            }
        }

        const auto symbol = ExternSymbol(definition);
        if (!isRejected && symbol.has_value())
        {
            for (const auto* earlierDefinition : earlierDefinitions)
            {
                if (ExternSymbol(*earlierDefinition) == symbol)
                {
                    locateDefinitionName(earlierDefinition->definitionNode(), otherSource, otherLocation);
                    m_diagnostics.addExternOverloadsShareSymbolError(
                        unit.source,
                        nameLocation(unit, declaration.index),
                        definition.fullName(),
                        symbol.value(),
                        otherSource,
                        otherLocation);
                    isRejected = true;
                    break;
                }
            }
        }

        if (!isRejected && IsCompilerCalledFunctionName(definition.fullName()))
        {
            locateDefinitionName(earlierDefinitions.front()->definitionNode(), otherSource, otherLocation);
            m_diagnostics.addCompilerCalledFunctionHasOverloadsError(
                unit.source,
                nameLocation(unit, declaration.index),
                definition.fullName(),
                otherSource,
                otherLocation);
            isRejected = true;
        }

        if (isRejected)
        {
            m_module.removeOverload(definition.fullName(), functionType);
            m_module.setNodeType(declaration, Type::Undefined());
        }

        return isRejected;
    }
}
