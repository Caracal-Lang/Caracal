#include <Caracal/CodeGen/LLVMCodeGenerator.h>
#include <Caracal/Profiling.h>

#include <Caracal/IR/AddInstruction.h>
#include <Caracal/IR/AddressOfFieldInstruction.h>
#include <Caracal/IR/AddressOfGlobalInstruction.h>
#include <Caracal/IR/AddressOfInstruction.h>
#include <Caracal/IR/AllocateLocalInstruction.h>
#include <Caracal/IR/BasicBlock.h>
#include <Caracal/IR/BranchIfTerminator.h>
#include <Caracal/IR/CallInstruction.h>
#include <Caracal/IR/CallVoidInstruction.h>
#include <Caracal/IR/ConstantInstruction.h>
#include <Caracal/IR/GlobalConstantDeclaration.h>
#include <Caracal/IR/GlobalReferenceDeclaration.h>
#include <Caracal/IR/DivideInstruction.h>
#include <Caracal/IR/ElementAddressInstruction.h>
#include <Caracal/IR/EqualInstruction.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/GreaterOrEqualInstruction.h>
#include <Caracal/IR/GreaterThanInstruction.h>
#include <Caracal/IR/IntToFloatInstruction.h>
#include <Caracal/IR/IntWidenInstruction.h>
#include <Caracal/IR/ArrayAddInstruction.h>
#include <Caracal/IR/ArrayCopyInstruction.h>
#include <Caracal/IR/ArrayRemoveInstruction.h>
#include <Caracal/IR/SizeOfInstruction.h>
#include <Caracal/IR/JumpTerminator.h>
#include <Caracal/IR/LessOrEqualInstruction.h>
#include <Caracal/IR/LessThanInstruction.h>
#include <Caracal/IR/LoadValueInstruction.h>
#include <Caracal/IR/BitAndInstruction.h>
#include <Caracal/IR/BitNotInstruction.h>
#include <Caracal/IR/BitOrInstruction.h>
#include <Caracal/IR/BitXorInstruction.h>
#include <Caracal/IR/LogicalNegationInstruction.h>
#include <Caracal/IR/ShiftLeftInstruction.h>
#include <Caracal/IR/ShiftRightInstruction.h>
#include <Caracal/IR/MakeSliceInstruction.h>
#include <Caracal/IR/MultiplyInstruction.h>
#include <Caracal/IR/NotEqualInstruction.h>
#include <Caracal/IR/ParameterInstruction.h>
#include <Caracal/IR/PhiInstruction.h>
#include <Caracal/IR/ReturnValueTerminator.h>
#include <Caracal/IR/StoreValueInstruction.h>
#include <Caracal/IR/SubtractInstruction.h>
#include <Caracal/IR/Terminator.h>
#include <Caracal/IR/ValueNegationInstruction.h>

#include <llvm/ADT/APFloat.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

#include <cstdint>
#include <type_traits>
#include <variant>
#include <vector>

namespace Caracal
{
    LLVMCodeGenerator::LLVMCodeGenerator(const Module& irModule, llvm::Module& llvmModule)
        : m_irModule{ irModule }
        , m_llvmModule{ llvmModule }
        , m_irBuilder{ std::make_unique<llvm::IRBuilder<>>(llvmModule.getContext()) }
        , m_currentFunction{ nullptr }
    {
    }

    // defaulted here so the unique_ptr<llvm::IRBuilderBase> member is
    // destroyed where the type is complete
    LLVMCodeGenerator::~LLVMCodeGenerator() = default;

    bool LLVMCodeGenerator::generate()
    {
        CARACAL_ZONE_NAMED("codegen");
        if (!lowerTypes())
            return false;

        if (!declareCallables())
            return false;

        if (!lowerGlobals())
            return false;

        if (!lowerFunctionBodies())
            return false;

        if (!lowerGlobalInit())
            return false;

        return true;
    }

    bool LLVMCodeGenerator::lowerTypes() noexcept
    {
        auto& context = m_llvmModule.getContext();

        // create every struct as opaque first so fields and signatures can reference any type regardless of order
        for (const auto& typeDeclaration : m_irModule.types())
            llvm::StructType::create(context, typeDeclaration.name());

        // then fill in the bodies in declared field order
        for (const auto& typeDeclaration : m_irModule.types())
        {
            auto* structType = llvm::StructType::getTypeByName(context, typeDeclaration.name());
            if (structType == nullptr)
                return false;

            std::vector<llvm::Type*> fieldTypes;
            for (const auto& field : typeDeclaration.fields())
            {
                auto* fieldType = lowerType(field.type);
                if (fieldType == nullptr)
                    return false;

                fieldTypes.push_back(fieldType);
            }

            structType->setBody(fieldTypes);
        }

        return true;
    }

    bool LLVMCodeGenerator::declareCallables() noexcept
    {
        for (const auto& externFunction : m_irModule.externFunctions())
        {
            const auto& symbolName = externFunction.symbolName().value_or(externFunction.name());
            if (!declareCallable(symbolName, externFunction.returnType(), externFunction.parameters()))
                return false;
        }

        for (const auto& function : m_irModule.functions())
        {
            const auto& symbolName = function.symbolName().value_or(function.name());
            if (!declareCallable(symbolName, function.returnType(), function.parameters()))
                return false;
        }

        return true;
    }

    bool LLVMCodeGenerator::lowerGlobals() noexcept
    {
        for (const auto& globalConstant : m_irModule.globalConstants())
        {
            if (!lowerGlobalConstant(globalConstant))
                return false;
        }

        // emitted after the constants so a reference can resolve its target global by name
        for (const auto& globalReference : m_irModule.globalReferences())
        {
            if (!lowerGlobalReference(globalReference))
                return false;
        }

        for (const auto& constructedGlobal : m_irModule.constructedGlobals())
        {
            if (!lowerConstructedGlobal(constructedGlobal))
                return false;
        }

        return true;
    }

    bool LLVMCodeGenerator::lowerFunctionBodies() noexcept
    {
        for (const auto& function : m_irModule.functions())
        {
            if (!lowerFunctionBody(function))
                return false;
        }

        return true;
    }

