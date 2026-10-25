#pragma once

#include <Caracal/Defines.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/Module.h>

#include <memory>
#include <unordered_map>

// forward declare llvm types to keep headers clean and avoid linking llvm into tests
namespace llvm
{
    class DIBuilder;
    class DICompileUnit;
    class DIFile;
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
        void finalize();

    private:
        [[nodiscard]] llvm::DIFile* fileFor(u16 fileId);

        const Module& m_irModule;
        llvm::Module& m_llvmModule;
        std::unique_ptr<llvm::DIBuilder> m_builder;
        llvm::DICompileUnit* m_compileUnit;
        std::unordered_map<u16, llvm::DIFile*> m_files;
    };
}
