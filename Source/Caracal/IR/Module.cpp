#include <Caracal/IR/Module.h>

#include <Caracal/Constants.h>

#include <iterator>
#include <string>
#include <utility>

namespace Caracal
{
    const EnumDeclaration* Module::tryGetEnum(Type type) const noexcept
    {
        const auto result = m_enumIndices.find(type.id());
        if (result == m_enumIndices.end())
            return nullptr;

        return &m_enums[result->second];
    }

    const ExternFunction* Module::tryGetExternFunction(FunctionId id) const noexcept
    {
        const auto result = m_externFunctionIndices.find(id);
        if (result == m_externFunctionIndices.end())
            return nullptr;

        return &m_externFunctions[result->second];
    }

    const Function* Module::tryGetFunction(FunctionId id) const noexcept
    {
        const auto result = m_functionIndices.find(id);
        if (result == m_functionIndices.end())
            return nullptr;

        return &m_functions[result->second];
    }

    const std::string* Module::tryGetFunctionName(FunctionId id) const noexcept
    {
        if (const auto* externFunction = tryGetExternFunction(id))
            return &externFunction->name();

        const auto result = m_functionIndices.find(id);
        if (result != m_functionIndices.end())
            return &m_functions[result->second].name();

        return nullptr;
    }

    const std::string* Module::tryGetTypeName(Type type) const noexcept
    {
        const auto baseType = type.toBaseType();

        if (const auto* enumDeclaration = tryGetEnum(baseType))
            return &enumDeclaration->name();

        const auto result = m_typeIndices.find(baseType.id());
        if (result != m_typeIndices.end())
            return &m_types[result->second].name();

        const auto registered = m_registeredTypeNames.find(baseType.id());
        if (registered != m_registeredTypeNames.end())
            return &registered->second;

        return nullptr;
    }

    void Module::addEnum(EnumDeclaration enumDeclaration)
    {
        const auto enumId = enumDeclaration.type().id();
        m_enumIndices.emplace(enumId, m_enums.size());
        m_enums.push_back(std::move(enumDeclaration));
    }

    void Module::addExternFunction(ExternFunction function)
    {
        m_externFunctionIndices.emplace(function.id(), m_externFunctions.size());
        m_externFunctions.push_back(std::move(function));
    }

    Function* Module::addFunction(Function function)
    {
        m_functionIndices.emplace(function.id(), m_functions.size());
        m_functions.push_back(std::move(function));
        return &m_functions.back();
    }

    void Module::reserveFunctions(size_t count)
    {
        m_functions.reserve(count);
        m_functionIndices.reserve(count);
    }

    void Module::addGlobalConstant(GlobalConstantDeclaration globalDeclaration)
    {
        m_globalConstants.push_back(std::move(globalDeclaration));
    }

    void Module::addGlobalReference(GlobalReferenceDeclaration globalDeclaration)
    {
        m_globalReferences.push_back(std::move(globalDeclaration));
    }

    void Module::addConstructedGlobal(ConstructedGlobalDeclaration globalDeclaration)
    {
        m_constructedGlobals.push_back(std::move(globalDeclaration));
    }

    const Function* Module::tryGetGlobalInit() const noexcept
    {
        if (!m_globalInit.has_value())
        {
            return nullptr;
        }

        return &m_globalInit.value();
    }

    Function* Module::tryGetGlobalInitMutable() noexcept
    {
        if (!m_globalInit.has_value())
        {
            return nullptr;
        }

        return &m_globalInit.value();
    }

    void Module::setGlobalInit(Function function)
    {
        m_globalInit = std::move(function);
    }

    void Module::addType(TypeDeclaration typeDeclaration)
    {
        m_typeIndices.emplace(typeDeclaration.type().id(), m_types.size());
        m_types.push_back(std::move(typeDeclaration));
    }

    void Module::registerTypeName(Type type, std::string name)
    {
        m_registeredTypeNames.insert_or_assign(type.toBaseType().id(), std::move(name));
    }

    void Module::registerArrayType(Type type, Type elementType, i32 length)
    {
        m_arrayTypes.insert_or_assign(type.toBaseType().id(), ArrayTypeDescription{ elementType, length });
    }

    const ArrayTypeDescription* Module::tryGetArrayType(Type type) const noexcept
    {
        const auto result = m_arrayTypes.find(type.toBaseType().id());
        if (result == m_arrayTypes.end())
            return nullptr;

        return &result->second;
    }

    void Module::registerBuiltinTypeDescription(Type type, const BuiltinTypeDescription& description)
    {
        m_builtinTypeDescriptionsById.try_emplace(type.toBaseType().id(), description);
    }

    const BuiltinTypeDescription* Module::tryGetBuiltinTypeDescription(Type type) const noexcept
    {
        const auto result = m_builtinTypeDescriptionsById.find(type.toBaseType().id());
        if (result == m_builtinTypeDescriptionsById.end())
            return nullptr;

        return &result->second;
    }

    ConstantValue Module::constantValue(const Instruction& constantInstruction) const
    {
        const auto form = static_cast<ConstantForm>(constantInstruction.a);
        if (form == ConstantForm::Scalar)
        {
            return ConstantValue::FromLiteralData(scalarConstantFromBits(static_cast<u32>(constantInstruction.b), constantInstruction.constantBits()));
        }

        if (form == ConstantForm::String)
        {
            const auto range = StringRange{ static_cast<u32>(constantInstruction.c), static_cast<u32>(constantInstruction.d) };
            return ConstantValue::FromString(std::string(m_constants.string(range)));
        }

        return m_constants.constant(static_cast<u32>(constantInstruction.b));
    }

    std::string_view Module::fieldName(Type objectType, i32 fieldIndex) const noexcept
    {
        static constexpr std::string_view descriptorFieldNames[] = { "data", ArrayLengthMemberName, "capacity" };

        const auto baseType = objectType.toBaseType();
        const auto kind = baseType.kind();
        if (kind == TypeKind::DynamicArray || kind == TypeKind::Slice || kind == TypeKind::FixedArray)
        {
            if (fieldIndex >= 0 && static_cast<size_t>(fieldIndex) < std::size(descriptorFieldNames))
            {
                return descriptorFieldNames[fieldIndex];
            }
            return "<unknown-field>";
        }

        const auto found = m_typeIndices.find(baseType.id());
        if (found != m_typeIndices.end())
        {
            const auto& fields = m_types[found->second].fields();
            if (fieldIndex >= 0 && static_cast<size_t>(fieldIndex) < fields.size())
            {
                return fields[static_cast<size_t>(fieldIndex)].name;
            }
        }

        return "<unknown-field>";
    }

    u32 Module::internName(std::string_view name)
    {
        if (const auto found = m_nameIds.find(name); found != m_nameIds.end())
        {
            return found->second;
        }

        const auto id = static_cast<u32>(m_names.size());
        m_names.emplace_back(name);
        m_nameIds.emplace(m_names.back(), id);
        return id;
    }

    void Module::setSourceFileName(u16 fileId, std::string fileName)
    {
        if (m_sourceFileNames.size() <= fileId)
        {
            m_sourceFileNames.resize(static_cast<size_t>(fileId) + 1);
        }

        m_sourceFileNames[fileId] = std::move(fileName);
    }
}
