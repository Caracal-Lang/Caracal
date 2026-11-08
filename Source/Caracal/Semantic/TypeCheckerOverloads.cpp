#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/ArgumentBinder.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>

#include <algorithm>
#include <iterator>
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

    [[nodiscard]] static bool IsBetterCandidate(std::span<const ArgumentRank> candidate, std::span<const ArgumentRank> other) noexcept
    {
        auto isBetterForOne = false;
        for (size_t position = 0; position < candidate.size(); ++position)
        {
            if (candidate[position] < other[position])
            {
                return false;
            }
            if (candidate[position] > other[position])
            {
                isBetterForOne = true;
            }
        }

        return isBetterForOne;
    }

    [[nodiscard]] static bool IsBestCandidate(std::span<const ArgumentRank> ranks, size_t argumentCount, size_t candidate, size_t candidateCount) noexcept
    {
        const auto candidateRanks = ranks.subspan(candidate * argumentCount, argumentCount);
        for (size_t other = 0; other < candidateCount; ++other)
        {
            if (other != candidate && !IsBetterCandidate(candidateRanks, ranks.subspan(other * argumentCount, argumentCount)))
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
        ++m_typeChecker.m_suspendedEmissionDepth;
    }

    TypeChecker::SuspendedEmissionScope::~SuspendedEmissionScope() noexcept
    {
        --m_typeChecker.m_suspendedEmissionDepth;
        m_typeChecker.m_emitter.resumeEmission(m_emissionPosition);
        m_typeChecker.m_diagnostics.removeDiagnosticsAfter(m_diagnosticCount);
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

    std::vector<Diagnostic> TypeChecker::SuspendedEmissionScope::takeDiagnostics()
    {
        return m_typeChecker.m_diagnostics.takeDiagnosticsAfter(m_diagnosticCount);
    }

    Type TypeChecker::checkArgumentWithoutContext(const CompilationUnit& unit, u32 argumentValue, std::vector<Diagnostic>& argumentErrors)
    {
        auto scope = SuspendedEmissionScope{ *this };
        const auto argumentType = typeCheckExpression(unit, argumentValue, ExpressionContext{}).type;
        if (scope.addedError() || argumentType == Type::Undefined())
        {
            auto diagnostics = scope.takeDiagnostics();
            argumentErrors.insert(argumentErrors.end(), std::make_move_iterator(diagnostics.begin()), std::make_move_iterator(diagnostics.end()));
            return Type::Undefined();
        }

        return argumentType;
    }

    Type TypeChecker::checkArgumentWithoutEmission(const CompilationUnit& unit, u32 argumentValue, const ExpressionContext& context)
    {
        auto scope = SuspendedEmissionScope{ *this };
        const auto argumentType = typeCheckExpression(unit, argumentValue, context).type;
        if (scope.addedError())
        {
            return Type::Undefined();
        }

        return argumentType;
    }

    bool TypeChecker::isUnresolvedOverloadCall(NodeIndex call) const noexcept
    {
        return std::ranges::find(m_unresolvedOverloadCalls, call) != m_unresolvedOverloadCalls.end();
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
        const auto callee = selectOverload(unit, callIndex, nameExpressionIndex, overloadSet);
        if (m_suspendedEmissionDepth == 0)
        {
            m_unresolvedOverloadCalls.clear();
        }

        return callee;
    }

    Type TypeChecker::selectOverload(const CompilationUnit& unit, u32 callIndex, u32 nameExpressionIndex, const OverloadSet& overloadSet)
    {
        const auto storedCallee = m_module.typeOfNode(NodeIndex{ unit.fileId, nameExpressionIndex });
        if (std::ranges::find(overloadSet.overloads, storedCallee) != overloadSet.overloads.end())
        {
            return storedCallee;
        }

        const auto call = NodeIndex{ unit.fileId, callIndex };
        if (m_suspendedEmissionDepth > 0 && isUnresolvedOverloadCall(call))
        {
            return Type::Undefined();
        }

        const auto& buffer = unit.nodeBuffer;
        NodeLayout::collectChildren(buffer, callIndex, m_childrenScratch);
        const auto argumentsStart = m_callArgumentStack.size();
        for (size_t childIndex = 1; childIndex < m_childrenScratch.size(); ++childIndex)
        {
            m_callArgumentStack.push_back(m_childrenScratch[childIndex]);
        }
        const auto argumentCount = m_callArgumentStack.size() - argumentsStart;

        auto typesWithoutContext = std::vector<Type>{};
        typesWithoutContext.reserve(argumentCount);
        auto argumentErrors = std::vector<Diagnostic>{};
        for (size_t position = 0; position < argumentCount; ++position)
        {
            const auto argumentValue = NodeLayout::argumentValue(buffer, m_callArgumentStack[argumentsStart + position]);
            typesWithoutContext.push_back(checkArgumentWithoutContext(unit, argumentValue, argumentErrors));
        }

        auto viableCandidates = std::vector<Type>{};
        auto ranks = std::vector<ArgumentRank>{};
        for (const auto candidate : overloadSet.overloads)
        {
            if (rankCandidate(unit, callIndex, candidate, argumentsStart, typesWithoutContext, ranks))
            {
                viableCandidates.push_back(candidate);
            }
        }
        m_callArgumentStack.resize(argumentsStart);

        const auto location = NodeSpans::expressionSourceLocation(buffer, m_compilationContext.tokenBuffer(unit.fileId), callIndex);
        if (viableCandidates.empty())
        {
            m_unresolvedOverloadCalls.push_back(call);
            if (!argumentErrors.empty())
            {
                m_diagnostics.addDiagnostics(std::move(argumentErrors));
                return Type::Undefined();
            }

            m_diagnostics.addNoOverloadMatchesCallError(unit.source, location, std::string(nameOf(unit, nameExpressionIndex)));
            return Type::Undefined();
        }

        for (size_t candidate = 0; candidate < viableCandidates.size(); ++candidate)
        {
            if (IsBestCandidate(ranks, argumentCount, candidate, viableCandidates.size()))
            {
                return recordType(unit, nameExpressionIndex, viableCandidates[candidate]);
            }
        }

        m_unresolvedOverloadCalls.push_back(call);
        m_diagnostics.addCallMatchesMoreThanOneOverloadError(unit.source, location, std::string(nameOf(unit, nameExpressionIndex)));
        return Type::Undefined();
    }

    bool TypeChecker::rankCandidate(const CompilationUnit& unit, u32 callIndex, Type candidate, size_t argumentsStart, std::span<const Type> typesWithoutContext, std::vector<ArgumentRank>& ranks)
    {
        const auto argumentCount = typesWithoutContext.size();
        const auto orderedStart = m_orderedSlotStack.size();
        const auto diagnosticCount = m_diagnostics.diagnostics().size();
        const auto& definition = m_module.getFunctionDefinition(candidate);
        const auto isExtern = definition.isExtern();
        const auto binding = bindCallArguments(
            m_compilationContext,
            unit,
            callIndex,
            std::span<const u32>{ m_callArgumentStack }.subspan(argumentsStart, argumentCount),
            definition.parameters(),
            0,
            definition.isVariadic(),
            definition.name(),
            m_diagnostics,
            m_orderedSlotStack,
            orderedStart);
        m_diagnostics.removeDiagnosticsAfter(diagnosticCount);
        const auto fixedParameterCount = m_orderedSlotStack.size() - orderedStart;
        if (!binding.ok)
        {
            m_orderedSlotStack.resize(orderedStart);
            return false;
        }

        const auto& buffer = unit.nodeBuffer;
        const auto ranksStart = ranks.size();
        auto isAccepted = true;
        for (size_t position = 0; position < argumentCount && isAccepted; ++position)
        {
            const auto argumentValue = NodeLayout::argumentValue(buffer, m_callArgumentStack[argumentsStart + position]);
            auto parameterIndex = fixedParameterCount;
            for (size_t slot = 0; slot < fixedParameterCount; ++slot)
            {
                if (m_orderedSlotStack[orderedStart + slot] == argumentValue)
                {
                    parameterIndex = slot;
                    break;
                }
            }

            auto rank = ArgumentRank::Variadic;
            if (parameterIndex == fixedParameterCount)
            {
                isAccepted = acceptsVariadicArgument(unit, argumentValue, isExtern);
            }
            else
            {
                isAccepted = rankArgument(unit, candidate, parameterIndex, argumentValue, typesWithoutContext[position], rank);
            }
            ranks.push_back(rank);
        }
        m_orderedSlotStack.resize(orderedStart);

        if (!isAccepted)
        {
            ranks.resize(ranksStart);
        }
        return isAccepted;
    }

    // false if the argument does not fit the parameter
    bool TypeChecker::rankArgument(const CompilationUnit& unit, Type candidate, size_t parameterIndex, u32 argumentValue, Type typeWithoutContext, ArgumentRank& rank)
    {
        // a check of an argument can move the definition
        const auto& parameter = m_module.getFunctionDefinition(candidate).parameters()[parameterIndex];
        const auto parameterType = parameter.type();
        const auto argumentContext = argumentContextFor(unit, parameter, argumentValue);
        auto context = ExpressionContext{};
        context.contextualNumberType = argumentContext.contextualNumberType;
        context.wantsAddress = argumentContext.wantsAddress;

        const auto argumentType = checkArgumentWithoutEmission(unit, argumentValue, context);
        if (argumentType == Type::Undefined() || parameterType == Type::Undefined() || !isAssignableTo(argumentType, parameterType))
        {
            return false;
        }

        if (parameterType == typeWithoutContext)
        {
            rank = ArgumentRank::Exact;
        }
        else if (parameterType == argumentType)
        {
            rank = ArgumentRank::Contextual;
        }
        else
        {
            rank = ArgumentRank::Widened;
        }
        return true;
    }

    // a C function gets a string literal in ... as a cstring
    bool TypeChecker::acceptsVariadicArgument(const CompilationUnit& unit, u32 argumentValue, bool isExtern)
    {
        auto context = ExpressionContext{};
        if (isExtern)
        {
            context.contextualNumberType = m_module.wellKnown().cstring;
        }

        const auto argumentType = checkArgumentWithoutEmission(unit, argumentValue, context);
        return argumentType != Type::Undefined() && argumentType != Type::Void();
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
