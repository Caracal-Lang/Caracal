#include <Caracal/Semantic/ArgumentBinder.h>

#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/TokenBuffer.h>

namespace Caracal
{
    ArgumentBinding bindCallArguments(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 callIndex,
        std::span<const u32> argumentNodes,
        const std::vector<Parameter>& parameters,
        size_t parameterOffset,
        bool isVariadic,
        const std::string& functionName,
        DiagnosticsBag& diagnostics,
        std::vector<u32>& orderedStack,
        size_t orderedStart)
    {
        ArgumentBinding binding;
        const auto& buffer = unit.nodeBuffer;

        const auto totalParameters = parameters.size();
        auto boundParameters = totalParameters;
        if (isVariadic)
        {
            boundParameters = totalParameters - 1;
        }
        const auto fixedParameterCount = boundParameters - parameterOffset;

        orderedStack.resize(orderedStart + fixedParameterCount, UnboundArgumentSlot);
        const auto ordered = orderedStack.begin() + orderedStart;

        size_t positionalSlot = 0;
        auto tooManyPositional = false;
        for (const auto argumentNode : argumentNodes)
        {
            const auto argumentValue = NodeLayout::argumentValue(buffer, argumentNode);
            if (NodeLayout::isNamedArgument(buffer, argumentNode))
            {
                const auto nameTokenIndex = buffer.mainToken(argumentNode);
                const auto argumentName = context.getLexeme(unit.fileId, nameTokenIndex);
                size_t parameterIndex = fixedParameterCount;
                for (size_t p = 0; p < fixedParameterCount; ++p)
                {
                    if (parameters[parameterOffset + p].name() == argumentName)
                    {
                        parameterIndex = p;
                        break;
                    }
                }

                if (parameterIndex == fixedParameterCount)
                {
                    binding.ok = false;
                    diagnostics.addUnknownArgumentNameError(unit.source, context.sourceLocation(unit.fileId, nameTokenIndex), functionName, std::string(argumentName));
                    continue;
                }

                if (ordered[parameterIndex] != UnboundArgumentSlot)
                {
                    binding.ok = false;
                    diagnostics.addDuplicateArgumentBindingError(unit.source, context.sourceLocation(unit.fileId, nameTokenIndex), functionName, std::string(argumentName));
                    continue;
                }

                ordered[parameterIndex] = argumentValue;
            }
            else if (positionalSlot < fixedParameterCount)
            {
                ordered[positionalSlot] = argumentValue;
                ++positionalSlot;
            }
            else if (isVariadic)
            {
                binding.variadic.push_back(argumentValue);
            }
            else
            {
                tooManyPositional = true;
                ++positionalSlot;
            }
        }

        // set default values or report missing arguments
        for (size_t i = 0; i < fixedParameterCount; ++i)
        {
            if (ordered[i] != UnboundArgumentSlot)
            {
                continue;
            }

            const auto& parameter = parameters[parameterOffset + i];
            if (parameter.hasDefault())
            {
                ordered[i] = DefaultBoundArgumentSlot;
                continue;
            }

            if (binding.ok)
            {
                diagnostics.addMissingRequiredArgumentError(unit.source, NodeSpans::callArgumentsLocation(buffer, context.tokenBuffer(unit.fileId), callIndex, argumentNodes), functionName, parameter.name());
            }
            binding.ok = false;
        }

        if (binding.ok && tooManyPositional)
        {
            binding.ok = false;
            diagnostics.addArgumentCountMismatchError(
                unit.source,
                NodeSpans::callArgumentsLocation(buffer, context.tokenBuffer(unit.fileId), callIndex, argumentNodes),
                functionName,
                static_cast<i32>(fixedParameterCount),
                static_cast<i32>(argumentNodes.size()),
                isVariadic);
        }

        return binding;
    }
}
