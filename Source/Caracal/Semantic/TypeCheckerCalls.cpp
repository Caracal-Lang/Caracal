#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/ArgumentBinder.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Caracal
{
    struct CallArgumentScope
    {
        std::vector<ValueRef>& stack;
        size_t start;

        ~CallArgumentScope()
        {
            stack.resize(start);
        }
    };

    [[nodiscard]] static SourceLocation CallArgumentsLocation(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 callIndex,
        std::span<const u32> argumentNodes)
    {
        return NodeSpans::callArgumentsLocation(unit.nodeBuffer, context.tokenBuffer(unit.fileId), callIndex, argumentNodes);
    }

    ValueRef TypeChecker::emitSourceTextDefault(const CompilationUnit& unit, size_t parameterIndex, const std::vector<Parameter>& parameters, size_t orderedStart, size_t parameterOffset)
    {
        auto sourceNode = parameters[parameterIndex].defaultValueNode();
        // only earlier parameters are allowed
        auto searchEnd = parameterIndex;
        while (unitFor(sourceNode).nodeBuffer.kind(sourceNode.index) == NodeKind::SourceTextDefault)
        {
            const auto namedParameter = nameOf(unitFor(sourceNode), sourceNode.index);
            auto namedIndex = parameterOffset;
            while (namedIndex < searchEnd && parameters[namedIndex].name() != namedParameter)
            {
                ++namedIndex;
            }
            if (namedIndex >= searchEnd)
            {
                return ValueRef{};
            }

            const auto argumentNode = m_orderedSlotStack[orderedStart + namedIndex - parameterOffset];
            if (argumentNode == UnboundArgumentSlot)
            {
                return ValueRef{};
            }

            if (argumentNode == DefaultBoundArgumentSlot)
            {
                sourceNode = parameters[namedIndex].defaultValueNode();
            }
            else
            {
                sourceNode = NodeIndex{ unit.fileId, argumentNode };
            }
            searchEnd = namedIndex;
        }

        const auto& sourceUnit = unitFor(sourceNode);
        const auto location = ExpressionSourceLocation(m_compilationContext, sourceUnit, sourceNode.index);
        const auto text = std::string_view(sourceUnit.source->text).substr(location.startIndex, location.endIndex - location.startIndex);
        const auto stringType = m_module.wellKnown().string;
        const auto temp = m_emitter.allocateTempAggregateSlot("temp", stringType);
        m_emitter.emitStringLiteralIntoAddress(temp, text);
        return m_emitter.emitLoad(temp, stringType);
    }

    ValueRef TypeChecker::emitSourceLocationDefault(const CompilationUnit& unit, u32 callIndex)
    {
        const auto locationType = m_module.wellKnown().sourceLocation;
        const auto temp = m_emitter.allocateTempAggregateSlot("temp", locationType);

        const auto filePathField = m_module.getTypeDefinition(locationType).tryGetFieldByName("filePath");
        const auto filePathAddress = m_emitter.emitFieldAddress(temp, locationType, filePathField.index(), filePathField.type());
        const auto path = unit.source->filePath.generic_u8string();
        m_emitter.emitStringLiteralIntoAddress(filePathAddress, std::string_view(reinterpret_cast<const char*>(path.data()), path.size()));

        const auto lineField = m_module.getTypeDefinition(locationType).tryGetFieldByName("line");
        const auto lineAddress = m_emitter.emitFieldAddress(temp, locationType, lineField.index(), lineField.type());
        const auto callStart = ExpressionSourceLocation(m_compilationContext, unit, callIndex).startIndex;
        const auto line = unit.source->lineColumnAt(callStart).line;
        m_emitter.emitStoreValue(m_emitter.emitConstant(ConstantValue::FromI32(line), lineField.type()), lineAddress, lineField.type());

        return m_emitter.emitLoad(temp, locationType);
    }

    ValueRef TypeChecker::emitDefaultArgumentExpression(NodeIndex defaultNode, Type parameterType)
    {
        const auto defaultType = m_module.typeOfNode(defaultNode);
        if (defaultType == Type::Undefined())
        {
            return ValueRef{};
        }

        auto& defaultUnit = unitFor(defaultNode);
        auto context = ExpressionContext{};
        context.contextualNumberType = contextualTypeForExpectedType(parameterType);
        const auto checkedDefault = typeCheckExpression(defaultUnit, defaultNode.index, context);
        const auto defaultValue = materializeExpressionValue(defaultUnit, defaultNode.index, checkedDefault);
        if (defaultValue.id() < 0)
        {
            return ValueRef{};
        }

        return m_emitter.emitWidened(defaultValue, checkedDefault.type.toValue(), parameterType.toValue());
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckFunctionCallExpression(const CompilationUnit& unit, u32 index, const ExpressionContext& context)
    {
        CARACAL_ZONE_DETAIL("expr.call");
        const auto nameExpressionIndex = NodeLayout::subtreeBegin(unit.nodeBuffer, index);
        const auto name = nameOf(unit, nameExpressionIndex);
        const auto* overloadSet = m_module.tryGetOverloadSet(name);
        if (overloadSet == nullptr)
        {
            if (m_currentType != Type::Undefined())
            {
                auto& typeDefinition = m_module.getTypeDefinition(m_currentType);
                if (typeDefinition.tryGetMethodOverloadSet(name) != nullptr)
                {
                    m_diagnostics.addMethodCallMissingDotError(
                        unit.source,
                        ExpressionSourceLocation(m_compilationContext, unit, index),
                        std::string(name),
                        typeDefinition.name());
                    return Type::Undefined();
                }
            }

            m_diagnostics.addUnknownFunctionError(
                unit.source,
                ExpressionSourceLocation(m_compilationContext, unit, index),
                std::string(name));
            return Type::Undefined();
        }

        const auto functionType = resolveCallee(unit, index, nameExpressionIndex, *overloadSet);
        if (functionType == Type::Undefined())
        {
            return Type::Undefined();
        }

        return typeCheckResolvedFunctionCall(unit, index, functionType, context);
    }

    TypeChecker::CheckedValue TypeChecker::typeCheckResolvedFunctionCall(const CompilationUnit& unit, u32 index, Type functionType, const ExpressionContext& context)
    {
        const auto& functionDefinition = m_module.getFunctionDefinition(functionType);

        // extern declarations only reach codegen when a call requires them
        if (functionDefinition.isExtern())
        {
            m_module.markExternRequired(functionType);
        }

        if (!m_isPreludePass)
        {
            for (const auto callReturnType : functionDefinition.returnTypes())
            {
                m_module.markPreludeTypeRequired(callReturnType);
            }
        }

        auto emittedCallValue = ValueRef{};
        auto emittedCallFold = std::optional<FoldValue>{};
        if (!typeCheckCallArguments(unit, index, functionType, context, emittedCallValue, emittedCallFold))
        {
            return Type::Undefined();
        }

        // the argument walk can move the definition store, resolve again
        const auto& returnTypes = m_module.getFunctionDefinition(functionType).returnTypes();
        Type returnType = Type::Void();
        if (returnTypes.size() == 1)
        {
            returnType = returnTypes[0];
        }
        else if (returnTypes.size() > 1)
        {
            TODO("Handle multiple return types");
        }

        auto result = CheckedValue{ recordType(unit, index, returnType) };
        result.value = emittedCallValue;
        recordType(unit, NodeLayout::subtreeBegin(unit.nodeBuffer, index), functionType);
        return result;
    }

    TypeChecker::CheckedValue TypeChecker::finishResolvedMethodCall(const CompilationUnit& unit, u32 index, u32 callIndex, u32 nameExpressionIndex, Type methodType, const ExpressionContext& context, std::optional<ValueRef> implicitArgument)
    {
        auto callValue = ValueRef{};
        auto callFold = std::optional<FoldValue>{};
        if (!typeCheckCallArguments(unit, callIndex, methodType, context, callValue, callFold, implicitArgument))
        {
            return Type::Undefined();
        }

        const auto& returnTypes = m_module.getFunctionDefinition(methodType).returnTypes();
        auto returnType = Type::Void();
        if (returnTypes.size() == 1)
        {
            returnType = returnTypes[0];
        }

        recordType(unit, index, returnType);
        recordType(unit, callIndex, returnType);
        recordType(unit, nameExpressionIndex, methodType);
        auto result = CheckedValue{ returnType };
        result.value = callValue;
        result.folded = callFold;
        return result;
    }

    TypeChecker::ArgumentContext TypeChecker::argumentContextFor(const CompilationUnit& unit, const Parameter& parameter, u32 argumentValue) const noexcept
    {
        auto argumentContext = ArgumentContext{};
        argumentContext.contextualNumberType = contextualTypeForExpectedType(parameter.type());
        argumentContext.wantsAddress = parameter.type().isReference();
        if (!argumentContext.wantsAddress
            && parameter.type().kind() == TypeKind::DynamicArray
            && unit.nodeBuffer.kind(NodeLayout::stripGroupings(unit.nodeBuffer, argumentValue)) == NodeKind::NameExpression)
        {
            // a dynamic array passed by name is deep copied
            argumentContext.wantsAddress = true;
        }

        return argumentContext;
    }

    bool TypeChecker::typeCheckCallArguments(const CompilationUnit& unit, u32 callIndex, Type functionType, const ExpressionContext& context, ValueRef& emittedCallValue, std::optional<FoldValue>& emittedCallFold, std::optional<ValueRef> implicitArgument)
    {
        CARACAL_ZONE_NAMED("check.callArguments");
        const auto& buffer = unit.nodeBuffer;

        // the argument walk below can grow the definition store, resolve again
        const auto& functionDefinition = m_module.getFunctionDefinition(functionType);
        const auto& parameterTypes = functionDefinition.parameters();
        auto isVariadic = functionDefinition.isVariadic();
        auto parameterCount = parameterTypes.size();
        if (isVariadic)
        {
            parameterCount = parameterTypes.size() - 1;
        }

        const auto isExternCall = functionDefinition.isExtern();
        size_t parameterOffset = 0;
        if (functionDefinition.hasImplicitThis())
        {
            parameterOffset = 1;
        }
        if (parameterCount < parameterOffset)
        {
            TODO("This shouldn't happen");
            return false;
        }

        NodeLayout::collectChildren(buffer, callIndex, m_childrenScratch);
        const auto argumentsStart = m_callArgumentStack.size();
        for (size_t childIndex = 1; childIndex < m_childrenScratch.size(); ++childIndex)
        {
            m_callArgumentStack.push_back(m_childrenScratch[childIndex]);
        }
        const auto argumentsEnd = m_callArgumentStack.size();
        const auto orderedStart = m_orderedSlotStack.size();
        auto popFrames = [&]
        {
            m_orderedSlotStack.resize(orderedStart);
            m_callArgumentStack.resize(argumentsStart);
        };

        auto binding = bindCallArguments(
            m_compilationContext,
            unit,
            callIndex,
            std::span<const u32>{ m_callArgumentStack }.subspan(argumentsStart),
            parameterTypes,
            parameterOffset,
            isVariadic,
            functionDefinition.name(),
            m_diagnostics,
            m_orderedSlotStack,
            orderedStart);
        const auto fixedParameterCount = m_orderedSlotStack.size() - orderedStart;
        if (!binding.ok)
        {
            popFrames();
            return false;
        }

        struct EmittedArgument
        {
            u32 node;
            ValueRef value;
            std::optional<FoldValue> folded;
        };
        std::vector<EmittedArgument> argumentValues;
        argumentValues.reserve(argumentsEnd - argumentsStart);
        for (auto argumentPosition = argumentsStart; argumentPosition < argumentsEnd; ++argumentPosition)
        {
            const auto argumentValue = NodeLayout::argumentValue(buffer, m_callArgumentStack[argumentPosition]);
            // resolve again every iteration, the previous argument's walk may have moved the definition store
            const auto& parameters = m_module.getFunctionDefinition(functionType).parameters();

            auto argumentContext = ArgumentContext{};
            auto isVariadicArgument = true;
            for (size_t i = 0; i < fixedParameterCount; ++i)
            {
                if (m_orderedSlotStack[orderedStart + i] != argumentValue)
                {
                    continue;
                }

                argumentContext = argumentContextFor(unit, parameters[i + parameterOffset], argumentValue);
                isVariadicArgument = false;
                break;
            }

            if (isVariadicArgument)
            {
                if (isExternCall)
                {
                    // extern calls to C need a cstring
                    argumentContext.contextualNumberType = m_module.wellKnown().cstring;
                }
            }

            // a constructor call as argument builds the object in a temp slot and passes it to the call
            auto pushedTempDestination = false;
            if (!currentScopeIsGlobal())
            {
                const auto constructedType = tryResolveConstructedType(unit, NodeLayout::stripGroupings(buffer, argumentValue));
                if (constructedType != Type::Undefined())
                {
                    const auto destination = m_emitter.allocateTempAggregateSlot("constructed", constructedType);
                    if (destination.id() >= 0)
                    {
                        pushConstructorDestination(unit, argumentValue, destination, true);
                        pushedTempDestination = true;
                    }
                }
            }

            auto argumentExpressionContext = context;
            argumentExpressionContext.contextualNumberType = argumentContext.contextualNumberType;
            argumentExpressionContext.wantsAddress = argumentContext.wantsAddress;
            argumentExpressionContext.receiverPosition = false;
            const auto checkedArgument = typeCheckExpression(unit, argumentValue, argumentExpressionContext);
            if (pushedTempDestination)
            {
                m_constructorDestinations.pop_back();
            }
            auto emittedArgument = checkedArgument.value;
            if (!argumentContext.wantsAddress && !isVariadicArgument)
            {
                emittedArgument = copyValueIfNeeded(unit, argumentValue, emittedArgument, checkedArgument.type);
            }
            argumentValues.push_back(EmittedArgument{ argumentValue, emittedArgument, checkedArgument.folded });
        }

        const auto expectedArgumentCount = parameterCount - parameterOffset;
        const auto& checkedDefinition = m_module.getFunctionDefinition(functionType);
        const auto& checkedParameters = checkedDefinition.parameters();
        std::vector<ArgumentTypeMismatchInfo> argumentTypeMismatches{};
        for (size_t i = 0; i < fixedParameterCount; ++i)
        {
            const auto argumentNode = m_orderedSlotStack[orderedStart + i];
            if (argumentNode == DefaultBoundArgumentSlot)
            {
                const auto& parameter = checkedParameters[i + parameterOffset];
                if (parameter.hasDefaultNode())
                {
                    const auto defaultNode = parameter.defaultValueNode();
                    const auto& defaultUnit = unitFor(defaultNode);
                    const auto defaultType = m_module.typeOfNode(defaultNode);
                    const auto expectedType = parameter.type();
                    // either side being undefined means its error was already reported
                    if (defaultType != Type::Undefined() 
                        && expectedType != Type::Undefined()
                        && !isAssignableTo(defaultType, expectedType))
                    {
                        argumentTypeMismatches.push_back(ArgumentTypeMismatchInfo{
                            ExpressionSourceLocation(m_compilationContext, defaultUnit, defaultNode.index),
                            static_cast<i32>(i + 1),
                            formatTypeName(m_module, expectedType),
                            formatTypeName(m_module, defaultType),
                            });
                    }
                }
                continue;
            }

            const auto argumentType = m_module.typeOfNode(NodeIndex{ unit.fileId, argumentNode });
            const auto expectedType = checkedParameters[i + parameterOffset].type();
            // either side being undefined means its error was already reported
            if (argumentType != Type::Undefined() 
                && expectedType != Type::Undefined()
                && !isAssignableTo(argumentType, expectedType))
            {
                argumentTypeMismatches.push_back(ArgumentTypeMismatchInfo{
                    ExpressionSourceLocation(m_compilationContext, unit, argumentNode),
                    static_cast<i32>(i + 1),
                    formatTypeName(m_module, expectedType),
                    formatTypeName(m_module, argumentType),
                    });
            }
        }

        for (size_t j = 0; j < binding.variadic.size(); ++j)
        {
            const auto argumentNode = binding.variadic[j];
            const auto argumentType = m_module.typeOfNode(NodeIndex{ unit.fileId, argumentNode });
            // an undefined argument means its error was already reported
            if (argumentType == Type::Undefined())
            {
                popFrames();
                return false;
            }
            if (argumentType == Type::Void())
            {
                m_diagnostics.addInvalidVariadicArgumentTypeError(
                    unit.source,
                    ExpressionSourceLocation(m_compilationContext, unit, argumentNode),
                    checkedDefinition.name(),
                    static_cast<i32>(expectedArgumentCount + j + 1),
                    formatTypeName(m_module, argumentType));
                popFrames();
                return false;
            }
        }

        if (!argumentTypeMismatches.empty())
        {
            const auto argumentLocation = CallArgumentsLocation(
                m_compilationContext,
                unit,
                callIndex,
                std::span<const u32>{ m_callArgumentStack }.subspan(argumentsStart, argumentsEnd - argumentsStart));
            m_diagnostics.addArgumentTypeMismatchError(
                unit.source,
                argumentLocation,
                checkedDefinition.name(),
                argumentTypeMismatches);
            popFrames();
            return false;
        }

        m_emitter.recordCalledPreludeFunction(functionType);

        const auto intrinsicKind = checkedDefinition.intrinsicKind();
        const auto isElementIntrinsic = intrinsicKind == IntrinsicKind::ArrayAt
            || intrinsicKind == IntrinsicKind::ArraySet;
        const auto isSliceIntrinsic = intrinsicKind == IntrinsicKind::ArraySlice;
        const auto isToCStringIntrinsic = intrinsicKind == IntrinsicKind::StringToCString;
        const auto isGrowthIntrinsic = intrinsicKind == IntrinsicKind::ArrayAdd
            || intrinsicKind == IntrinsicKind::ArrayRemove;
        const auto isBitwiseIntrinsic = intrinsicKind == IntrinsicKind::BitAnd
            || intrinsicKind == IntrinsicKind::BitOr
            || intrinsicKind == IntrinsicKind::BitXor
            || intrinsicKind == IntrinsicKind::BitNot
            || intrinsicKind == IntrinsicKind::ShiftLeft
            || intrinsicKind == IntrinsicKind::ShiftRight;

        auto foldedCall = std::optional<FoldValue>{};
        if (isBitwiseIntrinsic)
        {
            std::vector<const FoldValue*> foldedArguments(fixedParameterCount, nullptr);
            for (size_t i = 0; i < fixedParameterCount; ++i)
            {
                const auto argumentNode = m_orderedSlotStack[orderedStart + i];
                for (const auto& argument : argumentValues)
                {
                    if (argument.node == argumentNode && argument.folded.has_value())
                    {
                        foldedArguments[i] = &argument.folded.value();
                    }
                }
            }

            auto foldResult = FoldResult{ FoldResultKind::NotFoldable, FoldValue{} };
            if (intrinsicKind == IntrinsicKind::BitNot)
            {
                if (fixedParameterCount >= 1 && foldedArguments[0] != nullptr)
                {
                    foldResult = foldBitNot(*foldedArguments[0]);
                }
            }
            else if (fixedParameterCount >= 2 && foldedArguments[0] != nullptr && foldedArguments[1] != nullptr)
            {
                switch (intrinsicKind)
                {
                    case IntrinsicKind::BitAnd:
                    {
                        foldResult = foldBitAnd(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    case IntrinsicKind::BitOr:
                    {
                        foldResult = foldBitOr(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    case IntrinsicKind::BitXor:
                    {
                        foldResult = foldBitXor(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    case IntrinsicKind::ShiftLeft:
                    {
                        foldResult = foldShiftLeft(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    case IntrinsicKind::ShiftRight:
                    {
                        foldResult = foldShiftRight(*foldedArguments[0], *foldedArguments[1]);
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }

            if (foldResult.kind == FoldResultKind::Value)
            {
                foldedCall = foldResult.value;
            }
            else
            {
                m_sawUnfoldedBitwiseCall = true;
            }
        }

        // the call is not emitted when it folded, a parameter got no argument or the receiver is missing
        const auto calleeKind = checkedDefinition.functionType();
        auto canEmitCall = !foldedCall.has_value()
            && (isBitwiseIntrinsic
                || isElementIntrinsic
                || isSliceIntrinsic
                || isGrowthIntrinsic
                || isToCStringIntrinsic
                || checkedDefinition.symbolName().has_value()
                || calleeKind == FunctionType::FreeFunction
                || calleeKind == FunctionType::StaticMethod
                || calleeKind == FunctionType::PublicMethod
                || calleeKind == FunctionType::PrivateMethod
                || calleeKind == FunctionType::SynthesizedConstructor);

        auto receiverAddress = ValueRef{};
        auto loadConstructedObject = false;
        const auto needsReceiver = calleeKind == FunctionType::PublicMethod
            || calleeKind == FunctionType::PrivateMethod
            || calleeKind == FunctionType::SynthesizedConstructor;
        if (canEmitCall && (isElementIntrinsic || isSliceIntrinsic || isGrowthIntrinsic || isToCStringIntrinsic)
            && (!implicitArgument.has_value() || implicitArgument.value().id() < 0))
        {
            canEmitCall = false;
        }
        if (canEmitCall && needsReceiver)
        {
            if (implicitArgument.has_value())
            {
                receiverAddress = implicitArgument.value();
            }
            else if (calleeKind == FunctionType::SynthesizedConstructor)
            {
                if (!m_constructorDestinations.empty() && m_constructorDestinations.back().call == NodeIndex{ unit.fileId, callIndex })
                {
                    receiverAddress = m_constructorDestinations.back().address;
                    loadConstructedObject = m_constructorDestinations.back().loadResult;
                }
                else
                {
                    receiverAddress = m_emitter.allocateTempAggregateSlot("constructed", checkedDefinition.parentType());
                    loadConstructedObject = true;
                }
            }
            else
            {
                receiverAddress = m_emitter.addressOfLocal(m_thisNameId);
            }

            if (receiverAddress.id() < 0)
            {
                canEmitCall = false;
            }
        }
        const auto callArgumentStart = m_callArgumentValues.size();
        const CallArgumentScope callArgumentScope{ m_callArgumentValues, callArgumentStart };
        if (canEmitCall)
        {
            for (size_t i = 0; i < fixedParameterCount; ++i)
            {
                const auto argumentNode = m_orderedSlotStack[orderedStart + i];
                const auto& parameter = checkedParameters[i + parameterOffset];
                if (argumentNode == UnboundArgumentSlot)
                {
                    canEmitCall = false;
                    break;
                }

                if (argumentNode == DefaultBoundArgumentSlot)
                {
                    // an omitted argument re-emits the default's folded value at the call site
                    auto emittedDefault = ValueRef{};
                    if (parameter.hasDefaultNode())
                    {
                        const auto defaultNode = parameter.defaultValueNode();
                        const auto defaultKind = unitFor(defaultNode).nodeBuffer.kind(defaultNode.index);
                        if (defaultKind == NodeKind::SourceTextDefault)
                        {
                            emittedDefault = emitSourceTextDefault(unit, i + parameterOffset, checkedParameters, orderedStart, parameterOffset);
                        }
                        else if (defaultKind == NodeKind::SourceLocationDefault)
                        {
                            emittedDefault = emitSourceLocationDefault(unit, callIndex);
                        }
                        else
                        {
                            emittedDefault = m_emitter.emitDefaultArgument(
                                defaultNode.fileId,
                                defaultNode.index,
                                m_module.typeOfNode(defaultNode).toValue(),
                                parameter.type().toValue());
                            if (emittedDefault.id() < 0)
                            {
                                // a non-scalar default has no fold, its expression emits here
                                emittedDefault = emitDefaultArgumentExpression(defaultNode, parameter.type());
                            }
                        }
                    }
                    if (emittedDefault.id() < 0)
                    {
                        canEmitCall = false;
                        break;
                    }
                    m_callArgumentValues.push_back(emittedDefault);
                    continue;
                }

                if (parameter.type().isReference())
                {
                    const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
                    const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, argumentNode);
                    auto addressNode = stripped;
                    auto aliasesConstant = false;
                    if (unit.nodeBuffer.kind(stripped) == NodeKind::UnaryExpression
                        && TokenKindAt(tokens, unit.nodeBuffer.mainToken(stripped)) == TokenKind::RefKeyword)
                    {
                        addressNode = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(stripped));
                        if (unit.nodeBuffer.kind(addressNode) == NodeKind::NameExpression)
                        {
                            const auto* innerBinding = tryGetBinding(nameIdOf(unit, addressNode));
                            aliasesConstant = innerBinding != nullptr
                                && (innerBinding->kind == VariableBindingKind::LocalConstant || innerBinding->referencesConstant);
                        }
                    }
                    else if (unit.nodeBuffer.kind(stripped) == NodeKind::NameExpression)
                    {
                        const auto* passthroughBinding = tryGetBinding(nameIdOf(unit, stripped));
                        aliasesConstant = passthroughBinding != nullptr && passthroughBinding->referencesConstant;
                    }

                    auto address = addressOfExpression(unit, addressNode);
                    if (aliasesConstant)
                    {
                        address = m_emitter.emitDefensiveRefCopy(address, parameter.type().toValue());
                    }
                    if (address.id() < 0)
                    {
                        canEmitCall = false;
                        break;
                    }
                    m_callArgumentValues.push_back(address);
                    continue;
                }

                const auto argumentType = m_module.typeOfNode(NodeIndex{ unit.fileId, argumentNode }).toValue();

                {
                    // a literal argument materializes in a temp slot and loads back
                    const auto strippedArgument = NodeLayout::stripGroupings(unit.nodeBuffer, argumentNode);
                    if (unit.nodeBuffer.kind(strippedArgument) == NodeKind::ArrayLiteral
                        && (argumentType.kind() == TypeKind::FixedArray || argumentType.kind() == TypeKind::DynamicArray))
                    {
                        const auto loaded = m_emitter.emitLoad(emitArrayLiteralToTempSlot(unit, strippedArgument, argumentType), argumentType);
                        if (loaded.id() < 0)
                        {
                            canEmitCall = false;
                            break;
                        }
                        m_callArgumentValues.push_back(m_emitter.emitWidened(loaded, argumentType, parameter.type().toValue()));
                        continue;
                    }
                }

                if (parameter.type().kind() == TypeKind::DynamicArray && !parameter.type().isReference())
                {
                    // owning dynamic arrays deep copy on the way into a call
                    const auto strippedArgument = NodeLayout::stripGroupings(unit.nodeBuffer, argumentNode);
                    if (unit.nodeBuffer.kind(strippedArgument) == NodeKind::NameExpression)
                    {
                        const auto sourceAddress = addressOfName(unit, strippedArgument);
                        const auto copied = m_emitter.emitArrayCopy(sourceAddress, parameter.type().toValue());
                        if (copied.id() < 0)
                        {
                            canEmitCall = false;
                            break;
                        }
                        m_callArgumentValues.push_back(copied);
                        continue;
                    }
                }

                auto argumentRef = ValueRef{};
                for (const auto& argument : argumentValues)
                {
                    if (argument.node != argumentNode)
                    {
                        continue;
                    }

                    argumentRef = argument.value;
                    if (argumentRef.id() < 0 && argument.folded.has_value())
                    {
                        argumentRef = m_emitter.emitFoldedConstant(argument.folded.value(), argumentType);
                    }
                    break;
                }
                if (argumentRef.id() < 0)
                {
                    canEmitCall = false;
                    break;
                }
                m_callArgumentValues.push_back(m_emitter.emitWidened(argumentRef, argumentType, parameter.type().toValue()));
            }

            // variadic arguments
            for (size_t i = 0; canEmitCall && i < binding.variadic.size(); ++i)
            {
                const auto variadicNode = binding.variadic[i];
                auto variadicRef = ValueRef{};
                for (const auto& argument : argumentValues)
                {
                    if (argument.node != variadicNode)
                    {
                        continue;
                    }

                    variadicRef = argument.value;
                    if (variadicRef.id() < 0 && argument.folded.has_value())
                    {
                        variadicRef = m_emitter.emitFoldedConstant(argument.folded.value(), m_module.typeOfNode(NodeIndex{ unit.fileId, variadicNode }).toValue());
                    }
                    break;
                }
                if (variadicRef.id() < 0)
                {
                    canEmitCall = false;
                    break;
                }
                m_callArgumentValues.push_back(variadicRef);
            }
        }

        if (!foldedCall.has_value() && !canEmitCall)
        {
            m_emitter.recordEmissionFailure();
        }

        if (foldedCall.has_value())
        {
            auto callReturnType = Type::Void();
            if (!checkedDefinition.returnTypes().empty())
            {
                callReturnType = checkedDefinition.returnTypes().front();
            }
            emittedCallFold = foldedCall;
            emittedCallValue = m_emitter.emitFoldedConstant(foldedCall.value(), callReturnType.toValue());
        }
        else if (canEmitCall)
        {
            auto callReturnType = Type::Void();
            if (!checkedDefinition.returnTypes().empty())
            {
                callReturnType = checkedDefinition.returnTypes().front();
            }
            if (needsReceiver)
            {
                m_callArgumentValues.insert(m_callArgumentValues.begin() + static_cast<std::ptrdiff_t>(callArgumentStart), receiverAddress);
            }
            const auto callArguments = std::span<const ValueRef>{ m_callArgumentValues }.subspan(callArgumentStart);
            if (isElementIntrinsic)
            {
                const auto receiverType = checkedDefinition.parameters().front().type().toBaseType();
                if (!callArguments.empty())
                {
                    if (intrinsicKind == IntrinsicKind::ArrayAt)
                    {
                        emittedCallValue = m_emitter.emitElementAccess(
                            implicitArgument.value(), receiverType, callArguments[0], callReturnType.toValue(), context.receiverPosition);
                    }
                    else if (callArguments.size() >= 2)
                    {
                        const auto elementAddress = m_emitter.emitElementAddressForReceiver(implicitArgument.value(), receiverType, callArguments[0]);
                        m_emitter.emitStoreValue(callArguments[1], elementAddress, checkedDefinition.parameters()[2].type());
                    }
                }
            }
            else if (isToCStringIntrinsic)
            {
                const auto receiverType = checkedDefinition.parameters().front().type().toBaseType();
                emittedCallValue = m_emitter.emitStringToCString(implicitArgument.value(), receiverType);
            }
            else if (isGrowthIntrinsic)
            {
                const auto receiverType = checkedDefinition.parameters().front().type().toBaseType();
                if (!callArguments.empty())
                {
                    if (intrinsicKind == IntrinsicKind::ArrayAdd)
                    {
                        m_emitter.emitArrayAdd(implicitArgument.value(), callArguments[0], receiverType);
                    }
                    else
                    {
                        m_emitter.emitArrayRemove(implicitArgument.value(), callArguments[0], receiverType);
                    }
                }
            }
            else if (isSliceIntrinsic)
            {
                emittedCallValue = m_emitter.emitSliceIntrinsic(
                    implicitArgument.value(), checkedDefinition.parameters().front().type().toBaseType(), callReturnType.toValue());
            }
            else if (isBitwiseIntrinsic)
            {
                emittedCallValue = m_emitter.emitBitwiseIntrinsic(intrinsicKind, callArguments, callReturnType.toValue());
            }
            else
            {
                emittedCallValue = m_emitter.emitCall(functionType.id(), callArguments, callReturnType);
            }

            if (loadConstructedObject)
            {
                emittedCallValue = m_emitter.emitLoad(receiverAddress, checkedDefinition.parentType());
            }
        }

        popFrames();
        return true;
    }
}
