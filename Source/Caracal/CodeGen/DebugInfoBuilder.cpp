#include <Caracal/CodeGen/DebugInfoBuilder.h>

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DebugLoc.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#include <filesystem>
#include <string>

namespace Caracal
{
    // CodeView has no language id for Caracal
    static constexpr auto SourceLanguage = llvm::dwarf::DW_LANG_C;
    static constexpr auto Producer = "Caracal";

    DebugInfoBuilder::DebugInfoBuilder(const Module& irModule, llvm::Module& llvmModule)
        : m_irModule{ irModule }
        , m_llvmModule{ llvmModule }
        , m_builder{ std::make_unique<llvm::DIBuilder>(llvmModule) }
        , m_compileUnit{ nullptr }
    {
        const auto compileUnitFileId = m_irModule.firstUserSourceFileId();
        if (!compileUnitFileId.has_value())
            return;

        m_compileUnit = m_builder->createCompileUnit(
            SourceLanguage,
            fileFor(compileUnitFileId.value()),
            Producer,
            false,
            "",
            0,
            "",
            llvm::DICompileUnit::LineTablesOnly);
    }

    // defaulted here so the unique_ptr<llvm::DIBuilder> member is
    // destroyed where the type is complete
    DebugInfoBuilder::~DebugInfoBuilder() = default;

    void DebugInfoBuilder::beginFunction(const Function& function, llvm::Function& llvmFunction, llvm::IRBuilderBase& irBuilder)
    {
        const auto sourceLine = function.sourceLine();
        m_subprogram = nullptr;
        m_functionLine = sourceLine.line;
        m_currentLine = sourceLine.line;
        if (m_compileUnit == nullptr || sourceLine.line == -1)
        {
            irBuilder.SetCurrentDebugLocation(llvm::DebugLoc{});
            return;
        }

        auto* file = fileFor(sourceLine.fileId);
        std::string linkageName{};
        if (function.symbolName().has_value())
        {
            linkageName = function.symbolName().value();
        }

        auto* functionType = m_builder->createSubroutineType(m_builder->getOrCreateTypeArray({}));
        const auto line = static_cast<unsigned>(sourceLine.line);
        auto* subprogram = m_builder->createFunction(
            file,
            function.name(),
            linkageName,
            file,
            line,
            functionType,
            line,
            llvm::DINode::FlagPrototyped,
            llvm::DISubprogram::SPFlagDefinition);
        llvmFunction.setSubprogram(subprogram);
        m_subprogram = subprogram;
        irBuilder.SetCurrentDebugLocation(llvm::DILocation::get(m_llvmModule.getContext(), line, 0, subprogram));
    }

    void DebugInfoBuilder::setInstructionLine(SourceLine sourceLine, llvm::IRBuilderBase& irBuilder)
    {
        if (m_subprogram == nullptr)
        {
            return;
        }

        auto line = sourceLine.line;
        if (line == -1)
        {
            line = m_functionLine;
        }

        if (line == m_currentLine)
        {
            return;
        }

        m_currentLine = line;
        irBuilder.SetCurrentDebugLocation(llvm::DILocation::get(m_llvmModule.getContext(), static_cast<unsigned>(line), 0, m_subprogram));
    }

    void DebugInfoBuilder::finalize()
    {
        m_builder->finalize();
        m_llvmModule.addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
        m_llvmModule.addModuleFlag(llvm::Module::Warning, "CodeView", 1);
    }

    llvm::DIFile* DebugInfoBuilder::fileFor(u16 fileId)
    {
        if (const auto file = m_files.find(fileId); file != m_files.end())
            return file->second;

        const auto filePath = std::filesystem::path(m_irModule.sourceFileNames()[fileId]);
        auto* file = m_builder->createFile(filePath.filename().generic_string(), filePath.parent_path().generic_string());
        m_files.emplace(fileId, file);
        return file;
    }
}
