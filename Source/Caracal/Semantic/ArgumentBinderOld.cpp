#include <Caracal/Semantic/ArgumentBinderOld.h>

#include <Caracal/Diagnostics/DiagnosticsBag.h>
#include <Caracal/Syntax/Expression.h>
#include <Caracal/Syntax/FunctionCallExpression.h>
#include <Caracal/Syntax/TokenBuffer.h>

namespace Caracal
{
    ArgumentBindingResult bindCallArgumentsOld(
        const std::vector<Parameter>& parameters,
        size_t parameterOffset,
        bool isVariadic,
        const FunctionCallExpression& call,
        const std::string& functionName,
        DiagnosticsBag& diagnostics,
        const TokenBuffer& tokens)
    {
        ArgumentBindingResult binding;

        const auto totalParameters = parameters.size();
        const auto fixedParameterCount = (isVariadic ? totalParameters - 1 : totalParameters) - parameterOffset;
        const auto& callArguments = call.arguments();

        binding.ordered.assign(fixedParameterCount, nullptr);

        size_t positionalSlot = 0;
        auto tooManyPositional = false;
        for (const auto& argument : callArguments)
        {
            const auto* value = argument.value().get();
            if (argument.isNamed())
            {
                size_t parameterIndex = fixedParameterCount;
                for (size_t p = 0; p < fixedParameterCount; ++p)
                {
                    if (parameters[parameterOffset + p].name() == argument.name())
                    {
                        parameterIndex = p;
                        break;
                    }
                }

                if (parameterIndex == fixedParameterCount)
                {
                    binding.ok = false;
                    diagnostics.addUnknownArgumentNameError(tokens.source(), tokens.getSourceLocation(argument.nameToken().value()), functionName, argument.name());
                    continue;
                }

                if (binding.ordered[parameterIndex] != nullptr)
                {
                    binding.ok = false;
                    diagnostics.addDuplicateArgumentBindingError(tokens.source(), tokens.getSourceLocation(argument.nameToken().value()), functionName, argument.name());
                    continue;
                }

                binding.ordered[parameterIndex] = value;
            }
            else if (positionalSlot < fixedParameterCount)
            {
                binding.ordered[positionalSlot] = value;
                ++positionalSlot;
            }
            else if (isVariadic)
            {
                binding.variadic.push_back(value);
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
            if (binding.ordered[i] != nullptr)
            {
                continue;
            }

            const auto& parameter = parameters[parameterOffset + i];
            if (parameter.hasDefault())
            {
                binding.ordered[i] = parameter.defaultValue();
                continue;
            }

            if (binding.ok)
            {
                diagnostics.addMissingRequiredArgumentError(tokens.source(), call.argumentsLocation(tokens), functionName, parameter.name());
            }
            binding.ok = false;
        }

        if (binding.ok && tooManyPositional)
        {
            binding.ok = false;
            diagnostics.addArgumentCountMismatchError(
                tokens.source(),
                call.argumentsLocation(tokens),
                functionName,
                static_cast<i32>(fixedParameterCount),
                static_cast<i32>(callArguments.size()),
                isVariadic);
        }

        return binding;
    }
}
