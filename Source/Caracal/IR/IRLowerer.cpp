#include <Caracal/IR/IRLowerer.h>
#include <Caracal/Profiling.h>

#include <Caracal/Constants.h>
#include <Caracal/IR/AddInstruction.h>
#include <Caracal/IR/AddressOfInstruction.h>
#include <Caracal/IR/BitAndInstruction.h>
#include <Caracal/IR/BitNotInstruction.h>
#include <Caracal/IR/BitOrInstruction.h>
#include <Caracal/IR/BitXorInstruction.h>
#include <Caracal/IR/ShiftLeftInstruction.h>
#include <Caracal/IR/ShiftRightInstruction.h>
#include <Caracal/IR/AddressOfFieldInstruction.h>
#include <Caracal/IR/AddressOfGlobalInstruction.h>
#include <Caracal/IR/AllocateLocalInstruction.h>
#include <Caracal/IR/CallInstruction.h>
#include <Caracal/IR/CallVoidInstruction.h>
#include <Caracal/IR/ConstantValue.h>
#include <Caracal/IR/ConstantInstruction.h>
#include <Caracal/IR/ConstructedGlobalDeclaration.h>
#include <Caracal/IR/DivideInstruction.h>
#include <Caracal/IR/ElementAddressInstruction.h>
#include <Caracal/IR/EqualInstruction.h>
#include <Caracal/IR/GlobalConstantDeclaration.h>
#include <Caracal/IR/GlobalReferenceDeclaration.h>
#include <Caracal/IR/GreaterOrEqualInstruction.h>
#include <Caracal/IR/GreaterThanInstruction.h>
#include <Caracal/IR/IntToFloatInstruction.h>
#include <Caracal/IR/IntWidenInstruction.h>
#include <Caracal/IR/ArrayAddInstruction.h>
#include <Caracal/IR/ArrayCopyInstruction.h>
#include <Caracal/IR/ArrayRemoveInstruction.h>
#include <Caracal/IR/SizeOfInstruction.h>
#include <Caracal/IR/BranchIfTerminator.h>
#include <Caracal/IR/JumpTerminator.h>
#include <Caracal/IR/LessOrEqualInstruction.h>
#include <Caracal/IR/LessThanInstruction.h>
#include <Caracal/IR/LoadValueInstruction.h>
#include <Caracal/IR/LogicalNegationInstruction.h>
#include <Caracal/IR/MakeSliceInstruction.h>
#include <Caracal/IR/MultiplyInstruction.h>
#include <Caracal/IR/NotEqualInstruction.h>
#include <Caracal/IR/ParameterInstruction.h>
#include <Caracal/IR/PhiInstruction.h>
#include <Caracal/IR/ReturnTerminator.h>
#include <Caracal/IR/ReturnValueTerminator.h>
#include <Caracal/IR/UnreachableTerminator.h>
#include <Caracal/IR/StoreValueInstruction.h>
#include <Caracal/IR/SubtractInstruction.h>
#include <Caracal/IR/ValueNegationInstruction.h>
#include <Caracal/Semantic/FunctionDefinition.h>
#include <Caracal/Syntax/ArrayLiteral.h>
#include <Caracal/Syntax/AssignmentStatement.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/BlockNode.h>
#include <Caracal/Syntax/BoolLiteral.h>
#include <Caracal/Syntax/ConstantDeclaration.h>
#include <Caracal/Syntax/Expression.h>
#include <Caracal/Syntax/GroupingExpression.h>
#include <Caracal/Syntax/IfStatement.h>
#include <Caracal/Syntax/MemberAccessExpression.h>
#include <Caracal/Syntax/NameExpression.h>
#include <Caracal/Syntax/NodeKind.h>
#include <Caracal/Syntax/NumberLiteral.h>
#include <Caracal/Syntax/ParseTree.h>
#include <Caracal/Syntax/ReturnStatement.h>
#include <Caracal/Syntax/Statement.h>
#include <Caracal/Syntax/StringLiteral.h>
#include <Caracal/Syntax/UnaryExpression.h>
#include <Caracal/Syntax/VariableDeclaration.h>
#include <Caracal/Syntax/WhileStatement.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

namespace Caracal
{
    static bool IsConstructorCall(const Expression* expression) noexcept
    {
        expression = stripGroupings(expression);
        if (expression == nullptr || expression->kind() != NodeKind::BinaryExpression)
            return false;

        const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
        return binaryExpression->binaryOperator() == BinaryOperatorKind::ConstructorCall
            && binaryExpression->rightExpression()->kind() == NodeKind::FunctionCallExpression;
    }

    static bool ExpressionContainsCall(const Expression* expression) noexcept
    {
        if (expression == nullptr)
            return false;

        switch (expression->kind())
        {
            case NodeKind::FunctionCallExpression:
            {
                return true;
            }
            case NodeKind::GroupingExpression:
            {
                const auto* groupingExpression = static_cast<const GroupingExpression*>(expression);
                return ExpressionContainsCall(groupingExpression->expression().get());
            }
            case NodeKind::UnaryExpression:
            {
                const auto* unaryExpression = static_cast<const UnaryExpression*>(expression);
                return ExpressionContainsCall(unaryExpression->expression().get());
            }
            case NodeKind::MemberAccessExpression:
            {
                const auto* memberAccessExpression = static_cast<const MemberAccessExpression*>(expression);
                return ExpressionContainsCall(memberAccessExpression->expression().get());
            }
            case NodeKind::BinaryExpression:
            {
                const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
                return (ExpressionContainsCall(binaryExpression->leftExpression().get())
                    || ExpressionContainsCall(binaryExpression->rightExpression().get()));
            }
            default:
                return false;
        }
    }

    static BasicBlock* TryGetCurrentBlock(Function& function, const std::optional<BlockId>& blockId) noexcept
    {
        if (!blockId.has_value())
            return nullptr;

        return function.tryGetBlock(blockId.value());
    }

