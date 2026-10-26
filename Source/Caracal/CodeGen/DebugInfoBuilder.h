#pragma once

#include <Caracal/Defines.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/Module.h>

#include <memory>
#include <string_view>
#include <unordered_map>

// forward declare llvm types to keep headers clean and avoid linking llvm into tests
namespace llvm
{
    class AllocaInst;
    class DIBuilder;
    class DICompileUnit;
    class DIFile;
    class DISubprogram;
    class DISubroutineType;
    class DIType;
    class Function;
    class IRBuilderBase;
    class Module;
}

namespace Caracal
{
    class DebugInfoBuilder
    {
    public:
        DebugInfoBuilder(const Module& irModule, llvm::Module& llvmModule);
        ~DebugInfoBuilder();

        CARACAL_DELETE_COPY_DELETE_MOVE(DebugInfoBuilder)

        void beginFunction(const Function& function, llvm::Function& llvmFunction, llvm::IRBuilderBase& irBuilder);
        void setInstructionLine(SourceLine sourceLine, llvm::IRBuilderBase& irBuilder);
        void declareVariable(std::string_view name, Type type, i32 variable, SourceLine sourceLine, llvm::AllocaInst& slot);
        void finalize();

    private:
        [[nodiscard]] llvm::DIFile* fileFor(u16 fileId);
        [[nodiscard]] llvm::DIType* basicType(Type type);
        [[nodiscard]] llvm::DISubroutineType* subroutineType(const Function& function);

        const Module& m_irModule;
        llvm::Module& m_llvmModule;
        std::unique_ptr<llvm::DIBuilder> m_builder;
        llvm::DICompileUnit* m_compileUnit;
        std::unordered_map<u16, llvm::DIFile*> m_files;
        std::unordered_map<i32, llvm::DIType*> m_basicTypes;
        llvm::DISubprogram* m_subprogram = nullptr;
        i32 m_functionLine = -1;
        i32 m_currentLine = -1;
    };
}
