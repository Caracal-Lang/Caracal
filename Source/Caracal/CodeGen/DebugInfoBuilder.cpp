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
#include <vector>

namespace Caracal
{
    // CodeView has no language id for Caracal
    static constexpr auto SourceLanguage = llvm::dwarf::DW_LANG_C;
    static constexpr auto Producer = "Caracal";

    static unsigned IntegerEncoding(const BuiltinTypeDescription& description)
    {
        if (description.bits == 8)
        {
            if (description.isSigned)
            {
                return llvm::dwarf::DW_ATE_signed_char;
            }

            return llvm::dwarf::DW_ATE_unsigned_char;
        }

        if (description.isSigned)
        {
            return llvm::dwarf::DW_ATE_signed;
        }

        return llvm::dwarf::DW_ATE_unsigned;
    }

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
            llvm::DICompileUnit::FullDebug);
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

        auto* functionType = subroutineType(function);
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

    void DebugInfoBuilder::declareVariable(std::string_view name, Type type, i32 variable, SourceLine sourceLine, llvm::AllocaInst& slot)
    {
        if (m_subprogram == nullptr)
        {
            return;
        }

        auto* variableType = basicType(type);
        if (variableType == nullptr)
        {
            return;
        }

        auto line = sourceLine.line;
        if (line == -1)
        {
            line = m_functionLine;
        }

        auto* file = m_subprogram->getFile();
        const auto lineNumber = static_cast<unsigned>(line);
        llvm::DILocalVariable* debugVariable = nullptr;
        if (variable == LocalVariableSlot)
        {
            debugVariable = m_builder->createAutoVariable(m_subprogram, name, file, lineNumber, variableType);
        }
        else
        {
            debugVariable = m_builder->createParameterVariable(m_subprogram, name, static_cast<unsigned>(variable), file, lineNumber, variableType);
        }

        auto* location = llvm::DILocation::get(m_llvmModule.getContext(), lineNumber, 0, m_subprogram);
        if (auto* next = slot.getNextNode(); next != nullptr)
        {
            m_builder->insertDeclare(&slot, debugVariable, m_builder->createExpression(), location, next->getIterator());
            return;
        }

        m_builder->insertDeclare(&slot, debugVariable, m_builder->createExpression(), location, slot.getParent());
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
    
    llvm::DIType* DebugInfoBuilder::basicType(Type type)
    {
        if (!type.isBaseType())
        {
            return nullptr;
        }

        if (const auto known = m_basicTypes.find(type.id()); known != m_basicTypes.end())
        {
            return known->second;
        }

        const auto* description = m_irModule.tryGetBuiltinTypeDescription(type);
        const auto* name = m_irModule.tryGetTypeName(type);
        if (description == nullptr || name == nullptr)
        {
            return nullptr;
        }

        llvm::DIType* result = nullptr;
        switch (description->kind)
        {
            case BuiltinTypeKind::Int:
            {
                result = m_builder->createBasicType(*name, static_cast<std::uint64_t>(description->bits), IntegerEncoding(*description));
                break;
            }
            case BuiltinTypeKind::Float:
            {
                result = m_builder->createBasicType(*name, static_cast<std::uint64_t>(description->bits), llvm::dwarf::DW_ATE_float);
                break;
            }
            case BuiltinTypeKind::Bool:
            {
                result = m_builder->createBasicType(*name, 8, llvm::dwarf::DW_ATE_boolean);
                break;
            }
            case BuiltinTypeKind::Pointer:
            {
                if (type == m_irModule.wellKnown().cstring)
                {
                    const auto pointerBits = m_llvmModule.getDataLayout().getPointerSizeInBits();
                    result = m_builder->createPointerType(basicType(m_irModule.wellKnown().u8), pointerBits, 0, std::nullopt, *name);
                }
                break;
            }
        }

        m_basicTypes.emplace(type.id(), result);
        return result;
    }

    llvm::DISubroutineType* DebugInfoBuilder::subroutineType(const Function& function)
    {
        std::vector<llvm::Metadata*> types;
        types.reserve(function.parameters().size() + 1);
        types.push_back(basicType(function.returnType()));
        for (const auto& parameter : function.parameters())
        {
            types.push_back(basicType(parameter.type()));
        }

        return m_builder->createSubroutineType(m_builder->getOrCreateTypeArray(types));
    }
}