    static std::optional<ConstantValue> CreateConstantValue(const NumberLiteral& literal) noexcept
    {
        if (!literal.hasParsedValue())
            return std::nullopt;

        return std::visit([](const auto value) -> std::optional<ConstantValue>
            {
                using Payload = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Payload, u8>)
                    return ConstantValue::FromU8(value);
                else if constexpr (std::is_same_v<Payload, i32>)
                    return ConstantValue::FromI32(value);
                else if constexpr (std::is_same_v<Payload, i64>)
                    return ConstantValue::FromI64(value);
                else if constexpr (std::is_same_v<Payload, f32>)
                    return ConstantValue::FromF32(value);
                else if constexpr (std::is_same_v<Payload, f64>)
                    return ConstantValue::FromF64(value);
                else
                    return std::nullopt;
            }, literal.parsedValue().value());
    }

    static ConstantValue FromFoldValue(const FoldValue& value) noexcept
    {
        return std::visit(
            [](const auto& payload)
            {
                return ConstantValue::FromLiteralData(ConstantValue::LiteralData{ payload });
            },
            value);
    }

    static std::optional<ConstantValue> CreateEnumConstantValue(Type baseType, i32 value, const SemanticContext& semanticContext) noexcept
    {
        const auto* description = semanticContext.tryGetBuiltinTypeDescription(baseType);
        if (description == nullptr)
            return std::nullopt;

        switch (description->kind)
        {
            case BuiltinTypeKind::Bool:
            {
                return ConstantValue::FromBool(value != 0);
            }
            case BuiltinTypeKind::Int:
            {
                if (!description->isSigned && description->bits <= 8)
                    return ConstantValue::FromU8(static_cast<u8>(value));

                return ConstantValue::FromI32(value);
            }
            case BuiltinTypeKind::Float:
            {
                return ConstantValue::FromF32(static_cast<f32>(value));
            }
            case BuiltinTypeKind::Pointer:
            {
                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    IRLowerer::IRLowerer(SemanticContext& semanticContext)
        : m_semanticContext{ semanticContext }
    {
    }

    void IRLowerer::registerBuiltinTypes(Module& module) noexcept
    {
        module.setWellKnownTypes(m_semanticContext.wellKnown());

        for (const auto& [typeId, description] : m_semanticContext.builtinTypeDescriptions())
        {
            const auto builtinType = Type{ typeId, TypeKind::Builtin };
            module.registerBuiltinTypeDescription(builtinType, description);
            module.registerTypeName(builtinType, std::string(m_semanticContext.getNameByType(builtinType)));
        }
    }

    bool IRLowerer::lower(Module& module) noexcept
    {
        CARACAL_ZONE_NAMED("lower");
        m_externFunctionIdCache.clear();
        m_requiredPreludeFunctions.clear();
        m_queuedPreludeFunctionIds.clear();
        resetState();
        m_globalTypes.clear();
        registerBuiltinTypes(module);
        registerRequiredExterns(module);

        for (const auto requiredType : m_semanticContext.requiredPreludeTypes())
        {
            if (!lowerTypeDefinition(m_semanticContext.getTypeDefinition(requiredType), module))
                return false;
        }

        // array types have no TypeDeclaration, so the printer needs their canonical names registered
        for (const auto arrayType : m_semanticContext.arrayTypes())
        {
            module.registerTypeName(arrayType, std::string(m_semanticContext.getNameByType(arrayType)));
            module.registerArrayType(
                arrayType,
                m_semanticContext.getArrayElementType(arrayType),
                m_semanticContext.getArrayLength(arrayType));
        }

        bool hasUserMain = false;
        bool hasEntryPoint = false;
        for (const auto& functionDefinition : m_semanticContext.functionDefinitions())
        {
            if (functionDefinition.functionType() != FunctionType::FreeFunction)
                continue;

            if (functionDefinition.fullName() == UserMainFunctionName)
                hasUserMain = true;
            else if (functionDefinition.fullName() == EntryPointFunctionName)
                hasEntryPoint = true;
        }
        m_emitEntryPoint = hasUserMain && hasEntryPoint;

        for (const auto& enumDefinition : m_semanticContext.enumDefinitions())
        {
            if (enumDefinition.statement() == nullptr)
                continue;

            if (!lowerEnumDefinition(enumDefinition, module))
                return false;
        }

        for (size_t typeIndex = 0; typeIndex < m_semanticContext.typeDefinitions().size(); ++typeIndex)
        {
            // prelude types like "C" only exist for the extern bindings, nothing to lower
            if (typeIndex < m_semanticContext.preludeTypeDefinitionCount())
                continue;

            const auto& typeDefinition = m_semanticContext.typeDefinitions()[typeIndex];
            if (typeDefinition.statement() == nullptr)
                continue;

            if (typeDefinition.type().kind() == TypeKind::Builtin)
                continue;

            if (!lowerTypeDefinition(typeDefinition, module))
                return false;
        }

        std::vector<const ConstantDefinition*> constructedGlobalDefinitions;
        for (const auto& constantDefinition : m_semanticContext.constantDefinitions())
        {
            if (lowerGlobalConstant(constantDefinition, module))
                continue;

            if (lowerGlobalReference(constantDefinition, module))
                continue;

            if (registerConstructedGlobal(constantDefinition, module))
            {
                if (!constantDefinition.isInit())
                    constructedGlobalDefinitions.push_back(&constantDefinition);

                continue;
            }
        }

        std::vector<const Expression*> globalDiscardEffects;
        for (const auto* discardExpression : m_semanticContext.globalDiscardExpressions())
        {
            // we only need evaluade discards that contain a call
            if (ExpressionContainsCall(discardExpression))
                globalDiscardEffects.push_back(discardExpression);
        }

        if (!constructedGlobalDefinitions.empty() || !globalDiscardEffects.empty())
        {
            if (!lowerGlobalInitializer(constructedGlobalDefinitions, globalDiscardEffects, module))
                return false;
        }

        for (size_t definitionIndex = 0; definitionIndex < m_semanticContext.functionDefinitions().size(); ++definitionIndex)
        {
            // prelude definitions only lower when needed
            if (definitionIndex < m_semanticContext.preludeFunctionDefinitionCount())
                continue;

            const auto& functionDefinition = m_semanticContext.functionDefinitions()[definitionIndex];
            if (!lowerFunctionDefinitionByKind(functionDefinition, module))
                return false;
        }

        // finally lower previously marked functions
        for (size_t queueIndex = 0; queueIndex < m_requiredPreludeFunctions.size(); ++queueIndex)
        {
            const auto& definition = m_semanticContext.getFunctionDefinition(m_requiredPreludeFunctions[queueIndex]);
            if (!lowerFunctionDefinitionByKind(definition, module))
                return false;
        }

        return true;
    }

    bool IRLowerer::lowerFunctionDefinitionByKind(const FunctionDefinition& functionDefinition, Module& module) noexcept
    {
        if (functionDefinition.functionType() == FunctionType::SynthesizedConstructor)
        {
            return lowerSynthesizedConstructorDefinition(functionDefinition, module);
        }

        // intrinsics have no bodies
        if (functionDefinition.functionType() == FunctionType::Intrinsic)
            return true;

        const auto* statement = functionDefinition.statement();
        switch (statement->kind())
        {
            case NodeKind::FunctionDefinitionStatement:
            {
                const auto* functionStatement = static_cast<const FunctionDefinitionStatement*>(statement);
                return lowerFunctionDefinition(functionDefinition, functionStatement->bodyNode().get(), functionStatement->isExtern(), module);
            }
            case NodeKind::MethodDefinitionStatement:
            {
                const auto parentType = functionDefinition.parentType();
                if (parentType.kind() == TypeKind::Builtin && parentType.id() >= 0)
                    return true;

                const auto* methodStatement = static_cast<const MethodDefinitionStatement*>(statement);
                return lowerFunctionDefinition(functionDefinition, methodStatement->bodyNode().get(), methodStatement->isExtern(), module);
            }
            default:
                return true;
        }
    }

    bool IRLowerer::lowerSynthesizedConstructorDefinition(const FunctionDefinition& definition, Module& module) noexcept
    {
        resetState();

        std::vector<IRParameter> parameters;
        for (const auto& parameter : definition.parameters())
        {
            parameters.emplace_back(parameter.name(), parameter.type());
        }

        const auto functionId = definition.type().id();
        auto* function = module.addFunction(Function{ functionId, definition.fullName(), parameters, Type::Void() });
        auto blockId = m_nextBlockId++;
        function->addBlock(BasicBlock{ blockId, "entry", std::make_unique<ReturnTerminator>() });
        m_currentFunction = function;
        m_currentBlock = function->tryGetBlock(blockId);
        if (!lowerParameters(definition))
            return false;

        const auto thisResult = m_locals.find(ImplicitThisName);
        if (thisResult == m_locals.end() || thisResult->second.storageKind != LocalStorageKind::Address)
            return false;

        const auto& typeDefinition = m_semanticContext.getTypeDefinition(definition.parentType());
        // string construction reserves a NUL byte behind the copied bytes, toCString depends on it
        m_reserveNulOnArrayCopy = definition.parentType() == m_semanticContext.wellKnown().string;
        for (const auto& fieldDefinition : typeDefinition.fields())
        {
            const auto loweredValue = lowerValueExpressionExpecting(fieldDefinition.expression(), fieldDefinition.type());
            if (!loweredValue.has_value())
                continue;

            const auto fieldAddress = emitFieldAddress(
                thisResult->second.value,
                definition.parentType(),
                fieldDefinition.name(),
                fieldDefinition.index(),
                fieldDefinition.type());
            m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(
                loweredValue.value(),
                fieldAddress,
                fieldDefinition.type()));
        }
        m_reserveNulOnArrayCopy = false;

        return true;
    }

    bool IRLowerer::lowerEnumDefinition(const EnumDefinition& definition, Module& module) noexcept
    {
        const auto enumType = definition.type();
        if (enumType == Type::Undefined())
            return false;

        auto enumDeclaration = EnumDeclaration{ definition.name(), enumType, definition.baseType() };
        for (const auto& field : definition.fields())
        {
            auto loweredFieldValue = tryLowerEnumFieldValue(enumType, field.name());
            if (!loweredFieldValue.has_value())
                return false;

            enumDeclaration.addField(field.name(), loweredFieldValue.value());
        }

        module.addEnum(std::move(enumDeclaration));
        return true;
    }

    bool IRLowerer::lowerTypeDefinition(const TypeDefinition& definition, Module& module) noexcept
    {
        const auto typeType = definition.type();
        if (typeType == Type::Undefined())
            return false;

        TypeDeclaration typeDeclaration{ definition.name(), typeType };
        for (const auto& fieldDefinition : definition.fields())
        {
            typeDeclaration.addField(fieldDefinition.name(), fieldDefinition.type(), fieldDefinition.isConstant());
        }

        module.addType(std::move(typeDeclaration));
        return true;
    }

    bool IRLowerer::lowerGlobalConstant(const ConstantDefinition& definition, Module& module) noexcept
    {
        const auto* expression = definition.expression();
        if (expression == nullptr)
            return false;

        const auto constantValue = tryLowerConstantExpression(expression);
        if (!constantValue.has_value())
            return false;

        module.addGlobalConstant(GlobalConstantDeclaration{ definition.name(), definition.type(), constantValue.value() });
        m_globalTypes.insert_or_assign(definition.name(), definition.type());
        return true;
    }

    bool IRLowerer::lowerGlobalReference(const ConstantDefinition& definition, Module& module) noexcept
    {
        const auto* expression = stripGroupings(definition.expression());
        if (expression == nullptr || expression->kind() != NodeKind::UnaryExpression)
            return false;

        const auto* unaryExpression = static_cast<const UnaryExpression*>(expression);
        if (unaryExpression->unaryOperator() != UnaryOperatorKind::ReferenceOf)
            return false;

        const auto* operand = stripGroupings(unaryExpression->expression().get());
        if (operand->kind() != NodeKind::NameExpression)
            return false;

        const auto& targetName = static_cast<const NameExpression*>(operand)->name();
        if (m_globalTypes.find(targetName) == m_globalTypes.end())
            return false;

        module.addGlobalReference(GlobalReferenceDeclaration{ definition.name(), definition.type(), targetName });
        m_globalTypes.insert_or_assign(definition.name(), definition.type());
        return true;
    }

    bool IRLowerer::registerConstructedGlobal(const ConstantDefinition& definition, Module& module) noexcept
    {
        if (!definition.isInit() && !IsConstructorCall(definition.expression()))
            return false;

        if (definition.type() == Type::Undefined())
            return false;

        module.addConstructedGlobal(ConstructedGlobalDeclaration{ definition.name(), definition.type() });
        m_globalTypes.insert_or_assign(definition.name(), definition.type());
        return true;
    }

    bool IRLowerer::lowerGlobalInitializer(const std::vector<const ConstantDefinition*>& definitions, const std::vector<const Expression*>& discardEffects, Module& module) noexcept
    {
        resetState();

        const auto blockId = m_nextBlockId++;
        Function function{ FunctionId{ -1 }, GlobalInitializerName, {}, Type::Void() };
        function.addBlock(BasicBlock{ blockId, "entry", std::make_unique<ReturnTerminator>() });
        m_currentFunction = &function;
        m_currentBlock = function.tryGetBlock(blockId);

        for (const auto* definition : definitions)
        {
            const auto globalType = m_globalTypes.find(definition->name());
            if (globalType == m_globalTypes.end())
                return false;

            const auto globalAddress = emitGlobalAddress(definition->name(), globalType->second);
            if (!tryLowerConstructorCallIntoAddress(definition->expression(), globalAddress))
                return false;
        }

        for (const auto* discardExpression : discardEffects)
        {
            if (!lowerExpressionForEffect(discardExpression))
                return false;
        }

        module.setGlobalInit(std::move(function));
        m_currentFunction = nullptr;
        m_currentBlock = nullptr;
        return true;
    }

    bool IRLowerer::lowerFunctionDefinition(const FunctionDefinition& definition, const BlockNode* bodyNode, bool isExtern, Module& module) noexcept
    {
        resetState();

        auto returnType = Type::Void();
        if (!definition.returnTypes().empty())
        {
            returnType = definition.returnTypes().front();
        }

        std::vector<IRParameter> parameters;
        for (const auto& parameter : definition.parameters())
        {
            parameters.emplace_back(parameter.name(), parameter.type());
        }

        const auto& functionName = definition.fullName();
        const auto functionId = definition.type().id();
        if (isExtern)
        {
            registerExternDefinition(definition, module);
            return true;
        }

        auto* function = module.addFunction(Function{ functionId, functionName, parameters, returnType });
        if (m_emitEntryPoint)
        {
            // the C runtime calls caracalMain as the entry point, we need to rename the user's main
            if (functionName == EntryPointFunctionName)
                function->setSymbolName(CRuntimeEntrySymbolName);
            else if (functionName == UserMainFunctionName)
                function->setSymbolName(UserMainSymbolName);
        }
        auto blockId = m_nextBlockId++;
        function->addBlock(BasicBlock{ blockId, "entry", nullptr });
        m_currentFunction = function;
        m_currentBlock = function->tryGetBlock(blockId);
        m_currentReturnType = returnType;
        collectAddressTakenLocals(bodyNode);
        if (!lowerParameters(definition))
            return false;

        std::optional<BlockId> entryBlockId = blockId;
        if (!lowerBlock(bodyNode, *function, entryBlockId))
            return false;

        return ensureExitTerminator(*function, entryBlockId, returnType);
    }

    bool IRLowerer::ensureExitTerminator(Function& function, std::optional<BlockId> currentBlockId, Type returnType) noexcept
    {
        if (!currentBlockId.has_value())
            return true;

        auto* exitBlock = function.tryGetBlock(currentBlockId.value());
        if (exitBlock == nullptr)
            return false;

        if (exitBlock->hasTerminator())
            return true;

        if (returnType == Type::Void())
        {
            exitBlock->setTerminator(std::make_unique<ReturnTerminator>());
            return true;
        }

        exitBlock->setTerminator(std::make_unique<UnreachableTerminator>());
        return true;
    }

    void IRLowerer::collectAddressTakenLocals(const Statement* statement) noexcept
    {
        switch (statement->kind())
        {
            case NodeKind::BlockNode:
            {
                collectAddressTakenLocals(static_cast<const BlockNode*>(statement)->statements());
                return;
            }
            case NodeKind::VariableDeclaration:
            {
                collectAddressTakenLocals(static_cast<const VariableDeclaration*>(statement)->rightExpression().get());
                return;
            }
            case NodeKind::ConstantDeclaration:
            {
                collectAddressTakenLocals(static_cast<const ConstantDeclaration*>(statement)->rightExpression().get());
                return;
            }
            case NodeKind::AssignmentStatement:
            {
                const auto* assignment = static_cast<const AssignmentStatement*>(statement);
                collectAddressTakenLocals(assignment->leftExpression().get());
                collectAddressTakenLocals(assignment->rightExpression().get());
                return;
            }
            case NodeKind::ExpressionStatement:
            {
                collectAddressTakenLocals(static_cast<const ExpressionStatement*>(statement)->expression().get());
                return;
            }
            case NodeKind::IfStatement:
            {
                const auto* ifStatement = static_cast<const IfStatement*>(statement);
                collectAddressTakenLocals(ifStatement->condition().get());
                collectAddressTakenLocals(ifStatement->trueStatement().get());
                if (ifStatement->falseStatement().has_value())
                    collectAddressTakenLocals(ifStatement->falseStatement().value().get());
                return;
            }
            case NodeKind::WhileStatement:
            {
                const auto* whileStatement = static_cast<const WhileStatement*>(statement);
                collectAddressTakenLocals(whileStatement->condition().get());
                collectAddressTakenLocals(whileStatement->trueStatement().get());
                return;
            }
            case NodeKind::ReturnStatement:
            {
                const auto& expression = static_cast<const ReturnStatement*>(statement)->expression();
                if (expression.has_value())
                    collectAddressTakenLocals(expression.value().get());
                return;
            }
            default:
                return;
        }
    }

    void IRLowerer::collectAddressTakenLocals(const Expression* expression) noexcept
    {
        switch (expression->kind())
        {
            case NodeKind::GroupingExpression:
            {
                const auto* groupingExpression = static_cast<const GroupingExpression*>(expression);
                collectAddressTakenLocals(groupingExpression->expression().get());
                return;
            }
            case NodeKind::UnaryExpression:
            {
                const auto* unaryExpression = static_cast<const UnaryExpression*>(expression);
                const auto* operand = stripGroupings(unaryExpression->expression().get());
                if (unaryExpression->unaryOperator() == UnaryOperatorKind::ReferenceOf
                    && operand != nullptr
                    && operand->kind() == NodeKind::NameExpression)
                {
                    m_addressTakenLocals.insert(static_cast<const NameExpression*>(operand)->name());
                }

                collectAddressTakenLocals(unaryExpression->expression().get());
                return;
            }
            case NodeKind::BinaryExpression:
            {
                const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
                collectAddressTakenLocals(binaryExpression->leftExpression().get());
                collectAddressTakenLocals(binaryExpression->rightExpression().get());
                return;
            }
            case NodeKind::FunctionCallExpression:
            {
                const auto* functionCallExpression = static_cast<const FunctionCallExpression*>(expression);
                for (const auto& argument : functionCallExpression->arguments())
                    collectAddressTakenLocals(argument.value().get());
                return;
            }
            case NodeKind::MemberAccessExpression:
            {
                collectAddressTakenLocals(static_cast<const MemberAccessExpression*>(expression)->expression().get());
                return;
            }
            default:
                return;
        }
    }

    void IRLowerer::collectAddressTakenLocals(const std::vector<std::unique_ptr<Statement>>& statements) noexcept
    {
        for (const auto& statement : statements)
            collectAddressTakenLocals(statement.get());
    }

    std::optional<ValueRef> IRLowerer::allocateLocalSlot(std::string localName, Type valueType, std::optional<ValueRef> initialValue) noexcept
    {
        if (valueType == Type::Undefined())
            return std::nullopt;

        const auto localId = m_nextLocalSlotId++;
        m_currentBlock->addPrologueInstruction(std::make_unique<AllocateLocalInstruction>(localId, std::move(localName), valueType));

        const auto addressId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<AddressOfInstruction>(addressId, LocalSlotRef{ localId }, valueType.toReference()));

        if (initialValue.has_value())
            m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(initialValue.value(), ValueRef{ addressId }, valueType));

        return ValueRef{ addressId };
    }

    ValueRef IRLowerer::emitFieldAddress(ValueRef objectAddress, Type objectType, const std::string& fieldName, i32 fieldIndex, Type fieldType) noexcept
    {
        const auto temporaryId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<AddressOfFieldInstruction>(
            temporaryId,
            objectAddress,
            objectType,
            fieldName,
            fieldIndex,
            fieldType.toReference()));
        return ValueRef{ temporaryId };
    }

    void IRLowerer::registerRequiredExterns(Module& module) noexcept
    {
        for (const auto functionType : m_semanticContext.requiredExternFunctions())
        {
            registerExternDefinition(m_semanticContext.getFunctionDefinition(functionType), module);
        }
    }

    void IRLowerer::registerExternDefinition(const FunctionDefinition& definition, Module& module) noexcept
    {
        const auto functionId = definition.type().id();
        if (module.tryGetExternFunction(functionId) != nullptr)
            return;

        std::vector<IRParameter> parameters;
        for (const auto& parameter : definition.parameters())
        {
            parameters.emplace_back(parameter.name(), parameter.type());
        }

        auto returnType = Type::Void();
        if (!definition.returnTypes().empty())
        {
            returnType = definition.returnTypes().front();
        }

        module.addExternFunction(ExternFunction{ functionId, definition.fullName(), definition.symbolName(), parameters, returnType });
    }

    FunctionId IRLowerer::resolveExternFunctionId(const std::string& fullName) noexcept
    {
        if (const auto existing = m_externFunctionIdCache.find(fullName); existing != m_externFunctionIdCache.end())
            return existing->second;

        const auto methodType = m_semanticContext.tryGetExternFunctionByFullName(fullName);
        if (methodType == Type::Undefined())
            return FunctionId{ -1 };

        m_externFunctionIdCache.try_emplace(fullName, methodType.id());
        return methodType.id();
    }

    void IRLowerer::markPreludeFunction(Type functionType) noexcept
    {
        const auto& functionDefinition = m_semanticContext.getFunctionDefinition(functionType);
        if (!functionDefinition.symbolName().has_value()
            && m_semanticContext.isPreludeFunctionDefinition(functionType)
            && m_queuedPreludeFunctionIds.insert(functionDefinition.type().id()).second)
        {
            m_requiredPreludeFunctions.push_back(functionType);
        }
    }

    ValueRef IRLowerer::emitLoad(ValueRef address, Type valueType) noexcept
    {
        const auto temporaryId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<LoadValueInstruction>(temporaryId, address, valueType));
        return ValueRef{ temporaryId };
    }

    ValueRef IRLowerer::emitGlobalAddress(const std::string& name, Type valueType) noexcept
    {
        const auto temporaryId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<AddressOfGlobalInstruction>(temporaryId, name, valueType.toReference()));
        return ValueRef{ temporaryId };
    }

    std::optional<ValueRef> IRLowerer::tryGetGlobalAddress(const std::string& name) noexcept
    {
        const auto result = m_globalTypes.find(name);
        if (result == m_globalTypes.end())
            return std::nullopt;

        return emitGlobalAddress(name, result->second);
    }

    void IRLowerer::setLocalValue(std::string localName, ValueRef value, Type type) noexcept
    {
        m_locals.insert_or_assign(std::move(localName), LocalState{ value, type, LocalStorageKind::Value });
    }

    void IRLowerer::setAddressBackedLocal(std::string localName, ValueRef address, Type valueType) noexcept
    {
        m_locals.insert_or_assign(std::move(localName), LocalState{ address, valueType, LocalStorageKind::Address });
    }

    std::vector<std::string> IRLowerer::sortedDefinedLocalNames(const LocalStateMap& localValues) const noexcept
    {
        std::vector<std::string> names;
        names.reserve(localValues.size());
        for (const auto& [name, localState] : localValues)
        {
            if (localState.type == Type::Undefined())
                continue;

            names.push_back(name);
        }

        std::sort(names.begin(), names.end());
        return names;
    }

    std::optional<ValueRef> IRLowerer::tryGetAddressBackedLocal(const std::string& localName) const noexcept
    {
        const auto result = m_locals.find(localName);
        if (result == m_locals.end() || result->second.storageKind != LocalStorageKind::Address)
            return std::nullopt;

        return result->second.value;
    }

    bool IRLowerer::isLocalDefinedOnAllEdges(const std::vector<IncomingLocalValues>& incomingValues, const std::string& name) const noexcept
    {
        return std::all_of(
            incomingValues.begin(),
            incomingValues.end(),
            [&name](const IncomingLocalValues& incomingValue)
            {
                return incomingValue.values.contains(name);
            });
    }

    bool IRLowerer::localNeedsPhi(const std::vector<IncomingLocalValues>& incomingValues, const std::string& name, const LocalState& firstState) const noexcept
    {
        return std::any_of(
            incomingValues.begin() + 1,
            incomingValues.end(),
            [&name, &firstState](const IncomingLocalValues& incomingValue)
            {
                return incomingValue.values.at(name).value.id() != firstState.value.id();
            });
    }

    bool IRLowerer::lowerParameters(const FunctionDefinition& definition) noexcept
    {
        const auto& parameters = definition.parameters();
        for (size_t index = 0; index < parameters.size(); ++index)
        {
            const auto parameterId = m_nextTemporaryId++;
            const auto parameterType = parameters[index].type();
            m_currentBlock->addInstruction(std::make_unique<ParameterInstruction>(
                parameterId,
                static_cast<i32>(index),
                IRParameter{ parameters[index].name(), parameterType}));

            const auto& parameterName = parameters[index].name();
            const auto needsAddressStorage = (parameterType.isReference()
                || parameterType.kind() == TypeKind::Type
                || parameterType.kind() == TypeKind::FixedArray
                || m_addressTakenLocals.contains(parameterName));

            if (!needsAddressStorage)
            {
                setLocalValue(parameterName, ValueRef{ parameterId }, parameterType);
                continue;
            }

            if (parameterType.isReference())
            {
                setAddressBackedLocal(parameterName, ValueRef{ parameterId }, parameterType);
                continue;
            }

            const auto addressId = allocateLocalSlot(parameterName, parameterType, ValueRef{ parameterId });
            if (!addressId.has_value())
                return false;

            setAddressBackedLocal(parameterName, addressId.value(), parameterType);
        }

        return true;
    }

    bool IRLowerer::lowerStatement(const Statement* statement, Function& function, std::optional<BlockId>& currentBlockId) noexcept
    {
        if (!currentBlockId.has_value())
            return true;

        auto* currentBlock = function.tryGetBlock(currentBlockId.value());
        if (currentBlock == nullptr)
            return false;

        m_currentFunction = &function;
        m_currentBlock = currentBlock;

        switch (statement->kind())
        {
            case NodeKind::BlockNode:
            {
                const auto* blockNode = static_cast<const BlockNode*>(statement);
                return lowerBlock(blockNode, function, currentBlockId);
            }
            case NodeKind::VariableDeclaration:
            {
                const auto* variableDeclaration = static_cast<const VariableDeclaration*>(statement);
                if (!lowerLocalDeclaration(
                    variableDeclaration->leftExpression().get(),
                    variableDeclaration->rightExpression().get()))
                {
                    return false;
                }

                currentBlockId = m_currentBlock->id();
                return true;
            }
            case NodeKind::ConstantDeclaration:
            {
                const auto* constantDeclaration = static_cast<const ConstantDeclaration*>(statement);
                if (!lowerLocalDeclaration(
                    constantDeclaration->leftExpression().get(),
                    constantDeclaration->rightExpression().get()))
                {
                    return false;
                }

                currentBlockId = m_currentBlock->id();
                return true;
            }
            case NodeKind::AssignmentStatement:
            {
                const auto* assignmentStatement = static_cast<const AssignmentStatement*>(statement);
                if (!lowerAssignmentStatement(
                    assignmentStatement->leftExpression().get(),
                    assignmentStatement->rightExpression().get()))
                {
                    return false;
                }

                currentBlockId = m_currentBlock->id();
                return true;
            }
            case NodeKind::ExpressionStatement:
            {
                const auto* expressionStatement = static_cast<const ExpressionStatement*>(statement);
                if (!lowerExpressionStatement(expressionStatement))
                {
                    return false;
                }

                currentBlockId = m_currentBlock->id();
                return true;
            }
            case NodeKind::IfStatement:
            {
                const auto* ifStatement = static_cast<const IfStatement*>(statement);
                return lowerIfStatement(ifStatement, function, currentBlockId);
            }
            case NodeKind::WhileStatement:
            {
                const auto* whileStatement = static_cast<const WhileStatement*>(statement);
                return lowerWhileStatement(whileStatement, function, currentBlockId);
            }
            case NodeKind::BreakStatement:
            {
                return lowerBreakStatement(*currentBlock, currentBlockId);
            }
            case NodeKind::SkipStatement:
            {
                return lowerSkipStatement(*currentBlock, currentBlockId);
            }
            case NodeKind::ReturnStatement:
            {
                const auto* returnStatement = static_cast<const ReturnStatement*>(statement);
                return lowerReturnStatement(returnStatement, currentBlockId);
            }
            default:
            {
                return false;
            }
        }
    }

    bool IRLowerer::lowerBlock(const BlockNode* block, Function& function, std::optional<BlockId>& currentBlockId) noexcept
    {
        for (const auto& statement : block->statements())
        {
            if (!lowerStatement(statement.get(), function, currentBlockId))
                return false;
        }

        return true;
    }

    bool IRLowerer::lowerIfStatement(const IfStatement* statement, Function& function, std::optional<BlockId>& currentBlockId) noexcept
    {
        auto* currentBlock = TryGetCurrentBlock(function, currentBlockId);
        if (currentBlock == nullptr)
            return false;

        // copy the locals before branching so each path can be lowered from the same state
        const auto preBranchValues = m_locals;

        const auto conditionValue = lowerValueExpression(statement->condition().get());
        if (!conditionValue.has_value())
            return false;

        // short-circuit lowering may have moved emission into a merge block
        currentBlock = m_currentBlock;

        const auto trueId = m_nextBlockId++;
        function.addBlock(BasicBlock{ trueId, "if.true", nullptr });

        if (!statement->hasFalseBlock())
        {
            const auto continuationId = m_nextBlockId++;
            currentBlock->setTerminator(std::make_unique<BranchIfTerminator>(conditionValue.value(), trueId, continuationId));

            restoreLocalValues(preBranchValues);
            std::optional<BlockId> trueBlockId = trueId;
            if (!lowerStatement(statement->trueStatement().get(), function, trueBlockId))
                return false;
            const auto trueExitValues = m_locals;

            function.addBlock(BasicBlock{ continuationId, "if.continuation", nullptr });
            auto* continuationBlock = function.tryGetBlock(continuationId);
            if (continuationBlock == nullptr)
                return false;

            auto* trueBlock = TryGetCurrentBlock(function, trueBlockId);
            std::vector<IncomingLocalValues> continuationInputs;
            continuationInputs.push_back(IncomingLocalValues{ currentBlock->id(), preBranchValues });
            const auto needsContinuationJump = trueBlock != nullptr && !trueBlock->hasTerminator();
            if (needsContinuationJump)
            {
                trueBlock->setTerminator(std::make_unique<JumpTerminator>(continuationId));
                continuationInputs.push_back(IncomingLocalValues{ trueBlock->id(), trueExitValues });
            }

            mergeLocalValues(*continuationBlock, continuationInputs);

            currentBlockId = continuationId;
            return true;
        }

        const auto falseId = m_nextBlockId++;
        currentBlock->setTerminator(std::make_unique<BranchIfTerminator>(conditionValue.value(), trueId, falseId));

        // restpre values so later phi decisions are comparable
        restoreLocalValues(preBranchValues);
        std::optional<BlockId> trueBlockId = trueId;
        if (!lowerStatement(statement->trueStatement().get(), function, trueBlockId))
            return false;
        const auto trueExitValues = m_locals;

        function.addBlock(BasicBlock{ falseId, "if.false", nullptr });

        restoreLocalValues(preBranchValues);
        std::optional<BlockId> falseBlockId = falseId;
        if (!lowerStatement(statement->falseStatement().value().get(), function, falseBlockId))
            return false;
        const auto falseExitValues = m_locals;

        auto* trueBlock = TryGetCurrentBlock(function, trueBlockId);
        auto* falseBlock = TryGetCurrentBlock(function, falseBlockId);
        const auto trueFallsThrough = trueBlock != nullptr && !trueBlock->hasTerminator();
        const auto falseFallsThrough = falseBlock != nullptr && !falseBlock->hasTerminator();
        if (!trueFallsThrough && !falseFallsThrough)
        {
            currentBlockId.reset();
            return true;
        }

        const auto continuationId = m_nextBlockId++;
        function.addBlock(BasicBlock{ continuationId, "if.continuation", nullptr });
        auto* continuationBlock = function.tryGetBlock(continuationId);
        if (continuationBlock == nullptr)
            return false;

        std::vector<IncomingLocalValues> continuationInputs;
        if (trueFallsThrough)
        {
            trueBlock->setTerminator(std::make_unique<JumpTerminator>(continuationId));
            continuationInputs.push_back(IncomingLocalValues{ trueBlock->id(), trueExitValues });
        }
        if (falseFallsThrough)
        {
            falseBlock->setTerminator(std::make_unique<JumpTerminator>(continuationId));
            continuationInputs.push_back(IncomingLocalValues{ falseBlock->id(), falseExitValues });
        }

        mergeLocalValues(*continuationBlock, continuationInputs);

        currentBlockId = continuationId;
        return true;
    }

    bool IRLowerer::lowerWhileStatement(const WhileStatement* statement, Function& function, std::optional<BlockId>& currentBlockId) noexcept
    {
        auto* currentBlock = TryGetCurrentBlock(function, currentBlockId);
        if (currentBlock == nullptr)
            return false;

        // copy the locals before branching so loop condition can be lowered from the same state as the body
        const auto preLoopValues = m_locals;
        const auto conditionId = m_nextBlockId++;
        const auto loopId = m_nextBlockId++;
        const auto continuationId = m_nextBlockId++;

        currentBlock->setTerminator(std::make_unique<JumpTerminator>(conditionId));

        function.addBlock(BasicBlock{ conditionId, "while.condition", nullptr });
        auto* conditionBlock = function.tryGetBlock(conditionId);
        if (conditionBlock == nullptr)
            return false;

        const auto headerNames = sortedDefinedLocalNames(preLoopValues);

        LocalStateMap loopHeaderValues;
        loopHeaderValues.reserve(headerNames.size());
        std::vector<LoopHeaderPhi> loopHeaderPhis;
        loopHeaderPhis.reserve(headerNames.size());
        for (const auto& name : headerNames)
        {
            const auto& localState = preLoopValues.at(name);
            if (localState.storageKind == LocalStorageKind::Address)
            {
                loopHeaderValues.emplace(name, localState);
                continue;
            }

            std::vector<PhiInput> phiInputs;
            phiInputs.emplace_back(currentBlock->id(), localState.value);
            
            const auto phiId = m_nextTemporaryId++;
            auto phiInstruction = std::make_unique<PhiInstruction>(phiId, std::move(phiInputs), localState.type);
            auto* phiInstructionPtr = phiInstruction.get();
            conditionBlock->addInstruction(std::move(phiInstruction));

            loopHeaderValues.emplace(name, LocalState{ ValueRef{ phiId }, localState.type, LocalStorageKind::Value });
            loopHeaderPhis.push_back(LoopHeaderPhi{ name, phiInstructionPtr });
        }

        restoreLocalValues(loopHeaderValues);

        m_currentBlock = conditionBlock;
        const auto conditionValue = lowerValueExpression(statement->condition().get());
        if (!conditionValue.has_value())
            return false;

        // short-circuit lowering may have moved emission into a merge block
        auto* conditionExitBlock = m_currentBlock;
        conditionExitBlock->setTerminator(std::make_unique<BranchIfTerminator>(conditionValue.value(), loopId, continuationId));

        function.addBlock(BasicBlock{ loopId, "while.body", nullptr });

        // add loop exit targets so break statements can contribute values for continuation phis
        m_loopContexts.push_back(LoopContext{ conditionId, continuationId, {}, {} });
        restoreLocalValues(loopHeaderValues);

        std::optional<BlockId> loopBlockId = loopId;
        const auto loweredBody = lowerStatement(statement->trueStatement().get(), function, loopBlockId);

        // copy the values added by any breaks before dropping the loop context
        auto conditionInputs = m_loopContexts.back().conditionInputs;
        auto continuationInputs = m_loopContexts.back().continuationInputs;
        m_loopContexts.pop_back();
        if (!loweredBody)
            return false;

        auto* loopBlock = TryGetCurrentBlock(function, loopBlockId);
        if (loopBlock != nullptr && !loopBlock->hasTerminator())
        {
            conditionInputs.push_back(IncomingLocalValues{ loopBlock->id(), m_locals });
            loopBlock->setTerminator(std::make_unique<JumpTerminator>(conditionId));
        }

        for (const auto& loopHeaderPhi : loopHeaderPhis)
        {
            std::vector<PhiInput> phiInputs;
            phiInputs.reserve(1 + conditionInputs.size());
            phiInputs.emplace_back(currentBlock->id(), preLoopValues.at(loopHeaderPhi.name).value);
            for (const auto& conditionInput : conditionInputs)
            {
                phiInputs.emplace_back(conditionInput.predecessorBlockId, conditionInput.values.at(loopHeaderPhi.name).value);
            }

            loopHeaderPhi.instruction->setInputs(std::move(phiInputs));
        }

        function.addBlock(BasicBlock{ continuationId, "while.continuation", nullptr });
        auto* continuationBlock = function.tryGetBlock(continuationId);
        if (continuationBlock == nullptr)
            return false;

        // condition-false edge reaches the continuation with the bindings visible at the loop header
        continuationInputs.insert(continuationInputs.begin(), IncomingLocalValues{ conditionExitBlock->id(), loopHeaderValues });
        mergeLocalValues(*continuationBlock, continuationInputs);
        currentBlockId = continuationId;

        return true;
    }

    bool IRLowerer::lowerBreakStatement(BasicBlock& block, std::optional<BlockId>& currentBlockId) noexcept
    {
        if (m_loopContexts.empty())
            return false;

        auto& loopContext = m_loopContexts.back();
        loopContext.continuationInputs.push_back(IncomingLocalValues{ block.id(), m_locals });
        block.setTerminator(std::make_unique<JumpTerminator>(loopContext.continuationBlockId));
        currentBlockId.reset();

        return true;
    }

    bool IRLowerer::lowerSkipStatement(BasicBlock& block, std::optional<BlockId>& currentBlockId) noexcept
    {
        if (m_loopContexts.empty())
            return false;

        auto& loopContext = m_loopContexts.back();
        loopContext.conditionInputs.push_back(IncomingLocalValues{ block.id(), m_locals });
        block.setTerminator(std::make_unique<JumpTerminator>(loopContext.conditionBlockId));
        currentBlockId.reset();

        return true;
    }

    bool IRLowerer::lowerExpressionStatement(const ExpressionStatement* statement) noexcept
    {
        return lowerExpressionForEffect(statement->expression().get());
    }

    bool IRLowerer::lowerLocalDeclaration(const Expression* leftExpression, const Expression* rightExpression) noexcept
    {
        if (leftExpression->kind() == NodeKind::DiscardLiteral)
        {
            return lowerExpressionForEffect(rightExpression);
        }

        if (leftExpression->kind() != NodeKind::NameExpression)
            return false;

        const auto* nameExpression = static_cast<const NameExpression*>(leftExpression);
        const auto* referenceCandidate = stripGroupings(rightExpression);
        const auto isExplicitReferenceBinding =
            (referenceCandidate->kind() == NodeKind::UnaryExpression
            && static_cast<const UnaryExpression*>(referenceCandidate)->unaryOperator() == UnaryOperatorKind::ReferenceOf);

        if (nameExpression->type().isReference() && isExplicitReferenceBinding)
        {
            const auto loweredAddress = lowerAddressExpression(rightExpression);
            if (!loweredAddress.has_value())
                return false;

            setAddressBackedLocal(nameExpression->name(), loweredAddress.value(), nameExpression->type());

            if (static_cast<const UnaryExpression*>(referenceCandidate)->referencesConstant())
                m_locals.at(nameExpression->name()).referencesConstant = true;

            return true;
        }

        const auto needsAddressStorage = m_addressTakenLocals.contains(nameExpression->name());
        if (nameExpression->type().kind() == TypeKind::Type
            || nameExpression->type().kind() == TypeKind::FixedArray
            || nameExpression->type().kind() == TypeKind::DynamicArray)
        {
            const auto addressId = allocateSlotFromExpression(
                nameExpression->name(),
                rightExpression,
                nameExpression->type().toValue());
            if (!addressId.has_value())
                return false;

            setAddressBackedLocal(nameExpression->name(), addressId.value(), nameExpression->type().toValue());
            return true;
        }

        auto localType = nameExpression->type();
        if (localType.isReference())
            localType = localType.toValue();

        const auto loweredValue = lowerValueExpressionExpecting(rightExpression, localType);
        if (!loweredValue.has_value())
            return false;

        if (needsAddressStorage)
        {
            const auto addressId = allocateLocalSlot(nameExpression->name(), localType, loweredValue.value());
            if (!addressId.has_value())
                return false;

            setAddressBackedLocal(nameExpression->name(), addressId.value(), localType);
            return true;
        }

        setLocalValue(nameExpression->name(), loweredValue.value(), localType);

        return true;
    }

    bool IRLowerer::tryLowerConstructorCallIntoAddress(const Expression* expression, ValueRef destinationAddress) noexcept
    {
        expression = stripGroupings(expression);
        if (expression == nullptr || expression->kind() != NodeKind::BinaryExpression)
            return false;

        const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
        if (binaryExpression->binaryOperator() != BinaryOperatorKind::ConstructorCall)
            return false;

        if (binaryExpression->rightExpression()->kind() != NodeKind::FunctionCallExpression)
            return false;

        const auto* functionCallExpression = static_cast<const FunctionCallExpression*>(binaryExpression->rightExpression().get());
        const auto loweredResult = emitCall(functionCallExpression, destinationAddress);
        return loweredResult.has_value();
    }

    std::optional<ValueRef> IRLowerer::allocateSlotFromExpression(std::string localName, const Expression* expression, Type valueType) noexcept
    {
        const auto addressId = allocateLocalSlot(std::move(localName), valueType);
        if (!addressId.has_value())
            return std::nullopt;

        if (tryLowerConstructorCallIntoAddress(expression, addressId.value()))
            return addressId;

        if (tryLowerArrayLiteralIntoAddress(expression, addressId.value(), valueType))
            return addressId;

        if (tryLowerStringLiteralIntoAddress(expression, addressId.value()))
            return addressId;

        const auto loweredValue = lowerValueExpressionExpecting(expression, valueType);
        if (!loweredValue.has_value())
            return std::nullopt;

        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(
            loweredValue.value(),
            addressId.value(),
            valueType));
        return addressId;
    }

    void IRLowerer::emitDynamicArrayDescriptor(ValueRef destinationAddress, Type arrayType, ValueRef dataPointer, i32 length, i32 capacity) noexcept
    {
        const auto elementType = m_semanticContext.getArrayElementType(arrayType);

        const auto dataFieldAddress = emitFieldAddress(destinationAddress, arrayType, "data", 0, elementType.toReference());
        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(
            dataPointer,
            dataFieldAddress,
            elementType.toReference()));

        const auto lengthId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
            lengthId,
            ConstantValue::FromI32(length),
            m_semanticContext.wellKnown().i32));
        const auto lengthFieldAddress = emitFieldAddress(destinationAddress, arrayType, "length", 1, m_semanticContext.wellKnown().i32);
        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(
            ValueRef{ lengthId },
            lengthFieldAddress,
            m_semanticContext.wellKnown().i32));

        const auto capacityId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
            capacityId,
            ConstantValue::FromI32(capacity),
            m_semanticContext.wellKnown().i32));
        const auto capacityFieldAddress = emitFieldAddress(destinationAddress, arrayType, "capacity", 2, m_semanticContext.wellKnown().i32);
        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(
            ValueRef{ capacityId },
            capacityFieldAddress,
            m_semanticContext.wellKnown().i32));
    }

    bool IRLowerer::tryLowerStringLiteralIntoAddress(const Expression* expression, ValueRef destinationAddress) noexcept
    {
        const auto* stripped = stripGroupings(expression);
        if (stripped == nullptr || stripped->kind() != NodeKind::StringLiteral)
            return false;

        const auto stringType = m_semanticContext.wellKnown().string;
        if (stripped->type() != stringType)
            return false;

        const auto* literal = static_cast<const StringLiteral*>(stripped);
        const auto& typeDefinition = m_semanticContext.getTypeDefinition(stringType);
        const auto& bytesField = typeDefinition.tryGetFieldByName("_bytes");
        if (bytesField.type() == Type::Undefined())
            return false;

        const auto bytesType = bytesField.type();
        const auto bytesAddress = emitFieldAddress(destinationAddress, stringType, "_bytes", 0, bytesType);

        // the literal bytes live in a NUL-terminated data global, the descriptor borrows it without allocating
        const auto dataId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
            dataId,
            ConstantValue::FromString(literal->escapedContent()),
            m_semanticContext.wellKnown().cstring));

        const auto length = static_cast<i32>(literal->escapedContent().size());
        emitDynamicArrayDescriptor(bytesAddress, bytesType, ValueRef{ dataId }, length, length);

        return true;
    }

    bool IRLowerer::tryLowerArrayLiteralIntoAddress(const Expression* expression, ValueRef destinationAddress, Type arrayType) noexcept
    {
        const auto* stripped = stripGroupings(expression);
        if (stripped == nullptr || stripped->kind() != NodeKind::ArrayLiteral)
            return false;

        if (arrayType.kind() == TypeKind::DynamicArray)
            return lowerDynamicArrayLiteralIntoAddress(static_cast<const ArrayLiteral*>(stripped), destinationAddress, arrayType);

        if (arrayType.kind() != TypeKind::FixedArray)
            return false;

        const auto* literal = static_cast<const ArrayLiteral*>(stripped);
        const auto elementType = m_semanticContext.getArrayElementType(arrayType);
        const auto& elements = literal->elements();
        for (size_t index = 0; index < elements.size(); ++index)
        {
            const auto indexId = m_nextTemporaryId++;
            m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                indexId,
                ConstantValue::FromI32(static_cast<i32>(index)),
                m_semanticContext.wellKnown().i32));

            const auto elementAddressId = m_nextTemporaryId++;
            m_currentBlock->addInstruction(std::make_unique<ElementAddressInstruction>(
                elementAddressId,
                destinationAddress,
                arrayType,
                ValueRef{ indexId },
                elementType));

            // nested literals fill their element storage directly instead of through a temp slot
            if (tryLowerArrayLiteralIntoAddress(elements[index].get(), ValueRef{ elementAddressId }, elementType))
                continue;

            const auto elementValue = lowerValueExpressionExpecting(elements[index].get(), elementType);
            if (!elementValue.has_value())
                return false;

            m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(
                elementValue.value(),
                ValueRef{ elementAddressId },
                elementType));
        }

        return true;
    }

    bool IRLowerer::lowerDynamicArrayLiteralIntoAddress(const ArrayLiteral* literal, ValueRef destinationAddress, Type arrayType) noexcept
    {
        const auto elementType = m_semanticContext.getArrayElementType(arrayType);
        const auto count = static_cast<i32>(literal->elements().size());
        auto capacity = count;
        if (capacity < 8)
        {
            capacity = 8;
        }

        // calloc(capacity, sizeof(element)) provides the zero-initialized backing buffer
        const auto capacityArgumentId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
            capacityArgumentId,
            ConstantValue::FromI64(capacity),
            m_semanticContext.wellKnown().i64));

        const auto elementSizeId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<SizeOfInstruction>(
            elementSizeId,
            elementType,
            m_semanticContext.wellKnown().i64));

        const auto callocId = resolveExternFunctionId("C.calloc");

        const auto dataPointerId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<CallInstruction>(
            dataPointerId,
            callocId,
            std::vector<ValueRef>{ ValueRef{ capacityArgumentId }, ValueRef{ elementSizeId } },
            m_semanticContext.wellKnown().rawptr));

        emitDynamicArrayDescriptor(destinationAddress, arrayType, ValueRef{ dataPointerId }, count, capacity);

        const auto& elements = literal->elements();
        for (size_t index = 0; index < elements.size(); ++index)
        {
            const auto indexId = m_nextTemporaryId++;
            m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                indexId,
                ConstantValue::FromI32(static_cast<i32>(index)),
                m_semanticContext.wellKnown().i32));

            const auto elementAddressId = m_nextTemporaryId++;
            m_currentBlock->addInstruction(std::make_unique<ElementAddressInstruction>(
                elementAddressId,
                ValueRef{ dataPointerId },
                arrayType,
                ValueRef{ indexId },
                elementType));

            // nested literals fill their element storage directly instead of through a temp slot
            if (tryLowerArrayLiteralIntoAddress(elements[index].get(), ValueRef{ elementAddressId }, elementType))
                continue;

            const auto elementValue = lowerValueExpressionExpecting(elements[index].get(), elementType);
            if (!elementValue.has_value())
                return false;

            m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(
                elementValue.value(),
                ValueRef{ elementAddressId },
                elementType));
        }

        return true;
    }

    bool IRLowerer::lowerAssignmentStatement(const Expression* leftExpression, const Expression* rightExpression) noexcept
    {
        if (leftExpression->kind() == NodeKind::DiscardLiteral)
        {
            return lowerExpressionForEffect(rightExpression);
        }

        // handle local variables first, since they may be address-backed and we want to avoid unnecessary loads/stores
        if (leftExpression->kind() == NodeKind::NameExpression)
        {
            const auto local = m_locals.find(static_cast<const NameExpression*>(leftExpression)->name());
            if (local != m_locals.end())
            {
                const auto loweredValue = lowerValueExpressionExpecting(rightExpression, local->second.type.toValue());
                if (!loweredValue.has_value())
                    return false;

                if (local->second.storageKind == LocalStorageKind::Address)
                    m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(loweredValue.value(), local->second.value, local->second.type.toValue()));
                else
                    local->second.value = loweredValue.value();

                return true;
            }
        }

        if (IsConstructorCall(rightExpression))
        {
            const auto destinationAddress = lowerAddressExpression(leftExpression);
            if (!destinationAddress.has_value())
                return false;

            return tryLowerConstructorCallIntoAddress(rightExpression, destinationAddress.value());
        }

        const auto* strippedRight = stripGroupings(rightExpression);
        if (strippedRight != nullptr
            && strippedRight->kind() == NodeKind::StringLiteral
            && strippedRight->type() == m_semanticContext.wellKnown().string)
        {
            const auto destinationAddress = lowerAddressExpression(leftExpression);
            if (!destinationAddress.has_value())
                return false;

            return tryLowerStringLiteralIntoAddress(rightExpression, destinationAddress.value());
        }

        const auto loweredValue = lowerValueExpressionExpecting(rightExpression, leftExpression->type().toValue());
        if (!loweredValue.has_value())
            return false;

        const auto destinationAddress = lowerAddressExpression(leftExpression);
        if (!destinationAddress.has_value())
            return false;

        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(
            loweredValue.value(),
            destinationAddress.value(),
            leftExpression->type().toValue()));
        return true;
    }

    std::optional<ValueRef> IRLowerer::spillValueToTempSlot(const Expression* expression, ValueRef value) noexcept
    {
        const auto valueType = expression->type().toValue();
        return allocateLocalSlot("temp", valueType, value);
    }

    std::optional<ValueRef> IRLowerer::lowerValueExpressionExpecting(const Expression* expression, Type targetType, bool copyOwningDynamic) noexcept
    {
        const auto sourceType = expression->type().toValue();

        // value semantics: binding, assigning or passing an existing owning dynamic array deep-copies it
        if (copyOwningDynamic
            && targetType.kind() == TypeKind::DynamicArray
            && !targetType.isReference()
            && sourceType.kind() == TypeKind::DynamicArray
            && stripGroupings(expression)->kind() == NodeKind::NameExpression)
        {
            auto sourceAddress = lowerAddressExpression(expression);
            if (!sourceAddress.has_value())
            {
                const auto sourceValue = lowerValueExpression(expression);
                if (!sourceValue.has_value())
                    return std::nullopt;

                sourceAddress = spillValueToTempSlot(expression, sourceValue.value());
                if (!sourceAddress.has_value())
                    return std::nullopt;
            }

            const auto copyId = m_nextTemporaryId++;
            m_currentBlock->addInstruction(std::make_unique<ArrayCopyInstruction>(
                copyId,
                sourceAddress.value(),
                targetType,
                resolveExternFunctionId("C.calloc"),
                resolveExternFunctionId("C.memmove"),
                m_reserveNulOnArrayCopy));
            return ValueRef{ copyId };
        }

        // integers widen one way within the same signedness, the checker already validated the coercion
        if (sourceType.isBaseType() && targetType.isBaseType() && sourceType != targetType)
        {
            const auto* sourceDescription = m_semanticContext.tryGetBuiltinTypeDescription(sourceType);
            const auto* targetDescription = m_semanticContext.tryGetBuiltinTypeDescription(targetType);
            if (sourceDescription != nullptr && targetDescription != nullptr
                && sourceDescription->kind == BuiltinTypeKind::Int
                && targetDescription->kind == BuiltinTypeKind::Int
                && sourceDescription->bits < targetDescription->bits)
            {
                const auto loweredValue = lowerValueExpression(expression);
                if (!loweredValue.has_value())
                    return std::nullopt;

                const auto widenedId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<IntWidenInstruction>(
                    widenedId,
                    loweredValue.value(),
                    sourceType,
                    targetType));
                return ValueRef{ widenedId };
            }
        }

        return lowerValueExpression(expression);
    }

    std::optional<ValueRef> IRLowerer::lowerMethodReceiverAddress(const Expression* receiverExpression) noexcept
    {
        if (receiverExpression == nullptr)
            return tryGetAddressBackedLocal(ImplicitThisName);

        auto receiverAddress = lowerAddressExpression(receiverExpression);
        if (receiverAddress.has_value())
            return receiverAddress;

        const auto receiverValue = lowerValueExpression(receiverExpression);
        if (!receiverValue.has_value())
            return std::nullopt;

        return spillValueToTempSlot(receiverExpression, receiverValue.value());
    }

    std::optional<ValueRef> IRLowerer::lowerCallWithReceiver(const FunctionCallExpression* expression, const Expression* receiverExpression) noexcept
    {
        const auto functionType = expression->functionType();
        if (functionType == Type::Undefined())
            return std::nullopt;

        const auto& functionDefinition = m_semanticContext.getFunctionDefinition(functionType);
        if (functionDefinition.functionType() == FunctionType::Intrinsic)
        {
            if (functionDefinition.intrinsicKind() == IntrinsicKind::ArrayAt
                || functionDefinition.intrinsicKind() == IntrinsicKind::ArraySet
                || functionDefinition.intrinsicKind() == IntrinsicKind::ArraySlice
                || functionDefinition.intrinsicKind() == IntrinsicKind::ArrayAdd
                || functionDefinition.intrinsicKind() == IntrinsicKind::ArrayRemove
                || functionDefinition.intrinsicKind() == IntrinsicKind::StringToCString)
            {
                return lowerArrayIntrinsicCall(expression, receiverExpression, functionDefinition);
            }

            return lowerBitwiseIntrinsicCall(expression, functionDefinition);
        }

        if (functionDefinition.functionType() != FunctionType::PublicMethod
            && functionDefinition.functionType() != FunctionType::PrivateMethod)
        {
            return emitCall(expression);
        }

        const auto receiverAddress = lowerMethodReceiverAddress(receiverExpression);
        if (!receiverAddress.has_value())
            return std::nullopt;

        return emitCall(expression, receiverAddress.value());
    }

    std::optional<ValueRef> IRLowerer::lowerArrayIntrinsicCall(const FunctionCallExpression* expression, const Expression* receiverExpression, const FunctionDefinition& functionDefinition) noexcept
    {
        if (receiverExpression == nullptr)
            return std::nullopt;

        if (functionDefinition.intrinsicKind() == IntrinsicKind::ArrayAt)
        {
            const auto elementAddress = lowerElementAddressForCall(receiverExpression, expression);
            if (!elementAddress.has_value())
                return std::nullopt;

            return emitLoad(elementAddress.value(), expression->type().toValue());
        }

        if (functionDefinition.intrinsicKind() == IntrinsicKind::ArraySet)
        {
            const auto elementAddress = lowerElementAddressForCall(receiverExpression, expression);
            if (!elementAddress.has_value())
                return std::nullopt;

            const auto& orderedArguments = expression->orderedArguments();
            if (orderedArguments.size() < 2)
                return std::nullopt;

            const auto value = lowerValueExpression(orderedArguments[1]);
            if (!value.has_value())
                return std::nullopt;

            const auto elementType = functionDefinition.parameters()[2].type();
            m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(
                value.value(),
                elementAddress.value(),
                elementType));
            return ValueRef{};
        }

        if (functionDefinition.intrinsicKind() == IntrinsicKind::ArrayAdd
            || functionDefinition.intrinsicKind() == IntrinsicKind::ArrayRemove)
        {
            const auto receiverType = receiverExpression->type().toValue();
            const auto receiverAddress = lowerMethodReceiverAddress(receiverExpression);
            if (!receiverAddress.has_value())
                return std::nullopt;

            const auto& orderedArguments = expression->orderedArguments();
            if (orderedArguments.empty())
                return std::nullopt;

            const auto argumentType = functionDefinition.parameters()[1].type();
            const auto argument = lowerValueExpressionExpecting(orderedArguments[0], argumentType);
            if (!argument.has_value())
                return std::nullopt;

            if (functionDefinition.intrinsicKind() == IntrinsicKind::ArrayAdd)
            {
                m_currentBlock->addInstruction(std::make_unique<ArrayAddInstruction>(
                    receiverAddress.value(),
                    argument.value(),
                    receiverType,
                    resolveExternFunctionId("C.realloc")));
            }
            else
            {
                m_currentBlock->addInstruction(std::make_unique<ArrayRemoveInstruction>(
                    receiverAddress.value(),
                    argument.value(),
                    receiverType,
                    resolveExternFunctionId("C.memmove")));
            }
            return ValueRef{};
        }

        if (functionDefinition.intrinsicKind() == IntrinsicKind::StringToCString)
        {
            const auto receiverType = receiverExpression->type().toValue();
            const auto receiverAddress = lowerMethodReceiverAddress(receiverExpression);
            if (!receiverAddress.has_value())
                return std::nullopt;

            const auto& typeDefinition = m_semanticContext.getTypeDefinition(receiverType);
            const auto& bytesField = typeDefinition.tryGetFieldByName("_bytes");
            if (bytesField.type() == Type::Undefined())
                return std::nullopt;

            const auto elementType = m_semanticContext.getArrayElementType(bytesField.type());
            const auto bytesAddress = emitFieldAddress(receiverAddress.value(), receiverType, "_bytes", 0, bytesField.type());
            const auto dataPointerAddress = emitFieldAddress(bytesAddress, bytesField.type(), "data", 0, elementType.toReference());
            return emitLoad(dataPointerAddress, m_semanticContext.wellKnown().cstring);
        }

        if (functionDefinition.intrinsicKind() == IntrinsicKind::ArraySlice)
        {
            const auto receiverType = receiverExpression->type().toValue();
            const auto receiverAddress = lowerMethodReceiverAddress(receiverExpression);
            if (!receiverAddress.has_value())
                return std::nullopt;

            auto baseAddress = receiverAddress.value();
            auto sourceArrayType = receiverType;
            if (receiverType.kind() == TypeKind::Type)
            {
                // a string's view borrows its private _bytes storage
                const auto& typeDefinition = m_semanticContext.getTypeDefinition(receiverType);
                const auto& bytesField = typeDefinition.tryGetFieldByName("_bytes");
                if (bytesField.type() == Type::Undefined())
                    return std::nullopt;

                baseAddress = emitFieldAddress(baseAddress, receiverType, "_bytes", 0, bytesField.type());
                sourceArrayType = bytesField.type();
            }

            auto length = ValueRef{};
            if (sourceArrayType.kind() == TypeKind::FixedArray)
            {
                const auto lengthId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                    lengthId,
                    ConstantValue::FromI32(m_semanticContext.getArrayLength(sourceArrayType)),
                    m_semanticContext.wellKnown().i32));
                length = ValueRef{ lengthId };
            }
            else
            {
                // a dynamic array's slice view loads the data pointer and the runtime length
                const auto elementType = m_semanticContext.getArrayElementType(sourceArrayType);
                const auto dataPointerAddress = emitFieldAddress(baseAddress, sourceArrayType, "data", 0, elementType.toReference());
                const auto arrayAddress = baseAddress;
                baseAddress = emitLoad(dataPointerAddress, elementType.toReference());
                const auto lengthAddress = emitFieldAddress(arrayAddress, sourceArrayType, ArrayLengthMemberName, 1, m_semanticContext.wellKnown().i32);
                length = emitLoad(lengthAddress, m_semanticContext.wellKnown().i32);
            }

            const auto sliceId = m_nextTemporaryId++;
            m_currentBlock->addInstruction(std::make_unique<MakeSliceInstruction>(
                sliceId,
                baseAddress,
                length,
                expression->type().toValue()));
            return ValueRef{ sliceId };
        }

        return std::nullopt;
    }

    [[nodiscard]] static const BinaryExpression* TryGetAtIntrinsicCall(const Expression* expression, SemanticContext& semanticContext) noexcept
    {
        const auto* stripped = stripGroupings(expression);
        if (stripped == nullptr || stripped->kind() != NodeKind::BinaryExpression)
            return nullptr;

        const auto* binaryExpression = static_cast<const BinaryExpression*>(stripped);
        if (binaryExpression->binaryOperator() != BinaryOperatorKind::MemberAccess)
            return nullptr;

        if (binaryExpression->rightExpression()->kind() != NodeKind::FunctionCallExpression)
            return nullptr;

        const auto* call = static_cast<const FunctionCallExpression*>(binaryExpression->rightExpression().get());
        if (call->functionType() == Type::Undefined())
            return nullptr;

        const auto& definition = semanticContext.getFunctionDefinition(call->functionType());
        if (definition.intrinsicKind() != IntrinsicKind::ArrayAt)
            return nullptr;

        return binaryExpression;
    }

    std::optional<ValueRef> IRLowerer::lowerBitwiseIntrinsicCall(const FunctionCallExpression* expression, const FunctionDefinition& functionDefinition) noexcept
    {
        const auto& orderedArguments = expression->orderedArguments();
        if (orderedArguments.empty())
            return std::nullopt;

        const auto resultType = expression->type().toValue();
        const auto firstValue = lowerValueExpression(orderedArguments[0]);
        if (!firstValue.has_value())
            return std::nullopt;

        const auto intrinsicKind = functionDefinition.intrinsicKind();
        if (intrinsicKind == IntrinsicKind::BitNot)
        {
            const auto resultId = m_nextTemporaryId++;
            m_currentBlock->addInstruction(std::make_unique<BitNotInstruction>(resultId, firstValue.value(), resultType));
            return ValueRef{ resultId };
        }

        if (orderedArguments.size() < 2)
            return std::nullopt;

        const auto secondValue = lowerValueExpression(orderedArguments[1]);
        if (!secondValue.has_value())
            return std::nullopt;

        switch (intrinsicKind)
        {
            case IntrinsicKind::BitAnd:
            {
                const auto resultId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<BitAndInstruction>(resultId, firstValue.value(), secondValue.value(), resultType));
                return ValueRef{ resultId };
            }
            case IntrinsicKind::BitOr:
            {
                const auto resultId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<BitOrInstruction>(resultId, firstValue.value(), secondValue.value(), resultType));
                return ValueRef{ resultId };
            }
            case IntrinsicKind::BitXor:
            {
                const auto resultId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<BitXorInstruction>(resultId, firstValue.value(), secondValue.value(), resultType));
                return ValueRef{ resultId };
            }
            case IntrinsicKind::ShiftLeft:
            case IntrinsicKind::ShiftRight:
            {
                // mask the amount so out of range shifts stay defined instead of poisoning the result
                auto maskValue = 31;
                if (const auto* description = m_semanticContext.tryGetBuiltinTypeDescription(resultType))
                {
                    if ((description->bits & (description->bits - 1)) != 0)
                    {
                        // bits - 1 only equals modulo for power of two widths, this needs a remainder instruction instead
                        TODO("Shift amount masking is not implemented for non power of two widths");
                    }

                    maskValue = description->bits - 1;
                }

                const auto maskId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                    maskId,
                    ConstantValue::FromI32(maskValue),
                    m_semanticContext.wellKnown().i32));

                const auto maskedAmountId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<BitAndInstruction>(maskedAmountId, secondValue.value(), ValueRef{ maskId }, m_semanticContext.wellKnown().i32));

                const auto resultId = m_nextTemporaryId++;
                if (intrinsicKind == IntrinsicKind::ShiftLeft)
                {
                    m_currentBlock->addInstruction(std::make_unique<ShiftLeftInstruction>(resultId, firstValue.value(), ValueRef{ maskedAmountId }, resultType));
                }
                else
                {
                    m_currentBlock->addInstruction(std::make_unique<ShiftRightInstruction>(resultId, firstValue.value(), ValueRef{ maskedAmountId }, resultType));
                }

                return ValueRef{ resultId };
            }
            default:
            {
                return std::nullopt;
            }
        }
    }

    std::optional<ValueRef> IRLowerer::lowerIntrinsicReceiverAddress(const Expression* receiverExpression) noexcept
    {
        const auto* atCall = TryGetAtIntrinsicCall(receiverExpression, m_semanticContext);
        if (atCall == nullptr)
            return lowerMethodReceiverAddress(receiverExpression);

        // an at() receiver keeps its element address, so nested mutation works fine
        const auto* innerCall = static_cast<const FunctionCallExpression*>(atCall->rightExpression().get());
        return lowerElementAddressForCall(atCall->leftExpression().get(), innerCall);
    }

    std::optional<ValueRef> IRLowerer::lowerElementAddressForCall(const Expression* receiverExpression, const FunctionCallExpression* call) noexcept
    {
        const auto receiverType = receiverExpression->type().toBaseType();
        auto receiverAddress = lowerIntrinsicReceiverAddress(receiverExpression);
        if (!receiverAddress.has_value())
            return std::nullopt;

        const auto& orderedArguments = call->orderedArguments();
        if (orderedArguments.empty())
            return std::nullopt;

        const auto index = lowerValueExpression(orderedArguments[0]);
        if (!index.has_value())
            return std::nullopt;

        const auto elementType = m_semanticContext.getArrayElementType(receiverType);
        auto baseAddress = receiverAddress.value();
        if (receiverType.kind() == TypeKind::Slice || receiverType.kind() == TypeKind::DynamicArray)
        {
            // the slice's or dynamic array's data pointer is its first field
            const auto dataPointerAddress = emitFieldAddress(baseAddress, receiverType, "data", 0, elementType.toReference());
            baseAddress = emitLoad(dataPointerAddress, elementType.toReference());
        }

        const auto elementAddressId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<ElementAddressInstruction>(
            elementAddressId,
            baseAddress,
            receiverType,
            index.value(),
            elementType));
        return ValueRef{ elementAddressId };
    }

    std::optional<ValueRef> IRLowerer::lowerMemberFieldAddress(const Expression* receiverExpression, const NameExpression* fieldNameExpression) noexcept
    {
        const auto objectType = receiverExpression->type().toValue();
        if (objectType.kind() != TypeKind::Type)
            return std::nullopt;

        auto objectAddress = lowerAddressExpression(receiverExpression);
        if (!objectAddress.has_value())
        {
            const auto objectValue = lowerValueExpression(receiverExpression);
            if (!objectValue.has_value())
                return std::nullopt;

            objectAddress = spillValueToTempSlot(receiverExpression, objectValue.value());
            if (!objectAddress.has_value())
                return std::nullopt;
        }

        const auto& typeDefinition = m_semanticContext.getTypeDefinition(objectType);
        const auto& fieldDefinition = typeDefinition.tryGetFieldByName(fieldNameExpression->name());
        if (fieldDefinition.type() == Type::Undefined())
            return std::nullopt;

        return emitFieldAddress(
            objectAddress.value(),
            objectType,
            fieldNameExpression->name(),
            fieldDefinition.index(),
            fieldDefinition.type());
    }

    bool IRLowerer::referenceArgumentAliasesConstant(const Expression* argument) const noexcept
    {
        const auto* stripped = stripGroupings(argument);
        if (stripped->kind() == NodeKind::UnaryExpression)
        {
            const auto* unary = static_cast<const UnaryExpression*>(stripped);
            return unary->unaryOperator() == UnaryOperatorKind::ReferenceOf && unary->referencesConstant();
        }

        if (stripped->kind() == NodeKind::NameExpression)
        {
            const auto result = m_locals.find(static_cast<const NameExpression*>(stripped)->name());
            return result != m_locals.end() && result->second.referencesConstant;
        }

        return false;
    }

    std::optional<ValueRef> IRLowerer::lowerAddressExpression(const Expression* expression) noexcept
    {
        switch (expression->kind())
        {
            case NodeKind::GroupingExpression:
            {
                const auto* groupingExpression = static_cast<const GroupingExpression*>(expression);
                return lowerAddressExpression(groupingExpression->expression().get());
            }
            case NodeKind::NameExpression:
            {
                const auto* nameExpression = static_cast<const NameExpression*>(expression);
                if (const auto localAddress = tryGetAddressBackedLocal(nameExpression->name()); localAddress.has_value())
                    return localAddress;

                return tryGetGlobalAddress(nameExpression->name());
            }
            case NodeKind::UnaryExpression:
            {
                const auto* unaryExpression = static_cast<const UnaryExpression*>(expression);
                if (unaryExpression->unaryOperator() != UnaryOperatorKind::ReferenceOf)
                    return std::nullopt;

                return lowerAddressExpression(stripGroupings(unaryExpression->expression().get()));
            }
            case NodeKind::MemberAccessExpression:
            {
                const auto* memberAccessExpression = static_cast<const MemberAccessExpression*>(expression);
                if (memberAccessExpression->expression()->kind() != NodeKind::NameExpression)
                    return std::nullopt;

                const auto thisAddress = tryGetAddressBackedLocal(ImplicitThisName);
                if (!thisAddress.has_value())
                    return std::nullopt;

                const auto* fieldNameExpression = static_cast<const NameExpression*>(memberAccessExpression->expression().get());
                // the implicit this parameter is a reference but field addressing needs the value type
                const auto objectType = m_locals.at(ImplicitThisName).type.toValue();
                const auto& typeDefinition = m_semanticContext.getTypeDefinition(objectType);
                const auto& fieldDefinition = typeDefinition.tryGetFieldByName(fieldNameExpression->name());
                if (fieldDefinition.type() == Type::Undefined())
                    return std::nullopt;

                return emitFieldAddress(
                    thisAddress.value(),
                    objectType,
                    fieldNameExpression->name(),
                    fieldDefinition.index(),
                    fieldDefinition.type());
            }
            case NodeKind::BinaryExpression:
            {
                const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
                if (binaryExpression->binaryOperator() != BinaryOperatorKind::MemberAccess)
                    return std::nullopt;

                if (binaryExpression->rightExpression()->kind() != NodeKind::NameExpression)
                    return std::nullopt;

                return lowerMemberFieldAddress(
                    binaryExpression->leftExpression().get(),
                    static_cast<const NameExpression*>(binaryExpression->rightExpression().get()));
            }
            default:
                return std::nullopt;
        }
    }

    bool IRLowerer::lowerReturnStatement(const ReturnStatement* statement, std::optional<BlockId>& currentBlockId) noexcept
    {
        if (!statement->expression().has_value())
        {
            m_currentBlock->setTerminator(std::make_unique<ReturnTerminator>());
            currentBlockId.reset();
            return true;
        }

        const auto loweredValue = lowerValueExpressionExpecting(statement->expression().value().get(), m_currentReturnType, false);
        if (!loweredValue.has_value())
            return false;

        m_currentBlock->setTerminator(std::make_unique<ReturnValueTerminator>(loweredValue.value()));
        currentBlockId.reset();
        return true;
    }

    std::optional<ConstantValue> IRLowerer::tryLowerConstantExpression(const Expression* expression) noexcept
    {
        if (expression->foldedValue().has_value())
            return FromFoldValue(expression->foldedValue().value());

        switch (expression->kind())
        {
            case NodeKind::NumberLiteral:
            {
                return CreateConstantValue(*static_cast<const NumberLiteral*>(expression));
            }
            case NodeKind::StringLiteral:
            {
                const auto* literal = static_cast<const StringLiteral*>(expression);
                return ConstantValue::FromString(literal->escapedContent());
            }
            case NodeKind::GroupingExpression:
            {
                const auto* groupingExpression = static_cast<const GroupingExpression*>(expression);
                return tryLowerConstantExpression(groupingExpression->expression().get());
            }
            case NodeKind::NameExpression:
            {
                // a name in a global initializer can only refer to an earlier global constant,
                // so its own initializer lowers in its place (cycles are impossible, forward
                // references already fail name resolution in the checker)
                const auto* nameExpression = static_cast<const NameExpression*>(expression);
                const auto* definition = m_semanticContext.tryGetConstantDefinition(nameExpression->name());
                if (definition == nullptr || definition->expression() == nullptr)
                    return std::nullopt;

                return tryLowerConstantExpression(definition->expression());
            }
            case NodeKind::ArrayLiteral:
            {
                const auto* literal = static_cast<const ArrayLiteral*>(expression);
                ConstantValue::AggregateData elements{};
                elements.reserve(literal->elements().size());
                for (const auto& element : literal->elements())
                {
                    const auto elementValue = tryLowerConstantExpression(element.get());
                    if (!elementValue.has_value())
                        return std::nullopt;

                    elements.push_back(elementValue.value());
                }

                return ConstantValue::FromAggregate(std::move(elements));
            }
            case NodeKind::BinaryExpression:
            {
                const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
                if (binaryExpression->binaryOperator() != BinaryOperatorKind::MemberAccess)
                    return std::nullopt;

                const auto enumConstant = tryLowerEnumMemberConstant(binaryExpression);
                if (!enumConstant.has_value())
                    return std::nullopt;

                if (const auto* enumValue = enumConstant->tryGetEnumConstant())
                    return ConstantValue::FromLiteralData(enumValue->underlyingValue);

                return std::nullopt;
            }
            default:
                return std::nullopt;
        }
    }

    std::optional<ConstantValue> IRLowerer::tryLowerEnumFieldValue(Type enumType, const std::string& fieldName) noexcept
    {
        auto& enumDefinition = m_semanticContext.getEnumDefinition(enumType);
        const auto* enumField = enumDefinition.tryGetFieldByName(fieldName);
        if (enumField == nullptr)
            return std::nullopt;

        if (enumField->expression() != nullptr)
            return tryLowerConstantExpression(enumField->expression());

        return CreateEnumConstantValue(enumDefinition.baseType(), enumField->value(), m_semanticContext);
    }

    std::optional<ConstantValue> IRLowerer::tryLowerEnumMemberConstant(const BinaryExpression* expression) noexcept
    {
        const auto enumType = expression->leftExpression()->type();
        if (enumType.kind() != TypeKind::Enum)
            return std::nullopt;

        if (expression->rightExpression()->kind() != NodeKind::NameExpression)
            return std::nullopt;

        const auto* fieldNameExpression = static_cast<const NameExpression*>(expression->rightExpression().get());
        auto underlyingValue = tryLowerEnumFieldValue(enumType, fieldNameExpression->name());
        if (!underlyingValue.has_value())
            return std::nullopt;

        const auto* literalData = underlyingValue->tryGetLiteralData();
        if (literalData == nullptr)
            return std::nullopt;

        auto& enumDefinition = m_semanticContext.getEnumDefinition(enumType);
        return ConstantValue::FromEnum(
            enumType,
            enumDefinition.name(),
            fieldNameExpression->name(),
            *literalData);
    }

    bool IRLowerer::lowerExpressionForEffect(const Expression* expression) noexcept
    {
        expression = stripGroupings(expression);
        if (expression == nullptr)
            return false;

        if (expression->kind() == NodeKind::FunctionCallExpression)
            return emitCall(static_cast<const FunctionCallExpression*>(expression)).has_value();

        if (expression->kind() == NodeKind::MemberAccessExpression)
        {
            const auto* memberAccessExpression = static_cast<const MemberAccessExpression*>(expression);
            if (memberAccessExpression->expression()->kind() == NodeKind::FunctionCallExpression)
            {
                const auto* functionCallExpression = static_cast<const FunctionCallExpression*>(memberAccessExpression->expression().get());
                return lowerCallWithReceiver(functionCallExpression).has_value();
            }
        }

        if (expression->kind() == NodeKind::BinaryExpression)
        {
            const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
            if ((binaryExpression->binaryOperator() == BinaryOperatorKind::MemberAccess
                || binaryExpression->binaryOperator() == BinaryOperatorKind::ConstructorCall)
                && binaryExpression->rightExpression()->kind() == NodeKind::FunctionCallExpression)
            {
                const auto* functionCallExpression = static_cast<const FunctionCallExpression*>(binaryExpression->rightExpression().get());
                if (binaryExpression->binaryOperator() == BinaryOperatorKind::ConstructorCall)
                {
                    return lowerValueExpression(expression).has_value();
                }

                return lowerCallWithReceiver(functionCallExpression, binaryExpression->leftExpression().get()).has_value();
            }
        }

        return lowerValueExpression(expression).has_value();
    }

    std::optional<ValueRef> IRLowerer::lowerValueExpression(const Expression* expression) noexcept
    {
        if (expression->foldedValue().has_value())
        {
            const auto temporaryId = m_nextTemporaryId++;
            m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                temporaryId,
                FromFoldValue(expression->foldedValue().value()),
                expression->type()));
            return ValueRef{ temporaryId };
        }

        switch (expression->kind())
        {
            case NodeKind::GroupingExpression:
            {
                const auto* groupingExpression = static_cast<const GroupingExpression*>(expression);
                return lowerValueExpression(groupingExpression->expression().get());
            }
            case NodeKind::NumberLiteral:
            {
                const auto* literal = static_cast<const NumberLiteral*>(expression);
                const auto constantValue = CreateConstantValue(*literal);
                if (!constantValue.has_value())
                    return std::nullopt;

                const auto temporaryId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                    temporaryId,
                    constantValue.value(),
                    literal->type()));
                return ValueRef{ temporaryId };
            }
            case NodeKind::BoolLiteral:
            {
                const auto* literal = static_cast<const BoolLiteral*>(expression);
                const auto temporaryId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                    temporaryId,
                    ConstantValue::FromBool(literal->value()),
                    literal->type()));
                return ValueRef{ temporaryId };
            }
            case NodeKind::StringLiteral:
            {
                const auto* literal = static_cast<const StringLiteral*>(expression);
                if (literal->type() == m_semanticContext.wellKnown().string)
                {
                    const auto slotAddress = allocateLocalSlot("temp", literal->type());
                    if (!slotAddress.has_value())
                        return std::nullopt;

                    if (!tryLowerStringLiteralIntoAddress(literal, slotAddress.value()))
                        return std::nullopt;

                    return emitLoad(slotAddress.value(), literal->type());
                }

                const auto temporaryId = m_nextTemporaryId++;
                m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                    temporaryId,
                    ConstantValue::FromString(literal->escapedContent()),
                    literal->type()));
                return ValueRef{ temporaryId };
            }
            case NodeKind::NameExpression:
            {
                const auto* nameExpression = static_cast<const NameExpression*>(expression);
                const auto result = m_locals.find(nameExpression->name());
                if (result == m_locals.end())
                {
                    const auto globalAddress = tryGetGlobalAddress(nameExpression->name());
                    if (!globalAddress.has_value())
                        return std::nullopt;

                    return emitLoad(globalAddress.value(), expression->type().toValue());
                }

                if (result->second.storageKind == LocalStorageKind::Address)
                    return emitLoad(result->second.value, result->second.type.toValue());

                return result->second.value;
            }
            case NodeKind::ArrayLiteral:
            {
                // a literal in value position materializes in a temp slot and loads the aggregate
                const auto arrayType = expression->type().toValue();
                const auto slotAddress = allocateLocalSlot("temp", arrayType);
                if (!slotAddress.has_value())
                    return std::nullopt;

                if (!tryLowerArrayLiteralIntoAddress(expression, slotAddress.value(), arrayType))
                    return std::nullopt;

                return emitLoad(slotAddress.value(), arrayType);
            }
            case NodeKind::FunctionCallExpression:
            {
                if (expression->type() == Type::Void())
                return std::nullopt;

                return emitCall(static_cast<const FunctionCallExpression*>(expression));
            }
            case NodeKind::MemberAccessExpression:
            {
                const auto* memberAccessExpression = static_cast<const MemberAccessExpression*>(expression);
                if (memberAccessExpression->expression()->kind() == NodeKind::FunctionCallExpression)
                {
                    if (expression->type() == Type::Void())
                        return std::nullopt;

                    const auto* functionCallExpression = static_cast<const FunctionCallExpression*>(memberAccessExpression->expression().get());
                    return lowerCallWithReceiver(functionCallExpression);
                }

                const auto loweredAddress = lowerAddressExpression(expression);
                if (!loweredAddress.has_value())
                    return std::nullopt;

                if (expression->type().isReference())
                    return loweredAddress;

                return emitLoad(loweredAddress.value(), expression->type().toValue());
            }
            case NodeKind::UnaryExpression:
            {
                const auto* unaryExpression = static_cast<const UnaryExpression*>(expression);
                if (unaryExpression->unaryOperator() == UnaryOperatorKind::ReferenceOf)
                {
                    // a ref expression in value context yields the referent's value, so load through the address
                    const auto loweredAddress = lowerAddressExpression(expression);
                    if (!loweredAddress.has_value())
                        return std::nullopt;

                    return emitLoad(loweredAddress.value(), expression->type().toValue());
                }

                const auto operandValue = lowerValueExpression(unaryExpression->expression().get());
                if (!operandValue.has_value())
                    return std::nullopt;

                switch (unaryExpression->unaryOperator())
                {
                    case UnaryOperatorKind::ValueNegation:
                    {
                        if (unaryExpression->signFolded())
                            return operandValue;

                        const auto temporaryId = m_nextTemporaryId++;
                        m_currentBlock->addInstruction(std::make_unique<ValueNegationInstruction>(temporaryId, operandValue.value(), expression->type()));
                        return ValueRef{ temporaryId };
                    }
                    case UnaryOperatorKind::LogicalNegation:
                    {
                        const auto temporaryId = m_nextTemporaryId++;
                        m_currentBlock->addInstruction(std::make_unique<LogicalNegationInstruction>(temporaryId, operandValue.value(), expression->type()));
                        return ValueRef{ temporaryId };
                    }
                    default:
                        return std::nullopt;
                }
            }
            case NodeKind::BinaryExpression:
            {
                const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
                if (binaryExpression->binaryOperator() == BinaryOperatorKind::MemberAccess
                    || binaryExpression->binaryOperator() == BinaryOperatorKind::ConstructorCall)
                {
                    if (binaryExpression->rightExpression()->kind() == NodeKind::FunctionCallExpression)
                    {
                        if (binaryExpression->type() == Type::Void())
                            return std::nullopt;

                        if (binaryExpression->binaryOperator() == BinaryOperatorKind::ConstructorCall)
                        {
                            const auto addressId = allocateSlotFromExpression(
                                "constructed",
                                expression,
                                expression->type().toValue());
                            if (!addressId.has_value())
                                return std::nullopt;

                            return emitLoad(addressId.value(), expression->type().toValue());
                        }

                        const auto* functionCallExpression = static_cast<const FunctionCallExpression*>(binaryExpression->rightExpression().get());
                        return lowerCallWithReceiver(functionCallExpression, binaryExpression->leftExpression().get());
                    }

                    if (binaryExpression->binaryOperator() == BinaryOperatorKind::MemberAccess
                        && binaryExpression->rightExpression()->kind() == NodeKind::NameExpression)
                    {
                        const auto receiverType = binaryExpression->leftExpression()->type().toBaseType();
                        if (receiverType.kind() == TypeKind::FixedArray)
                        {
                            // a fixed array's length normally folds, this covers unfolded paths
                            const auto temporaryId = m_nextTemporaryId++;
                            m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                                temporaryId,
                                ConstantValue::FromI32(m_semanticContext.getArrayLength(receiverType)),
                                m_semanticContext.wellKnown().i32));
                            return ValueRef{ temporaryId };
                        }

                        if (receiverType.kind() == TypeKind::Slice || receiverType.kind() == TypeKind::DynamicArray)
                        {
                            const auto receiverAddress = lowerIntrinsicReceiverAddress(binaryExpression->leftExpression().get());
                            if (!receiverAddress.has_value())
                                return std::nullopt;

                            const auto lengthAddress = emitFieldAddress(receiverAddress.value(), receiverType, ArrayLengthMemberName, 1, m_semanticContext.wellKnown().i32);
                            return emitLoad(lengthAddress, m_semanticContext.wellKnown().i32);
                        }

                        if (binaryExpression->leftExpression()->type().kind() == TypeKind::Enum)
                        {
                            const auto enumConstant = tryLowerEnumMemberConstant(binaryExpression);
                            if (!enumConstant.has_value())
                                return std::nullopt;

                            auto loweredType = expression->type();
                            if (const auto* enumValue = enumConstant->tryGetEnumConstant())
                                loweredType = enumValue->enumType;

                            const auto temporaryId = m_nextTemporaryId++;
                            m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                                temporaryId,
                                enumConstant.value(),
                                loweredType));
                            return ValueRef{ temporaryId };
                        }

                        const auto loweredAddress = lowerAddressExpression(expression);
                        if (!loweredAddress.has_value())
                            return std::nullopt;

                        return emitLoad(loweredAddress.value(), expression->type().toValue());
                    }
                }

                if (binaryExpression->binaryOperator() == BinaryOperatorKind::LogicalAnd
                    || binaryExpression->binaryOperator() == BinaryOperatorKind::LogicalOr)
                {
                    return lowerShortCircuitExpression(binaryExpression);
                }

                const auto leftValue = lowerValueExpression(binaryExpression->leftExpression().get());
                if (!leftValue.has_value())
                    return std::nullopt;

                const auto rightValue = lowerValueExpression(binaryExpression->rightExpression().get());
                if (!rightValue.has_value())
                    return std::nullopt;

                const auto temporaryId = m_nextTemporaryId++;
                const auto comparisonOperandType = binaryExpression->leftExpression()->type().toValue();

                switch (binaryExpression->binaryOperator())
                {
                    case BinaryOperatorKind::AdditionWrapping:
                        m_currentBlock->addInstruction(std::make_unique<AddInstruction>(temporaryId, leftValue.value(), rightValue.value(), expression->type()));
                        break;
                    case BinaryOperatorKind::SubtractionWrapping:
                        m_currentBlock->addInstruction(std::make_unique<SubtractInstruction>(temporaryId, leftValue.value(), rightValue.value(), expression->type()));
                        break;
                    case BinaryOperatorKind::MultiplicationWrapping:
                        m_currentBlock->addInstruction(std::make_unique<MultiplyInstruction>(temporaryId, leftValue.value(), rightValue.value(), expression->type()));
                        break;
                    case BinaryOperatorKind::Division:
                    {
                        const auto operandType = binaryExpression->leftExpression()->type().toValue();
                        const auto* operandDescription = m_semanticContext.tryGetBuiltinTypeDescription(operandType);
                        if (operandDescription != nullptr && operandDescription->kind == BuiltinTypeKind::Int)
                        {
                            const auto lhsConvertedId = temporaryId;
                            m_currentBlock->addInstruction(std::make_unique<IntToFloatInstruction>(lhsConvertedId, leftValue.value(), operandType, expression->type()));

                            const auto rhsConvertedId = m_nextTemporaryId++;
                            m_currentBlock->addInstruction(std::make_unique<IntToFloatInstruction>(rhsConvertedId, rightValue.value(), operandType, expression->type()));

                            const auto divideId = m_nextTemporaryId++;
                            m_currentBlock->addInstruction(std::make_unique<DivideInstruction>(divideId, ValueRef{ lhsConvertedId }, ValueRef{ rhsConvertedId }, expression->type()));
                            return ValueRef{ divideId };
                        }

                        m_currentBlock->addInstruction(std::make_unique<DivideInstruction>(temporaryId, leftValue.value(), rightValue.value(), expression->type()));
                        break;
                    }
                    case BinaryOperatorKind::Equal:
                    case BinaryOperatorKind::NotEqual:
                    {
                        // we lower == / != operators to static Type.x(lhs, rhs) function calls
                        const auto* operatorSignature = m_semanticContext.tryGetOperatorSignature(comparisonOperandType, binaryExpression->binaryOperator());
                        if (operatorSignature != nullptr && operatorSignature->callee != Type::Undefined())
                        {
                            markPreludeFunction(operatorSignature->callee);
                            m_currentBlock->addInstruction(std::make_unique<CallInstruction>(
                                temporaryId,
                                operatorSignature->callee.id(),
                                std::vector<ValueRef>{ leftValue.value(), rightValue.value() },
                                expression->type()));
                            break;
                        }

                        // we use strcmp for comparing cstrings
                        auto lhs = leftValue.value();
                        auto rhs = rightValue.value();
                        auto operandType = comparisonOperandType;
                        if (comparisonOperandType == m_semanticContext.wellKnown().cstring)
                        {
                            const auto strcmpId = resolveExternFunctionId("C.strcmp");
                            const auto callId = m_nextTemporaryId++;
                            m_currentBlock->addInstruction(std::make_unique<CallInstruction>(
                                callId,
                                strcmpId,
                                std::vector<ValueRef>{ lhs, rhs },
                                m_semanticContext.wellKnown().i32));

                            const auto zeroId = m_nextTemporaryId++;
                            m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(
                                zeroId,
                                ConstantValue::FromI32(0),
                                m_semanticContext.wellKnown().i32));

                            lhs = ValueRef{ callId };
                            rhs = ValueRef{ zeroId };
                            operandType = m_semanticContext.wellKnown().i32;
                        }

                        if (binaryExpression->binaryOperator() == BinaryOperatorKind::Equal)
                        {
                            m_currentBlock->addInstruction(std::make_unique<EqualInstruction>(temporaryId, lhs, rhs, expression->type(), operandType));
                        }
                        else
                        {
                            m_currentBlock->addInstruction(std::make_unique<NotEqualInstruction>(temporaryId, lhs, rhs, expression->type(), operandType));
                        }
                        break;
                    }
                    case BinaryOperatorKind::LessThan:
                        m_currentBlock->addInstruction(std::make_unique<LessThanInstruction>(temporaryId, leftValue.value(), rightValue.value(), expression->type(), comparisonOperandType));
                        break;
                    case BinaryOperatorKind::LessOrEqual:
                        m_currentBlock->addInstruction(std::make_unique<LessOrEqualInstruction>(temporaryId, leftValue.value(), rightValue.value(), expression->type(), comparisonOperandType));
                        break;
                    case BinaryOperatorKind::GreaterThan:
                        m_currentBlock->addInstruction(std::make_unique<GreaterThanInstruction>(temporaryId, leftValue.value(), rightValue.value(), expression->type(), comparisonOperandType));
                        break;
                    case BinaryOperatorKind::GreaterOrEqual:
                        m_currentBlock->addInstruction(std::make_unique<GreaterOrEqualInstruction>(temporaryId, leftValue.value(), rightValue.value(), expression->type(), comparisonOperandType));
                        break;
                    default:
                        return std::nullopt;
                }

                return ValueRef{ temporaryId };
            }
            default:
                return std::nullopt;
        }
    }

    std::optional<ValueRef> IRLowerer::lowerShortCircuitExpression(const BinaryExpression* expression) noexcept
    {
        const auto isLogicalAnd = expression->binaryOperator() == BinaryOperatorKind::LogicalAnd;
        const auto leftValue = lowerValueExpression(expression->leftExpression().get());
        if (!leftValue.has_value())
            return std::nullopt;

        const auto rightId = m_nextBlockId++;
        if (isLogicalAnd)
        {
            m_currentFunction->addBlock(BasicBlock{ rightId, "and.rhs", nullptr });
        }
        else
        {
            m_currentFunction->addBlock(BasicBlock{ rightId, "or.rhs", nullptr });
        }

        auto* conditionBlock = m_currentBlock;
        m_currentBlock = m_currentFunction->tryGetBlock(rightId);
        const auto rightValue = lowerValueExpression(expression->rightExpression().get());
        if (!rightValue.has_value())
            return std::nullopt;

        const auto mergeId = m_nextBlockId++;
        if (isLogicalAnd)
        {
            // false skips the right hand side, so it only runs when the left hand side is true
            m_currentFunction->addBlock(BasicBlock{ mergeId, "and.merge", nullptr });
            conditionBlock->setTerminator(std::make_unique<BranchIfTerminator>(leftValue.value(), rightId, mergeId));
        }
        else
        {
            // true skips the right hand side, so it only runs when the left hand side is false
            m_currentFunction->addBlock(BasicBlock{ mergeId, "or.merge", nullptr });
            conditionBlock->setTerminator(std::make_unique<BranchIfTerminator>(leftValue.value(), mergeId, rightId));
        }

        auto* rightExitBlock = m_currentBlock;
        rightExitBlock->setTerminator(std::make_unique<JumpTerminator>(mergeId));
        m_currentBlock = m_currentFunction->tryGetBlock(mergeId);

        std::vector<PhiInput> phiInputs;
        phiInputs.emplace_back(conditionBlock->id(), leftValue.value());
        phiInputs.emplace_back(rightExitBlock->id(), rightValue.value());

        const auto phiId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<PhiInstruction>(phiId, std::move(phiInputs), m_semanticContext.wellKnown().boolean));
        return ValueRef{ phiId };
    }

    std::optional<ValueRef> IRLowerer::emitCall(const FunctionCallExpression* expression, std::optional<ValueRef> implicitArgument) noexcept
    {
        const auto functionType = expression->functionType();
        if (functionType == Type::Undefined())
            return std::nullopt;

        auto& functionDefinition = m_semanticContext.getFunctionDefinition(functionType);
        const auto functionId = functionDefinition.type().id();

        markPreludeFunction(functionType);

        const auto& parameterTypes = functionDefinition.parameters();
        const size_t parameterOffset = implicitArgument.has_value() ? 1 : 0;

        const auto& orderedArguments = expression->orderedArguments();
        const auto& variadicArguments = expression->variadicArguments();

        std::vector<ValueRef> loweredArguments;
        loweredArguments.reserve(orderedArguments.size() + variadicArguments.size() + (implicitArgument.has_value() ? 1 : 0));
        if (implicitArgument.has_value())
        {
            loweredArguments.push_back(implicitArgument.value());
        }

        for (size_t i = 0; i < orderedArguments.size(); ++i)
        {
            const auto* argument = orderedArguments[i];
            std::optional<ValueRef> loweredArgument;
            if (parameterTypes[i + parameterOffset].type().isReference())
            {
                if (referenceArgumentAliasesConstant(argument))
                {
                    // for now we create a defensive copy when we pass a ref to constants to function calls
                    const auto referentAddress = lowerAddressExpression(argument);
                    if (!referentAddress.has_value())
                        return std::nullopt;

                    const auto valueType = argument->type().toValue();
                    const auto referentValue = emitLoad(referentAddress.value(), valueType);
                    loweredArgument = allocateLocalSlot("refConstTemp", valueType, referentValue);
                }
                else
                {
                    loweredArgument = lowerAddressExpression(argument);
                }
            }
            else
            {
                loweredArgument = lowerValueExpressionExpecting(argument, parameterTypes[i + parameterOffset].type().toValue());
            }

            if (!loweredArgument.has_value())
                return std::nullopt;

            loweredArguments.push_back(loweredArgument.value());
        }

        for (const auto* argument : variadicArguments)
        {
            auto loweredArgument = lowerValueExpression(argument);
            if (!loweredArgument.has_value())
                return std::nullopt;

            loweredArguments.push_back(loweredArgument.value());
        }

        const auto isSynthesizedConstructor = functionDefinition.functionType() == FunctionType::SynthesizedConstructor;
        if (isSynthesizedConstructor && !implicitArgument.has_value())
            return std::nullopt;

        if (isSynthesizedConstructor || expression->type() == Type::Void())
        {
            m_currentBlock->addInstruction(std::make_unique<CallVoidInstruction>(functionId, std::move(loweredArguments)));
            return ValueRef{};
        }

        const auto temporaryId = m_nextTemporaryId++;
        m_currentBlock->addInstruction(std::make_unique<CallInstruction>(temporaryId, functionId, std::move(loweredArguments), expression->type()));
        return ValueRef{ temporaryId };
    }

    void IRLowerer::resetState()
    {
        m_locals.clear();
        m_addressTakenLocals.clear();
        m_nextTemporaryId = 0;
        m_nextLocalSlotId = 0;
        m_nextBlockId = 0;
        m_currentFunction = nullptr;
        m_currentBlock = nullptr;
        m_currentReturnType = Type::Void();
    }

    void IRLowerer::restoreLocalValues(const LocalStateMap& values) noexcept
    {
        m_locals = values;
    }

    void IRLowerer::mergeLocalValues(BasicBlock& block, const std::vector<IncomingLocalValues>& incomingValues) noexcept
    {
        LocalStateMap mergedLocalValues;
        if (incomingValues.empty())
        {
            m_locals = std::move(mergedLocalValues);
            return;
        }

        const auto& firstIncomingValues = incomingValues.front().values;

        const auto mergeCandidateNames = sortedDefinedLocalNames(firstIncomingValues);

        for (const auto& name : mergeCandidateNames)
        {
            // only merge locals that are defined along every incoming edge
            const auto isAvailableOnAllPaths = isLocalDefinedOnAllEdges(incomingValues, name);
            if (!isAvailableOnAllPaths)
                continue;

            const auto& firstState = firstIncomingValues.at(name);
            // phi is only needed when at least one predecessor contributes a different SSA value
            const auto requiresPhi = localNeedsPhi(incomingValues, name, firstState);
            if (!requiresPhi)
            {
                mergedLocalValues.emplace(name, firstState);
                continue;
            }

            std::vector<PhiInput> phiInputs;
            phiInputs.reserve(incomingValues.size());
            // phi inputs in predecessor order for the merged block
            for (const auto& incomingValue : incomingValues)
            {
                phiInputs.emplace_back(incomingValue.predecessorBlockId, incomingValue.values.at(name).value);
            }

            const auto phiId = m_nextTemporaryId++;
            auto phiType = firstState.type;
            if (firstState.storageKind == LocalStorageKind::Address)
                phiType = phiType.toReference();

            block.addInstruction(std::make_unique<PhiInstruction>(phiId, std::move(phiInputs), phiType));
            mergedLocalValues.emplace(name, LocalState{ ValueRef{ phiId }, firstState.type, firstState.storageKind });
        }

        // replace locals with values after the merge
        m_locals = std::move(mergedLocalValues);
    }
}
