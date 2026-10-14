#pragma once

#include <Caracal/API.h>
#include <Caracal/IR/BasicBlock.h>
#include <Caracal/IR/ConstantValue.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/Module.h>
#include <Caracal/IR/ValueRef.h>
#include <Caracal/IR/PhiInstruction.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/BlockNode.h>
#include <Caracal/Syntax/Expression.h>
#include <Caracal/Syntax/GroupingExpression.h>
#include <Caracal/Syntax/IfStatement.h>
#include <Caracal/Syntax/ParseTree.h>
#include <Caracal/Syntax/ReturnStatement.h>
#include <Caracal/Syntax/Statement.h>
#include <Caracal/Syntax/WhileStatement.h>
#include <Caracal/Syntax/ExpressionStatement.h>
#include <Caracal/Syntax/FunctionCallExpression.h>
#include <Caracal/Syntax/TypeDefinitionStatement.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Caracal
{
    class ArrayLiteral;
    class CARACAL_API IRLowerer
    {
    private:
        enum class LocalStorageKind
        {
            Value,
            Address,
        };

        struct LocalState final
        {
            ValueRef value;
            Type type;
            LocalStorageKind storageKind{ LocalStorageKind::Value };
            bool referencesConstant{ false };
        };
        using LocalStateMap = std::unordered_map<std::string, LocalState>;

        struct IncomingLocalValues final
        {
            BlockId predecessorBlockId;
            LocalStateMap values;
        };

        struct LoopContext final
        {
            BlockId conditionBlockId;
            BlockId continuationBlockId;
            std::vector<IncomingLocalValues> conditionInputs;
            std::vector<IncomingLocalValues> continuationInputs;
        };

        struct LoopHeaderPhi final
        {
            std::string name;
            PhiInstruction* instruction;
        };

    public:
        explicit IRLowerer(SemanticContext& semanticContext);

        CARACAL_DELETE_COPY_DELETE_MOVE(IRLowerer)

        [[nodiscard]] bool lower(Module& module) noexcept;

    private:
        [[nodiscard]] bool lowerStatement(const Statement* statement, Function& function, std::optional<BlockId>& currentBlockId) noexcept;
        [[nodiscard]] bool lowerEnumDefinition(const EnumDefinition& definition, Module& module) noexcept;
        [[nodiscard]] bool lowerTypeDefinition(const TypeDefinition& definition, Module& module) noexcept;
        [[nodiscard]] bool lowerGlobalConstant(const ConstantDefinition& definition, Module& module) noexcept;
        [[nodiscard]] bool lowerGlobalReference(const ConstantDefinition& definition, Module& module) noexcept;
        [[nodiscard]] bool registerConstructedGlobal(const ConstantDefinition& definition, Module& module) noexcept;
        [[nodiscard]] bool lowerGlobalInitializer(const std::vector<const ConstantDefinition*>& definitions, const std::vector<const Expression*>& discardEffects, Module& module) noexcept;
        [[nodiscard]] bool lowerFunctionDefinition(const FunctionDefinition& definition, const BlockNode* bodyNode, bool isExtern, Module& module) noexcept;
        [[nodiscard]] bool lowerSynthesizedConstructorDefinition(const FunctionDefinition& definition, Module& module) noexcept;
        [[nodiscard]] bool ensureExitTerminator(Function& function, std::optional<BlockId> currentBlockId, Type returnType) noexcept;
        void collectAddressTakenLocals(const Statement* statement) noexcept;
        void collectAddressTakenLocals(const Expression* expression) noexcept;
        void collectAddressTakenLocals(const std::vector<std::unique_ptr<Statement>>& statements) noexcept;
        [[nodiscard]] bool lowerBlock(const BlockNode* block, Function& function, std::optional<BlockId>& currentBlockId) noexcept;
        [[nodiscard]] bool lowerIfStatement(const IfStatement* statement, Function& function, std::optional<BlockId>& currentBlockId) noexcept;
        [[nodiscard]] bool lowerWhileStatement(const WhileStatement* statement, Function& function, std::optional<BlockId>& currentBlockId) noexcept;
        [[nodiscard]] bool lowerBreakStatement(BasicBlock& block, std::optional<BlockId>& currentBlockId) noexcept;
        [[nodiscard]] bool lowerSkipStatement(BasicBlock& block, std::optional<BlockId>& currentBlockId) noexcept;
        [[nodiscard]] bool lowerExpressionStatement(const ExpressionStatement* statement) noexcept;
        [[nodiscard]] bool lowerParameters(const FunctionDefinition& definition) noexcept;
        [[nodiscard]] bool lowerLocalDeclaration(const Expression* leftExpression, const Expression* rightExpression) noexcept;
        [[nodiscard]] bool lowerAssignmentStatement(const Expression* leftExpression, const Expression* rightExpression) noexcept;
        [[nodiscard]] bool lowerReturnStatement(const ReturnStatement* statement, std::optional<BlockId>& currentBlockId) noexcept;
        [[nodiscard]] std::optional<ConstantValue> tryLowerConstantExpression(const Expression* expression) noexcept;
        [[nodiscard]] std::optional<ConstantValue> tryLowerEnumFieldValue(Type enumType, const std::string& fieldName) noexcept;
        [[nodiscard]] std::optional<ConstantValue> tryLowerEnumMemberConstant(const BinaryExpression* expression) noexcept;
        [[nodiscard]] std::optional<ValueRef> allocateLocalSlot(std::string localName, Type valueType, std::optional<ValueRef> initialValue = std::nullopt) noexcept;
        [[nodiscard]] ValueRef emitFieldAddress(ValueRef objectAddress, Type objectType, const std::string& fieldName, i32 fieldIndex, Type fieldType) noexcept;
        [[nodiscard]] ValueRef emitLoad(ValueRef address, Type valueType) noexcept;
        [[nodiscard]] ValueRef emitGlobalAddress(const std::string& name, Type valueType) noexcept;
        [[nodiscard]] std::optional<ValueRef> tryGetGlobalAddress(const std::string& name) noexcept;
        void setLocalValue(std::string localName, ValueRef value, Type type) noexcept;
        void setAddressBackedLocal(std::string localName, ValueRef address, Type valueType) noexcept;
        [[nodiscard]] std::vector<std::string> sortedDefinedLocalNames(const LocalStateMap& localValues) const noexcept;
        [[nodiscard]] std::optional<ValueRef> tryGetAddressBackedLocal(const std::string& localName) const noexcept;
        [[nodiscard]] bool isLocalDefinedOnAllEdges(const std::vector<IncomingLocalValues>& incomingValues, const std::string& name) const noexcept;
        [[nodiscard]] bool localNeedsPhi(const std::vector<IncomingLocalValues>& incomingValues, const std::string& name, const LocalState& firstState) const noexcept;
        [[nodiscard]] bool tryLowerConstructorCallIntoAddress(const Expression* expression, ValueRef destinationAddress) noexcept;
        [[nodiscard]] bool tryLowerArrayLiteralIntoAddress(const Expression* expression, ValueRef destinationAddress, Type arrayType) noexcept;
        [[nodiscard]] bool lowerDynamicArrayLiteralIntoAddress(const ArrayLiteral* literal, ValueRef destinationAddress, Type arrayType) noexcept;
        [[nodiscard]] bool tryLowerStringLiteralIntoAddress(const Expression* expression, ValueRef destinationAddress) noexcept;
        void emitDynamicArrayDescriptor(ValueRef destinationAddress, Type arrayType, ValueRef dataPointer, i32 length, i32 capacity) noexcept;
        void markPreludeFunction(Type functionType) noexcept;
        [[nodiscard]] bool addPreludeFunctionFromPool(Type functionType, Module& module) noexcept;
        [[nodiscard]] std::optional<ValueRef> allocateSlotFromExpression(std::string localName, const Expression* expression, Type valueType) noexcept;
        [[nodiscard]] std::optional<ValueRef> spillValueToTempSlot(const Expression* expression, ValueRef value) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerMethodReceiverAddress(const Expression* receiverExpression) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerCallWithReceiver(const FunctionCallExpression* expression, const Expression* receiverExpression = nullptr) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerArrayIntrinsicCall(const FunctionCallExpression* expression, const Expression* receiverExpression, const FunctionDefinition& functionDefinition) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerBitwiseIntrinsicCall(const FunctionCallExpression* expression, const FunctionDefinition& functionDefinition) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerIntrinsicReceiverAddress(const Expression* receiverExpression) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerElementAddressForCall(const Expression* receiverExpression, const FunctionCallExpression* call) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerMemberFieldAddress(const Expression* receiverExpression, const NameExpression* fieldNameExpression) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerAddressExpression(const Expression* expression) noexcept;
        [[nodiscard]] bool referenceArgumentAliasesConstant(const Expression* argument) const noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerValueExpression(const Expression* expression) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerValueExpressionExpecting(const Expression* expression, Type targetType, bool copyOwningDynamic = true) noexcept;
        [[nodiscard]] std::optional<ValueRef> lowerShortCircuitExpression(const BinaryExpression* expression) noexcept;
        [[nodiscard]] bool lowerExpressionForEffect(const Expression* expression) noexcept;
        [[nodiscard]] std::optional<ValueRef> emitCall(const FunctionCallExpression* expression, std::optional<ValueRef> implicitArgument = std::nullopt) noexcept;
        void registerRequiredExterns(Module& module) noexcept;
        void registerExternDefinition(const FunctionDefinition& definition, Module& module) noexcept;
        [[nodiscard]] FunctionId resolveExternFunctionId(const std::string& fullName) noexcept;
        [[nodiscard]] bool lowerFunctionDefinitionByKind(const FunctionDefinition& definition, Module& module) noexcept;
        
        void resetState();
        void registerBuiltinTypes(Module& module) noexcept;
        void restoreLocalValues(const LocalStateMap& values) noexcept;
        void mergeLocalValues(BasicBlock& block, const std::vector<IncomingLocalValues>& incomingValues) noexcept;

        SemanticContext& m_semanticContext;
        bool m_reserveNulOnArrayCopy = false;
        std::unordered_map<std::string, FunctionId> m_externFunctionIdCache;
        std::vector<Type> m_requiredPreludeFunctions;
        std::unordered_set<i32> m_queuedPreludeFunctionIds;
        LocalStateMap m_locals;
        std::unordered_set<std::string> m_addressTakenLocals;
        std::unordered_map<std::string, Type> m_globalTypes;
        std::vector<LoopContext> m_loopContexts;
        TemporaryId m_nextTemporaryId{ 0 };
        LocalSlotId m_nextLocalSlotId{ 0 };
        BlockId m_nextBlockId{ 0 };
        bool m_emitEntryPoint{ false };
        Function* m_currentFunction{ nullptr };
        BasicBlock* m_currentBlock{ nullptr };
        Type m_currentReturnType{ Type::Void() };
    };
}