    bool LLVMCodeGenerator::lowerGlobalInit() noexcept
    {
        const auto* globalInit = m_irModule.tryGetGlobalInit();
        if (globalInit == nullptr)
            return true;

        if (!declareCallable(globalInit->name(), globalInit->returnType(), globalInit->parameters()))
            return false;

        if (!lowerFunctionBody(*globalInit))
            return false;

        auto* initFunction = m_llvmModule.getFunction(globalInit->name());
        if (initFunction == nullptr)
            return false;

        llvm::appendToGlobalCtors(m_llvmModule, initFunction, /*priority=*/65535);
        return true;
    }

    bool LLVMCodeGenerator::lowerGlobalConstant(const GlobalConstantDeclaration& globalConstant) noexcept
    {
        llvm::Constant* initializer = nullptr;
        if (globalConstant.value().tryGetAggregate() != nullptr)
        {
            auto* globalType = lowerType(globalConstant.type());
            if (globalType == nullptr)
                return false;

            initializer = lowerAggregateConstant(globalConstant.value(), globalType);
        }
        else
        {
            initializer = llvm::dyn_cast_or_null<llvm::Constant>(lowerConstant(globalConstant.value()));
        }

        if (initializer == nullptr)
            return false;

        new llvm::GlobalVariable(
            m_llvmModule,
            initializer->getType(),
            true, // is constant
            llvm::GlobalValue::ExternalLinkage,
            initializer,
            globalConstant.name());
        return true;
    }

    bool LLVMCodeGenerator::lowerGlobalReference(const GlobalReferenceDeclaration& globalReference) noexcept
    {
        auto* target = m_llvmModule.getNamedGlobal(globalReference.targetName());
        if (target == nullptr)
            return false;

        // a global reference is the constant address of another global
        new llvm::GlobalVariable(
            m_llvmModule,
            target->getType(),
            true, // is constant
            llvm::GlobalValue::ExternalLinkage,
            target,
            globalReference.name());
        return true;
    }

    bool LLVMCodeGenerator::lowerConstructedGlobal(const ConstructedGlobalDeclaration& constructedGlobal) noexcept
    {
        auto* type = lowerType(constructedGlobal.type());
        if (type == nullptr)
            return false;

        new llvm::GlobalVariable(
            m_llvmModule,
            type,
            false, // not constant
            llvm::GlobalValue::ExternalLinkage,
            llvm::Constant::getNullValue(type),
            constructedGlobal.name());
        return true;
    }

    bool LLVMCodeGenerator::declareCallable(const std::string& name, Type returnType, const std::vector<IRParameter>& parameters) noexcept
    {
        auto* functionType = tryLowerFunctionType(returnType, parameters);
        if (functionType == nullptr)
            return false;

        auto* function = llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, name, m_llvmModule);

        unsigned argumentIndex = 0;
        for (const auto& parameter : parameters)
        {
            if (parameter.type().isReference())
                function->addParamAttr(argumentIndex, llvm::Attribute::NonNull);

            ++argumentIndex;
        }

