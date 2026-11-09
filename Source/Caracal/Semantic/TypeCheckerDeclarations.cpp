#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/BinaryOperatorKind.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Caracal
{
    template <typename Target>
    [[nodiscard]] static FoldValue CastFoldValue(const FoldValue& value) noexcept
    {
        return std::visit([](const auto payload) { return FoldValue{ static_cast<Target>(payload) }; }, value);
    }

    [[nodiscard]] static FoldValue WidenFoldValue(const FoldValue& value, const BuiltinTypeDescription& target) noexcept
    {
        if (target.kind == BuiltinTypeKind::Float)
        {
            if (target.bits == 64)
            {
                return CastFoldValue<f64>(value);
            }
            else
            {
                return CastFoldValue<f32>(value);
            }
        }

        if (target.isSigned)
        {
            switch (target.bits)
            {
                case 8:
                {
                    return CastFoldValue<i8>(value);
                }
                case 16:
                {
                    return CastFoldValue<i16>(value);
                }
                case 32:
                {
                    return CastFoldValue<i32>(value);
                }
                default:
                {
                    return CastFoldValue<i64>(value);
                }
            }
        }

        switch (target.bits)
        {
            case 8:
            {
                return CastFoldValue<u8>(value);
            }
            case 16:
            {
                return CastFoldValue<u16>(value);
            }
            case 32:
            {
                return CastFoldValue<u32>(value);
            }
            default:
            {
                return CastFoldValue<u64>(value);
            }
        }
    }

    [[nodiscard]] static bool IsConstructorCallExpression(
        SemanticContext& module,
        const CompilationUnit& unit,
        const TokenBuffer& tokens,
        u32 index);

    // a global constant can only contain constants and constructor calls
    [[nodiscard]] static bool InitializerCallsFunction(
        SemanticContext& module,
        const CompilationUnit& unit,
        const TokenBuffer& tokens,
        u32 index)
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, index);
        if (unit.nodeBuffer.kind(stripped) == NodeKind::ArrayLiteral)
        {
            std::vector<u32> elements;
            NodeLayout::collectChildren(unit.nodeBuffer, stripped, elements);
            for (const auto element : elements)
            {
                if (InitializerCallsFunction(module, unit, tokens, element))
                {
                    return true;
                }
            }

            return false;
        }

        if (!IsConstructorCallExpression(module, unit, tokens, stripped))
        {
            return ContainsRuntimeCall(module, unit, tokens, stripped);
        }

        std::vector<u32> arguments;
        NodeLayout::collectChildren(unit.nodeBuffer, NodeLayout::binary(unit.nodeBuffer, stripped).right, arguments);
        for (size_t argumentIndex = 1; argumentIndex < arguments.size(); ++argumentIndex)
        {
            auto valueIndex = arguments[argumentIndex];
            if (unit.nodeBuffer.kind(valueIndex) == NodeKind::NamedArgument)
            {
                valueIndex = NodeLayout::namedArgumentValue(valueIndex);
            }

            if (InitializerCallsFunction(module, unit, tokens, valueIndex))
            {
                return true;
            }
        }

        return false;
    }

    [[nodiscard]] static bool IsConstructorCallExpression(
        SemanticContext& module,
        const CompilationUnit& unit,
        const TokenBuffer& tokens,
        u32 index)
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, index);
        if (unit.nodeBuffer.kind(stripped) != NodeKind::BinaryExpression)
        {
            return false;
        }

        const auto binaryLayout = NodeLayout::binary(unit.nodeBuffer, stripped);
        if (!unit.nodeBuffer.isMemberAccessBinary(tokens, stripped)
            || unit.nodeBuffer.kind(binaryLayout.right) != NodeKind::FunctionCallExpression)
        {
            return false;
        }

        const auto receiverType = module.typeOfNode(NodeIndex{ unit.fileId, binaryLayout.left });
        if (receiverType.kind() != TypeKind::Type)
        {
            return false;
        }

        if (module.typeOfNode(NodeIndex{ unit.fileId, binaryLayout.right }) == Type::Undefined())
        {
            return false;
        }

        const auto methodType = module.typeOfNode(NodeIndex{ unit.fileId, NodeLayout::subtreeBegin(unit.nodeBuffer, binaryLayout.right) });
        const auto* definition = module.tryGetFunctionDefinition(methodType);
        return definition != nullptr && definition->functionType() == FunctionType::SynthesizedConstructor;
    }

    bool TypeChecker::initializerNeedsGlobalInit(const CompilationUnit& unit, u32 index)
    {
        const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, index);
        switch (unit.nodeBuffer.kind(stripped))
        {
            case NodeKind::NumberLiteral:
            case NodeKind::BoolLiteral:
            case NodeKind::UnaryExpression:
            {
                return false;
            }
            case NodeKind::NameExpression:
            {
                // limit :: 5 has a constant value
                // origin :: Point.new(5) has no constant value
                const auto* binding = tryGetBinding(nameIdOf(unit, stripped));
                return binding != nullptr && !binding->folded.has_value();
            }
            case NodeKind::ArrayLiteral:
            {
                std::vector<u32> elements;
                NodeLayout::collectChildren(unit.nodeBuffer, stripped, elements);
                for (const auto element : elements)
                {
                    if (initializerNeedsGlobalInit(unit, element))
                    {
                        return true;
                    }
                }

                return false;
            }
            case NodeKind::BinaryExpression:
            {
                // 2 %+ 3 can fold, Point.new(5) and Color.Red cannot
                return unit.nodeBuffer.isMemberAccessBinary(m_compilationContext.tokenBuffer(unit.fileId), stripped);
            }
            default:
            {
                return true;
            }
        }
    }

    void TypeChecker::emitLocalBinding(
        const CompilationUnit& unit, 
        u32 nameId,
        std::string_view name,
        Type bindingType,
        Type rightType,
        u32 strippedRight,
        const CheckedValue& checkedRight,
        bool referencesConstant,
        ValueRef aggregateDestination,
        bool constructorFilled)
    {
        CARACAL_ZONE_DETAIL("decl.emitBinding");
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto strippedRightKind = unit.nodeBuffer.kind(strippedRight);

        if (aggregateDestination.id() >= 0)
        {
            // a constructor call already filled the slot through its receiver
            if (constructorFilled)
            {
                return;
            }

            if (strippedRightKind == NodeKind::ArrayLiteral
                && (bindingType.kind() == TypeKind::FixedArray 
                || bindingType.kind() == TypeKind::DynamicArray))
            {
                emitCapturedArrayLiteral(unit, strippedRight, aggregateDestination, bindingType);
            }
            else if (strippedRightKind == NodeKind::StringLiteral && bindingType == m_module.wellKnown().string)
            {
                m_emitter.emitStringLiteralIntoAddress(aggregateDestination, escapedStringContent(unit, strippedRight));
            }
            else if (bindingType.kind() == TypeKind::DynamicArray && strippedRightKind == NodeKind::NameExpression)
            {
                // owning dynamic arrays deep copy on binding
                const auto sourceAddress = dynamicArraySourceAddress(unit, strippedRight, bindingType.toValue());
                const auto copied = m_emitter.emitArrayCopy(sourceAddress, bindingType.toValue());
                m_emitter.emitStoreValue(copied, aggregateDestination, bindingType.toValue());
            }
            else
            {
                const auto storedValue = copyValueIfNeeded(unit, strippedRight, materializeValue(checkedRight), bindingType);
                m_emitter.emitStoreValue(storedValue, aggregateDestination, bindingType.toValue());
            }
            return;
        }

        if ((bindingType.kind() == TypeKind::FixedArray || bindingType.kind() == TypeKind::DynamicArray)
            && strippedRightKind == NodeKind::ArrayLiteral)
        {
            // array literals with an inferred type allocate their slot here and fill it with the checked elements
            const auto destination = m_emitter.allocateAggregateSlot(nameId, name, bindingType);
            emitCapturedArrayLiteral(unit, strippedRight, destination, bindingType);
            return;
        }

        if (bindingType.isReference())
        {
            const auto isExplicitRefBinding = 
                strippedRightKind == NodeKind::UnaryExpression
                && TokenKindAt(tokens, unit.nodeBuffer.mainToken(strippedRight)) == TokenKind::RefKeyword;
            if (isExplicitRefBinding)
            {
                // an explicit ref binds to the address of its referent, field chains included
                // a referent without an address cannot be emitted
                const auto refOperand = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(strippedRight));
                const auto referentAddress = addressOfExpression(unit, refOperand);
                if (referentAddress.id() < 0)
                {
                    m_emitter.recordEmissionFailure();
                    return;
                }

                m_emitter.emitRefLocalDeclaration(nameId, name, bindingType, referentAddress);
            }
            else
            {
                // a ref typed initializer without an explicit ref copies the value for now
                m_emitter.emitLocalDeclaration(nameId, name, bindingType.toValue(), materializeValue(checkedRight));
            }
            return;
        }

        if (bindingType.kind() == TypeKind::FixedArray || bindingType.kind() == TypeKind::DynamicArray || bindingType.kind() == TypeKind::Type)
        {
            // aggregates without a preallocated slot allocate one here and store the value into it
            const auto storedValue = copyValueIfNeeded(unit, strippedRight, materializeValue(checkedRight), bindingType);
            const auto destination = m_emitter.allocateAggregateSlot(nameId, name, bindingType);
            m_emitter.emitStoreValue(storedValue, destination, bindingType.toValue());
            return;
        }

        const auto initializerValue = m_emitter.emitWidened(materializeValue(checkedRight), rightType.toValue(), bindingType.toValue());
        m_emitter.emitLocalDeclaration(nameId, name, bindingType, initializerValue);
    }

    Type TypeChecker::resolveAggregateInitializerType(
        const CompilationUnit& unit,
        Type declaredType,
        u32 strippedInitializer,
        Type constructedType,
        bool& initializerWantsAddress)
    {
        CARACAL_ZONE_NAMED("check.aggregateInitializerType");
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        auto aggregateType = constructedType;
        if (aggregateType == Type::Undefined() && !declaredType.isReference()
            && (declaredType.kind() == TypeKind::Type
                || declaredType.kind() == TypeKind::FixedArray
                || declaredType.kind() == TypeKind::DynamicArray))
        {
            aggregateType = declaredType;
        }
        if (aggregateType == Type::Undefined() && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::NameExpression
            && !declaredType.isReference())
        {
            if (const auto* initializerBinding = tryGetBinding(nameIdOf(unit, strippedInitializer));
                initializerBinding != nullptr
                && !initializerBinding->type.isReference()
                && (initializerBinding->type.kind() == TypeKind::Type
                    || initializerBinding->type.kind() == TypeKind::FixedArray
                    || initializerBinding->type.toValue().kind() == TypeKind::DynamicArray))
            {
                aggregateType = initializerBinding->type.toValue();
            }
        }
        if (aggregateType == Type::Undefined() 
            && !declaredType.isReference()
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::FunctionCallExpression)
        {
            // a call returning an aggregate stores into a preallocated slot
            const auto calleeNameIndex = NodeLayout::subtreeBegin(unit.nodeBuffer, strippedInitializer);
            auto calleeType = Type::Undefined();
            if (const auto* calleeSet = m_module.tryGetOverloadSet(nameOf(unit, calleeNameIndex)); calleeSet != nullptr)
            {
                const auto diagnosticCount = m_diagnostics.diagnostics().size();
                calleeType = resolveCallee(unit, strippedInitializer, calleeNameIndex, *calleeSet);
                m_diagnostics.removeDiagnosticsAfter(diagnosticCount);
            }
            if (calleeType != Type::Undefined())
            {
                const auto& calleeReturns = m_module.getFunctionDefinition(calleeType).returnTypes();
                if (calleeReturns.size() == 1 && !calleeReturns.front().isReference()
                    && (calleeReturns.front().kind() == TypeKind::Type
                        || calleeReturns.front().kind() == TypeKind::FixedArray
                        || calleeReturns.front().kind() == TypeKind::DynamicArray))
                {
                    aggregateType = calleeReturns.front();
                }
            }
        }
        if (aggregateType == Type::Undefined()
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::StringLiteral
            && m_module.wellKnown().string != Type::Undefined()
            && (declaredType == Type::Undefined() || declaredType == m_module.wellKnown().string))
        {
            aggregateType = m_module.wellKnown().string;
        }
        if (aggregateType.kind() == TypeKind::DynamicArray
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::NameExpression)
        {
            // a dynamic array copied from a name is read through its address and deep copied
            initializerWantsAddress = true;
        }
        if (aggregateType != Type::Undefined() && aggregateType == m_module.wellKnown().string
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::StringLiteral)
        {
            // the literal fills the declaration's slot directly
            initializerWantsAddress = true;
        }
        if (aggregateType == Type::Undefined()
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::BinaryExpression
            && unit.nodeBuffer.isMemberAccessBinary(tokens, strippedInitializer))
        {
            // a method call returning an aggregate preallocates like a
            // free call, the receiver type resolves syntactically
            const auto callLayout = NodeLayout::binary(unit.nodeBuffer, strippedInitializer);
            if (unit.nodeBuffer.kind(callLayout.right) == NodeKind::FunctionCallExpression)
            {
                const auto receiverNode = NodeLayout::stripGroupings(unit.nodeBuffer, callLayout.left);
                auto receiverType = Type::Undefined();
                if (unit.nodeBuffer.kind(receiverNode) == NodeKind::NameExpression)
                {
                    if (const auto* receiverBinding = tryGetBinding(nameIdOf(unit, receiverNode)); receiverBinding != nullptr)
                    {
                        receiverType = receiverBinding->type.toValue();
                    }
                }
                else if (unit.nodeBuffer.kind(receiverNode) == NodeKind::StringLiteral)
                {
                    receiverType = m_module.wellKnown().string;
                }

                if (receiverType.kind() == TypeKind::Type)
                {
                    const auto methodNameIndex = NodeLayout::subtreeBegin(unit.nodeBuffer, callLayout.right);
                    auto methodType = Type::Undefined();
                    if (const auto* methodSet = m_module.getTypeDefinition(receiverType).tryGetMethodOverloadSet(nameOf(unit, methodNameIndex));
                        methodSet != nullptr && !methodSet->overloads.empty())
                    {
                        const auto diagnosticCount = m_diagnostics.diagnostics().size();
                        methodType = resolveMethodCallee(unit, callLayout.right, methodNameIndex, *methodSet, false);
                        m_diagnostics.removeDiagnosticsAfter(diagnosticCount);
                    }
                    if (methodType != Type::Undefined())
                    {
                        const auto& methodReturns = m_module.getFunctionDefinition(methodType).returnTypes();
                        if (methodReturns.size() == 1 && !methodReturns.front().isReference()
                            && (methodReturns.front().kind() == TypeKind::Type
                                || methodReturns.front().kind() == TypeKind::FixedArray
                                || methodReturns.front().kind() == TypeKind::DynamicArray))
                        {
                            aggregateType = methodReturns.front();
                        }
                    }
                }
            }
        }
        if (aggregateType == Type::Undefined()
            && unit.nodeBuffer.kind(strippedInitializer) == NodeKind::BinaryExpression
            && !unit.nodeBuffer.isMemberAccessBinary(tokens, strippedInitializer)
            && ConvertToBinaryOperator(TokenKindAt(tokens, unit.nodeBuffer.mainToken(strippedInitializer))) == BinaryOperatorKind::AdditionWrapping
            && m_module.wellKnown().string != Type::Undefined())
        {
            // only a concat chain is string typed, a comparison of strings is not
            auto leftmostOperand = strippedInitializer;
            while (unit.nodeBuffer.kind(leftmostOperand) == NodeKind::BinaryExpression)
            {
                leftmostOperand = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::binary(unit.nodeBuffer, leftmostOperand).left);
            }
            auto leftmostOperandType = Type::Undefined();
            if (unit.nodeBuffer.kind(leftmostOperand) == NodeKind::StringLiteral)
            {
                leftmostOperandType = m_module.wellKnown().string;
            }
            else if (unit.nodeBuffer.kind(leftmostOperand) == NodeKind::NameExpression)
            {
                if (const auto* leftmostBinding = tryGetBinding(nameIdOf(unit, leftmostOperand)); leftmostBinding != nullptr)
                {
                    leftmostOperandType = leftmostBinding->type.toValue();
                }
            }
            if (leftmostOperandType == m_module.wellKnown().string)
            {
                aggregateType = leftmostOperandType;
            }
        }

        return aggregateType;
    }

    void TypeChecker::typeCheckConstantDeclaration(const CompilationUnit& unit, u32 index)
    {
        CARACAL_ZONE_NAMED("check.constantDeclaration");
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto isGlobalConstant = currentScopeIsGlobal();

        if (isGlobalConstant)
        {
            std::vector<u32> annotations;
            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
            for (const auto annotation : annotations)
            {
                m_diagnostics.addUnexpectedAnnotationTargetError(
                    unit.source,
                    annotationLocation(unit, annotation));
            }
        }

        const auto declarationLayout = NodeLayout::declaration(unit.nodeBuffer, index);
        auto declaredType = Type::Undefined();
        if (declarationLayout.hasExplicitType)
        {
            declaredType = typeCheckTypeNameNode(unit, declarationLayout.explicitType);
        }

        auto constantAggregateDestination = ValueRef{};
        auto pushedConstantConstruction = false;
        auto constantInitializerWantsAddress = false;
        if (!currentScopeIsGlobal() && unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::NameExpression)
        {
            const auto strippedInitializer = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
            const auto constructedType = tryResolveConstructedType(unit, strippedInitializer);
            const auto aggregateType = resolveAggregateInitializerType(unit, declaredType, strippedInitializer, constructedType, constantInitializerWantsAddress);
            if (aggregateType != Type::Undefined())
            {
                constantAggregateDestination = m_emitter.allocateAggregateSlot(
                    nameIdOf(unit, declarationLayout.left),
                    nameOf(unit, declarationLayout.left),
                    aggregateType);
                if (constructedType != Type::Undefined() && constantAggregateDestination.id() >= 0)
                {
                    pushConstructorDestination(unit, strippedInitializer, constantAggregateDestination, false);
                    pushedConstantConstruction = true;
                }
            }
        }

        // a constructed global is built inside .global_init
        // its slot address is pushed before the arguments are checked, like any constructor
        // a discarded global with a runtime call is emitted into .global_init for its side effects
        auto pushedGlobalConstruction = false;
        auto resumedGlobalInit = false;
        if (currentScopeIsGlobal() && !m_isPreludePass)
        {
            if (unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::DiscardLiteral)
            {
                if (ContainsRuntimeCall(m_module, unit, tokens, declarationLayout.right))
                {
                    m_emitter.resumeGlobalInitEffects();
                    resumedGlobalInit = true;
                }
            }
            else if (unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::NameExpression)
            {
                const auto constructedType = tryResolveConstructedType(unit, NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right));
                if (constructedType != Type::Undefined())
                {
                    resumedGlobalInit = true;
                    const auto destination = m_emitter.beginGlobalInitConstruction(nameOf(unit, declarationLayout.left), constructedType);
                    if (destination.id() >= 0)
                    {
                        pushConstructorDestination(unit, declarationLayout.right, destination, false);
                        pushedGlobalConstruction = true;
                    }
                }
                else if (const auto stripped = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
                    (unit.nodeBuffer.kind(stripped) == NodeKind::ArrayLiteral || unit.nodeBuffer.kind(stripped) == NodeKind::NameExpression)
                    && initializerNeedsGlobalInit(unit, stripped))
                {
                    m_emitter.resumeGlobalInitEffects();
                    resumedGlobalInit = true;
                }
            }
        }

        m_sawUnfoldedBitwiseCall = false;
        auto checkedRight = CheckedValue{ Type::Undefined() };
        {
            auto context = ExpressionContext{};
            // number literals get their type from the declared type
            context.contextualNumberType = contextualTypeForExpectedType(declaredType);
            // a ref initializer binds to the address, not to the value
            const auto addressRight = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
            context.wantsAddress = constantInitializerWantsAddress
                || (unit.nodeBuffer.kind(addressRight) == NodeKind::UnaryExpression
                    && TokenKindAt(tokens, unit.nodeBuffer.mainToken(addressRight)) == TokenKind::RefKeyword);
            checkedRight = typeCheckExpression(unit, declarationLayout.right, context);
        }
        if (pushedConstantConstruction)
        {
            m_constructorDestinations.pop_back();
        }
        if (pushedGlobalConstruction)
        {
            m_constructorDestinations.pop_back();
        }
        if (resumedGlobalInit)
        {
            m_emitter.suspendGlobalInit();
        }
        const auto rightType = checkedRight.type;

        // the binding takes the declared type if there is one
        auto bindingType = rightType;
        if (declaredType != Type::Undefined())
        {
            bindingType = declaredType;
        }

        const auto leftKind = unit.nodeBuffer.kind(declarationLayout.left);
        if (leftKind == NodeKind::NameExpression)
        {
            const auto name = nameOf(unit, declarationLayout.left);
            if (bindingType == Type::Void())
            {
                m_diagnostics.addCallReturnsNoValueError(unit.source, nameLocation(unit, declarationLayout.left), std::string(name));
                recordType(unit, declarationLayout.left, Type::Undefined());
                recordType(unit, index, Type::Undefined());
                return;
            }

            if (isGlobalConstant
                && InitializerCallsFunction(m_module, unit, tokens, declarationLayout.right))
            {
                m_diagnostics.addGlobalConstantWithCallError(
                    unit.source,
                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right),
                    std::string(name));
            }
            else if (isGlobalConstant && !rightType.isReference() && rightType.kind() == TypeKind::DynamicArray)
            {
                m_diagnostics.addGlobalConstantDynamicArrayError(
                    unit.source,
                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right),
                    std::string(name));
            }

            if (isGlobalConstant && m_sawUnfoldedBitwiseCall)
            {
                m_diagnostics.addGlobalConstantNotComputableError(
                    unit.source,
                    NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right),
                    std::string(name));
            }

            auto folded = checkedRight.folded;
            const auto* bindingDescription = m_module.tryGetBuiltinTypeDescription(bindingType);
            if (folded.has_value() && bindingType != rightType && isAssignableTo(rightType, bindingType)
                && bindingDescription != nullptr
                && (bindingDescription->kind == BuiltinTypeKind::Int || bindingDescription->kind == BuiltinTypeKind::Float))
            {
                folded = WidenFoldValue(folded.value(), *bindingDescription);
            }

            const auto nameId = nameIdOf(unit, declarationLayout.left);
            if (const auto* existing = tryGetConflictingBinding(nameId); existing == nullptr)
            {
                addVariableBinding(nameId, bindingType, nameLocation(unit, declarationLayout.left), unit.source, VariableBindingKind::LocalConstant);
                if (auto* newBinding = tryGetBindingMutable(nameId); newBinding != nullptr)
                {
                    newBinding->folded = folded;
                }

                if (isGlobalConstant)
                {
                    static_cast<void>(m_module.createConstant(name, bindingType, NodeIndex{ unit.fileId, declarationLayout.right }));
                    if (!m_isPreludePass)
                    {
                        if (folded.has_value())
                        {
                            m_emitter.emitGlobalConstant(name, bindingType, IREmitter::constantFromFold(folded.value()));
                        }
                        else
                        {
                            const auto strippedRight = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
                            const auto strippedKind = unit.nodeBuffer.kind(strippedRight);
                            if (strippedKind == NodeKind::UnaryExpression
                                && TokenKindAt(tokens, unit.nodeBuffer.mainToken(strippedRight)) == TokenKind::RefKeyword)
                            {
                                const auto refTarget = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(strippedRight));
                                if (unit.nodeBuffer.kind(refTarget) == NodeKind::NameExpression)
                                {
                                    m_emitter.emitGlobalReference(name, rightType, nameOf(unit, refTarget));
                                }
                            }
                            else if (strippedKind == NodeKind::StringLiteral)
                            {
                                m_emitter.emitGlobalConstant(name, rightType, ConstantValue::FromString(escapedStringContent(unit, strippedRight)));
                            }
                            else if (strippedKind == NodeKind::ArrayLiteral)
                            {
                                // constant element arrays store an aggregate value
                                if (const auto aggregate = tryBuildAggregateConstant(unit, strippedRight); aggregate.has_value())
                                {
                                    m_emitter.emitGlobalConstant(name, rightType, aggregate.value());
                                }
                                else if (rightType != Type::Undefined())
                                {
                                    // the elements were checked in .global_init and write to the global here
                                    emitCapturedArrayLiteral(unit, strippedRight, m_emitter.beginGlobalInitConstruction(name, rightType), rightType);
                                    m_emitter.suspendGlobalInit();
                                }
                            }
                            else if (strippedKind == NodeKind::NameExpression && rightType != Type::Undefined())
                            {
                                // v :: o with a global object, the value was read in .global_init
                                const auto destination = m_emitter.beginGlobalInitConstruction(name, rightType);
                                m_emitter.emitStoreValue(checkedRight.value, destination, rightType);
                                m_emitter.suspendGlobalInit();
                            }
                            else if (strippedKind == NodeKind::BinaryExpression
                                && rightType.kind() == TypeKind::Enum
                                && unit.nodeBuffer.isMemberAccessBinary(tokens, strippedRight))
                            {
                                // an enum member global stores the underlying literal
                                const auto memberLayout = NodeLayout::binary(unit.nodeBuffer, strippedRight);
                                if (unit.nodeBuffer.kind(memberLayout.right) == NodeKind::NameExpression)
                                {
                                    auto& initializerEnum = m_module.getEnumDefinition(rightType);
                                    if (const auto* field = initializerEnum.tryGetFieldByName(nameOf(unit, memberLayout.right)); field != nullptr && field->hasConstantValue())
                                    {
                                        if (const auto constant = m_emitter.makeEnumConstantValue(initializerEnum.baseType(), field->value()); constant.has_value())
                                        {
                                            m_emitter.emitGlobalConstant(name, rightType, constant.value());
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    emitLocalBinding(unit, nameId, name, bindingType, rightType,
                        NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right),
                        checkedRight, false, constantAggregateDestination, pushedConstantConstruction);
                }
            }
            else
            {
                m_diagnostics.addDuplicateConstantDeclarationError(
                    unit.source,
                    nameLocation(unit, declarationLayout.left),
                    std::string(name),
                    existing->source,
                    existing->location);
            }
            recordType(unit, declarationLayout.left, bindingType);
        }

        if (declaredType != Type::Undefined() && rightType != Type::Undefined() && !isAssignableTo(rightType, declaredType))
        {
            const auto location = NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right);
            if (!tryAddArrayLengthMismatchError(unit, location, declaredType, rightType))
            {
                m_diagnostics.addExplicitConstantTypeMismatchError(
                    unit.source,
                    location,
                    formatTypeName(m_module, declaredType),
                    formatTypeName(m_module, rightType));
            }
        }

        recordType(unit, index, bindingType);
    }

    void TypeChecker::typeCheckInitConstantDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto isGlobalConstant = currentScopeIsGlobal();

        if (isGlobalConstant)
        {
            std::vector<u32> annotations;
            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
            for (const auto annotation : annotations)
            {
                m_diagnostics.addUnexpectedAnnotationTargetError(
                    unit.source,
                    annotationLocation(unit, annotation));
            }
        }

        const auto initLayout = NodeLayout::initConstant(unit.nodeBuffer, index);
        const auto declaredType = typeCheckTypeNameNode(unit, initLayout.initType);

        const auto isValidInitConstant = isGlobalConstant;
        if (unit.nodeBuffer.kind(initLayout.left) == NodeKind::NameExpression)
        {
            const auto name = nameOf(unit, initLayout.left);

            if (!isValidInitConstant)
            {
                m_diagnostics.addNonGlobalInitConstantError(
                    unit.source,
                    nameLocation(unit, initLayout.left),
                    std::string(name));
            }

            const auto nameId = nameIdOf(unit, initLayout.left);
            if (const auto* existing = tryGetConflictingBinding(nameId); existing == nullptr)
            {
                addVariableBinding(nameId, declaredType, nameLocation(unit, initLayout.left), unit.source, VariableBindingKind::LocalConstant);

                if (isValidInitConstant)
                {
                    static_cast<void>(m_module.createInitConstant(name, declaredType));
                    m_initConstants.insert_or_assign(std::string(name), InitConstantRecord{ nameLocation(unit, initLayout.left), unit.source });
                    if (!m_isPreludePass)
                    {
                        m_emitter.emitConstructedGlobal(name, declaredType);
                    }
                }
            }
            else
            {
                m_diagnostics.addDuplicateConstantDeclarationError(
                    unit.source,
                    nameLocation(unit, initLayout.left),
                    std::string(name),
                    existing->source,
                    existing->location);
            }
            recordType(unit, initLayout.left, declaredType);
        }

        recordType(unit, index, declaredType);
    }

    void TypeChecker::typeCheckVariableDeclaration(const CompilationUnit& unit, u32 index)
    {
        CARACAL_ZONE_NAMED("check.variableDeclaration");
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto declarationLayout = NodeLayout::declaration(unit.nodeBuffer, index);

        auto declaredType = Type::Undefined();
        if (declarationLayout.hasExplicitType)
        {
            declaredType = typeCheckTypeNameNode(unit, declarationLayout.explicitType);
        }

        auto pushedConstructorDestination = false;
        auto aggregateDestination = ValueRef{};
        auto initializerWantsAddress = false;
        if (unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::NameExpression && !currentScopeIsGlobal())
        {
            CARACAL_ZONE_DETAIL("decl.preallocateSlot");
            const auto strippedInitializer = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
            const auto constructedType = tryResolveConstructedType(unit, strippedInitializer);
            const auto aggregateType = resolveAggregateInitializerType(unit, declaredType, strippedInitializer, constructedType, initializerWantsAddress);

            if (aggregateType != Type::Undefined())
            {
                aggregateDestination = m_emitter.allocateAggregateSlot(
                    nameIdOf(unit, declarationLayout.left),
                    nameOf(unit, declarationLayout.left),
                    aggregateType);
                if (constructedType != Type::Undefined() && aggregateDestination.id() >= 0)
                {
                    pushConstructorDestination(unit, strippedInitializer, aggregateDestination, false);
                    pushedConstructorDestination = true;
                }
            }
        }

        auto checkedRight = CheckedValue{ Type::Undefined() };
        {
            CARACAL_ZONE_DETAIL("decl.initializer");
            auto context = ExpressionContext{};
            // number literals get their type from the declared type
            context.contextualNumberType = contextualTypeForExpectedType(declaredType);
            // a ref initializer binds to the address, not to the value
            const auto addressRight = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
            context.wantsAddress = initializerWantsAddress
                || (unit.nodeBuffer.kind(addressRight) == NodeKind::UnaryExpression
                    && TokenKindAt(tokens, unit.nodeBuffer.mainToken(addressRight)) == TokenKind::RefKeyword);
            checkedRight = typeCheckExpression(unit, declarationLayout.right, context);
        }
        if (pushedConstructorDestination)
        {
            m_constructorDestinations.pop_back();
        }
        const auto rightType = checkedRight.type;

        // the binding takes the declared type if there is one
        auto bindingType = rightType;
        if (declaredType != Type::Undefined())
        {
            bindingType = declaredType;
        }

        auto referencesConstant = false;
        const auto strippedRight = NodeLayout::stripGroupings(unit.nodeBuffer, declarationLayout.right);
        const auto strippedRightKind = unit.nodeBuffer.kind(strippedRight);
        {
            CARACAL_ZONE_DETAIL("decl.constness");
            if (strippedRightKind == NodeKind::UnaryExpression)
            {
                // taking a ref of a constant is allowed, but assigning through it is not
                const auto isReferenceOf = TokenKindAt(tokens, unit.nodeBuffer.mainToken(strippedRight)) == TokenKind::RefKeyword;
                if (isReferenceOf && m_module.typeOfNode(NodeIndex{ unit.fileId, strippedRight }).isReference())
                {
                    const auto operand = NodeLayout::stripGroupings(unit.nodeBuffer, NodeLayout::unaryOperand(strippedRight));
                    if (unit.nodeBuffer.kind(operand) == NodeKind::NameExpression)
                    {
                        const auto* operandBinding = tryGetBinding(nameIdOf(unit, operand));
                        referencesConstant = operandBinding != nullptr && operandBinding->kind == VariableBindingKind::LocalConstant;
                    }
                    else
                    {
                        auto rootName = std::string{};
                        auto rootReferencesConstant = false;
                        referencesConstant = receiverChainIsConstant(unit, operand, rootName, rootReferencesConstant);
                    }
                }
            }
            else if (strippedRightKind == NodeKind::NameExpression)
            {
                const auto* rightBinding = tryGetBinding(nameIdOf(unit, strippedRight));
                referencesConstant = rightBinding != nullptr && rightBinding->referencesConstant;
            }
            else if (strippedRightKind == NodeKind::BinaryExpression)
            {
                // a slice() view borrows its receiver, a view of a constant stays constant
                const auto binaryLayout = NodeLayout::binary(unit.nodeBuffer, strippedRight);
                if (unit.nodeBuffer.isMemberAccessBinary(tokens, strippedRight)
                    && unit.nodeBuffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression
                    && unit.nodeBuffer.kind(binaryLayout.left) == NodeKind::NameExpression)
                {
                    const auto methodType = m_module.typeOfNode(NodeIndex{ unit.fileId, NodeLayout::subtreeBegin(unit.nodeBuffer, binaryLayout.right) });
                    if (methodType != Type::Undefined()
                        && m_module.getFunctionDefinition(methodType).intrinsicKind() == IntrinsicKind::ArraySlice)
                    {
                        const auto* receiverBinding = tryGetBinding(nameIdOf(unit, binaryLayout.left));
                        referencesConstant = receiverBinding != nullptr
                            && (receiverBinding->kind == VariableBindingKind::LocalConstant || receiverBinding->referencesConstant);
                    }
                }
            }
        }

        if (unit.nodeBuffer.kind(declarationLayout.left) == NodeKind::NameExpression)
        {
            CARACAL_ZONE_DETAIL("decl.bind");
            const auto name = nameOf(unit, declarationLayout.left);
            if (bindingType == Type::Void())
            {
                m_diagnostics.addCallReturnsNoValueError(unit.source, nameLocation(unit, declarationLayout.left), std::string(name));
                recordType(unit, declarationLayout.left, Type::Undefined());
                recordType(unit, index, Type::Undefined());
                return;
            }

            const auto nameId = nameIdOf(unit, declarationLayout.left);
            if (const auto* existing = tryGetConflictingBinding(nameId); existing == nullptr)
            {
                addVariableBinding(nameId, bindingType, nameLocation(unit, declarationLayout.left), unit.source, VariableBindingKind::LocalVariable, referencesConstant);
                emitLocalBinding(unit, nameId, name, bindingType, rightType, strippedRight, checkedRight, referencesConstant, aggregateDestination, pushedConstructorDestination);
            }
            else
            {
                m_diagnostics.addDuplicateVariableDeclarationError(
                    unit.source,
                    nameLocation(unit, declarationLayout.left),
                    std::string(name),
                    existing->source,
                    existing->location);
            }
            recordType(unit, declarationLayout.left, bindingType);
        }

        if (declaredType != Type::Undefined() && rightType != Type::Undefined() && !isAssignableTo(rightType, declaredType))
        {
            const auto location = NodeSpans::expressionLocation(unit.nodeBuffer, tokens, declarationLayout.right);
            if (!tryAddArrayLengthMismatchError(unit, location, declaredType, rightType))
            {
                m_diagnostics.addExplicitVariableTypeMismatchError(
                    unit.source,
                    location,
                    formatTypeName(m_module, declaredType),
                    formatTypeName(m_module, rightType));
            }
        }

        recordType(unit, index, bindingType);
    }
}
