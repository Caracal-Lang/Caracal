#pragma once

#include <Caracal/Semantic/BuiltinTypeDescription.h>
#include <Caracal/IR/ConstructedGlobalDeclaration.h>
#include <Caracal/IR/EnumDeclaration.h>
#include <Caracal/IR/ExternFunction.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/GlobalConstantDeclaration.h>
#include <Caracal/IR/GlobalReferenceDeclaration.h>
#include <Caracal/IR/TypeDeclaration.h>

#include <optional>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Caracal
{
    struct ArrayTypeDescription
    {
        Type elementType;
        i32 length;
    };

    class Module
    {
    public:
        [[nodiscard]] const std::vector<EnumDeclaration>& enums() const noexcept { return m_enums; }
        [[nodiscard]] const std::vector<ExternFunction>& externFunctions() const noexcept { return m_externFunctions; }
        [[nodiscard]] const std::vector<Function>& functions() const noexcept { return m_functions; }
        [[nodiscard]] std::vector<Function>& functions() noexcept { return m_functions; }
        [[nodiscard]] const std::vector<GlobalConstantDeclaration>& globalConstants() const noexcept { return m_globalConstants; }
        [[nodiscard]] const std::vector<GlobalReferenceDeclaration>& globalReferences() const noexcept { return m_globalReferences; }
        [[nodiscard]] const std::vector<ConstructedGlobalDeclaration>& constructedGlobals() const noexcept { return m_constructedGlobals; }
        [[nodiscard]] const Function* tryGetGlobalInit() const noexcept;
        [[nodiscard]] Function* tryGetGlobalInitMutable() noexcept;
        [[nodiscard]] const std::vector<TypeDeclaration>& types() const noexcept { return m_types; }
        [[nodiscard]] const EnumDeclaration* tryGetEnum(Type type) const noexcept;
        [[nodiscard]] const ExternFunction* tryGetExternFunction(FunctionId id) const noexcept;
        [[nodiscard]] const Function* tryGetFunction(FunctionId id) const noexcept;
        [[nodiscard]] const std::string* tryGetFunctionName(FunctionId id) const noexcept;
        [[nodiscard]] const std::string* tryGetTypeName(Type type) const noexcept;

        void addEnum(EnumDeclaration enumDeclaration);
        void addExternFunction(ExternFunction function);
        Function* addFunction(Function function);
        void reserveFunctions(size_t count);
        void addGlobalConstant(GlobalConstantDeclaration globalDeclaration);
        void addGlobalReference(GlobalReferenceDeclaration globalDeclaration);
        void addConstructedGlobal(ConstructedGlobalDeclaration globalDeclaration);
        void setGlobalInit(Function function);
        void addType(TypeDeclaration typeDeclaration);
        void registerTypeName(Type type, std::string name);
        void registerArrayType(Type type, Type elementType, i32 length);
        [[nodiscard]] const ArrayTypeDescription* tryGetArrayType(Type type) const noexcept;
        void setWellKnownTypes(const WellKnownTypes& wellKnownTypes) noexcept { m_wellKnownTypes = wellKnownTypes; }
        [[nodiscard]] const WellKnownTypes& wellKnown() const noexcept { return m_wellKnownTypes; }
        void registerBuiltinTypeDescription(Type type, const BuiltinTypeDescription& description);
        [[nodiscard]] const BuiltinTypeDescription* tryGetBuiltinTypeDescription(Type type) const noexcept;

    private:
        std::vector<EnumDeclaration> m_enums;
        std::vector<ExternFunction> m_externFunctions;
        std::vector<Function> m_functions;
        std::vector<GlobalConstantDeclaration> m_globalConstants;
        std::vector<GlobalReferenceDeclaration> m_globalReferences;
        std::vector<ConstructedGlobalDeclaration> m_constructedGlobals;
        std::optional<Function> m_globalInit;
        std::vector<TypeDeclaration> m_types;
        std::unordered_map<EnumId, size_t> m_enumIndices;
        std::unordered_map<FunctionId, size_t> m_externFunctionIndices;
        std::unordered_map<FunctionId, size_t> m_functionIndices;
        std::unordered_map<i32, size_t> m_typeIndices;
        std::unordered_map<i32, std::string> m_registeredTypeNames;
        std::unordered_map<i32, ArrayTypeDescription> m_arrayTypes;
        WellKnownTypes m_wellKnownTypes;
        std::unordered_map<i32, BuiltinTypeDescription> m_builtinTypeDescriptionsById;
    };
}
