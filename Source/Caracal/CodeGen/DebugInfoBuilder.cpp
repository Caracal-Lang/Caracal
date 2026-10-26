#include <Caracal/CodeGen/DebugInfoBuilder.h>

#include <Caracal/Constants.h>

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/DebugLoc.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Caracal
{
    // CodeView has no language id for Caracal
    static constexpr auto SourceLanguage = llvm::dwarf::DW_LANG_C;
    static constexpr auto Producer = "Caracal";

    static u32 IntegerEncoding(const BuiltinTypeDescription& description)
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

    static std::optional<std::uint64_t> EnumValue(const ConstantValue& value)
    {
        const auto* literalData = value.tryGetLiteralData();
        if (const auto* enumConstant = value.tryGetEnumConstant(); enumConstant != nullptr)
        {
            literalData = &enumConstant->underlyingValue;
        }

        if (literalData == nullptr)
        {
            return std::nullopt;
        }

        return std::visit(
            [](const auto& payload) -> std::optional<std::uint64_t>
            {
                using Payload = std::decay_t<decltype(payload)>;
                if constexpr (std::is_integral_v<Payload> && !std::is_same_v<Payload, bool>)
                {
                    return static_cast<std::uint64_t>(payload);
                }
                else
                {
                    return std::nullopt;
                }
            },
            *literalData);
    }

    static bool IsInstanceMethod(const Function& function)
    {
        return !function.parameters().empty() && function.parameters().front().name() == ImplicitThisName;
    }

    static std::string_view LinkageName(const Function& function)
    {
        if (function.symbolName().has_value())
        {
            return function.symbolName().value();
        }

        return {};
    }

    DebugInfoBuilder::DebugInfoBuilder(const Module& irModule, llvm::Module& llvmModule, TypeLowering lowerType)
        : m_irModule{ irModule }
        , m_llvmModule{ llvmModule }
        , m_lowerType{ std::move(lowerType) }
        , m_builder{ std::make_unique<llvm::DIBuilder>(llvmModule) }
        , m_compileUnit{ nullptr }
    {
    }

    // defaulted here so the unique_ptr<llvm::DIBuilder> member is
    // destroyed where the type is complete
    DebugInfoBuilder::~DebugInfoBuilder() = default;

    void DebugInfoBuilder::initialize()
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

        for (const auto& typeDeclaration : m_irModule.types())
        {
            for (const auto& method : typeDeclaration.methods())
            {
                m_methodOwners.emplace(method.functionId, MethodOwner{ typeDeclaration.type(), method.name });
            }
        }
    }

    void DebugInfoBuilder::beginFunction(const Function& function, llvm::Function& llvmFunction, llvm::IRBuilderBase& irBuilder)
    {
        const auto sourceLine = function.sourceLine();
        m_subprogram = nullptr;
        m_thisType = nullptr;
        m_functionLine = sourceLine.line;
        m_currentLine = sourceLine.line;
        if (m_compileUnit == nullptr || sourceLine.line == -1)
        {
            irBuilder.SetCurrentDebugLocation(llvm::DebugLoc{});
            return;
        }

        auto* file = fileFor(sourceLine.fileId);
        llvm::DIScope* scope = file;
        std::string_view name = function.name();
        llvm::DISubroutineType* functionType = nullptr;
        auto* declaration = tryGetMethodDeclaration(function);
        if (declaration != nullptr)
        {
            scope = declaration->getScope();
            name = declaration->getName();
            functionType = declaration->getType();
            if (IsInstanceMethod(function))
            {
                m_thisType = objectPointerType(llvm::cast<llvm::DIType>(declaration->getScope()));
            }
        }
        else
        {
            functionType = signatureType(function, nullptr);
        }

        const auto line = static_cast<u32>(sourceLine.line);
        auto* subprogram = m_builder->createFunction(
            scope,
            name,
            LinkageName(function),
            file,
            line,
            functionType,
            line,
            llvm::DINode::FlagPrototyped,
            llvm::DISubprogram::SPFlagDefinition,
            nullptr,
            declaration);
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
        irBuilder.SetCurrentDebugLocation(llvm::DILocation::get(m_llvmModule.getContext(), static_cast<u32>(line), 0, m_subprogram));
    }

    void DebugInfoBuilder::declareVariable(std::string_view name, Type type, i32 variable, SourceLine sourceLine, llvm::AllocaInst& slot)
    {
        if (m_subprogram == nullptr)
        {
            return;
        }

        auto* variableType = debugType(type);
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
        const auto lineNumber = static_cast<u32>(line);
        llvm::DILocalVariable* debugVariable = nullptr;
        if (variable == LocalVariableSlot)
        {
            debugVariable = m_builder->createAutoVariable(m_subprogram, name, file, lineNumber, variableType);
        }
        else if (name == ImplicitThisName)
        {
            if (m_thisType != nullptr)
            {
                variableType = m_thisType;
            }

            const auto flags = llvm::DINode::FlagArtificial | llvm::DINode::FlagObjectPointer;
            debugVariable = m_builder->createParameterVariable(m_subprogram, "this", static_cast<u32>(variable), file, lineNumber, variableType, false, flags);
        }
        else
        {
            debugVariable = m_builder->createParameterVariable(m_subprogram, name, static_cast<u32>(variable), file, lineNumber, variableType);
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

    llvm::DIType* DebugInfoBuilder::debugType(Type type)
    {
        if (const auto known = m_types.find(type.id()); known != m_types.end())
        {
            return known->second;
        }

        llvm::DIType* result = nullptr;
        if (type.isReference())
        {
            result = referenceType(type);
        }
        else if (m_irModule.tryGetBuiltinTypeDescription(type) != nullptr)
        {
            result = basicType(type);
        }
        else if (type.kind() == TypeKind::FixedArray)
        {
            result = fixedArrayType(type);
        }
        else if (type.kind() == TypeKind::Slice || type.kind() == TypeKind::DynamicArray)
        {
            result = sliceOrDynamicArrayType(type);
        }
        else if (const auto* enumDeclaration = m_irModule.tryGetEnum(type); enumDeclaration != nullptr)
        {
            result = enumType(*enumDeclaration);
        }
        else if (m_irModule.tryGetType(type) != nullptr)
        {
            return structType(type);
        }

        m_types.emplace(type.id(), result);
        return result;
    }

    llvm::DIType* DebugInfoBuilder::basicType(Type type)
    {
        const auto* description = m_irModule.tryGetBuiltinTypeDescription(type);
        const auto* name = m_irModule.tryGetTypeName(type);
        if (description == nullptr || name == nullptr)
        {
            return nullptr;
        }

        switch (description->kind)
        {
            case BuiltinTypeKind::Int:
            {
                return m_builder->createBasicType(*name, static_cast<std::uint64_t>(description->bits), IntegerEncoding(*description));
            }
            case BuiltinTypeKind::Float:
            {
                return m_builder->createBasicType(*name, static_cast<std::uint64_t>(description->bits), llvm::dwarf::DW_ATE_float);
            }
            case BuiltinTypeKind::Bool:
            {
                return m_builder->createBasicType(*name, 8, llvm::dwarf::DW_ATE_boolean);
            }
            case BuiltinTypeKind::Pointer:
            {
                // a rawptr points to void
                llvm::DIType* pointeeType = nullptr;
                if (type == m_irModule.wellKnown().cstring)
                {
                    pointeeType = debugType(m_irModule.wellKnown().u8);
                }

                return m_builder->createPointerType(pointeeType, pointerBits(), 0, std::nullopt, *name);
            }
        }

        return nullptr;
    }

    llvm::DIType* DebugInfoBuilder::referenceType(Type type)
    {
        auto* referencedType = debugType(type.toValue());
        if (referencedType == nullptr)
        {
            return nullptr;
        }

        return m_builder->createReferenceType(llvm::dwarf::DW_TAG_reference_type, referencedType, pointerBits());
    }

    llvm::DIType* DebugInfoBuilder::enumType(const EnumDeclaration& enumDeclaration)
    {
        auto* baseType = debugType(enumDeclaration.baseType());
        const auto* description = m_irModule.tryGetBuiltinTypeDescription(enumDeclaration.baseType());
        if (baseType == nullptr || description == nullptr || description->kind != BuiltinTypeKind::Int)
        {
            return baseType;
        }

        std::vector<llvm::Metadata*> enumerators;
        enumerators.reserve(enumDeclaration.fields().size());
        for (const auto& field : enumDeclaration.fields())
        {
            const auto value = EnumValue(field.value);
            if (!value.has_value())
            {
                return baseType;
            }

            const auto enumeratorName = enumDeclaration.name() + "." + field.name;
            enumerators.push_back(m_builder->createEnumerator(enumeratorName, value.value(), !description->isSigned));
        }

        return m_builder->createEnumerationType(
            m_compileUnit,
            enumDeclaration.name(),
            nullptr,
            0,
            baseType->getSizeInBits(),
            0,
            m_builder->getOrCreateArray(enumerators),
            baseType);
    }

    llvm::DIType* DebugInfoBuilder::fixedArrayType(Type type)
    {
        const auto* arrayType = m_irModule.tryGetArrayType(type);
        auto* llvmType = m_lowerType(type);
        if (arrayType == nullptr || llvmType == nullptr)
        {
            return nullptr;
        }

        auto* elementType = debugType(arrayType->elementType);
        if (elementType == nullptr)
        {
            return nullptr;
        }

        const auto sizeInBits = m_llvmModule.getDataLayout().getTypeAllocSizeInBits(llvmType).getFixedValue();
        llvm::Metadata* subrange = m_builder->getOrCreateSubrange(0, arrayType->length);
        return m_builder->createArrayType(sizeInBits, 0, elementType, m_builder->getOrCreateArray(subrange));
    }

    llvm::DIType* DebugInfoBuilder::structType(Type type)
    {
        const auto& typeDeclaration = *m_irModule.tryGetType(type);
        auto* llvmType = llvm::dyn_cast_or_null<llvm::StructType>(m_lowerType(type));
        if (llvmType == nullptr)
        {
            m_types.emplace(type.id(), nullptr);
            return nullptr;
        }

        auto* structure = beginStructure(type, typeDeclaration.name(), *llvmType);
        std::vector<llvm::Metadata*> members;
        members.reserve(typeDeclaration.fields().size());
        for (u32 index = 0; index < typeDeclaration.fields().size(); ++index)
        {
            const auto& field = typeDeclaration.fields()[index];
            if (auto* fieldType = debugType(field.type); fieldType != nullptr)
            {
                members.push_back(memberType(structure, field.name, fieldType, *llvmType, index));
            }
        }

        for (const auto& method : typeDeclaration.methods())
        {
            const auto* function = m_irModule.tryGetFunction(method.functionId);
            if (function != nullptr && function->sourceLine().line != -1)
            {
                members.push_back(methodDeclaration(structure, method.name, *function));
            }
        }

        return endStructure(type, structure, members);
    }

    llvm::DIType* DebugInfoBuilder::sliceOrDynamicArrayType(Type type)
    {
        const auto* arrayType = m_irModule.tryGetArrayType(type);
        const auto* name = m_irModule.tryGetTypeName(type);
        auto* llvmType = llvm::dyn_cast_or_null<llvm::StructType>(m_lowerType(type));
        if (arrayType == nullptr || name == nullptr || llvmType == nullptr)
        {
            return nullptr;
        }

        auto* structure = beginStructure(type, *name, *llvmType);
        auto* dataType = m_builder->createPointerType(debugType(arrayType->elementType), pointerBits());
        auto* lengthType = debugType(m_irModule.wellKnown().i32);
        std::vector<llvm::Metadata*> members;
        members.push_back(memberType(structure, m_irModule.fieldName(type, 0), dataType, *llvmType, 0));
        members.push_back(memberType(structure, m_irModule.fieldName(type, 1), lengthType, *llvmType, 1));
        if (type.kind() == TypeKind::DynamicArray)
        {
            members.push_back(memberType(structure, m_irModule.fieldName(type, 2), lengthType, *llvmType, 2));
        }

        return endStructure(type, structure, members);
    }

    llvm::DICompositeType* DebugInfoBuilder::beginStructure(Type type, std::string_view name, llvm::StructType& llvmType)
    {
        const auto sizeInBits = m_llvmModule.getDataLayout().getStructLayout(&llvmType)->getSizeInBits().getFixedValue();
        auto* structure = m_builder->createReplaceableCompositeType(
            llvm::dwarf::DW_TAG_structure_type,
            name,
            m_compileUnit,
            nullptr,
            0,
            0,
            sizeInBits,
            0,
            llvm::DINode::FlagZero);
        m_types.insert_or_assign(type.id(), structure);
        return structure;
    }

    llvm::Metadata* DebugInfoBuilder::memberType(llvm::DICompositeType* structure, std::string_view name, llvm::DIType* type, llvm::StructType& llvmType, u32 index)
    {
        const auto offsetInBits = m_llvmModule.getDataLayout().getStructLayout(&llvmType)->getElementOffsetInBits(index).getFixedValue();
        return m_builder->createMemberType(structure, name, nullptr, 0, type->getSizeInBits(), 0, offsetInBits, llvm::DINode::FlagZero, type);
    }

    llvm::DIType* DebugInfoBuilder::endStructure(Type type, llvm::DICompositeType* structure, const std::vector<llvm::Metadata*>& members)
    {
        m_builder->replaceArrays(structure, m_builder->getOrCreateArray(members));
        auto* result = llvm::MDNode::replaceWithPermanent(llvm::TempDICompositeType(structure));
        m_types.insert_or_assign(type.id(), result);
        return result;
    }

    llvm::DISubprogram* DebugInfoBuilder::methodDeclaration(llvm::DICompositeType* structure, std::string_view name, const Function& method)
    {
        llvm::DIType* thisType = nullptr;
        auto flags = llvm::DINode::FlagPrototyped;
        if (IsInstanceMethod(method))
        {
            thisType = objectPointerType(structure);
        }
        else
        {
            flags |= llvm::DINode::FlagStaticMember;
        }

        const auto sourceLine = method.sourceLine();
        return m_builder->createMethod(
            structure,
            name,
            LinkageName(method),
            fileFor(sourceLine.fileId),
            static_cast<u32>(sourceLine.line),
            signatureType(method, thisType),
            0,
            0,
            nullptr,
            flags);
    }

    llvm::DISubprogram* DebugInfoBuilder::tryGetMethodDeclaration(const Function& function)
    {
        const auto owner = m_methodOwners.find(function.id());
        if (owner == m_methodOwners.end())
        {
            return nullptr;
        }

        auto* structure = llvm::dyn_cast_or_null<llvm::DICompositeType>(debugType(owner->second.type));
        if (structure == nullptr)
        {
            return nullptr;
        }

        const llvm::StringRef name = owner->second.methodName;
        const auto line = static_cast<u32>(function.sourceLine().line);
        for (auto* element : structure->getElements())
        {
            auto* declaration = llvm::dyn_cast_or_null<llvm::DISubprogram>(element);
            if (declaration != nullptr && declaration->getName() == name && declaration->getLine() == line)
            {
                return declaration;
            }
        }

        return nullptr;
    }

    llvm::DIType* DebugInfoBuilder::objectPointerType(llvm::DIType* structure)
    {
        return llvm::DIBuilder::createObjectPointerType(m_builder->createPointerType(structure, pointerBits()), true);
    }

    llvm::DISubroutineType* DebugInfoBuilder::signatureType(const Function& function, llvm::DIType* thisType)
    {
        std::vector<llvm::Metadata*> types;
        types.reserve(function.parameters().size() + 1);
        types.push_back(debugType(function.returnType()));
        for (const auto& parameter : function.parameters())
        {
            if (thisType != nullptr && types.size() == 1)
            {
                types.push_back(thisType);
                continue;
            }

            types.push_back(debugType(parameter.type()));
        }

        return m_builder->createSubroutineType(m_builder->getOrCreateTypeArray(types));
    }

    u32 DebugInfoBuilder::pointerBits() const
    {
        return m_llvmModule.getDataLayout().getPointerSizeInBits();
    }
}