        return true;
    }

    bool LLVMCodeGenerator::lowerFunctionBody(const Function& function) noexcept
    {
        m_values.clear();
        m_slots.clear();
        m_blocks.clear();
        m_pendingPhis.clear();

        const auto symbolName = function.symbolName().value_or(function.name());
        m_currentFunction = m_llvmModule.getFunction(symbolName);
        if (m_currentFunction == nullptr)
            return false;

        if (!function.hasBlocks())
            return false;

        // pre-create every block (the first is the entry) so jumps, branches and phis can reference them
        auto& context = m_llvmModule.getContext();
        for (const auto& block : function.blocks())
            m_blocks[block->id()] = llvm::BasicBlock::Create(context, block->label(), m_currentFunction);

        for (const auto& block : function.blocks())
        {
            m_irBuilder->SetInsertPoint(m_blocks[block->id()]);

            for (const auto& instruction : block->instructions())
            {
                if (!lowerInstruction(*instruction))
                    return false;
            }

            if (!block->hasTerminator())
                return false;

            if (!lowerTerminator(*block->terminator()))
                return false;
        }

        // wire up phi incomings now that every block is built and every value is defined
        for (const auto& [phi, node] : m_pendingPhis)
        {
            for (const auto& input : phi->inputs())
            {
                auto* incomingValue = tryResolve(input.value());
                auto* incomingBlock = tryGetBlock(input.blockId());
                if (incomingValue == nullptr || incomingBlock == nullptr)
                    return false;

                node->addIncoming(incomingValue, incomingBlock);
            }
        }

        return true;
    }

    bool LLVMCodeGenerator::lowerInstruction(const Instruction& instruction) noexcept
    {
        switch (instruction.kind())
        {
            case InstructionKind::Parameter:
            {
                const auto& parameter = static_cast<const ParameterInstruction&>(instruction);
                defineValue(parameter.resultId(), m_currentFunction->getArg(static_cast<unsigned>(parameter.parameterIndex())));
                return true;
            }
            case InstructionKind::Constant:
            {
                const auto& constant = static_cast<const ConstantInstruction&>(instruction);
                auto* value = lowerConstant(constant.value());
                if (value == nullptr)
                    return false;

                defineValue(constant.resultId(), value);
                return true;
            }
            case InstructionKind::AddressOfGlobal:
            {
                const auto& addressOfGlobal = static_cast<const AddressOfGlobalInstruction&>(instruction);
                auto* global = m_llvmModule.getNamedGlobal(addressOfGlobal.name());
                if (global == nullptr)
                    return false;

                defineValue(addressOfGlobal.resultId(), global);
                return true;
            }
            case InstructionKind::AllocateLocal:
            {
                const auto& allocate = static_cast<const AllocateLocalInstruction&>(instruction);
                auto* slotType = lowerType(allocate.type());
                if (slotType == nullptr)
                    return false;

                // allocas always live in the entry block, so loops reuse one slot and phis stay first in their block
                auto* entryBlock = &m_currentFunction->getEntryBlock();
                if (m_irBuilder->GetInsertBlock() != entryBlock)
                {
                    llvm::IRBuilderBase::InsertPointGuard guard{ *m_irBuilder };
                    if (auto* terminator = entryBlock->getTerminator(); terminator != nullptr)
                    {
                        m_irBuilder->SetInsertPoint(terminator);
                    }
                    else
                    {
                        m_irBuilder->SetInsertPoint(entryBlock);
                    }

                    m_slots[allocate.slotId()] = m_irBuilder->CreateAlloca(slotType, nullptr, allocate.localName());
                    return true;
                }

                m_slots[allocate.slotId()] = m_irBuilder->CreateAlloca(slotType, nullptr, allocate.localName());
                return true;
            }
            case InstructionKind::AddressOf:
            {
                // the address of a slot is just the slot's alloca pointer, no instruction is emitted
                const auto& addressOf = static_cast<const AddressOfInstruction&>(instruction);
                const auto slot = m_slots.find(addressOf.local().id());
                if (slot == m_slots.end())
                    return false;

                defineValue(addressOf.resultId(), slot->second);
                return true;
            }
            case InstructionKind::FieldAddress:
            {
                const auto& fieldAddress = static_cast<const AddressOfFieldInstruction&>(instruction);
                auto* objectAddress = tryResolve(fieldAddress.objectAddress());
                auto* structType = lowerType(fieldAddress.objectType());
                if (objectAddress == nullptr || structType == nullptr)
                    return false;

                auto* fieldPointer = m_irBuilder->CreateStructGEP(structType, objectAddress, static_cast<unsigned>(fieldAddress.fieldIndex()), fieldAddress.fieldName());
                defineValue(fieldAddress.resultId(), fieldPointer);
                return true;
            }
            case InstructionKind::ElementAddress:
            {
                const auto& elementAddress = static_cast<const ElementAddressInstruction&>(instruction);
                auto* baseAddress = tryResolve(elementAddress.baseAddress());
                auto* index = tryResolve(elementAddress.index());
                if (baseAddress == nullptr || index == nullptr)
                    return false;

                // a slice's or dynamic array's base is its loaded data pointer, so the address is element-typed
                if (elementAddress.arrayType().kind() == TypeKind::Slice
                    || elementAddress.arrayType().kind() == TypeKind::DynamicArray)
                {
                    auto* elementType = lowerType(elementAddress.type());
                    if (elementType == nullptr)
                        return false;

                    defineValue(elementAddress.resultId(), m_irBuilder->CreateGEP(elementType, baseAddress, { index }, "element"));
                    return true;
                }

                auto* arrayType = lowerType(elementAddress.arrayType());
                if (arrayType == nullptr)
                    return false;

                auto* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(m_llvmModule.getContext()), 0);
                defineValue(elementAddress.resultId(), m_irBuilder->CreateGEP(arrayType, baseAddress, { zero, index }, "element"));
                return true;
            }
            case InstructionKind::MakeSlice:
            {
                const auto& makeSlice = static_cast<const MakeSliceInstruction&>(instruction);
                auto* baseAddress = tryResolve(makeSlice.baseAddress());
                auto* length = tryResolve(makeSlice.length());
                auto* sliceType = lowerType(makeSlice.type());
                if (baseAddress == nullptr || length == nullptr || sliceType == nullptr)
                    return false;

                llvm::Value* slice = llvm::PoisonValue::get(sliceType);
                slice = m_irBuilder->CreateInsertValue(slice, baseAddress, { 0 });
                slice = m_irBuilder->CreateInsertValue(slice, length, { 1 }, "slice");
                defineValue(makeSlice.resultId(), slice);
                return true;
            }
            case InstructionKind::ArrayAdd:
            {
                const auto& arrayAdd = static_cast<const ArrayAddInstruction&>(instruction);
                auto* descriptorAddress = tryResolve(arrayAdd.descriptorAddress());
                auto* value = tryResolve(arrayAdd.value());
                auto* descriptorType = lowerType(arrayAdd.arrayType());
                auto* reallocCallee = tryResolveCallee(arrayAdd.reallocFunctionId());
                const auto* arrayInfo = m_irModule.tryGetArrayType(arrayAdd.arrayType());
                if (descriptorAddress == nullptr || value == nullptr || descriptorType == nullptr || reallocCallee == nullptr || arrayInfo == nullptr)
                    return false;

                auto* elementType = lowerType(arrayInfo->elementType);
                if (elementType == nullptr)
                    return false;

                auto& context = m_llvmModule.getContext();
                auto* i32Type = llvm::Type::getInt32Ty(context);
                auto* i64Type = llvm::Type::getInt64Ty(context);
                auto* dataPointerAddress = m_irBuilder->CreateStructGEP(descriptorType, descriptorAddress, 0, "data");
                auto* lengthAddress = m_irBuilder->CreateStructGEP(descriptorType, descriptorAddress, 1, "length");
                auto* capacityAddress = m_irBuilder->CreateStructGEP(descriptorType, descriptorAddress, 2, "capacity");
                auto* data = m_irBuilder->CreateLoad(llvm::PointerType::getUnqual(context), dataPointerAddress, "load");
                auto* length = m_irBuilder->CreateLoad(i32Type, lengthAddress, "load");
                auto* capacity = m_irBuilder->CreateLoad(i32Type, capacityAddress, "load");

                // branchless growth, a same-size realloc when there is room keeps the pointer stable
                auto* isFull = m_irBuilder->CreateICmpEQ(length, capacity, "is_full");
                auto* doubled = m_irBuilder->CreateMul(capacity, llvm::ConstantInt::get(i32Type, 2), "doubled");
                auto* newCapacity = m_irBuilder->CreateSelect(isFull, doubled, capacity, "new_capacity");
                auto* newCapacity64 = m_irBuilder->CreateSExt(newCapacity, i64Type, "widened");
                const auto elementSize = m_llvmModule.getDataLayout().getTypeAllocSize(elementType);
                auto* byteCount = m_irBuilder->CreateMul(newCapacity64, llvm::ConstantInt::get(i64Type, elementSize.getFixedValue()), "bytes");
                auto* newData = m_irBuilder->CreateCall(reallocCallee, { data, byteCount }, "grown");
                m_irBuilder->CreateStore(newData, dataPointerAddress);
                m_irBuilder->CreateStore(newCapacity, capacityAddress);

                auto* elementAddress = m_irBuilder->CreateGEP(elementType, newData, { length }, "element");
                m_irBuilder->CreateStore(value, elementAddress);
                auto* newLength = m_irBuilder->CreateAdd(length, llvm::ConstantInt::get(i32Type, 1), "new_length");
                m_irBuilder->CreateStore(newLength, lengthAddress);
                return true;
            }
            case InstructionKind::ArrayRemove:
            {
                const auto& arrayRemove = static_cast<const ArrayRemoveInstruction&>(instruction);
                auto* descriptorAddress = tryResolve(arrayRemove.descriptorAddress());
                auto* index = tryResolve(arrayRemove.index());
                auto* descriptorType = lowerType(arrayRemove.arrayType());
                auto* memmoveCallee = tryResolveCallee(arrayRemove.memmoveFunctionId());
                const auto* arrayInfo = m_irModule.tryGetArrayType(arrayRemove.arrayType());
                if (descriptorAddress == nullptr || index == nullptr || descriptorType == nullptr || memmoveCallee == nullptr || arrayInfo == nullptr)
                    return false;

                auto* elementType = lowerType(arrayInfo->elementType);
                if (elementType == nullptr)
                    return false;

                auto& context = m_llvmModule.getContext();
                auto* i32Type = llvm::Type::getInt32Ty(context);
                auto* i64Type = llvm::Type::getInt64Ty(context);
                auto* dataPointerAddress = m_irBuilder->CreateStructGEP(descriptorType, descriptorAddress, 0, "data");
                auto* lengthAddress = m_irBuilder->CreateStructGEP(descriptorType, descriptorAddress, 1, "length");
                auto* data = m_irBuilder->CreateLoad(llvm::PointerType::getUnqual(context), dataPointerAddress, "load");
                auto* length = m_irBuilder->CreateLoad(i32Type, lengthAddress, "load");

                // shift the tail one slot left over the removed element, memmove handles the overlap
                auto* destination = m_irBuilder->CreateGEP(elementType, data, { index }, "element");
                auto* sourceIndex = m_irBuilder->CreateAdd(index, llvm::ConstantInt::get(i32Type, 1), "source_index");
                auto* source = m_irBuilder->CreateGEP(elementType, data, { sourceIndex }, "element");
                auto* tailCount = m_irBuilder->CreateSub(length, sourceIndex, "tail_count");
                auto* tailCount64 = m_irBuilder->CreateSExt(tailCount, i64Type, "widened");
                const auto elementSize = m_llvmModule.getDataLayout().getTypeAllocSize(elementType);
                auto* byteCount = m_irBuilder->CreateMul(tailCount64, llvm::ConstantInt::get(i64Type, elementSize.getFixedValue()), "bytes");
                m_irBuilder->CreateCall(memmoveCallee, { destination, source, byteCount }, "shifted");
                auto* newLength = m_irBuilder->CreateSub(length, llvm::ConstantInt::get(i32Type, 1), "new_length");
                m_irBuilder->CreateStore(newLength, lengthAddress);
                return true;
            }
            case InstructionKind::ArrayCopy:
            {
                const auto& arrayCopy = static_cast<const ArrayCopyInstruction&>(instruction);
                auto* sourceAddress = tryResolve(arrayCopy.sourceAddress());
                auto* descriptorType = lowerType(arrayCopy.arrayType());
                auto* callocCallee = tryResolveCallee(arrayCopy.callocFunctionId());
                auto* memmoveCallee = tryResolveCallee(arrayCopy.memmoveFunctionId());
                const auto* arrayInfo = m_irModule.tryGetArrayType(arrayCopy.arrayType());
                if (sourceAddress == nullptr || descriptorType == nullptr || callocCallee == nullptr || memmoveCallee == nullptr || arrayInfo == nullptr)
                    return false;

                auto* elementType = lowerType(arrayInfo->elementType);
                if (elementType == nullptr)
                    return false;

                auto& context = m_llvmModule.getContext();
                auto* i32Type = llvm::Type::getInt32Ty(context);
                auto* i64Type = llvm::Type::getInt64Ty(context);
                auto* dataPointerAddress = m_irBuilder->CreateStructGEP(descriptorType, sourceAddress, 0, "data");
                auto* lengthAddress = m_irBuilder->CreateStructGEP(descriptorType, sourceAddress, 1, "length");
                auto* capacityAddress = m_irBuilder->CreateStructGEP(descriptorType, sourceAddress, 2, "capacity");
                auto* sourceData = m_irBuilder->CreateLoad(llvm::PointerType::getUnqual(context), dataPointerAddress, "load");
                auto* length = m_irBuilder->CreateLoad(i32Type, lengthAddress, "load");
                auto* capacity = m_irBuilder->CreateLoad(i32Type, capacityAddress, "load");

                // a copy keeps the source capacity, every dynamic array is created with at least capacity 8;
                // a NUL-reserving copy allocates length + 1 zeroed bytes instead
                const auto elementSize = m_llvmModule.getDataLayout().getTypeAllocSize(elementType);
                auto* elementSizeValue = llvm::ConstantInt::get(i64Type, elementSize.getFixedValue());
                llvm::Value* newData = nullptr;
                llvm::Value* length64 = nullptr;
                llvm::Value* descriptorCapacity = nullptr;
                if (arrayCopy.reserveNulByte())
                {
                    length64 = m_irBuilder->CreateSExt(length, i64Type, "widened");
                    auto* callocCount = m_irBuilder->CreateAdd(length64, llvm::ConstantInt::get(i64Type, 1), "nul_reserved");
                    newData = m_irBuilder->CreateCall(callocCallee, { callocCount, elementSizeValue }, "copy");
                    descriptorCapacity = length;
                }
                else
                {
                    auto* capacity64 = m_irBuilder->CreateSExt(capacity, i64Type, "widened");
                    newData = m_irBuilder->CreateCall(callocCallee, { capacity64, elementSizeValue }, "copy");
                    length64 = m_irBuilder->CreateSExt(length, i64Type, "widened");
                    descriptorCapacity = capacity;
                }
                auto* byteCount = m_irBuilder->CreateMul(length64, elementSizeValue, "bytes");
                m_irBuilder->CreateCall(memmoveCallee, { newData, sourceData, byteCount }, "copied");

                llvm::Value* descriptor = llvm::PoisonValue::get(descriptorType);
                descriptor = m_irBuilder->CreateInsertValue(descriptor, newData, { 0 });
                descriptor = m_irBuilder->CreateInsertValue(descriptor, length, { 1 });
                descriptor = m_irBuilder->CreateInsertValue(descriptor, descriptorCapacity, { 2 }, "array_copy");
                defineValue(arrayCopy.resultId(), descriptor);
                return true;
            }
            case InstructionKind::LoadValue:
            {
                const auto& load = static_cast<const LoadValueInstruction&>(instruction);
                auto* address = tryResolve(load.address());
                auto* valueType = lowerType(load.type());
                if (address == nullptr || valueType == nullptr)
                    return false;

                defineValue(load.resultId(), m_irBuilder->CreateLoad(valueType, address, "load"));
                return true;
            }
            case InstructionKind::StoreValue:
            {
                const auto& store = static_cast<const StoreValueInstruction&>(instruction);
                auto* value = tryResolve(store.value());
                auto* address = tryResolve(store.address());
                if (value == nullptr || address == nullptr)
                    return false;

                m_irBuilder->CreateStore(value, address);
                return true;
            }
            case InstructionKind::Phi:
            {
                // create the empty node now, its incomings are wired up in lowerFunction once all blocks exist
                const auto& phi = static_cast<const PhiInstruction&>(instruction);
                auto* phiType = lowerType(phi.type());
                if (phiType == nullptr)
                    return false;

                auto* node = m_irBuilder->CreatePHI(phiType, static_cast<unsigned>(phi.inputs().size()), "phi");
                defineValue(phi.resultId(), node);
                m_pendingPhis.emplace_back(&phi, node);
                return true;
            }
            case InstructionKind::Call:
            {
                const auto& call = static_cast<const CallInstruction&>(instruction);
                auto* callee = tryResolveCallee(call.functionId());
                if (callee == nullptr)
                    return false;

                std::vector<llvm::Value*> argumentValues;
                if (!buildCallArguments(call.arguments(), callee, argumentValues))
                    return false;

                auto value = m_irBuilder->CreateCall(callee, argumentValues, "call");
                defineValue(call.resultId(), value);
                return true;
            }
            case InstructionKind::CallVoid:
            {
                const auto& call = static_cast<const CallVoidInstruction&>(instruction);
                auto* callee = tryResolveCallee(call.functionId());
                if (callee == nullptr)
                    return false;

                std::vector<llvm::Value*> argumentValues;
                if (!buildCallArguments(call.arguments(), callee, argumentValues))
                    return false;

                m_irBuilder->CreateCall(callee, argumentValues);
                return true;
            }
            case InstructionKind::Add:
            {
                const auto& binary = static_cast<const AddInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind());
            }
            case InstructionKind::Subtract:
            {
                const auto& binary = static_cast<const SubtractInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind());
            }
            case InstructionKind::Multiply:
            {
                const auto& binary = static_cast<const MultiplyInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind());
            }
            case InstructionKind::Divide:
            {
                const auto& binary = static_cast<const DivideInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind(), binary.type());
            }
            case InstructionKind::Equal:
            {
                const auto& binary = static_cast<const EqualInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind(), binary.operandType());
            }
            case InstructionKind::NotEqual:
            {
                const auto& binary = static_cast<const NotEqualInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind(), binary.operandType());
            }
            case InstructionKind::LessThan:
            {
                const auto& binary = static_cast<const LessThanInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind(), binary.operandType());
            }
            case InstructionKind::LessOrEqual:
            {
                const auto& binary = static_cast<const LessOrEqualInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind(), binary.operandType());
            }
            case InstructionKind::GreaterThan:
            {
                const auto& binary = static_cast<const GreaterThanInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind(), binary.operandType());
            }
            case InstructionKind::GreaterOrEqual:
            {
                const auto& binary = static_cast<const GreaterOrEqualInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind(), binary.operandType());
            }
            case InstructionKind::BitAnd:
            {
                const auto& binary = static_cast<const BitAndInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind());
            }
            case InstructionKind::BitXor:
            {
                const auto& binary = static_cast<const BitXorInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind());
            }
            case InstructionKind::BitNot:
            {
                const auto& bitNot = static_cast<const BitNotInstruction&>(instruction);
                auto* operand = tryResolve(bitNot.operandValue());
                if (operand == nullptr)
                    return false;

                defineValue(bitNot.resultId(), m_irBuilder->CreateNot(operand, "bit_not"));
                return true;
            }
            case InstructionKind::ShiftLeft:
            {
                const auto& shiftLeft = static_cast<const ShiftLeftInstruction&>(instruction);
                auto* value = tryResolve(shiftLeft.value());
                auto* amount = tryResolve(shiftLeft.amount());
                if (value == nullptr || amount == nullptr)
                    return false;

                amount = m_irBuilder->CreateZExtOrTrunc(amount, value->getType());
                defineValue(shiftLeft.resultId(), m_irBuilder->CreateShl(value, amount, "shift_left"));
                return true;
            }
            case InstructionKind::ShiftRight:
            {
                const auto& shiftRight = static_cast<const ShiftRightInstruction&>(instruction);
                auto* value = tryResolve(shiftRight.value());
                auto* amount = tryResolve(shiftRight.amount());
                if (value == nullptr || amount == nullptr)
                    return false;

                amount = m_irBuilder->CreateZExtOrTrunc(amount, value->getType());
                // signed types shift arithmetic and unsigned types shift logical
                const auto* description = m_irModule.tryGetBuiltinTypeDescription(shiftRight.type());
                if (description != nullptr && !description->isSigned)
                {
                    defineValue(shiftRight.resultId(), m_irBuilder->CreateLShr(value, amount, "shift_right"));
                }
                else
                {
                    defineValue(shiftRight.resultId(), m_irBuilder->CreateAShr(value, amount, "shift_right"));
                }

                return true;
            }
            case InstructionKind::BitOr:
            {
                const auto& binary = static_cast<const BitOrInstruction&>(instruction);
                return emitBinary(binary.resultId(), binary.leftValue(), binary.rightValue(), instruction.kind());
            }
            case InstructionKind::IntToFloat:
            {
                const auto& conversion = static_cast<const IntToFloatInstruction&>(instruction);
                auto* operand = tryResolve(conversion.operandValue());
                if (operand == nullptr)
                    return false;

                auto* floatType = lowerType(conversion.type());
                if (floatType == nullptr)
                    return false;

                llvm::Value* result = nullptr;
                const auto* sourceDescription = m_irModule.tryGetBuiltinTypeDescription(conversion.sourceType());
                if (sourceDescription != nullptr && !sourceDescription->isSigned)
                {
                    result = m_irBuilder->CreateUIToFP(operand, floatType, "int_to_float");
                }
                else
                {
                    result = m_irBuilder->CreateSIToFP(operand, floatType, "int_to_float");
                }

                defineValue(conversion.resultId(), result);
                return true;
            }
            case InstructionKind::SizeOf:
            {
                const auto& sizeOf = static_cast<const SizeOfInstruction&>(instruction);
                auto* measuredType = lowerType(sizeOf.measuredType());
                auto* resultType = lowerType(sizeOf.type());
                if (measuredType == nullptr || resultType == nullptr)
                    return false;

                const auto size = m_llvmModule.getDataLayout().getTypeAllocSize(measuredType);
                defineValue(sizeOf.resultId(), llvm::ConstantInt::get(resultType, size.getFixedValue()));
                return true;
            }
            case InstructionKind::IntWiden:
            {
                const auto& conversion = static_cast<const IntWidenInstruction&>(instruction);
                auto* operand = tryResolve(conversion.operandValue());
                if (operand == nullptr)
                    return false;

                auto* targetType = lowerType(conversion.type());
                if (targetType == nullptr)
                    return false;

                // the source descriptor decides sign extension vs zero extension
                const auto* description = m_irModule.tryGetBuiltinTypeDescription(conversion.sourceType());
                llvm::Value* result = nullptr;
                if (description != nullptr && !description->isSigned)
                {
                    result = m_irBuilder->CreateZExt(operand, targetType, "int_widen");
                }
                else
                {
                    result = m_irBuilder->CreateSExt(operand, targetType, "int_widen");
                }

                defineValue(conversion.resultId(), result);
                return true;
            }
            case InstructionKind::ValueNegation:
            {
                const auto& negation = static_cast<const ValueNegationInstruction&>(instruction);
                auto* operand = tryResolve(negation.operandValue());
                if (operand == nullptr)
                    return false;

                llvm::Value* result = nullptr;
                if (operand->getType()->isFloatingPointTy())
                    result = m_irBuilder->CreateFNeg(operand);
                else
                    result = m_irBuilder->CreateNeg(operand);

                defineValue(negation.resultId(), result);
                return true;
            }
            case InstructionKind::LogicalNegation:
            {
                const auto& negation = static_cast<const LogicalNegationInstruction&>(instruction);
                auto* operand = tryResolve(negation.operandValue());
                if (operand == nullptr)
                    return false;

                defineValue(negation.resultId(), m_irBuilder->CreateNot(operand));
                return true;
            }
            default:
            {
                // todo other stuff
                return false;
            }
        }
    }

    bool LLVMCodeGenerator::emitBinary(TemporaryId resultId, ValueRef leftRef, ValueRef rightRef, InstructionKind kind, Type operandType) noexcept
    {
        auto* lhs = tryResolve(leftRef);
        auto* rhs = tryResolve(rightRef);
        if (lhs == nullptr || rhs == nullptr)
            return false;

        const auto* operandDescription = m_irModule.tryGetBuiltinTypeDescription(operandType.toValue());
        const auto isUnsigned = operandDescription != nullptr && !operandDescription->isSigned;
        const auto isFloat = lhs->getType()->isFloatingPointTy();
        if (isFloat)
        {
            switch (kind)
            {
                case InstructionKind::Add:
                {
                    defineValue(resultId, m_irBuilder->CreateFAdd(lhs, rhs, "add"));
                    return true;
                }
                case InstructionKind::Subtract:
                {
                    defineValue(resultId, m_irBuilder->CreateFSub(lhs, rhs, "subtract"));
                    return true;
                }
                case InstructionKind::Multiply:
                {
                    defineValue(resultId, m_irBuilder->CreateFMul(lhs, rhs, "multiply"));
                    return true;
                }
                case InstructionKind::Divide:
                {
                    defineValue(resultId, m_irBuilder->CreateFDiv(lhs, rhs, "divide"));
                    return true;
                }
                case InstructionKind::Equal:
                {
                    defineValue(resultId, m_irBuilder->CreateFCmpOEQ(lhs, rhs, "equal"));
                    return true;
                }
                case InstructionKind::NotEqual:
                {
                    defineValue(resultId, m_irBuilder->CreateFCmpUNE(lhs, rhs, "not_equal"));
                    return true;
                }
                case InstructionKind::LessThan:
                {
                    defineValue(resultId, m_irBuilder->CreateFCmpOLT(lhs, rhs, "less_than"));
                    return true;
                }
                case InstructionKind::LessOrEqual:
                {
                    defineValue(resultId, m_irBuilder->CreateFCmpOLE(lhs, rhs, "less_or_equal"));
                    return true;
                }
                case InstructionKind::GreaterThan:
                {
                    defineValue(resultId, m_irBuilder->CreateFCmpOGT(lhs, rhs, "greater_than"));
                    return true;
                }
                case InstructionKind::GreaterOrEqual:
                {
                    defineValue(resultId, m_irBuilder->CreateFCmpOGE(lhs, rhs, "greater_or_equal"));
                    return true;
                }
                default:
                {
                    return false;
                }
            }
        }
        else
        {
            switch (kind)
            {
                case InstructionKind::Add:
                {
                    defineValue(resultId, m_irBuilder->CreateAdd(lhs, rhs, "add"));
                    return true;
                }
                case InstructionKind::Subtract:
                {
                    defineValue(resultId, m_irBuilder->CreateSub(lhs, rhs, "subtract"));
                    return true;
                }
                case InstructionKind::Multiply:
                {
                    defineValue(resultId, m_irBuilder->CreateMul(lhs, rhs, "multiply"));
                    return true;
                }
                case InstructionKind::Divide:
                {
                    if (isUnsigned)
                    {
                        defineValue(resultId, m_irBuilder->CreateUDiv(lhs, rhs, "divide"));
                    }
                    else
                    {
                        defineValue(resultId, m_irBuilder->CreateSDiv(lhs, rhs, "divide"));
                    }

                    return true;
                }
                case InstructionKind::Equal:
                {
                    defineValue(resultId, m_irBuilder->CreateICmpEQ(lhs, rhs, "equal"));
                    return true;
                }
                case InstructionKind::NotEqual:
                {
                    defineValue(resultId, m_irBuilder->CreateICmpNE(lhs, rhs, "not_equal"));
                    return true;
                }
                case InstructionKind::LessThan:
                {
                    if (isUnsigned)
                    {
                        defineValue(resultId, m_irBuilder->CreateICmpULT(lhs, rhs, "less_than"));
                    }
                    else
                    {
                        defineValue(resultId, m_irBuilder->CreateICmpSLT(lhs, rhs, "less_than"));
                    }

                    return true;
                }
                case InstructionKind::LessOrEqual:
                {
                    if (isUnsigned)
                    {
                        defineValue(resultId, m_irBuilder->CreateICmpULE(lhs, rhs, "less_or_equal"));
                    }
                    else
                    {
                        defineValue(resultId, m_irBuilder->CreateICmpSLE(lhs, rhs, "less_or_equal"));
                    }

                    return true;
                }
                case InstructionKind::GreaterThan:
                {
                    if (isUnsigned)
                    {
                        defineValue(resultId, m_irBuilder->CreateICmpUGT(lhs, rhs, "greater_than"));
                    }
                    else
                    {
                        defineValue(resultId, m_irBuilder->CreateICmpSGT(lhs, rhs, "greater_than"));
                    }

                    return true;
                }
                case InstructionKind::GreaterOrEqual:
                {
                    if (isUnsigned)
                    {
                        defineValue(resultId, m_irBuilder->CreateICmpUGE(lhs, rhs, "greater_or_equal"));
                    }
                    else
                    {
                        defineValue(resultId, m_irBuilder->CreateICmpSGE(lhs, rhs, "greater_or_equal"));
                    }

                    return true;
                }
                case InstructionKind::BitAnd:
                {
                    defineValue(resultId, m_irBuilder->CreateAnd(lhs, rhs, "bit_and"));
                    return true;
                }
                case InstructionKind::BitOr:
                {
                    defineValue(resultId, m_irBuilder->CreateOr(lhs, rhs, "bit_or"));
                    return true;
                }
                case InstructionKind::BitXor:
                {
                    defineValue(resultId, m_irBuilder->CreateXor(lhs, rhs, "bit_xor"));
                    return true;
                }
                default:
                {
                    return false;
                }
            }
        }
    }

    bool LLVMCodeGenerator::lowerTerminator(const Terminator& terminator) noexcept
    {
        switch (terminator.kind())
        {
            case TerminatorKind::Return:
            {
                m_irBuilder->CreateRetVoid();
                return true;
            }
            case TerminatorKind::Unreachable:
            {
                m_irBuilder->CreateUnreachable();
                return true;
            }
            case TerminatorKind::ReturnValue:
            {
                const auto& returnValue = static_cast<const ReturnValueTerminator&>(terminator);
                auto* value = tryResolve(returnValue.value());
                if (value == nullptr)
                    return false;

                m_irBuilder->CreateRet(value);
                return true;
            }
            case TerminatorKind::Jump:
            {
                const auto& jump = static_cast<const JumpTerminator&>(terminator);
                auto* target = tryGetBlock(jump.targetBlockId());
                if (target == nullptr)
                    return false;

                m_irBuilder->CreateBr(target);
                return true;
            }
            case TerminatorKind::Branch:
            {
                const auto& branch = static_cast<const BranchIfTerminator&>(terminator);
                auto* condition = tryResolve(branch.condition());
                auto* trueBlock = tryGetBlock(branch.trueBlockId());
                auto* falseBlock = tryGetBlock(branch.falseBlockId());
                if (condition == nullptr || trueBlock == nullptr || falseBlock == nullptr)
                    return false;

                m_irBuilder->CreateCondBr(condition, trueBlock, falseBlock);
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    llvm::Type* LLVMCodeGenerator::lowerType(Type type) const noexcept
    {
        auto& context = m_llvmModule.getContext();

        if (type.isReference())
            return llvm::PointerType::getUnqual(context);
        else if (type == Type::Void())
            return llvm::Type::getVoidTy(context);

        // lower types with the builtin annotation
        if (const auto* description = m_irModule.tryGetBuiltinTypeDescription(type))
        {
            switch (description->kind)
            {
                case BuiltinTypeKind::Int:
                {
                    return llvm::Type::getIntNTy(context, static_cast<unsigned>(description->bits));
                }
                case BuiltinTypeKind::Float:
                {
                    if (description->bits == 64)
                    {
                        return llvm::Type::getDoubleTy(context);
                    }
                    return llvm::Type::getFloatTy(context);
                }
                case BuiltinTypeKind::Bool:
                {
                    return llvm::Type::getInt1Ty(context);
                }
                case BuiltinTypeKind::Pointer:
                {
                    return llvm::PointerType::getUnqual(context);
                }
            }
        }

        // a fixed array lowers to an inline llvm array of its element type
        if (type.kind() == TypeKind::FixedArray)
        {
            const auto* arrayType = m_irModule.tryGetArrayType(type);
            if (arrayType == nullptr)
                return nullptr;

            auto* elementType = lowerType(arrayType->elementType);
            if (elementType == nullptr)
                return nullptr;

            return llvm::ArrayType::get(elementType, static_cast<std::uint64_t>(arrayType->length));
        }

        // a slice lowers to a { pointer, i32 length } pair
        if (type.kind() == TypeKind::Slice)
            return llvm::StructType::get(llvm::PointerType::getUnqual(context), llvm::Type::getInt32Ty(context));

        // a dynamic array lowers to a { pointer, i32 length, i32 capacity }
        if (type.kind() == TypeKind::DynamicArray)
            return llvm::StructType::get(llvm::PointerType::getUnqual(context), llvm::Type::getInt32Ty(context), llvm::Type::getInt32Ty(context));

        // an enum lowers to its underlying integer type
        if (const auto* enumDeclaration = m_irModule.tryGetEnum(type))
            return lowerType(enumDeclaration->baseType());

        // a user type lowers to its named struct created in lowerTypes
        if (const auto* typeName = m_irModule.tryGetTypeName(type))
            return llvm::StructType::getTypeByName(context, *typeName);

        return nullptr;
    }

    llvm::Constant* LLVMCodeGenerator::lowerAggregateConstant(const ConstantValue& value, llvm::Type* type) noexcept
    {
        const auto* aggregate = value.tryGetAggregate();
        if (aggregate == nullptr)
            return llvm::dyn_cast_or_null<llvm::Constant>(lowerConstant(value));

        auto* arrayType = llvm::dyn_cast<llvm::ArrayType>(type);
        if (arrayType == nullptr)
            return nullptr;

        std::vector<llvm::Constant*> elements{};
        elements.reserve(aggregate->size());
        for (const auto& element : *aggregate)
        {
            auto* elementConstant = lowerAggregateConstant(element, arrayType->getElementType());
            if (elementConstant == nullptr)
                return nullptr;

            elements.push_back(elementConstant);
        }

        return llvm::ConstantArray::get(arrayType, elements);
    }

    llvm::Value* LLVMCodeGenerator::lowerConstant(const ConstantValue& value) noexcept
    {
        // the llvm type follows the literal payload (which is the base type for enum constants),
        // not the declared type, so enum constants lower to their underlying integer type
        const auto* literalData = value.tryGetLiteralData();
        ConstantValue::LiteralData enumUnderlying;
        if (literalData == nullptr)
        {
            if (const auto* enumConstant = value.tryGetEnumConstant())
            {
                enumUnderlying = enumConstant->underlyingValue;
                literalData = &enumUnderlying;
            }
        }

        if (literalData == nullptr)
            return nullptr;

        auto& context = m_llvmModule.getContext();
        return std::visit(
            [&](const auto& payload) -> llvm::Value*
            {
                using Payload = std::decay_t<decltype(payload)>;

                if constexpr (std::is_same_v<Payload, bool>)
                {
                    if (payload)
                        return llvm::ConstantInt::getTrue(context);

                    return llvm::ConstantInt::getFalse(context);
                }
                else if constexpr (std::is_integral_v<Payload> && !std::is_same_v<Payload, bool>)
                {
                    constexpr auto isSignedPayload = std::is_signed_v<Payload>;
                    auto* integerType = llvm::Type::getIntNTy(context, static_cast<unsigned>(sizeof(Payload) * 8));
                    return llvm::ConstantInt::get(integerType, static_cast<std::uint64_t>(payload), isSignedPayload);
                }
                else if constexpr (std::is_same_v<Payload, f32> || std::is_same_v<Payload, f64>)
                {
                    return llvm::ConstantFP::get(context, llvm::APFloat(payload));
                }
                else if constexpr (std::is_same_v<Payload, std::string>)
                {
                    return m_irBuilder->CreateGlobalString(payload, "", 0, &m_llvmModule);
                }
                else
                {
                    return nullptr;
                }
            },
            *literalData);
    }

    llvm::Value* LLVMCodeGenerator::tryResolve(ValueRef value) const noexcept
    {
        const auto result = m_values.find(value.id());
        if (result == m_values.end())
            return nullptr;

        return result->second;
    }

    llvm::BasicBlock* LLVMCodeGenerator::tryGetBlock(BlockId id) const noexcept
    {
        const auto result = m_blocks.find(id);
        if (result == m_blocks.end())
            return nullptr;

        return result->second;
    }

    llvm::FunctionType* LLVMCodeGenerator::tryLowerFunctionType(Type returnType, const std::vector<IRParameter>& parameters) const noexcept
    {
        auto* llvmReturnType = lowerType(returnType);
        if (llvmReturnType == nullptr)
            return nullptr;

        std::vector<llvm::Type*> parameterTypes;
        bool isVariadic = false;
        for (const auto& parameter : parameters)
        {
            if (parameter.type() == Type::CVariadic())
            {
                isVariadic = true;
                continue;
            }

            auto* parameterType = lowerType(parameter.type());
            if (parameterType == nullptr)
                return nullptr;

            if (parameterType->isVoidTy())
                return nullptr;

            parameterTypes.push_back(parameterType);
        }

        return llvm::FunctionType::get(llvmReturnType, parameterTypes, isVariadic);
    }

    llvm::Function* LLVMCodeGenerator::tryResolveCallee(FunctionId functionId) const noexcept
    {
        // an extern might link to a symbol name different from the caracal name
        if (const auto* externFunction = m_irModule.tryGetExternFunction(functionId))
        {
            const auto symbolName = externFunction->symbolName().value_or(externFunction->name());
            return m_llvmModule.getFunction(symbolName);
        }

        if (const auto* function = m_irModule.tryGetFunction(functionId))
        {
            const auto symbolName = function->symbolName().value_or(function->name());
            return m_llvmModule.getFunction(symbolName);
        }

        return nullptr;
    }

    llvm::Value* LLVMCodeGenerator::promoteVariadicArgument(llvm::Value* value) noexcept
    {
        // we need to extend i1 and i8 to i32, and f32 to f64 for variadic calls
        auto& context = m_llvmModule.getContext();
        auto* type = value->getType();
        if (type->isIntegerTy(1) || type->isIntegerTy(8))
        {
            return m_irBuilder->CreateZExt(value, llvm::Type::getInt32Ty(context));
        }
        else if (type->isFloatTy())
        {
            return m_irBuilder->CreateFPExt(value, llvm::Type::getDoubleTy(context));
        }

        return value;
    }

    bool LLVMCodeGenerator::buildCallArguments(const std::vector<ValueRef>& arguments, llvm::Function* callee, std::vector<llvm::Value*>& argumentValues) noexcept
    {
        const auto isVariadic = callee->isVarArg();
        const auto fixedParameterCount = callee->getFunctionType()->getNumParams();
        for (size_t index = 0; index < arguments.size(); ++index)
        {
            auto* argumentValue = tryResolve(arguments[index]);
            if (argumentValue == nullptr)
                return false;

            // we need to promote arguments in the variadic position
            if (isVariadic && index >= fixedParameterCount)
                argumentValue = promoteVariadicArgument(argumentValue);

            argumentValues.push_back(argumentValue);
        }

        return true;
    }

    void LLVMCodeGenerator::defineValue(TemporaryId id, llvm::Value* value) noexcept
    {
        m_values.insert_or_assign(id, value);
    }
}
