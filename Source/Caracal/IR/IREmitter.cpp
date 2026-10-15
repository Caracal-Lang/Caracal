#include <Caracal/IR/IREmitter.h>


#include <Caracal/Constants.h>
#include <Caracal/Profiling.h>
#include <Caracal/IR/AddInstruction.h>
#include <Caracal/IR/AddressOfInstruction.h>
#include <Caracal/IR/DivideInstruction.h>
#include <Caracal/IR/EqualInstruction.h>
#include <Caracal/IR/GreaterOrEqualInstruction.h>
#include <Caracal/IR/GreaterThanInstruction.h>
#include <Caracal/IR/AddressOfFieldInstruction.h>
#include <Caracal/IR/AddressOfGlobalInstruction.h>
#include <Caracal/IR/BitAndInstruction.h>
#include <Caracal/IR/BitNotInstruction.h>
#include <Caracal/IR/BitOrInstruction.h>
#include <Caracal/IR/BitXorInstruction.h>
#include <Caracal/IR/ShiftLeftInstruction.h>
#include <Caracal/IR/ShiftRightInstruction.h>
#include <Caracal/IR/ElementAddressInstruction.h>
#include <Caracal/IR/MakeSliceInstruction.h>
#include <Caracal/IR/ArrayAddInstruction.h>
#include <Caracal/IR/ArrayCopyInstruction.h>
#include <Caracal/IR/ArrayRemoveInstruction.h>
#include <Caracal/IR/SizeOfInstruction.h>
#include <Caracal/IR/ConstructedGlobalDeclaration.h>
#include <Caracal/IR/GlobalConstantDeclaration.h>
#include <Caracal/IR/GlobalReferenceDeclaration.h>
#include <Caracal/IR/IntToFloatInstruction.h>
#include <Caracal/IR/IntWidenInstruction.h>
#include <Caracal/IR/LessOrEqualInstruction.h>
#include <Caracal/IR/LessThanInstruction.h>
#include <Caracal/IR/MultiplyInstruction.h>
#include <Caracal/IR/NotEqualInstruction.h>
#include <Caracal/IR/SubtractInstruction.h>
#include <Caracal/IR/AllocateLocalInstruction.h>
#include <Caracal/IR/BranchIfTerminator.h>
#include <Caracal/IR/CallInstruction.h>
#include <Caracal/IR/CallVoidInstruction.h>
#include <Caracal/IR/ConstantInstruction.h>
#include <Caracal/IR/LogicalNegationInstruction.h>
#include <Caracal/IR/ValueNegationInstruction.h>
#include <Caracal/IR/JumpTerminator.h>
#include <Caracal/IR/LoadValueInstruction.h>
#include <Caracal/IR/LocalSlotRef.h>
#include <Caracal/IR/ParameterInstruction.h>
#include <Caracal/IR/ReturnTerminator.h>
#include <Caracal/IR/ReturnValueTerminator.h>
#include <Caracal/IR/StoreValueInstruction.h>
#include <Caracal/IR/UnreachableTerminator.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Caracal
{
    static TypeDeclaration MakeTypeDeclaration(const TypeDefinition& definition)
    {
        TypeDeclaration typeDeclaration{ definition.name(), definition.type() };
        for (const auto& fieldDefinition : definition.fields())
        {
            typeDeclaration.addField(fieldDefinition.name(), fieldDefinition.type(), fieldDefinition.isConstant());
        }
        return typeDeclaration;
    }

    IREmitter::IREmitter(SemanticContext& semanticContext, Module& module)
        : m_semanticContext{ semanticContext }
        , m_module{ module }
    {
    }

    bool IREmitter::canEmit() const noexcept
    {
        return m_currentFunction != nullptr
            && m_currentBlock != nullptr
            && !m_currentBlock->hasTerminator();
    }

    void IREmitter::recordEmissionFailure() noexcept
    {
        if (canEmit())
        {
            m_emissionFailed = true;
        }
    }

    bool IREmitter::canEmitWith(std::initializer_list<ValueRef> operands)
    {
        if (!canEmit())
        {
            return false;
        }

        for (const auto operand : operands)
        {
            if (operand.id() < 0)
            {
                m_emissionFailed = true;
                return false;
            }
        }

        return true;
    }

    // if both main and caracalMain exist, caracalMain becomes the C entry point and main is renamed
    // runs once before the first function is emitted, all definitions are known by then
    void IREmitter::computeEntryPointFlags()
    {
        if (m_entryPointFlagsComputed)
        {
            return;
        }
        m_entryPointFlagsComputed = true;
        m_completedFunctions.reserve(m_semanticContext.functionDefinitions().size());

        auto hasUserMain = false;
        auto hasEntryPoint = false;
        for (const auto& functionDefinition : m_semanticContext.functionDefinitions())
        {
            if (functionDefinition.functionType() != FunctionType::FreeFunction)
            {
                continue;
            }

            if (functionDefinition.fullName() == UserMainFunctionName)
            {
                hasUserMain = true;
            }
            else if (functionDefinition.fullName() == EntryPointFunctionName)
            {
                hasEntryPoint = true;
            }
        }
        m_emitEntryPoint = hasUserMain && hasEntryPoint;
    }

    void IREmitter::beginFunction(const FunctionDefinition& definition)
    {
        CARACAL_ZONE_NAMED("emit.beginFunction");
        computeEntryPointFlags();

        m_locals.clear();
        m_nextTemporaryId = 0;
        m_nextLocalSlotId = 0;
        m_nextBlockId = 0;
        m_currentCalledPreludeFunctions.clear();
        m_currentCalledPreludeFunctionIds.clear();

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

        m_pendingFunction.emplace(Function{ definition.type().id(), definition.fullName(), std::move(parameters), returnType });
        auto* function = &m_pendingFunction.value();
        if (m_emitEntryPoint)
        {
            // caracalMain is emitted as the C symbol main, the user's main as caracal.userMain
            if (definition.fullName() == EntryPointFunctionName)
            {
                function->setSymbolName(CRuntimeEntrySymbolName);
            }
            else if (definition.fullName() == UserMainFunctionName)
            {
                function->setSymbolName(UserMainSymbolName);
            }
        }

        const auto blockId = m_nextBlockId++;
        function->addBlock(BasicBlock{ blockId, "entry", nullptr });
        m_currentFunction = function;
        m_currentBlock = function->tryGetBlock(blockId);
        m_currentReturnType = returnType;
    }

    void IREmitter::endFunction()
    {
        CARACAL_ZONE_NAMED("emit.endFunction");
        if (m_currentFunction != nullptr && m_currentBlock != nullptr && !m_currentBlock->hasTerminator())
        {
            if (m_currentReturnType == Type::Void())
            {
                m_currentBlock->setTerminator(std::make_unique<ReturnTerminator>());
            }
            else
            {
                m_currentBlock->setTerminator(std::make_unique<UnreachableTerminator>());
            }
        }

        if (m_pendingFunction.has_value())
        {
            m_pendingFunction->setTemporaryCount(m_nextTemporaryId);
            if (m_isPreludePass)
            {
                m_semanticContext.preludeFunctionPool().add(PreludeFunctionBody{ std::move(m_pendingFunction.value()), std::move(m_currentCalledPreludeFunctions) });
                m_currentCalledPreludeFunctions.clear();
                m_currentCalledPreludeFunctionIds.clear();
            }
            else
            {
                m_completedFunctions.push_back(std::move(m_pendingFunction.value()));
            }
            m_pendingFunction.reset();
        }
        m_currentFunction = nullptr;
        m_currentBlock = nullptr;
        m_locals.clear();
        m_loopContexts.clear();
        m_addressTakenLocals.clear();
    }

    ValueRef IREmitter::emitConstant(ConstantValue value, Type type)
    {
        CARACAL_ZONE_DETAIL("emit.constant");
        if (!canEmit())
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(result.id(), std::move(value), type));
        return result;
    }

    void IREmitter::emitReturn()
    {
        if (!canEmit())
        {
            return;
        }

        m_currentBlock->setTerminator(std::make_unique<ReturnTerminator>());
    }

    void IREmitter::emitReturnValue(ValueRef value)
    {
        if (!canEmitWith({ value }))
        {
            return;
        }

        m_currentBlock->setTerminator(std::make_unique<ReturnValueTerminator>(value));
    }

    ValueRef IREmitter::emitLoad(ValueRef address, Type valueType)
    {
        if (!canEmitWith({ address }))
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<LoadValueInstruction>(result.id(), address, valueType));
        return result;
    }

    ValueRef IREmitter::allocateLocalSlot(std::string localName, Type valueType, ValueRef initialValue)
    {
        CARACAL_ZONE_NAMED("emit.allocateLocalSlot");
        const auto localId = m_nextLocalSlotId++;
        m_currentBlock->addPrologueInstruction(std::make_unique<AllocateLocalInstruction>(localId, std::move(localName), valueType));

        const auto address = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<AddressOfInstruction>(address.id(), LocalSlotRef{ localId }, valueType.toReference()));

        if (initialValue.id() >= 0)
        {
            m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(initialValue, address, valueType));
        }

        return address;
    }

    void IREmitter::emitParameter(u32 nameId, i32 position, const Parameter& parameter)
    {
        CARACAL_ZONE_NAMED("emit.emitParameter");
        if (!canEmit())
        {
            return;
        }

        const auto parameterId = ValueRef{ m_nextTemporaryId++ };
        const auto parameterType = parameter.type();
        m_currentBlock->addInstruction(std::make_unique<ParameterInstruction>(
            parameterId.id(),
            position,
            IRParameter{ parameter.name(), parameterType }));

        const auto needsAddressStorage = parameterType.isReference()
            || parameterType.kind() == TypeKind::Type
            || parameterType.kind() == TypeKind::FixedArray
            || std::binary_search(m_addressTakenLocals.begin(), m_addressTakenLocals.end(), nameId);
        if (!needsAddressStorage)
        {
            m_locals.set(nameId, LocalState{ parameterId, parameterType, LocalStorageKind::Value });
            return;
        }

        if (parameterType.isReference())
        {
            m_locals.set(nameId, LocalState{ parameterId, parameterType, LocalStorageKind::Address });
            return;
        }

        const auto address = allocateLocalSlot(parameter.name(), parameterType, parameterId);
        m_locals.set(nameId, LocalState{ address, parameterType, LocalStorageKind::Address });
    }

    void IREmitter::emitLocalDeclaration(u32 nameId, std::string_view name, Type localType, ValueRef value)
    {
        CARACAL_ZONE_DETAIL("emit.localDeclaration");
        if (!canEmitWith({ value }))
        {
            return;
        }

        if (localType.isReference()
            || localType.kind() == TypeKind::FixedArray
            || localType.kind() == TypeKind::DynamicArray)
        {
            return;
        }

        if (localType.kind() == TypeKind::Type)
        {
            const auto address = allocateLocalSlot(std::string(name), localType, value);
            m_locals.set(nameId, LocalState{ address, localType, LocalStorageKind::Address });
            return;
        }

        if (std::binary_search(m_addressTakenLocals.begin(), m_addressTakenLocals.end(), nameId))
        {
            const auto address = allocateLocalSlot(std::string(name), localType, value);
            m_locals.set(nameId, LocalState{ address, localType, LocalStorageKind::Address });
            return;
        }

        m_locals.set(nameId, LocalState{ value, localType, LocalStorageKind::Value });
    }

    void IREmitter::setAddressTakenLocals(const std::vector<u32>& sortedNameIds)
    {
        m_addressTakenLocals.assign(sortedNameIds.begin(), sortedNameIds.end());
    }

    void IREmitter::setPreludeMode(bool isPreludePass) noexcept
    {
        m_isPreludePass = isPreludePass;
    }

    void IREmitter::setNameLexemes(const std::vector<std::string_view>& lexemes) noexcept
    {
        m_nameLexemes = &lexemes;
    }

    std::string_view IREmitter::nameLexeme(u32 nameId) const noexcept
    {
        if (m_nameLexemes == nullptr || nameId >= m_nameLexemes->size())
        {
            return std::string_view{};
        }

        return (*m_nameLexemes)[nameId];
    }

    void IREmitter::emitRefLocalDeclaration(u32 nameId, Type refType, ValueRef address)
    {
        if (!canEmitWith({ address }))
        {
            return;
        }

        m_locals.set(nameId, LocalState{ address, refType, LocalStorageKind::Address });
    }

    ValueRef IREmitter::addressOfLocal(u32 nameId)
    {
        if (!canEmit())
        {
            return ValueRef{};
        }

        const auto* local = m_locals.tryGet(nameId);
        if (local == nullptr || local->storageKind != LocalStorageKind::Address)
        {
            return ValueRef{};
        }

        return local->value;
    }

    ValueRef IREmitter::allocateAggregateSlot(u32 nameId, std::string_view name, Type type)
    {
        if (!canEmit())
        {
            return ValueRef{};
        }

        const auto address = allocateLocalSlot(std::string(name), type, ValueRef{});
        m_locals.set(nameId, LocalState{ address, type, LocalStorageKind::Address });
        return address;
    }

    ValueRef IREmitter::allocateTempAggregateSlot(std::string_view name, Type type)
    {
        if (!canEmit())
        {
            return ValueRef{};
        }

        return allocateLocalSlot(std::string(name), type, ValueRef{});
    }

    ValueRef IREmitter::spillToTempSlot(ValueRef value, Type valueType)
    {
        if (!canEmitWith({ value }))
        {
            return ValueRef{};
        }

        return allocateLocalSlot("temp", valueType, value);
    }

    ValueRef IREmitter::emitFieldAddress(ValueRef objectAddress, Type objectType, std::string_view fieldName, i32 fieldIndex, Type fieldType)
    {
        if (!canEmitWith({ objectAddress }))
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<AddressOfFieldInstruction>(
            result.id(),
            objectAddress,
            objectType,
            std::string(fieldName),
            fieldIndex,
            fieldType.toReference()));
        return result;
    }

    ValueRef IREmitter::emitElementAddress(ValueRef baseAddress, Type arrayType, ValueRef index, Type elementType)
    {
        if (!canEmitWith({ baseAddress, index }))
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<ElementAddressInstruction>(
            result.id(),
            baseAddress,
            arrayType,
            index,
            elementType));
        return result;
    }

    ValueRef IREmitter::elementBaseAddress(ValueRef receiverAddress, Type receiverType, Type elementType)
    {
        if (receiverType.kind() != TypeKind::Slice && receiverType.kind() != TypeKind::DynamicArray)
        {
            return receiverAddress;
        }

        const auto dataPointerAddress = emitFieldAddress(receiverAddress, receiverType, "data", 0, elementType.toReference());
        return emitLoad(dataPointerAddress, elementType.toReference());
    }

    ValueRef IREmitter::emitElementAddressForReceiver(ValueRef receiverAddress, Type receiverType, ValueRef index)
    {
        if (!canEmitWith({ receiverAddress, index }))
        {
            return ValueRef{};
        }

        const auto elementType = m_semanticContext.getArrayElementType(receiverType);
        const auto baseAddress = elementBaseAddress(receiverAddress, receiverType, elementType);
        return emitElementAddress(baseAddress, receiverType, index, elementType);
    }

    ValueRef IREmitter::emitElementAccess(ValueRef receiverAddress, Type receiverType, ValueRef index, Type resultType, bool wantsElementAddress)
    {
        const auto elementAddress = emitElementAddressForReceiver(receiverAddress, receiverType, index);
        if (elementAddress.id() < 0 || wantsElementAddress)
        {
            return elementAddress;
        }

        return emitLoad(elementAddress, resultType);
    }

    ValueRef IREmitter::emitSliceIntrinsic(ValueRef receiverAddress, Type receiverType, Type sliceType)
    {
        if (!canEmitWith({ receiverAddress }))
        {
            return ValueRef{};
        }

        auto baseAddress = receiverAddress;
        auto sourceArrayType = receiverType;
        if (receiverType.kind() == TypeKind::Type)
        {
            const auto& bytesField = m_semanticContext.getTypeDefinition(receiverType).tryGetFieldByName("_bytes");
            if (bytesField.type() == Type::Undefined())
            {
                return ValueRef{};
            }

            baseAddress = emitFieldAddress(baseAddress, receiverType, "_bytes", 0, bytesField.type());
            sourceArrayType = bytesField.type();
        }

        auto sliceLength = ValueRef{};
        if (sourceArrayType.kind() == TypeKind::FixedArray)
        {
            sliceLength = emitConstant(ConstantValue::FromI32(m_semanticContext.getArrayLength(sourceArrayType)), m_semanticContext.wellKnown().i32);
        }
        else
        {
            const auto elementType = m_semanticContext.getArrayElementType(sourceArrayType);
            const auto arrayAddress = baseAddress;
            const auto dataPointerAddress = emitFieldAddress(arrayAddress, sourceArrayType, "data", 0, elementType.toReference());
            baseAddress = emitLoad(dataPointerAddress, elementType.toReference());
            const auto lengthAddress = emitFieldAddress(arrayAddress, sourceArrayType, ArrayLengthMemberName, 1, m_semanticContext.wellKnown().i32);
            sliceLength = emitLoad(lengthAddress, m_semanticContext.wellKnown().i32);
        }

        return emitMakeSlice(baseAddress, sliceLength, sliceType);
    }

    ValueRef IREmitter::emitStringToCString(ValueRef receiverAddress, Type receiverType)
    {
        if (!canEmitWith({ receiverAddress }))
        {
            return ValueRef{};
        }

        const auto& bytesField = m_semanticContext.getTypeDefinition(receiverType).tryGetFieldByName("_bytes");
        if (bytesField.type() == Type::Undefined())
        {
            return ValueRef{};
        }

        const auto elementType = m_semanticContext.getArrayElementType(bytesField.type());
        const auto bytesAddress = emitFieldAddress(receiverAddress, receiverType, "_bytes", 0, bytesField.type());
        const auto dataPointerAddress = emitFieldAddress(bytesAddress, bytesField.type(), "data", 0, elementType.toReference());
        return emitLoad(dataPointerAddress, m_semanticContext.wellKnown().cstring);
    }

    ValueRef IREmitter::emitMakeSlice(ValueRef baseAddress, ValueRef length, Type sliceType)
    {
        if (!canEmitWith({ baseAddress, length }))
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<MakeSliceInstruction>(result.id(), baseAddress, length, sliceType));
        return result;
    }

    ValueRef IREmitter::emitDynamicArrayStorage(ValueRef destinationAddress, Type arrayType, i32 count)
    {
        if (!canEmitWith({ destinationAddress }))
        {
            return ValueRef{};
        }

        const auto elementType = m_semanticContext.getArrayElementType(arrayType);
        auto capacity = count;
        if (capacity < 8)
        {
            capacity = 8;
        }

        // calloc(capacity, sizeof(element)) provides the zeroed backing buffer
        const auto capacityArgument = emitConstant(ConstantValue::FromI64(capacity), m_semanticContext.wellKnown().i64);
        const auto elementSize = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<SizeOfInstruction>(
            elementSize.id(),
            elementType,
            m_semanticContext.wellKnown().i64));

        const auto callocId = m_semanticContext.tryGetExternFunctionByFullName("C.calloc").id();
        const auto dataPointer = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<CallInstruction>(
            dataPointer.id(),
            callocId,
            std::vector<ValueRef>{ capacityArgument, elementSize },
            m_semanticContext.wellKnown().rawptr));

        emitDynamicArrayDescriptor(destinationAddress, arrayType, dataPointer, count, capacity);
        return dataPointer;
    }

    void IREmitter::emitDynamicArrayDescriptor(ValueRef destinationAddress, Type arrayType, ValueRef dataPointer, i32 length, i32 capacity)
    {
        const auto elementType = m_semanticContext.getArrayElementType(arrayType);
        const auto dataFieldAddress = emitFieldAddress(destinationAddress, arrayType, "data", 0, elementType.toReference());
        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(dataPointer, dataFieldAddress, elementType.toReference()));

        const auto lengthValue = emitConstant(ConstantValue::FromI32(length), m_semanticContext.wellKnown().i32);
        const auto lengthFieldAddress = emitFieldAddress(destinationAddress, arrayType, "length", 1, m_semanticContext.wellKnown().i32);
        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(lengthValue, lengthFieldAddress, m_semanticContext.wellKnown().i32));

        const auto capacityValue = emitConstant(ConstantValue::FromI32(capacity), m_semanticContext.wellKnown().i32);
        const auto capacityFieldAddress = emitFieldAddress(destinationAddress, arrayType, "capacity", 2, m_semanticContext.wellKnown().i32);
        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(capacityValue, capacityFieldAddress, m_semanticContext.wellKnown().i32));
    }

    ValueRef IREmitter::emitCStringEquality(BinaryOperatorKind operation, ValueRef left, ValueRef right)
    {
        if (!canEmitWith({ left, right }))
        {
            return ValueRef{};
        }

        const auto strcmpId = m_semanticContext.tryGetExternFunctionByFullName("C.strcmp").id();
        const auto comparison = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<CallInstruction>(
            comparison.id(),
            strcmpId,
            std::vector<ValueRef>{ left, right },
            m_semanticContext.wellKnown().i32));

        const auto zero = emitConstant(ConstantValue::FromI32(0), m_semanticContext.wellKnown().i32);
        return emitBinary(operation, comparison, zero, m_semanticContext.wellKnown().boolean, m_semanticContext.wellKnown().i32);
    }

    void IREmitter::emitStringLiteralIntoAddress(ValueRef destinationAddress, std::string_view escaped)
    {
        if (!canEmitWith({ destinationAddress }))
        {
            return;
        }

        const auto stringType = m_semanticContext.wellKnown().string;
        const auto& typeDefinition = m_semanticContext.getTypeDefinition(stringType);
        const auto& bytesField = typeDefinition.tryGetFieldByName("_bytes");
        if (bytesField.type() == Type::Undefined())
        {
            return;
        }

        const auto bytesType = bytesField.type();
        const auto bytesAddress = emitFieldAddress(destinationAddress, stringType, "_bytes", 0, bytesType);
        const auto data = emitConstant(ConstantValue::FromString(std::string(escaped)), m_semanticContext.wellKnown().cstring);
        const auto length = static_cast<i32>(escaped.size());
        emitDynamicArrayDescriptor(bytesAddress, bytesType, data, length, length);
    }

    void IREmitter::emitArrayAdd(ValueRef receiverAddress, ValueRef value, Type receiverType)
    {
        if (!canEmitWith({ receiverAddress, value }))
        {
            return;
        }

        m_currentBlock->addInstruction(std::make_unique<ArrayAddInstruction>(
            receiverAddress,
            value,
            receiverType,
            m_semanticContext.tryGetExternFunctionByFullName("C.realloc").id()));
    }

    void IREmitter::emitArrayRemove(ValueRef receiverAddress, ValueRef index, Type receiverType)
    {
        if (!canEmitWith({ receiverAddress, index }))
        {
            return;
        }

        m_currentBlock->addInstruction(std::make_unique<ArrayRemoveInstruction>(
            receiverAddress,
            index,
            receiverType,
            m_semanticContext.tryGetExternFunctionByFullName("C.memmove").id()));
    }

    ValueRef IREmitter::emitArrayCopy(ValueRef sourceAddress, Type arrayType, bool reserveNulByte)
    {
        if (!canEmitWith({ sourceAddress }))
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<ArrayCopyInstruction>(
            result.id(),
            sourceAddress,
            arrayType,
            m_semanticContext.tryGetExternFunctionByFullName("C.calloc").id(),
            m_semanticContext.tryGetExternFunctionByFullName("C.memmove").id(),
            reserveNulByte));
        return result;
    }

    void IREmitter::emitStoreValue(ValueRef value, ValueRef address, Type type)
    {
        if (!canEmitWith({ value, address }))
        {
            return;
        }

        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(value, address, type));
    }

    ValueRef IREmitter::emitRefValue(u32 nameId, Type valueType)
    {
        const auto address = addressOfLocal(nameId);
        if (address.id() < 0)
        {
            return ValueRef{};
        }

        return emitLoad(address, valueType);
    }

    ValueRef IREmitter::emitDefensiveRefCopy(ValueRef address, Type valueType)
    {
        if (!canEmitWith({ address }))
        {
            return ValueRef{};
        }

        const auto value = emitLoad(address, valueType);
        return allocateLocalSlot("refConstTemp", valueType, value);
    }

    ValueRef IREmitter::peekLocal(u32 nameId)
    {
        if (!canEmit())
        {
            return ValueRef{};
        }

        const auto* local = m_locals.tryGet(nameId);
        if (local == nullptr || local->storageKind != LocalStorageKind::Value)
        {
            return ValueRef{};
        }

        return local->value;
    }

    void IREmitter::emitLocalAssignment(u32 nameId, ValueRef value)
    {
        if (!canEmit() || value.id() < 0)
        {
            return;
        }

        auto* local = m_locals.tryGetMutable(nameId);
        if (local == nullptr)
        {
            recordEmissionFailure();
            return;
        }

        if (local->storageKind == LocalStorageKind::Address)
        {
            m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(value, local->value, local->type.toValue()));
            return;
        }

        local->value = value;
    }

    ValueRef IREmitter::readLocal(u32 nameId)
    {
        if (!canEmit())
        {
            return ValueRef{};
        }

        const auto* local = m_locals.tryGet(nameId);
        if (local == nullptr)
        {
            recordEmissionFailure();
            return ValueRef{};
        }

        if (local->storageKind == LocalStorageKind::Address)
        {
            return emitLoad(local->value, local->type.toValue());
        }

        return local->value;
    }

    ValueRef IREmitter::emitWidened(ValueRef value, Type sourceType, Type targetType)
    {
        CARACAL_ZONE_DETAIL("emit.widened");
        if (!canEmit() || value.id() < 0 || sourceType == targetType || !sourceType.isBaseType() || !targetType.isBaseType())
        {
            return value;
        }

        const auto* sourceDescription = m_semanticContext.tryGetBuiltinTypeDescription(sourceType);
        const auto* targetDescription = m_semanticContext.tryGetBuiltinTypeDescription(targetType);
        if (sourceDescription == nullptr 
            || targetDescription == nullptr
            || sourceDescription->kind != BuiltinTypeKind::Int
            || targetDescription->kind != BuiltinTypeKind::Int
            || sourceDescription->bits >= targetDescription->bits)
        {
            return value;
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<IntWidenInstruction>(result.id(), value, sourceType, targetType));
        return result;
    }

    ValueRef IREmitter::emitIntToFloat(ValueRef value, Type sourceType, Type targetType)
    {
        if (!canEmit() || value.id() < 0)
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<IntToFloatInstruction>(result.id(), value, sourceType, targetType));
        return result;
    }

    ValueRef IREmitter::emitBinary(BinaryOperatorKind binaryOperator, ValueRef left, ValueRef right, Type resultType, Type operandType)
    {
        if (!canEmit() || left.id() < 0 || right.id() < 0)
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        switch (binaryOperator)
        {
            case BinaryOperatorKind::AdditionWrapping:
            {
                m_currentBlock->addInstruction(std::make_unique<AddInstruction>(result.id(), left, right, resultType));
                break;
            }
            case BinaryOperatorKind::SubtractionWrapping:
            {
                m_currentBlock->addInstruction(std::make_unique<SubtractInstruction>(result.id(), left, right, resultType));
                break;
            }
            case BinaryOperatorKind::MultiplicationWrapping:
            {
                m_currentBlock->addInstruction(std::make_unique<MultiplyInstruction>(result.id(), left, right, resultType));
                break;
            }
            case BinaryOperatorKind::Division:
            {
                m_currentBlock->addInstruction(std::make_unique<DivideInstruction>(result.id(), left, right, resultType));
                break;
            }
            case BinaryOperatorKind::Equal:
            {
                m_currentBlock->addInstruction(std::make_unique<EqualInstruction>(result.id(), left, right, resultType, operandType));
                break;
            }
            case BinaryOperatorKind::NotEqual:
            {
                m_currentBlock->addInstruction(std::make_unique<NotEqualInstruction>(result.id(), left, right, resultType, operandType));
                break;
            }
            case BinaryOperatorKind::LessThan:
            {
                m_currentBlock->addInstruction(std::make_unique<LessThanInstruction>(result.id(), left, right, resultType, operandType));
                break;
            }
            case BinaryOperatorKind::LessOrEqual:
            {
                m_currentBlock->addInstruction(std::make_unique<LessOrEqualInstruction>(result.id(), left, right, resultType, operandType));
                break;
            }
            case BinaryOperatorKind::GreaterThan:
            {
                m_currentBlock->addInstruction(std::make_unique<GreaterThanInstruction>(result.id(), left, right, resultType, operandType));
                break;
            }
            case BinaryOperatorKind::GreaterOrEqual:
            {
                m_currentBlock->addInstruction(std::make_unique<GreaterOrEqualInstruction>(result.id(), left, right, resultType, operandType));
                break;
            }
            default:
            {
                // the id was reserved but nothing uses it, DCE renumbering closes gaps
                return ValueRef{};
            }
        }

        return result;
    }

    ValueRef IREmitter::emitUnary(UnaryOperatorKind unaryOperator, ValueRef operand, Type resultType)
    {
        if (!canEmit() || operand.id() < 0)
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        switch (unaryOperator)
        {
            case UnaryOperatorKind::ValueNegation:
            {
                m_currentBlock->addInstruction(std::make_unique<ValueNegationInstruction>(result.id(), operand, resultType));
                break;
            }
            case UnaryOperatorKind::LogicalNegation:
            {
                m_currentBlock->addInstruction(std::make_unique<LogicalNegationInstruction>(result.id(), operand, resultType));
                break;
            }
            default:
            {
                return ValueRef{};
            }
        }

        return result;
    }

    ValueRef IREmitter::emitBitwiseIntrinsic(IntrinsicKind intrinsicKind, const std::vector<ValueRef>& arguments, Type resultType)
    {
        if (arguments.empty() || !canEmitWith({ arguments[0] }))
        {
            return ValueRef{};
        }

        if (intrinsicKind == IntrinsicKind::BitNot)
        {
            const auto result = ValueRef{ m_nextTemporaryId++ };
            m_currentBlock->addInstruction(std::make_unique<BitNotInstruction>(result.id(), arguments[0], resultType));
            return result;
        }

        if (arguments.size() < 2 || arguments[1].id() < 0)
        {
            return ValueRef{};
        }

        switch (intrinsicKind)
        {
            case IntrinsicKind::BitAnd:
            {
                const auto result = ValueRef{ m_nextTemporaryId++ };
                m_currentBlock->addInstruction(std::make_unique<BitAndInstruction>(result.id(), arguments[0], arguments[1], resultType));
                return result;
            }
            case IntrinsicKind::BitOr:
            {
                const auto result = ValueRef{ m_nextTemporaryId++ };
                m_currentBlock->addInstruction(std::make_unique<BitOrInstruction>(result.id(), arguments[0], arguments[1], resultType));
                return result;
            }
            case IntrinsicKind::BitXor:
            {
                const auto result = ValueRef{ m_nextTemporaryId++ };
                m_currentBlock->addInstruction(std::make_unique<BitXorInstruction>(result.id(), arguments[0], arguments[1], resultType));
                return result;
            }
            case IntrinsicKind::ShiftLeft:
            case IntrinsicKind::ShiftRight:
            {
                // mask the amount so out of range shifts stay defined
                auto maskValue = 31;
                if (const auto* description = m_semanticContext.tryGetBuiltinTypeDescription(resultType))
                {
                    maskValue = description->bits - 1;
                }

                const auto mask = ValueRef{ m_nextTemporaryId++ };
                m_currentBlock->addInstruction(std::make_unique<ConstantInstruction>(mask.id(), ConstantValue::FromI32(maskValue), m_semanticContext.wellKnown().i32));

                const auto maskedAmount = ValueRef{ m_nextTemporaryId++ };
                m_currentBlock->addInstruction(std::make_unique<BitAndInstruction>(maskedAmount.id(), arguments[1], mask, m_semanticContext.wellKnown().i32));

                const auto result = ValueRef{ m_nextTemporaryId++ };
                if (intrinsicKind == IntrinsicKind::ShiftLeft)
                {
                    m_currentBlock->addInstruction(std::make_unique<ShiftLeftInstruction>(result.id(), arguments[0], maskedAmount, resultType));
                }
                else
                {
                    m_currentBlock->addInstruction(std::make_unique<ShiftRightInstruction>(result.id(), arguments[0], maskedAmount, resultType));
                }
                return result;
            }
            default:
            {
                return ValueRef{};
            }
        }
    }

    ValueRef IREmitter::emitCall(FunctionId functionId, std::vector<ValueRef> arguments, Type resultType)
    {
        if (!canEmit())
        {
            return ValueRef{};
        }

        if (resultType == Type::Void())
        {
            m_currentBlock->addInstruction(std::make_unique<CallVoidInstruction>(functionId, std::move(arguments)));
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<CallInstruction>(result.id(), functionId, std::move(arguments), resultType));
        return result;
    }

    ShortCircuitFrame IREmitter::beginShortCircuit(bool isLogicalAnd, ValueRef leftValue)
    {
        if (!canEmit() || leftValue.id() < 0)
        {
            return ShortCircuitFrame{};
        }

        ShortCircuitFrame frame;
        frame.conditionBlockId = m_currentBlock->id();
        frame.rightBlockId = m_nextBlockId++;
        frame.leftValue = leftValue;
        frame.isLogicalAnd = isLogicalAnd;
        frame.active = true;
        if (isLogicalAnd)
        {
            m_currentFunction->addBlock(BasicBlock{ frame.rightBlockId, "and.rhs", nullptr });
        }
        else
        {
            m_currentFunction->addBlock(BasicBlock{ frame.rightBlockId, "or.rhs", nullptr });
        }
        m_currentBlock = m_currentFunction->tryGetBlock(frame.rightBlockId);
        return frame;
    }

    void IREmitter::discardShortCircuit(const ShortCircuitFrame& frame)
    {
        if (!frame.active || m_currentFunction == nullptr)
        {
            return;
        }

        // if the right side folded, no blocks were added and the rhs block is still the last one
        // only reuse the block id if the block was really removed, otherwise two blocks share the id
        if (m_currentFunction->popLastBlock(frame.rightBlockId))
        {
            m_nextBlockId--;
        }

        m_currentBlock = m_currentFunction->tryGetBlock(frame.conditionBlockId);
    }

    ValueRef IREmitter::endShortCircuit(const ShortCircuitFrame& frame, ValueRef rightValue)
    {
        if (!frame.active || !canEmit())
        {
            return ValueRef{};
        }

        if (rightValue.id() < 0)
        {
            recordEmissionFailure();
            return ValueRef{};
        }

        const auto mergeId = m_nextBlockId++;
        auto* conditionBlock = m_currentFunction->tryGetBlock(frame.conditionBlockId);
        if (frame.isLogicalAnd)
        {
            // a false left side jumps straight to the merge block
            m_currentFunction->addBlock(BasicBlock{ mergeId, "and.merge", nullptr });
            conditionBlock->setTerminator(std::make_unique<BranchIfTerminator>(frame.leftValue, frame.rightBlockId, mergeId));
        }
        else
        {
            // a true left side jumps straight to the merge block
            m_currentFunction->addBlock(BasicBlock{ mergeId, "or.merge", nullptr });
            conditionBlock->setTerminator(std::make_unique<BranchIfTerminator>(frame.leftValue, mergeId, frame.rightBlockId));
        }

        auto* rightExitBlock = m_currentBlock;
        rightExitBlock->setTerminator(std::make_unique<JumpTerminator>(mergeId));
        m_currentBlock = m_currentFunction->tryGetBlock(mergeId);

        std::vector<PhiInput> phiInputs;
        phiInputs.emplace_back(frame.conditionBlockId, frame.leftValue);
        phiInputs.emplace_back(rightExitBlock->id(), rightValue);

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<PhiInstruction>(result.id(), std::move(phiInputs), m_semanticContext.wellKnown().boolean));
        return result;
    }

    std::vector<u32> IREmitter::sortedDefinedLocalIds(const LocalsMap& locals) const
    {
        CARACAL_ZONE_NAMED("emit.sortedDefinedLocalIds");
        std::vector<u32> ids;
        ids.reserve(locals.size());
        for (const auto& [nameId, localState] : locals)
        {
            if (localState.type == Type::Undefined())
            {
                continue;
            }
            ids.push_back(nameId);
        }

        // phis for locals are emitted in name order
        std::sort(ids.begin(), ids.end(), [this](u32 left, u32 right)
        {
            return nameLexeme(left) < nameLexeme(right);
        });
        return ids;
    }

    void IREmitter::mergeLocals(BasicBlock& block, const std::vector<IncomingLocals>& incomingValues)
    {
        CARACAL_ZONE_NAMED("emit.mergeLocals");
        LocalsMap mergedLocals;
        if (incomingValues.empty())
        {
            mergedLocals.finishBuild();
            m_locals = std::move(mergedLocals);
            return;
        }

        const auto& firstIncoming = incomingValues.front().values;
        mergedLocals.reserve(firstIncoming.size());
        for (const auto nameId : sortedDefinedLocalIds(firstIncoming))
        {
            // only merge locals defined along every incoming edge
            const auto definedOnAllEdges = std::all_of(
                incomingValues.begin(),
                incomingValues.end(),
                [nameId](const IncomingLocals& incoming)
                {
                    return incoming.values.contains(nameId);
                });
            if (!definedOnAllEdges)
            {
                continue;
            }

            const auto& firstState = firstIncoming.get(nameId);
            const auto requiresPhi = std::any_of(
                incomingValues.begin() + 1,
                incomingValues.end(),
                [nameId, &firstState](const IncomingLocals& incoming)
                {
                    return incoming.values.get(nameId).value.id() != firstState.value.id();
                });
            if (!requiresPhi)
            {
                mergedLocals.append(nameId, firstState);
                continue;
            }

            std::vector<PhiInput> phiInputs;
            phiInputs.reserve(incomingValues.size());
            for (const auto& incoming : incomingValues)
            {
                phiInputs.emplace_back(incoming.predecessorBlockId, incoming.values.get(nameId).value);
            }

            const auto phiId = m_nextTemporaryId++;
            auto phiType = firstState.type;
            if (firstState.storageKind == LocalStorageKind::Address)
            {
                phiType = phiType.toReference();
            }

            block.addInstruction(std::make_unique<PhiInstruction>(phiId, std::move(phiInputs), phiType));
            mergedLocals.append(nameId, LocalState{ ValueRef{ phiId }, firstState.type, firstState.storageKind });
        }

        mergedLocals.finishBuild();
        m_locals = std::move(mergedLocals);
    }

    IfFrame IREmitter::beginIf(ValueRef condition, bool hasElse)
    {
        CARACAL_ZONE_NAMED("emit.beginIf");
        if (!canEmitWith({ condition }))
        {
            return IfFrame{};
        }

        IfFrame frame;
        frame.active = true;
        frame.hasElse = hasElse;
        frame.conditionExitBlockId = m_currentBlock->id();
        frame.preBranchValues = m_locals;
        frame.trueBlockId = m_nextBlockId++;
        m_currentFunction->addBlock(BasicBlock{ frame.trueBlockId, "if.true", nullptr });

        if (hasElse)
        {
            frame.falseBlockId = m_nextBlockId++;
            m_currentBlock->setTerminator(std::make_unique<BranchIfTerminator>(condition, frame.trueBlockId, frame.falseBlockId));
        }
        else
        {
            frame.continuationBlockId = m_nextBlockId++;
            m_currentBlock->setTerminator(std::make_unique<BranchIfTerminator>(condition, frame.trueBlockId, frame.continuationBlockId));
        }

        m_currentBlock = m_currentFunction->tryGetBlock(frame.trueBlockId);
        return frame;
    }

    void IREmitter::endIfThen(IfFrame& frame)
    {
        CARACAL_ZONE_NAMED("emit.endIfThen");
        if (!frame.active)
        {
            return;
        }

        frame.trueFallsThrough = m_currentBlock != nullptr && !m_currentBlock->hasTerminator();
        if (m_currentBlock != nullptr)
        {
            frame.trueExitBlockId = m_currentBlock->id();
        }
        frame.trueExitValues = std::move(m_locals);

        if (frame.hasElse)
        {
            m_currentFunction->addBlock(BasicBlock{ frame.falseBlockId, "if.false", nullptr });
            m_locals = std::move(frame.preBranchValues);
            m_currentBlock = m_currentFunction->tryGetBlock(frame.falseBlockId);
        }
    }

    void IREmitter::endIf(IfFrame& frame)
    {
        CARACAL_ZONE_NAMED("emit.endIf");
        if (!frame.active)
        {
            return;
        }

        if (!frame.hasElse)
        {
            m_currentFunction->addBlock(BasicBlock{ frame.continuationBlockId, "if.continuation", nullptr });
            auto* continuationBlock = m_currentFunction->tryGetBlock(frame.continuationBlockId);

            std::vector<IncomingLocals> continuationInputs;
            continuationInputs.push_back(IncomingLocals{ frame.conditionExitBlockId, std::move(frame.preBranchValues) });
            if (frame.trueFallsThrough)
            {
                auto* trueBlock = m_currentFunction->tryGetBlock(frame.trueExitBlockId);
                trueBlock->setTerminator(std::make_unique<JumpTerminator>(frame.continuationBlockId));
                continuationInputs.push_back(IncomingLocals{ frame.trueExitBlockId, std::move(frame.trueExitValues) });
            }

            mergeLocals(*continuationBlock, continuationInputs);
            m_currentBlock = continuationBlock;
            return;
        }

        const auto falseFallsThrough = m_currentBlock != nullptr && !m_currentBlock->hasTerminator();
        auto falseExitBlockId = BlockId{ -1 };
        if (m_currentBlock != nullptr)
        {
            falseExitBlockId = m_currentBlock->id();
        }
        auto falseExitValues = std::move(m_locals);

        if (!frame.trueFallsThrough && !falseFallsThrough)
        {
            m_currentBlock = nullptr;
            return;
        }

        frame.continuationBlockId = m_nextBlockId++;
        m_currentFunction->addBlock(BasicBlock{ frame.continuationBlockId, "if.continuation", nullptr });
        auto* continuationBlock = m_currentFunction->tryGetBlock(frame.continuationBlockId);

        std::vector<IncomingLocals> continuationInputs;
        if (frame.trueFallsThrough)
        {
            auto* trueBlock = m_currentFunction->tryGetBlock(frame.trueExitBlockId);
            trueBlock->setTerminator(std::make_unique<JumpTerminator>(frame.continuationBlockId));
            continuationInputs.push_back(IncomingLocals{ frame.trueExitBlockId, std::move(frame.trueExitValues) });
        }
        if (falseFallsThrough)
        {
            m_currentBlock->setTerminator(std::make_unique<JumpTerminator>(frame.continuationBlockId));
            continuationInputs.push_back(IncomingLocals{ falseExitBlockId, std::move(falseExitValues) });
        }

        mergeLocals(*continuationBlock, continuationInputs);
        m_currentBlock = continuationBlock;
    }

    WhileFrame IREmitter::beginWhile()
    {
        CARACAL_ZONE_NAMED("emit.beginWhile");
        if (!canEmit())
        {
            return WhileFrame{};
        }

        WhileFrame frame;
        frame.active = true;
        frame.entryBlockId = m_currentBlock->id();
        frame.preLoopValues = m_locals;
        frame.conditionBlockId = m_nextBlockId++;
        frame.loopBlockId = m_nextBlockId++;
        frame.continuationBlockId = m_nextBlockId++;

        m_currentBlock->setTerminator(std::make_unique<JumpTerminator>(frame.conditionBlockId));
        m_currentFunction->addBlock(BasicBlock{ frame.conditionBlockId, "while.condition", nullptr });
        auto* conditionBlock = m_currentFunction->tryGetBlock(frame.conditionBlockId);

        // every value local gets a loop header phi, its inputs patch at endWhile
        frame.loopHeaderValues.reserve(frame.preLoopValues.size());
        for (const auto nameId : sortedDefinedLocalIds(frame.preLoopValues))
        {
            const auto& localState = frame.preLoopValues.get(nameId);
            if (localState.storageKind == LocalStorageKind::Address)
            {
                frame.loopHeaderValues.append(nameId, localState);
                continue;
            }

            std::vector<PhiInput> phiInputs;
            phiInputs.emplace_back(frame.entryBlockId, localState.value);

            const auto phiId = m_nextTemporaryId++;
            auto phiInstruction = std::make_unique<PhiInstruction>(phiId, std::move(phiInputs), localState.type);
            auto* phiInstructionPointer = phiInstruction.get();
            conditionBlock->addInstruction(std::move(phiInstruction));

            frame.loopHeaderValues.append(nameId, LocalState{ ValueRef{ phiId }, localState.type, LocalStorageKind::Value });
            frame.loopHeaderPhis.push_back(LoopHeaderPhi{ nameId, phiInstructionPointer });
        }

        frame.loopHeaderValues.finishBuild();
        m_locals = frame.loopHeaderValues;
        m_currentBlock = conditionBlock;
        return frame;
    }

    void IREmitter::beginWhileBody(WhileFrame& frame, ValueRef condition)
    {
        CARACAL_ZONE_NAMED("emit.beginWhileBody");
        if (!frame.active)
        {
            return;
        }

        if (condition.id() < 0)
        {
            recordEmissionFailure();
            frame.active = false;
            return;
        }

        frame.conditionExitBlockId = m_currentBlock->id();
        m_currentBlock->setTerminator(std::make_unique<BranchIfTerminator>(condition, frame.loopBlockId, frame.continuationBlockId));

        m_currentFunction->addBlock(BasicBlock{ frame.loopBlockId, "while.body", nullptr });
        m_loopContexts.push_back(LoopContext{ frame.conditionBlockId, frame.continuationBlockId, {}, {} });
        m_locals = frame.loopHeaderValues;
        m_currentBlock = m_currentFunction->tryGetBlock(frame.loopBlockId);
    }

    void IREmitter::endWhile(WhileFrame& frame)
    {
        CARACAL_ZONE_NAMED("emit.endWhile");
        if (!frame.active)
        {
            return;
        }

        auto conditionInputs = std::move(m_loopContexts.back().conditionInputs);
        auto continuationInputs = std::move(m_loopContexts.back().continuationInputs);
        m_loopContexts.pop_back();

        if (m_currentBlock != nullptr && !m_currentBlock->hasTerminator())
        {
            conditionInputs.push_back(IncomingLocals{ m_currentBlock->id(), std::move(m_locals) });
            m_currentBlock->setTerminator(std::make_unique<JumpTerminator>(frame.conditionBlockId));
        }

        for (const auto& loopHeaderPhi : frame.loopHeaderPhis)
        {
            std::vector<PhiInput> phiInputs;
            phiInputs.reserve(1 + conditionInputs.size());
            phiInputs.emplace_back(frame.entryBlockId, frame.preLoopValues.get(loopHeaderPhi.nameId).value);
            for (const auto& conditionInput : conditionInputs)
            {
                phiInputs.emplace_back(conditionInput.predecessorBlockId, conditionInput.values.get(loopHeaderPhi.nameId).value);
            }

            loopHeaderPhi.instruction->setInputs(std::move(phiInputs));
        }

        m_currentFunction->addBlock(BasicBlock{ frame.continuationBlockId, "while.continuation", nullptr });
        auto* continuationBlock = m_currentFunction->tryGetBlock(frame.continuationBlockId);

        // a false condition leaves the loop with the locals as they were at the loop header
        continuationInputs.insert(continuationInputs.begin(), IncomingLocals{ frame.conditionExitBlockId, std::move(frame.loopHeaderValues) });
        mergeLocals(*continuationBlock, continuationInputs);
        m_currentBlock = continuationBlock;
    }

    void IREmitter::emitBreak()
    {
        if (!canEmit() || m_loopContexts.empty())
        {
            return;
        }

        auto& loopContext = m_loopContexts.back();
        loopContext.continuationInputs.push_back(IncomingLocals{ m_currentBlock->id(), m_locals });
        m_currentBlock->setTerminator(std::make_unique<JumpTerminator>(loopContext.continuationBlockId));
        m_currentBlock = nullptr;
    }

    void IREmitter::emitSkip()
    {
        if (!canEmit() || m_loopContexts.empty())
        {
            return;
        }

        auto& loopContext = m_loopContexts.back();
        loopContext.conditionInputs.push_back(IncomingLocals{ m_currentBlock->id(), m_locals });
        m_currentBlock->setTerminator(std::make_unique<JumpTerminator>(loopContext.conditionBlockId));
        m_currentBlock = nullptr;
    }

    void IREmitter::emitGlobalConstant(std::string_view name, Type type, ConstantValue value)
    {
        m_module.addGlobalConstant(GlobalConstantDeclaration{ std::string(name), type, std::move(value) });
        m_globalTypes.insert_or_assign(std::string(name), type);
    }

    void IREmitter::emitGlobalReference(std::string_view name, Type type, std::string_view targetName)
    {
        // the target has to be emitted as a global before a reference to it
        if (m_globalTypes.find(std::string(targetName)) == m_globalTypes.end())
        {
            return;
        }

        m_module.addGlobalReference(GlobalReferenceDeclaration{ std::string(name), type, std::string(targetName) });
        m_globalTypes.insert_or_assign(std::string(name), type);
    }

    void IREmitter::emitConstructedGlobal(std::string_view name, Type type)
    {
        m_module.addConstructedGlobal(ConstructedGlobalDeclaration{ std::string(name), type });
        m_globalTypes.insert_or_assign(std::string(name), type);
    }

    ValueRef IREmitter::beginGlobalInitConstruction(std::string_view name, Type type)
    {
        emitConstructedGlobal(name, type);
        resumeGlobalInitEffects();
        return emitGlobalAddress(std::string(name));
    }

    void IREmitter::resumeGlobalInitEffects()
    {
        if (!m_globalInit.function.has_value())
        {
            m_globalInit.function.emplace(Function{ FunctionId{ -1 }, GlobalInitializerName, {}, Type::Void() });
            m_globalInit.function->addBlock(BasicBlock{ m_globalInit.nextBlockId++, "entry", nullptr });
        }

        m_currentFunction = &m_globalInit.function.value();
        m_currentBlock = m_currentFunction->tryGetBlock(0);
        m_currentReturnType = Type::Void();
        m_nextTemporaryId = m_globalInit.nextTemporaryId;
        m_nextLocalSlotId = m_globalInit.nextLocalSlotId;
        m_nextBlockId = m_globalInit.nextBlockId;
    }

    void IREmitter::suspendGlobalInit()
    {
        if (m_currentFunction == nullptr || !m_globalInit.function.has_value())
        {
            return;
        }

        m_globalInit.nextTemporaryId = m_nextTemporaryId;
        m_globalInit.nextLocalSlotId = m_nextLocalSlotId;
        m_globalInit.nextBlockId = m_nextBlockId;
        m_currentFunction = nullptr;
        m_currentBlock = nullptr;
    }

    ValueRef IREmitter::emitGlobalAddress(const std::string& name)
    {
        if (!canEmit())
        {
            return ValueRef{};
        }

        const auto global = m_globalTypes.find(name);
        if (global == m_globalTypes.end())
        {
            return ValueRef{};
        }

        const auto result = ValueRef{ m_nextTemporaryId++ };
        m_currentBlock->addInstruction(std::make_unique<AddressOfGlobalInstruction>(result.id(), name, global->second.toReference()));
        return result;
    }

    ValueRef IREmitter::readGlobal(const std::string& name)
    {
        const auto address = emitGlobalAddress(name);
        if (address.id() < 0)
        {
            return ValueRef{};
        }

        return emitLoad(address, m_globalTypes.find(name)->second.toValue());
    }

    void IREmitter::emitGlobalAssignment(const std::string& name, ValueRef value)
    {
        if (value.id() < 0)
        {
            return;
        }

        const auto address = emitGlobalAddress(name);
        if (address.id() < 0)
        {
            return;
        }

        m_currentBlock->addInstruction(std::make_unique<StoreValueInstruction>(value, address, m_globalTypes.find(name)->second.toValue()));
    }

    void IREmitter::recordEnumFieldConstant(i32 enumTypeId, std::string fieldName, ConstantValue constant, std::optional<FoldValue> folded)
    {
        m_enumFieldConstants.insert_or_assign(
            std::make_pair(enumTypeId, std::move(fieldName)),
            EnumFieldConstant{ std::move(constant), std::move(folded) });
    }

    const IREmitter::EnumFieldConstant* IREmitter::tryGetEnumFieldConstant(i32 enumTypeId, const std::string& fieldName) const
    {
        const auto found = m_enumFieldConstants.find(std::make_pair(enumTypeId, fieldName));
        if (found == m_enumFieldConstants.end())
        {
            return nullptr;
        }
        return &found->second;
    }

    std::optional<ConstantValue> IREmitter::makeEnumConstantValue(Type baseType, i32 value) const
    {
        const auto* description = m_semanticContext.tryGetBuiltinTypeDescription(baseType);
        if (description == nullptr)
        {
            return std::nullopt;
        }

        switch (description->kind)
        {
            case BuiltinTypeKind::Bool:
            {
                return ConstantValue::FromBool(value != 0);
            }
            case BuiltinTypeKind::Int:
            {
                if (!description->isSigned && description->bits <= 8)
                {
                    return ConstantValue::FromU8(static_cast<u8>(value));
                }

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

    ValueRef IREmitter::emitEnumMemberConstant(Type enumType, const std::string& enumName, const std::string& fieldName, const ConstantValue& underlying)
    {
        const auto* literalData = underlying.tryGetLiteralData();
        if (literalData == nullptr)
        {
            return ValueRef{};
        }

        return emitConstant(ConstantValue::FromEnum(enumType, enumName, fieldName, *literalData), enumType);
    }

    void IREmitter::recordExternDefinition(Type functionType)
    {
        m_pendingExternDefinitions.push_back(functionType);
    }

    ConstantValue IREmitter::constantFromFold(const FoldValue& value) noexcept
    {
        return std::visit(
            [](const auto& payload)
            {
                return ConstantValue::FromLiteralData(ConstantValue::LiteralData{ payload });
            },
            value);
    }

    void IREmitter::recordCalledPreludeFunction(Type functionType)
    {
        if (!canEmit())
        {
            return;
        }

        const auto& definition = m_semanticContext.getFunctionDefinition(functionType);
        if (definition.symbolName().has_value() || definition.functionType() == FunctionType::Intrinsic)
        {
            return;
        }

        if (m_isPreludePass)
        {
            // we also need to record function calls in prelude bodies
            if (m_currentCalledPreludeFunctionIds.insert(functionType.id()).second)
            {
                m_currentCalledPreludeFunctions.push_back(functionType);
            }
            return;
        }

        if (!m_semanticContext.isPreludeFunctionDefinition(functionType))
        {
            return;
        }

        if (m_queuedPreludeFunctionIds.insert(functionType.id()).second)
        {
            m_requiredPreludeFunctions.push_back(functionType);
        }
    }

    void IREmitter::recordDefaultArgumentFold(u16 fileId, u32 defaultNode, FoldValue value)
    {
        m_parameterDefaultFolds.insert_or_assign((static_cast<u64>(fileId) << 32) | defaultNode, value);
    }

    ValueRef IREmitter::emitDefaultArgument(u16 fileId, u32 defaultNode, Type defaultType, Type parameterType)
    {
        const auto fold = m_parameterDefaultFolds.find((static_cast<u64>(fileId) << 32) | defaultNode);
        if (fold == m_parameterDefaultFolds.end())
        {
            return ValueRef{};
        }

        const auto defaultValue = emitConstant(constantFromFold(fold->second), defaultType);
        return emitWidened(defaultValue, defaultType, parameterType);
    }

    ValueRef IREmitter::localOrGlobalAddress(u32 nameId, std::string_view name)
    {
        const auto address = addressOfLocal(nameId);
        if (address.id() >= 0)
        {
            return address;
        }

        return emitGlobalAddress(std::string(name));
    }

    void IREmitter::registerExternDefinition(const FunctionDefinition& definition)
    {
        const auto functionId = definition.type().id();
        if (m_module.tryGetExternFunction(functionId) != nullptr)
        {
            return;
        }

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

        m_module.addExternFunction(ExternFunction{ functionId, definition.fullName(), definition.symbolName(), parameters, returnType });
    }

    bool IREmitter::installPreludeFunction(Type functionType)
    {
        auto preludeBody = m_semanticContext.preludeFunctionPool().take(functionType.id());
        if (!preludeBody.has_value())
        {
            return false;
        }

        for (const auto calledFunction : preludeBody->calledPreludeFunctions)
        {
            if (m_queuedPreludeFunctionIds.insert(calledFunction.id()).second)
            {
                m_requiredPreludeFunctions.push_back(calledFunction);
            }
        }
        m_module.addFunction(std::move(preludeBody->body));
        return true;
    }

    void IREmitter::finishModule()
    {
        CARACAL_ZONE_NAMED("emit.finishModule");
        m_module.setWellKnownTypes(m_semanticContext.wellKnown());
        registerBuiltinTypes();
        registerRequiredExterns();
        installCompletedFunctions();
        installRequiredPreludeFunctions();
        installGlobalInit();
        registerPendingExterns();
        addRequiredPreludeTypes();
        addTypeDeclarations();
        registerArrayTypes();
        addEnumDeclarations();
    }

    void IREmitter::registerBuiltinTypes()
    {
        CARACAL_ZONE_NAMED("finish.registerBuiltinTypes");
        for (const auto& [typeId, description] : m_semanticContext.builtinTypeDescriptions())
        {
            const auto builtinType = Type{ typeId, TypeKind::Builtin };
            m_module.registerBuiltinTypeDescription(builtinType, description);
            m_module.registerTypeName(builtinType, std::string(m_semanticContext.getNameByType(builtinType)));
        }
    }

    void IREmitter::registerRequiredExterns()
    {
        CARACAL_ZONE_NAMED("finish.registerRequiredExterns");
        for (const auto functionType : m_semanticContext.requiredExternFunctions())
        {
            registerExternDefinition(m_semanticContext.getFunctionDefinition(functionType));
        }
    }

    void IREmitter::installCompletedFunctions()
    {
        CARACAL_ZONE_NAMED("finish.installCompletedFunctions");
        // we also need to reserve space for the prelude functions, growing the module later would move every function in it
        m_module.reserveFunctions(m_module.functions().size() + m_completedFunctions.size() + m_requiredPreludeFunctions.size());
        std::unordered_map<i32, size_t> completedById;
        completedById.reserve(m_completedFunctions.size());
        for (size_t completedIndex = 0; completedIndex < m_completedFunctions.size(); ++completedIndex)
        {
            completedById.insert_or_assign(m_completedFunctions[completedIndex].id(), completedIndex);
        }

        for (size_t definitionIndex = m_semanticContext.preludeFunctionDefinitionCount(); definitionIndex < m_semanticContext.functionDefinitions().size(); ++definitionIndex)
        {
            const auto functionId = m_semanticContext.functionDefinitions()[definitionIndex].type().id();
            if (const auto completed = completedById.find(functionId); completed != completedById.end())
            {
                m_module.addFunction(std::move(m_completedFunctions[completed->second]));
                m_completedFunctions[completed->second] = Function{};
            }
        }
    }

    void IREmitter::installRequiredPreludeFunctions()
    {
        for (size_t queueIndex = 0; queueIndex < m_requiredPreludeFunctions.size(); ++queueIndex)
        {
            if (!installPreludeFunction(m_requiredPreludeFunctions[queueIndex]))
            {
                // set directly, recordEmissionFailure ignores failures outside a function
                m_emissionFailed = true;
            }
        }
    }

    void IREmitter::installGlobalInit()
    {
        if (!m_globalInit.function.has_value())
        {
            return;
        }

        auto* entry = m_globalInit.function->tryGetBlock(0);
        if (entry != nullptr && !entry->hasTerminator())
        {
            entry->setTerminator(std::make_unique<ReturnTerminator>());
        }
        m_globalInit.function->setTemporaryCount(m_globalInit.nextTemporaryId);
        m_module.setGlobalInit(std::move(m_globalInit.function.value()));
        m_globalInit.function.reset();
    }

    void IREmitter::registerPendingExterns()
    {
        for (const auto functionType : m_pendingExternDefinitions)
        {
            registerExternDefinition(m_semanticContext.getFunctionDefinition(functionType));
        }
    }

    void IREmitter::addRequiredPreludeTypes()
    {
        CARACAL_ZONE_NAMED("finish.addRequiredPreludeTypes");
        for (const auto requiredType : m_semanticContext.requiredPreludeTypes())
        {
            m_module.addType(MakeTypeDeclaration(m_semanticContext.getTypeDefinition(requiredType)));
        }
    }

    void IREmitter::addTypeDeclarations()
    {
        CARACAL_ZONE_NAMED("finish.addTypeDeclarations");
        for (size_t typeIndex = m_semanticContext.preludeTypeDefinitionCount(); typeIndex < m_semanticContext.typeDefinitions().size(); ++typeIndex)
        {
            const auto& definition = m_semanticContext.typeDefinitions()[typeIndex];
            if (definition.definitionNode().index == InvalidNodeIndex
                || definition.type().kind() == TypeKind::Builtin
                || definition.type() == Type::Undefined())
            {
                continue;
            }

            m_module.addType(MakeTypeDeclaration(definition));
        }
    }

    // array types have no TypeDeclaration, the printer needs their canonical names
    void IREmitter::registerArrayTypes()
    {
        CARACAL_ZONE_NAMED("finish.registerArrayTypes");
        for (const auto arrayType : m_semanticContext.arrayTypes())
        {
            m_module.registerTypeName(arrayType, std::string(m_semanticContext.getNameByType(arrayType)));
            m_module.registerArrayType(
                arrayType,
                m_semanticContext.getArrayElementType(arrayType),
                m_semanticContext.getArrayLength(arrayType));
        }
    }

    void IREmitter::addEnumDeclarations()
    {
        CARACAL_ZONE_NAMED("finish.addEnumDeclarations");
        for (const auto& enumDefinition : m_semanticContext.enumDefinitions())
        {
            auto enumDeclaration = EnumDeclaration{ enumDefinition.name(), enumDefinition.type(), enumDefinition.baseType() };
            auto complete = true;
            for (const auto& field : enumDefinition.fields())
            {
                if (field.hasConstantValue())
                {
                    const auto constant = makeEnumConstantValue(enumDefinition.baseType(), field.value());
                    if (!constant.has_value())
                    {
                        complete = false;
                        break;
                    }
                    enumDeclaration.addField(field.name(), constant.value());
                    continue;
                }

                const auto* fieldConstant = tryGetEnumFieldConstant(enumDefinition.type().id(), field.name());
                if (fieldConstant == nullptr)
                {
                    complete = false;
                    break;
                }
                enumDeclaration.addField(field.name(), fieldConstant->constant);
            }

            if (complete)
            {
                m_module.addEnum(std::move(enumDeclaration));
            }
        }
    }
}
