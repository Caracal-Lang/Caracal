#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Profiling.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>

#include <optional>
#include <span>
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

    [[nodiscard]] static bool MatchesExactly(const FunctionDefinition& definition, std::span<const Type> argumentTypes) noexcept
    {
        const auto& parameters = definition.parameters();
        if (definition.isVariadic() || parameters.size() != argumentTypes.size())
        {
            return false;
        }

        for (size_t index = 0; index < parameters.size(); ++index)
        {
            if (parameters[index].type() != argumentTypes[index])
            {
                return false;
            }
        }

        return true;
    }

    TypeChecker::SuspendedEmissionScope::SuspendedEmissionScope(TypeChecker& typeChecker) noexcept
        : m_typeChecker{ typeChecker }
        , m_emissionPosition{ typeChecker.m_emitter.suspendEmission() }
        , m_diagnosticCount{ typeChecker.m_diagnostics.diagnostics().size() }
        , m_sawUnfoldedBitwiseCall{ typeChecker.m_sawUnfoldedBitwiseCall }
        , m_leadingDotRootPending{ typeChecker.m_leadingDotRootPending }
    {
    }

    TypeChecker::SuspendedEmissionScope::~SuspendedEmissionScope() noexcept
    {
        m_typeChecker.m_emitter.resumeEmission(m_emissionPosition);
        if (!m_keepsDiagnostics)
        {
            m_typeChecker.m_diagnostics.removeDiagnosticsAfter(m_diagnosticCount);
        }
        m_typeChecker.m_sawUnfoldedBitwiseCall = m_sawUnfoldedBitwiseCall;
        m_typeChecker.m_leadingDotRootPending = m_leadingDotRootPending;
    }

    bool TypeChecker::SuspendedEmissionScope::addedError() const noexcept
    {
        const auto& diagnostics = m_typeChecker.m_diagnostics.diagnostics();
        for (size_t index = m_diagnosticCount; index < diagnostics.size(); ++index)
        {
            if (diagnostics[index].level() == DiagnosticLevel::Error)
            {
                return true;
            }
        }

        return false;
    }

    Type TypeChecker::checkArgumentWithoutEmission(const CompilationUnit& unit, u32 argumentValue, const ExpressionContext& context)
    {
        auto scope = SuspendedEmissionScope{ *this };
        const auto argumentType = typeCheckExpression(unit, argumentValue, context).type;
        if (scope.addedError())
        {
            scope.keepDiagnostics();
            return Type::Undefined();
        }

        return argumentType;
    }

    Type TypeChecker::resolveCallee(const CompilationUnit& unit, u32 callIndex, u32 nameExpressionIndex, const OverloadSet& overloadSet)
    {
        if (!overloadSet.isOverloaded())
        {
            return overloadSet.firstOverload();
        }

        return resolveOverload(unit, callIndex, nameExpressionIndex, overloadSet);
    }

    Type TypeChecker::resolveOverload(const CompilationUnit& unit, u32 callIndex, u32 nameExpressionIndex, const OverloadSet& overloadSet)
    {
        CARACAL_ZONE_NAMED("check.resolveOverload");
        const auto& buffer = unit.nodeBuffer;

        NodeLayout::collectChildren(buffer, callIndex, m_childrenScratch);
        const auto argumentsStart = m_callArgumentStack.size();
        for (size_t childIndex = 1; childIndex < m_childrenScratch.size(); ++childIndex)
        {
            m_callArgumentStack.push_back(m_childrenScratch[childIndex]);
        }
        const auto argumentsEnd = m_callArgumentStack.size();

        auto argumentTypes = std::vector<Type>{};
        argumentTypes.reserve(argumentsEnd - argumentsStart);
        auto hasNamedArgument = false;
        auto hasUndefinedArgument = false;
        for (auto position = argumentsStart; position < argumentsEnd; ++position)
        {
            const auto argumentNode = m_callArgumentStack[position];
            if (NodeLayout::isNamedArgument(buffer, argumentNode))
            {
                hasNamedArgument = true;
            }

            const auto argumentType = checkArgumentWithoutEmission(unit, NodeLayout::argumentValue(buffer, argumentNode), ExpressionContext{});
            if (argumentType == Type::Undefined())
            {
                hasUndefinedArgument = true;
            }
            argumentTypes.push_back(argumentType);
        }
        m_callArgumentStack.resize(argumentsStart);
        if (hasUndefinedArgument)
        {
            return Type::Undefined();
        }

        if (!hasNamedArgument)
        {
            for (const auto candidate : overloadSet.overloads)
            {
                if (MatchesExactly(m_module.getFunctionDefinition(candidate), argumentTypes))
                {
                    return candidate;
                }
            }
        }

        m_diagnostics.addNoOverloadMatchesCallError(
            unit.source,
            NodeSpans::expressionSourceLocation(buffer, m_compilationContext.tokenBuffer(unit.fileId), callIndex),
            std::string(nameOf(unit, nameExpressionIndex)));
        return Type::Undefined();
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
