#include "SemanticContext.h"


#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace Caracal
{
    // value, reference, optional value, optional reference
    constexpr int VariantCount = 4;

    [[nodiscard]] static FunctionType ToFunctionType(MethodModifier modifier)
    {
        switch (modifier)
        {
            case MethodModifier::Public:
                return FunctionType::PublicMethod;
            case MethodModifier::Private:
                return FunctionType::PrivateMethod;
            case MethodModifier::Static:
                return FunctionType::StaticMethod;
            default:
                return FunctionType::None;
        }
    }

    static void ValidateCompilerEmittedCall(
        SemanticContext& module,
        const char* methodName,
        const std::vector<Type>& parameterTypes,
        Type returnType)
    {
        const auto methodType = module.tryGetExternFunctionByFullName(std::string("C.") + methodName);
        auto matches = methodType != Type::Undefined();
        if (matches)
        {
            // the compiler emits calls to it, so the binding must carry required = true
            auto isMarkedRequired = false;
            for (const auto requiredType : module.requiredExternFunctions())
            {
                if (requiredType == methodType)
                {
                    isMarkedRequired = true;
                }
            }
            matches = isMarkedRequired;
        }
        if (matches)
        {
            const auto& definition = module.getFunctionDefinition(methodType);
            const auto& parameters = definition.parameters();
            matches = definition.symbolName().has_value()
                && parameters.size() == parameterTypes.size()
                && definition.returnTypes().size() == 1
                && definition.returnTypes().front() == returnType;
            if (matches)
            {
                for (size_t index = 0; index < parameterTypes.size(); ++index)
                {
                    if (parameters[index].type() != parameterTypes[index])
                    {
                        matches = false;
                    }
                }
            }
        }

        if (!matches)
        {
            std::cerr << "error: the prelude did not define 'C." << methodName << "' with the signature the compiler emits\n";
            std::abort();
        }
    }

    SemanticContext SemanticContext::WithBuiltins() noexcept
    {
        SemanticContext module{};
        module.createBuiltinType(Type::CVariadic(), "...", false);
        module.createBuiltinType(Type::Function(), "function", false);
        module.createBuiltinType(Type::Discard(), "discard", false);
        module.createBuiltinType(Type::Undefined(), "undefined", false);
        module.createBuiltinType(Type::Void(), "void", false);

        return module;
    }

    void SemanticContext::finalizePrelude(bool preludeWasLoaded) noexcept
    {
        if (preludeWasLoaded)
        {
            const auto& wellKnownTypes = wellKnown();
            if (wellKnownTypes.boolean == Type::Undefined()
                || wellKnownTypes.u8 == Type::Undefined()
                || wellKnownTypes.i32 == Type::Undefined()
                || wellKnownTypes.f32 == Type::Undefined()
                || wellKnownTypes.cstring == Type::Undefined()
                || wellKnownTypes.rawptr == Type::Undefined()
                || wellKnownTypes.string == Type::Undefined())
            {
                std::cerr << "error: the prelude did not define all builtin types\n";
                std::abort();
            }

            // the compiler emits calls to these bindings itself, so their absence or a signature drift must fail fast
            ValidateCompilerEmittedCall(*this, "calloc", { wellKnownTypes.i64, wellKnownTypes.i64 }, wellKnownTypes.rawptr);
            ValidateCompilerEmittedCall(*this, "realloc", { wellKnownTypes.rawptr, wellKnownTypes.i64 }, wellKnownTypes.rawptr);
            ValidateCompilerEmittedCall(*this, "memmove", { wellKnownTypes.rawptr, wellKnownTypes.rawptr, wellKnownTypes.i64 }, wellKnownTypes.rawptr);
            ValidateCompilerEmittedCall(*this, "strcmp", { wellKnownTypes.cstring, wellKnownTypes.cstring }, wellKnownTypes.i32);

            // slice() hands out a read-only view of the string's bytes, intrinsic-blessed like the array methods
            auto& stringDefinition = getTypeDefinition(wellKnownTypes.string);
            const auto immutableByteSlice = getOrCreateArrayType(TypeKind::Slice, wellKnownTypes.u8, 0, true);
            auto sliceParameters = std::vector<Parameter>{};
            sliceParameters.emplace_back(ImplicitThisName, wellKnownTypes.string.toReference());
            auto& sliceDefinition = createMethod(stringDefinition, MethodModifier::Public, "slice", sliceParameters, { immutableByteSlice }, nullptr);
            sliceDefinition.setFunctionType(FunctionType::Intrinsic);
            sliceDefinition.setIntrinsicKind(IntrinsicKind::ArraySlice);

            // toCString() hands out the NUL-terminated data pointer for FFI, construction guarantees the NUL
            auto toCStringParameters = std::vector<Parameter>{};
            toCStringParameters.emplace_back(ImplicitThisName, wellKnownTypes.string.toReference());
            auto& toCStringDefinition = createMethod(stringDefinition, MethodModifier::Public, "toCString", toCStringParameters, { wellKnownTypes.cstring }, nullptr);
            toCStringDefinition.setFunctionType(FunctionType::Intrinsic);
            toCStringDefinition.setIntrinsicKind(IntrinsicKind::StringToCString);
        }

        // prelude definitions only lower on demand, the boundary lets the lowerer skip them
        m_preludeFunctionDefinitionCount = m_functionDefinitions.size();
        m_preludeTypeDefinitionCount = m_typeDefinitions.size();

        m_nextId = std::max(m_nextId, 2000);
    }

    Type SemanticContext::tryGetFunctionTypeByName(std::string_view typeName) const noexcept
    {
        if (const auto result = m_functionNameToType.find(typeName); result != m_functionNameToType.end())
            return result->second;

        return Type::Undefined();
    }

    EnumDefinition& SemanticContext::getEnumDefinition(Type type) noexcept
    {
        static auto invalidEnum = EnumDefinition{ nullptr, Type::Undefined(), std::string("???") };

        auto baseType = type.toBaseType();
        auto id = baseType.id();
        if (const auto index = m_enumDefinitionIndexById.find(id); index != m_enumDefinitionIndexById.end())
            return m_enumDefinitions.at(index->second);
        else
            return invalidEnum;
    }

    TypeDefinition& SemanticContext::getTypeDefinition(Type type) noexcept
    {
        static auto invalidType = TypeDefinition{ nullptr, Type::Undefined(), std::string("???") };

        auto baseType = type.toBaseType();
        auto id = baseType.id();
        if (const auto index = m_typeDefinitionIndexById.find(id); index != m_typeDefinitionIndexById.end())
            return m_typeDefinitions.at(index->second);
        else
            return invalidType;
    }

    const OperatorSignature* SemanticContext::tryGetOperatorSignature(Type type, BinaryOperatorKind operation) const noexcept
    {
        if (!type.isBaseType())
        {
            return nullptr;
        }

        const auto index = m_typeDefinitionIndexById.find(type.id());
        if (index == m_typeDefinitionIndexById.end())
        {
            return nullptr;
        }

        return m_typeDefinitions.at(index->second).tryGetOperatorSignature(operation);
    }

    const OperatorSignature* SemanticContext::tryGetOperatorSignature(Type type, UnaryOperatorKind operation) const noexcept
    {
        if (!type.isBaseType())
        {
            return nullptr;
        }

        const auto index = m_typeDefinitionIndexById.find(type.id());
        if (index == m_typeDefinitionIndexById.end())
        {
            return nullptr;
        }

        return m_typeDefinitions.at(index->second).tryGetOperatorSignature(operation);
    }

    FunctionDefinition& SemanticContext::getFunctionDefinition(Type type) noexcept
    {
        static auto invalidFunction = FunctionDefinition{ nullptr, Type::Undefined(), Type::Undefined(), FunctionType::None, std::string("???"), std::string("???"), false };

        auto id = type.id();
        if (const auto index = m_functionDefinitionIndexById.find(id); index != m_functionDefinitionIndexById.end())
            return m_functionDefinitions.at(index->second);
        else
            return invalidFunction;
    }

    const FunctionDefinition* SemanticContext::tryGetFunctionDefinition(Type type) const noexcept
    {
        if (const auto index = m_functionDefinitionIndexById.find(type.id()); index != m_functionDefinitionIndexById.end())
            return &m_functionDefinitions.at(index->second);

        return nullptr;
    }

    Type SemanticContext::tryGetTypeByName(std::string_view name) const noexcept
    {
        if (const auto result = m_nameToTypes.find(name); result != m_nameToTypes.end())
            return result->second;

        return Type::Undefined();
    }

    std::string_view SemanticContext::getNameByType(Type type) const noexcept
    {
        auto baseType = type.toBaseType();
        auto id = baseType.id();
        if (m_typeNames.contains(id))
            return m_typeNames.at(id);
        else
            return std::string_view("undefined");
    }

    [[nodiscard]] static std::string BuildArrayTypeName(
        const SemanticContext& module,
        TypeKind arrayKind,
        Type elementType,
        i32 length,
        bool immutableSlice)
    {
        auto elementName = std::string(module.getNameByType(elementType));
        if (elementType.isReference())
        {
            elementName = "ref " + elementName;
        }

        auto name = "[" + elementName;
        if (immutableSlice)
        {
            name = "const " + name;
        }
        if (arrayKind == TypeKind::FixedArray)
        {
            name += "; " + std::to_string(length);
        }
        else if (arrayKind == TypeKind::DynamicArray)
        {
            name += "; _";
        }

        name += "]";
        return name;
    }

    Type SemanticContext::getOrCreateArrayType(TypeKind arrayKind, Type elementType, i32 length, bool immutableSlice) noexcept
    {
        auto arrayName = BuildArrayTypeName(*this, arrayKind, elementType, length, immutableSlice);
        if (const auto existing = m_nameToTypes.find(arrayName); existing != m_nameToTypes.end())
        {
            return existing->second;
        }

        auto arrayId = m_nextId += VariantCount;
        auto arrayType = Type{ arrayId, arrayKind };
        m_typeNames.try_emplace(arrayId, arrayName);
        m_nameToTypes.try_emplace(arrayName, arrayType);
        m_arrayTypeInfoById.try_emplace(arrayId, ArrayTypeInfo{ elementType, length, immutableSlice });
        m_arrayTypes.push_back(arrayType);

        return arrayType;
    }

    bool SemanticContext::isImmutableSlice(Type type) const noexcept
    {
        const auto id = type.toBaseType().id();
        if (const auto info = m_arrayTypeInfoById.find(id); info != m_arrayTypeInfoById.end())
        {
            return info->second.isImmutableSlice;
        }

        return false;
    }

    Type SemanticContext::getArrayElementType(Type type) const noexcept
    {
        const auto id = type.toBaseType().id();
        if (const auto info = m_arrayTypeInfoById.find(id); info != m_arrayTypeInfoById.end())
        {
            return info->second.elementType;
        }

        return Type::Undefined();
    }

    i32 SemanticContext::getArrayLength(Type type) const noexcept
    {
        const auto id = type.toBaseType().id();
        if (const auto info = m_arrayTypeInfoById.find(id); info != m_arrayTypeInfoById.end())
        {
            return info->second.length;
        }

        return 0;
    }

    bool SemanticContext::isPreludeFunctionDefinition(Type functionType) const noexcept
    {
        const auto index = m_functionDefinitionIndexById.find(functionType.toBaseType().id());
        if (index == m_functionDefinitionIndexById.end())
        {
            return false;
        }

        return index->second < m_preludeFunctionDefinitionCount;
    }

    bool SemanticContext::isPreludeTypeDefinition(Type type) const noexcept
    {
        const auto index = m_typeDefinitionIndexById.find(type.toBaseType().id());
        if (index == m_typeDefinitionIndexById.end())
        {
            return false;
        }

        return index->second < m_preludeTypeDefinitionCount;
    }

    void SemanticContext::markPreludeTypeRequired(Type type) noexcept
    {
        const auto baseType = type.toBaseType();
        if (baseType.kind() != TypeKind::Type || !isPreludeTypeDefinition(baseType))
        {
            return;
        }

        for (const auto existing : m_requiredPreludeTypes)
        {
            if (existing == baseType)
            {
                return;
            }
        }
        m_requiredPreludeTypes.push_back(baseType);

        for (const auto& field : getTypeDefinition(baseType).fields())
        {
            markPreludeTypeRequired(field.type());
        }
    }

    void SemanticContext::markExternRequired(Type functionType) noexcept
    {
        for (const auto existing : m_requiredExternFunctions)
        {
            if (existing == functionType)
            {
                return;
            }
        }

        m_requiredExternFunctions.push_back(functionType);
    }

    Type SemanticContext::tryGetExternFunctionByFullName(std::string_view fullName) const noexcept
    {
        for (const auto& definition : m_functionDefinitions)
        {
            if (definition.symbolName().has_value() && definition.fullName() == fullName)
            {
                return definition.type();
            }
        }

        return Type::Undefined();
    }

    Type SemanticContext::tryGetOrCreateArrayIntrinsic(Type arrayType, std::string_view methodName) noexcept
    {
        const auto baseArrayType = arrayType.toBaseType();
        const auto elementType = getArrayElementType(baseArrayType);
        if (elementType == Type::Undefined())
        {
            return Type::Undefined();
        }

        auto fullMethodName = std::string(getNameByType(baseArrayType)) + "." + std::string(methodName);
        if (const auto existing = m_arrayIntrinsicsByFullName.find(fullMethodName); existing != m_arrayIntrinsicsByFullName.end())
        {
            return existing->second;
        }


        // TODO replace the following code once we got generics + array prelude
        // lazily add the intrinsics for this array type
        auto parameters = std::vector<Parameter>{};
        auto returnTypes = std::vector<Type>{};
        if (methodName == "at")
        {
            parameters.emplace_back(ImplicitThisName, baseArrayType.toReference());
            parameters.emplace_back("index", m_wellKnownTypes.i32);
            returnTypes.push_back(elementType);
        }
        else if (methodName == "set")
        {
            parameters.emplace_back(ImplicitThisName, baseArrayType.toReference());
            parameters.emplace_back("index", m_wellKnownTypes.i32);
            parameters.emplace_back("value", elementType);
            returnTypes.push_back(Type::Void());
        }
        else if (methodName == "add" && baseArrayType.kind() == TypeKind::DynamicArray)
        {
            parameters.emplace_back(ImplicitThisName, baseArrayType.toReference());
            parameters.emplace_back("value", elementType);
            returnTypes.push_back(Type::Void());
        }
        else if (methodName == "remove" && baseArrayType.kind() == TypeKind::DynamicArray)
        {
            parameters.emplace_back(ImplicitThisName, baseArrayType.toReference());
            parameters.emplace_back("index", m_wellKnownTypes.i32);
            returnTypes.push_back(Type::Void());
        }
        else if (methodName == "slice" && baseArrayType.kind() != TypeKind::Slice)
        {
            parameters.emplace_back(ImplicitThisName, baseArrayType.toReference());
            returnTypes.push_back(getOrCreateArrayType(TypeKind::Slice, elementType, 0));
        }
        else
        {
            return Type::Undefined();
        }

        auto methodId = m_nextId += VariantCount;
        auto methodType = Type{ methodId, TypeKind::Method };
        m_functionDefinitions.emplace_back(nullptr, methodType, baseArrayType, FunctionType::Intrinsic, std::string(methodName), fullMethodName, false, parameters, returnTypes);
        if (methodName == "at")
        {
            m_functionDefinitions.back().setIntrinsicKind(IntrinsicKind::ArrayAt);
        }
        else if (methodName == "set")
        {
            m_functionDefinitions.back().setIntrinsicKind(IntrinsicKind::ArraySet);
        }
        else if (methodName == "add")
        {
            m_functionDefinitions.back().setIntrinsicKind(IntrinsicKind::ArrayAdd);
        }
        else if (methodName == "remove")
        {
            m_functionDefinitions.back().setIntrinsicKind(IntrinsicKind::ArrayRemove);
        }
        else if (methodName == "slice")
        {
            m_functionDefinitions.back().setIntrinsicKind(IntrinsicKind::ArraySlice);
        }
        else
        {
            TODO("Unhandled array intrinsic method");
        }
        m_functionDefinitionIndexById.try_emplace(methodId, m_functionDefinitions.size() - 1);
        m_arrayIntrinsicsByFullName.try_emplace(std::move(fullMethodName), methodType);

        return methodType;
    }

    EnumDefinition& SemanticContext::createEnum(
        std::string_view name,
        const EnumDefinitionStatement* statement) noexcept
    {
        auto enumName = std::string(name);
        auto enumId = m_nextId += VariantCount;
        auto valueType = Type{ enumId, TypeKind::Enum };
        m_typeNames.try_emplace(enumId, enumName);
        m_nameToTypes.try_emplace(enumName, valueType);
        m_enumDefinitions.emplace_back(statement, valueType, enumName);
        m_enumDefinitionIndexById.try_emplace(enumId, m_enumDefinitions.size() - 1);

        return m_enumDefinitions.back();
    }

    TypeDefinition& SemanticContext::createType(
        std::string_view name,
        const TypeDefinitionStatement* statement) noexcept
    {
        auto typeName = std::string(name);
        auto typeId = m_nextId += VariantCount;
        auto valueType = Type{ typeId, TypeKind::Type };
        m_typeNames.try_emplace(typeId, typeName);
        m_nameToTypes.try_emplace(typeName, valueType);
        m_typeDefinitions.emplace_back(statement, valueType, typeName);
        m_typeDefinitionIndexById.try_emplace(typeId, m_typeDefinitions.size() - 1);

        return m_typeDefinitions.back();
    }

    TypeDefinition& SemanticContext::createBuiltinTypeFromDescription(std::string_view name, const BuiltinTypeDescription& description, const TypeDefinitionStatement* statement) noexcept
    {
        auto typeName = std::string(name);
        auto typeId = m_nextId += VariantCount;
        auto valueType = Type{ typeId, TypeKind::Builtin };
        m_typeNames.try_emplace(typeId, typeName);
        m_nameToTypes.try_emplace(typeName, valueType);
        m_builtinTypeDescriptionsById.try_emplace(typeId, description);
        m_typeDefinitions.emplace_back(statement, valueType, typeName);
        m_typeDefinitionIndexById.try_emplace(typeId, m_typeDefinitions.size() - 1);

        return m_typeDefinitions.back();
    }

    const BuiltinTypeDescription* SemanticContext::tryGetBuiltinTypeDescription(Type type) const noexcept
    {
        const auto it = m_builtinTypeDescriptionsById.find(type.toBaseType().id());
        if (it == m_builtinTypeDescriptionsById.end())
        {
            return nullptr;
        }

        return &it->second;
    }

    void SemanticContext::refreshWellKnownTypes() noexcept
    {
        m_wellKnownTypes.boolean = tryGetTypeByName("bool");
        m_wellKnownTypes.u8 = tryGetTypeByName("u8");
        m_wellKnownTypes.u16 = tryGetTypeByName("u16");
        m_wellKnownTypes.u32 = tryGetTypeByName("u32");
        m_wellKnownTypes.u64 = tryGetTypeByName("u64");
        m_wellKnownTypes.i8 = tryGetTypeByName("i8");
        m_wellKnownTypes.i16 = tryGetTypeByName("i16");
        m_wellKnownTypes.i32 = tryGetTypeByName("i32");
        m_wellKnownTypes.i64 = tryGetTypeByName("i64");
        m_wellKnownTypes.f32 = tryGetTypeByName("f32");
        m_wellKnownTypes.f64 = tryGetTypeByName("f64");
        m_wellKnownTypes.cstring = tryGetTypeByName("cstring");
        m_wellKnownTypes.rawptr = tryGetTypeByName("rawptr");
        m_wellKnownTypes.string = tryGetTypeByName("string");
    }

    TypeDefinition& SemanticContext::bindBuiltinTypeDefinition(Type type, const TypeDefinitionStatement* statement) noexcept
    {
        const auto typeId = type.id();
        m_typeDefinitions.emplace_back(statement, type, m_typeNames.at(typeId));
        m_typeDefinitionIndexById.try_emplace(typeId, m_typeDefinitions.size() - 1);

        return m_typeDefinitions.back();
    }

    FunctionDefinition& SemanticContext::createFunction(
        std::string_view name,
        std::vector<Parameter> parameters,
        std::vector<Type> returnTypes,
        const FunctionDefinitionStatement* functionStatement) noexcept
    {
        auto functionName = std::string(name);
        auto functionId = m_nextId += VariantCount;
        auto functionType = Type{ functionId, TypeKind::Function };
        auto isVariadic = !parameters.empty() && parameters.back().type() == Type::CVariadic();
        const Statement* statement = static_cast<const Statement*>(functionStatement);
        m_functionDefinitions.emplace_back(statement, functionType, Type::Undefined(), FunctionType::FreeFunction, functionName, functionName, isVariadic, std::move(parameters), std::move(returnTypes));
        m_functionDefinitionIndexById.try_emplace(functionId, m_functionDefinitions.size() - 1);
        m_functionNameToType.try_emplace(std::move(functionName), functionType);

        return m_functionDefinitions.back();
    }

    FunctionDefinition& SemanticContext::createMethod(
        TypeDefinition& typeDefinition,
        MethodModifier modifier,
        const std::string& methodName,
        std::vector<Parameter> parameters,
        std::vector<Type> returnTypes,
        const MethodDefinitionStatement* methodStatement) noexcept
    {
        auto parentType = typeDefinition.type();
        auto fullMethodName = typeDefinition.name() + "." + methodName;
        auto methodId = m_nextId += VariantCount;
        auto methodType = Type{ methodId, TypeKind::Method };
        auto functionType = ToFunctionType(modifier);
        const Statement* statement = static_cast<const Statement*>(methodStatement);
        m_functionDefinitions.emplace_back(statement, methodType, parentType, functionType, methodName, std::move(fullMethodName), false, std::move(parameters), std::move(returnTypes));
        m_functionDefinitionIndexById.try_emplace(methodId, m_functionDefinitions.size() - 1);
        typeDefinition.addMethod(methodType, methodName);

        return m_functionDefinitions.back();
    }

    FunctionDefinition& SemanticContext::createConstructor(
        TypeDefinition& typeDefinition,
        std::vector<Parameter> parameters) noexcept
    {
        auto parentType = typeDefinition.type();
        auto constructorName = std::string("new");
        auto fullConstructorName = typeDefinition.name() + "." + constructorName;
        auto constructorId = m_nextId += VariantCount;
        auto methodType = Type{ constructorId, TypeKind::Constructor };
        m_functionDefinitions.emplace_back(nullptr, methodType, parentType, FunctionType::SynthesizedConstructor, constructorName, std::move(fullConstructorName), false, std::move(parameters), std::vector<Type>{ Type::Void() });
        m_functionDefinitionIndexById.try_emplace(constructorId, m_functionDefinitions.size() - 1);
        typeDefinition.addMethod(methodType, constructorName);

        return m_functionDefinitions.back();
    }

    const ConstantDefinition* SemanticContext::tryGetConstantDefinition(std::string_view name) const noexcept
    {
        if (const auto result = m_constantDefinitionIndexByName.find(name); result != m_constantDefinitionIndexByName.end())
            return &m_constantDefinitions.at(result->second);

        return nullptr;
    }

    ConstantDefinition& SemanticContext::createConstant(
        std::string_view name,
        Expression* expression) noexcept
    {
        auto constantName = std::string(name);
        m_constantDefinitions.emplace_back(constantName, expression);
        m_constantDefinitionIndexByName.try_emplace(constantName, m_constantDefinitions.size() - 1);

        return m_constantDefinitions.back();
    }

    ConstantDefinition& SemanticContext::createConstant(
        std::string_view name,
        Type type,
        NodeIndex initializer) noexcept
    {
        auto constantName = std::string(name);
        m_constantDefinitions.emplace_back(constantName, type, initializer);
        m_constantDefinitionIndexByName.try_emplace(constantName, m_constantDefinitions.size() - 1);

        return m_constantDefinitions.back();
    }

    ConstantDefinition& SemanticContext::createInitConstant(
        std::string_view name,
        Type type) noexcept
    {
        auto constantName = std::string(name);
        m_constantDefinitions.emplace_back(constantName, type);
        m_constantDefinitionIndexByName.try_emplace(constantName, m_constantDefinitions.size() - 1);

        return m_constantDefinitions.back();
    }

    void SemanticContext::addGlobalDiscardExpression(const Expression* expression) noexcept
    {
        m_globalDiscardExpressions.push_back(expression);
    }

    void SemanticContext::createBuiltinType(Type type, std::string_view name, bool addVariants)
    {
        m_nameToTypes.try_emplace(std::string(name), type);

        if (type == Type::CVariadic())
        {
            m_typeNames.try_emplace(type.id(), std::string("C Variadic"));
        }
        else
        {
            m_typeNames.try_emplace(type.id(), std::string(name));
        }

        if (addVariants)
        {
            m_nextId += VariantCount;
        }
    }
    
    UnitSemantics& SemanticContext::createUnitSemantics(u16 fileId, u32 nodeCount) noexcept
    {
        auto& semantics = m_unitSemanticsByFileId[fileId];
        semantics.nodeTypes.assign(nodeCount, Type::Undefined());
        return semantics;
    }

    Type SemanticContext::typeOfNode(NodeIndex reference) const noexcept
    {
        const auto found = m_unitSemanticsByFileId.find(reference.fileId);
        if (found == m_unitSemanticsByFileId.end() || reference.index >= found->second.nodeTypes.size())
        {
            return Type::Undefined();
        }

        return found->second.nodeTypes[reference.index];
    }

    void SemanticContext::setNodeType(NodeIndex reference, Type type) noexcept
    {
        const auto found = m_unitSemanticsByFileId.find(reference.fileId);
        if (found == m_unitSemanticsByFileId.end() || reference.index >= found->second.nodeTypes.size())
        {
            return;
        }

        found->second.nodeTypes[reference.index] = type;
    }
}
