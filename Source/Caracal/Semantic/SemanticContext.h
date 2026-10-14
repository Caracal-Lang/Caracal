#pragma once

#include <Caracal/Defines.h>
#include <Caracal/API.h>
#include <Caracal/Constants.h>
#include <Caracal/Semantic/Type.h>
#include <Caracal/Semantic/BuiltinTypeDescription.h>
#include <Caracal/Semantic/TypeCheckerOptions.h>
#include <Caracal/Semantic/ConstantDefinition.h>
#include <Caracal/Semantic/EnumDefinition.h>
#include <Caracal/Semantic/TypeDefinition.h>
#include <Caracal/Semantic/UnitSemantics.h>
#include <Caracal/Semantic/FunctionDefinition.h>
#include <Caracal/Syntax/MethodModifier.h>
#include <Caracal/Syntax/NodeIndex.h>
#include <Caracal/Semantic/Parameter.h>
#include <Caracal/Semantic/PreludeFunctionPool.h>
#include <Caracal/Text/TransparentStringHash.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Caracal
{
    class CARACAL_API SemanticContext
    {
    public:
        SemanticContext() = default;

        CARACAL_DELETE_COPY_DEFAULT_MOVE(SemanticContext)

        [[nodiscard]] static SemanticContext WithBuiltins() noexcept;
        void finalizePrelude(bool preludeWasLoaded) noexcept;
        [[nodiscard]] PreludeFunctionPool& preludeFunctionPool() noexcept { return m_preludeFunctionPool; }

        [[nodiscard]] Type tryGetFunctionTypeByName(std::string_view typeName) const noexcept;
        [[nodiscard]] EnumDefinition& getEnumDefinition(Type type) noexcept;
        [[nodiscard]] TypeDefinition& getTypeDefinition(Type type) noexcept;
        [[nodiscard]] const OperatorSignature* tryGetOperatorSignature(Type type, BinaryOperatorKind operation) const noexcept;
        [[nodiscard]] const OperatorSignature* tryGetOperatorSignature(Type type, UnaryOperatorKind operation) const noexcept;
        TypeDefinition& bindBuiltinTypeDefinition(Type type) noexcept;
        [[nodiscard]] FunctionDefinition& getFunctionDefinition(Type type) noexcept;
        [[nodiscard]] const FunctionDefinition* tryGetFunctionDefinition(Type type) const noexcept;
        [[nodiscard]] Type tryGetTypeByName(std::string_view name) const noexcept;
        [[nodiscard]] std::string_view getNameByType(Type type) const noexcept;

        [[nodiscard]] Type getOrCreateArrayType(TypeKind arrayKind, Type elementType, i32 length, bool immutableSlice = false) noexcept;
        [[nodiscard]] bool isImmutableSlice(Type type) const noexcept;
        [[nodiscard]] Type getArrayElementType(Type type) const noexcept;
        [[nodiscard]] i32 getArrayLength(Type type) const noexcept;
        [[nodiscard]] const std::vector<Type>& arrayTypes() const noexcept { return m_arrayTypes; }
        [[nodiscard]] Type tryGetOrCreateArrayIntrinsic(Type arrayType, std::string_view methodName) noexcept;

        [[nodiscard]] EnumDefinition& createEnum(std::string_view name) noexcept;
        [[nodiscard]] TypeDefinition& createType(std::string_view name) noexcept;
        [[nodiscard]] FunctionDefinition& createFunction(
            std::string_view name,
            std::vector<Parameter> parameters,
            std::vector<Type> returnTypes) noexcept;
        [[nodiscard]] FunctionDefinition& createMethod(
            TypeDefinition& typeDefinition,
            MethodModifier modifier,
            const std::string& methodName,
            std::vector<Parameter> parameters,
            std::vector<Type> returnTypes) noexcept;
        [[nodiscard]] FunctionDefinition& createConstructor(
            TypeDefinition& typeDefinition,
            std::vector<Parameter> parameters) noexcept;
        [[nodiscard]] const ConstantDefinition* tryGetConstantDefinition(std::string_view name) const noexcept;
        [[nodiscard]] ConstantDefinition& createConstant(
            std::string_view name,
            Type type,
            NodeIndex initializer) noexcept;
        [[nodiscard]] ConstantDefinition& createInitConstant(
            std::string_view name,
            Type type) noexcept;
        void createBuiltinType(Type type, std::string_view name, bool addVariants = false);
        [[nodiscard]] TypeDefinition& createBuiltinTypeFromDescription(std::string_view name, const BuiltinTypeDescription& description) noexcept;
        [[nodiscard]] const BuiltinTypeDescription* tryGetBuiltinTypeDescription(Type type) const noexcept;
        [[nodiscard]] const std::unordered_map<i32, BuiltinTypeDescription>& builtinTypeDescriptions() const noexcept { return m_builtinTypeDescriptionsById; }
        void refreshWellKnownTypes() noexcept;
        [[nodiscard]] const WellKnownTypes& wellKnown() const noexcept { return m_wellKnownTypes; }

        [[nodiscard]] const std::vector<TypeDefinition>& typeDefinitions() const noexcept { return m_typeDefinitions; }
        [[nodiscard]] const std::vector<ConstantDefinition>& constantDefinitions() const noexcept { return m_constantDefinitions; }
        [[nodiscard]] const std::vector<EnumDefinition>& enumDefinitions() const noexcept { return m_enumDefinitions; }
        [[nodiscard]] const std::vector<FunctionDefinition>& functionDefinitions() const noexcept { return m_functionDefinitions; }
        [[nodiscard]] const std::vector<Type>& requiredExternFunctions() const noexcept { return m_requiredExternFunctions; }
        [[nodiscard]] size_t preludeFunctionDefinitionCount() const noexcept { return m_preludeFunctionDefinitionCount; }
        [[nodiscard]] bool isPreludeFunctionDefinition(Type functionType) const noexcept;
        [[nodiscard]] bool isPreludeTypeDefinition(Type type) const noexcept;
        [[nodiscard]] const std::vector<Type>& requiredPreludeTypes() const noexcept { return m_requiredPreludeTypes; }
        void markPreludeTypeRequired(Type type) noexcept;
        [[nodiscard]] size_t preludeTypeDefinitionCount() const noexcept { return m_preludeTypeDefinitionCount; }
        void markExternRequired(Type functionType) noexcept;
        [[nodiscard]] Type tryGetExternFunctionByFullName(std::string_view fullName) const noexcept;
        UnitSemantics& createUnitSemantics(u16 fileId, u32 nodeCount) noexcept;
        [[nodiscard]] Type typeOfNode(NodeIndex reference) const noexcept;
        void setNodeType(NodeIndex reference, Type type) noexcept;

    private:
        struct ArrayTypeInfo
        {
            Type elementType;
            i32 length;
            bool isImmutableSlice = false;
        };

        std::vector<TypeDefinition> m_typeDefinitions;
        std::vector<EnumDefinition> m_enumDefinitions;
        std::vector<Type> m_requiredExternFunctions;
        std::vector<Type> m_requiredPreludeTypes;
        size_t m_preludeFunctionDefinitionCount{ 0 };
        PreludeFunctionPool m_preludeFunctionPool;
        size_t m_preludeTypeDefinitionCount{ 0 };
        std::vector<FunctionDefinition> m_functionDefinitions;
        std::vector<ConstantDefinition> m_constantDefinitions;
        std::unordered_map<i32, size_t> m_typeDefinitionIndexById;
        std::unordered_map<i32, size_t> m_enumDefinitionIndexById;
        std::unordered_map<i32, size_t> m_functionDefinitionIndexById;
        std::unordered_map<std::string, Type, TransparentStringHash, std::equal_to<>> m_functionNameToType;
        std::unordered_map<std::string, size_t, TransparentStringHash, std::equal_to<>> m_constantDefinitionIndexByName;
        std::unordered_map<i32, std::string> m_typeNames;
        std::unordered_map<std::string, Type, TransparentStringHash, std::equal_to<>> m_nameToTypes;
        std::unordered_map<i32, ArrayTypeInfo> m_arrayTypeInfoById;
        std::vector<Type> m_arrayTypes;
        std::unordered_map<std::string, Type> m_arrayIntrinsicsByFullName;
        std::unordered_map<i32, BuiltinTypeDescription> m_builtinTypeDescriptionsById;
        std::unordered_map<u16, UnitSemantics> m_unitSemanticsByFileId;
        WellKnownTypes m_wellKnownTypes;
        i32 m_nextId = 0;
    };
}
