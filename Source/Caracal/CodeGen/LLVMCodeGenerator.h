#pragma once

#include <Caracal/API.h>
#include <Caracal/CodeGen/DebugInfoBuilder.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/Module.h>
#include <Caracal/Semantic/Type.h>

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// forward declare llvm types to keep headers clean and avoid linking llvm into tests
namespace llvm
{
    class Module;
    class Value;
    class Constant;
    class Function;
    class FunctionType;
    class Type;
    class IRBuilderBase;
    class BasicBlock;
    class PHINode;
}

namespace Caracal
{
    class LLVMCodeGenerator
    {
    public:
        LLVMCodeGenerator(const Module& irModule, llvm::Module& llvmModule, bool debugInfo);
        ~LLVMCodeGenerator();

        CARACAL_DELETE_COPY_DELETE_MOVE(LLVMCodeGenerator)

        [[nodiscard]] bool generate();

    private:
        [[nodiscard]] bool lowerTypes() noexcept;
        [[nodiscard]] bool declareCallables() noexcept;
        [[nodiscard]] bool declareCallable(const std::string& name, Type returnType, const std::vector<IRParameter>& parameters) noexcept;
        [[nodiscard]] bool lowerGlobals() noexcept;
        [[nodiscard]] bool lowerGlobalConstant(const GlobalConstantDeclaration& globalConstant) noexcept;
        [[nodiscard]] bool lowerGlobalReference(const GlobalReferenceDeclaration& globalReference) noexcept;
        [[nodiscard]] bool lowerConstructedGlobal(const ConstructedGlobalDeclaration& constructedGlobal) noexcept;
        [[nodiscard]] bool lowerFunctionBodies() noexcept;
        [[nodiscard]] bool lowerFunctionBody(const Function& function) noexcept;
        [[nodiscard]] bool lowerGlobalInit() noexcept;
        [[nodiscard]] bool lowerInstruction(const Function& function, u32 instructionIndex) noexcept;
        [[nodiscard]] bool emitBinary(TemporaryId resultId, TemporaryId leftId, TemporaryId rightId, InstructionKind kind, Type operandType = Type::Undefined()) noexcept;
        [[nodiscard]] bool buildCallArguments(std::span<const TemporaryId> arguments, llvm::Function* callee, std::vector<llvm::Value*>& argumentValues) noexcept;

        [[nodiscard]] llvm::FunctionType* tryLowerFunctionType(Type returnType, const std::vector<IRParameter>& parameters) const noexcept;
        [[nodiscard]] llvm::Type* lowerType(Type type) const noexcept;
        [[nodiscard]] llvm::Value* lowerConstant(const ConstantValue& value) noexcept;
        [[nodiscard]] llvm::Constant* lowerAggregateConstant(const ConstantValue& value, llvm::Type* type) noexcept;
        [[nodiscard]] llvm::Value* promoteVariadicArgument(llvm::Value* value) noexcept;
        [[nodiscard]] llvm::Value* tryResolve(TemporaryId id) const noexcept;
        [[nodiscard]] llvm::Function* tryResolveCallee(FunctionId functionId) const noexcept;
        [[nodiscard]] llvm::BasicBlock* tryGetBlock(BlockId id) const noexcept;
        void defineValue(TemporaryId id, llvm::Value* value) noexcept;

        const Module& m_irModule;
        llvm::Module& m_llvmModule;
        std::unique_ptr<llvm::IRBuilderBase> m_irBuilder;
        std::unique_ptr<DebugInfoBuilder> m_debugInfo;
        llvm::Function* m_currentFunction;
        std::unordered_map<TemporaryId, llvm::Value*> m_values;
        std::unordered_map<LocalSlotId, llvm::Value*> m_slots;
        std::unordered_map<BlockId, llvm::BasicBlock*> m_blocks;
        // phi incomings are resolved after every block is lowered, so back-edges and forward references work
        // the phi's instruction index and its node
        std::vector<std::pair<u32, llvm::PHINode*>> m_pendingPhis;
    };
}
