#pragma once

#include <Caracal/Defines.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/Module.h>

#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

// forward declare llvm types to keep headers clean and avoid linking llvm into tests
namespace llvm
{
    class AllocaInst;
    class DIBuilder;
    class DICompileUnit;
    class DICompositeType;
    class DIFile;
    class DISubprogram;
    class DISubroutineType;
    class DIType;
    class Function;
    class IRBuilderBase;
    class Metadata;
    class Module;
    class StructType;
    class Type;
}

namespace Caracal
{
    class DebugInfoBuilder
    {
    public:
        using TypeLowering = std::function<llvm::Type*(Type)>;

        DebugInfoBuilder(const Module& irModule, llvm::Module& llvmModule, TypeLowering lowerType);
        ~DebugInfoBuilder();

        CARACAL_DELETE_COPY_DELETE_MOVE(DebugInfoBuilder)

        void initialize();
        void beginFunction(const Function& function, llvm::Function& llvmFunction, llvm::IRBuilderBase& irBuilder);
        void setInstructionLine(SourceLine sourceLine, llvm::IRBuilderBase& irBuilder);
        void declareVariable(std::string_view name, Type type, i32 variable, SourceLine sourceLine, llvm::AllocaInst& slot);
        void finalize();

    private:
        struct MethodOwner final
        {
            Type type;
            std::string_view methodName;
        };

        [[nodiscard]] llvm::DIFile* fileFor(u16 fileId);
        [[nodiscard]] llvm::DIType* debugType(Type type);
        [[nodiscard]] llvm::DIType* basicType(Type type);
        [[nodiscard]] llvm::DIType* referenceType(Type type);
        [[nodiscard]] llvm::DIType* enumType(const EnumDeclaration& enumDeclaration);
        [[nodiscard]] llvm::DIType* fixedArrayType(Type type);
        [[nodiscard]] llvm::DIType* structType(Type type);
        [[nodiscard]] llvm::DIType* sliceOrDynamicArrayType(Type type);
        [[nodiscard]] llvm::DICompositeType* beginStructure(Type type, std::string_view name, llvm::StructType& llvmType);
        [[nodiscard]] llvm::Metadata* memberType(llvm::DICompositeType* structure, std::string_view name, llvm::DIType* type, llvm::StructType& llvmType, u32 index);
        [[nodiscard]] llvm::DIType* endStructure(Type type, llvm::DICompositeType* structure, const std::vector<llvm::Metadata*>& members);
        [[nodiscard]] llvm::DISubprogram* methodDeclaration(llvm::DICompositeType* structure, std::string_view name, const Function& method);
        [[nodiscard]] llvm::DISubprogram* tryGetMethodDeclaration(const Function& function);
        [[nodiscard]] llvm::DIType* objectPointerType(llvm::DIType* structure);
        [[nodiscard]] llvm::DISubroutineType* signatureType(const Function& function, llvm::DIType* thisType);
        [[nodiscard]] u32 pointerBits() const;

        const Module& m_irModule;
        llvm::Module& m_llvmModule;
        TypeLowering m_lowerType;
        std::unique_ptr<llvm::DIBuilder> m_builder;
        llvm::DICompileUnit* m_compileUnit;
        std::unordered_map<u16, llvm::DIFile*> m_files;
        std::unordered_map<i32, llvm::DIType*> m_types;
        std::unordered_map<FunctionId, MethodOwner> m_methodOwners;
        llvm::DISubprogram* m_subprogram = nullptr;
        llvm::DIType* m_thisType = nullptr;
        i32 m_functionLine = -1;
        i32 m_currentLine = -1;
    };
}
