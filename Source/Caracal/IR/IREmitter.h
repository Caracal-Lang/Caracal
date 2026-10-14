#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/IR/BasicBlock.h>
#include <Caracal/IR/ConstantValue.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/IfFrame.h>
#include <Caracal/IR/LocalsMap.h>
#include <Caracal/IR/Module.h>
#include <Caracal/IR/ShortCircuitFrame.h>
#include <Caracal/IR/ValueRef.h>
#include <Caracal/IR/WhileFrame.h>
#include <Caracal/Optimization/OperatorFolding.h>
#include <Caracal/Semantic/Parameter.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/UnaryExpression.h>

#include <vector>
#include <Caracal/Semantic/SemanticContext.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>
#include <unordered_set>

namespace Caracal
{
    class CARACAL_API IREmitter
    {
    private:
        struct IncomingLocals
        {
            BlockId predecessorBlockId;
            LocalsMap values;
        };

        struct LoopContext
        {
            BlockId conditionBlockId;
            BlockId continuationBlockId;
            std::vector<IncomingLocals> conditionInputs;
            std::vector<IncomingLocals> continuationInputs;
        };

        struct EnumFieldConstant
        {
            ConstantValue constant;
            std::optional<FoldValue> folded;
        };

    public:
        IREmitter(SemanticContext& semanticContext, Module& module);

        CARACAL_DELETE_COPY_DELETE_MOVE(IREmitter)

        [[nodiscard]] bool emissionFailed() const noexcept { return m_emissionFailed; }
        [[nodiscard]] static ConstantValue constantFromFold(const FoldValue& value) noexcept;
        void recordEmissionFailure() noexcept;
        void beginFunction(const FunctionDefinition& definition);
        void endFunction();
        [[nodiscard]] ValueRef emitConstant(ConstantValue value, Type type);
        void emitReturn();
        void emitReturnValue(ValueRef value);
        void emitParameter(u32 nameId, i32 position, const Parameter& parameter);
        void emitLocalDeclaration(u32 nameId, std::string_view name, Type localType, ValueRef value);
        void emitLocalAssignment(u32 nameId, ValueRef value);
        [[nodiscard]] ValueRef readLocal(u32 nameId);
        [[nodiscard]] ValueRef peekLocal(u32 nameId);
        void setAddressTakenLocals(const std::vector<u32>& sortedNameIds);
        void setPreludeMode(bool isPreludePass) noexcept;
        void setNameLexemes(const std::vector<std::string_view>& lexemes) noexcept;
        void emitRefLocalDeclaration(u32 nameId, Type refType, ValueRef address);
        [[nodiscard]] ValueRef addressOfLocal(u32 nameId);
        [[nodiscard]] ValueRef emitRefValue(u32 nameId, Type valueType);
        [[nodiscard]] ValueRef emitLoad(ValueRef address, Type valueType);

        [[nodiscard]] ValueRef allocateAggregateSlot(u32 nameId, std::string_view name, Type type);
        [[nodiscard]] ValueRef allocateTempAggregateSlot(std::string_view name, Type type);
        [[nodiscard]] ValueRef spillToTempSlot(ValueRef value, Type valueType);
        [[nodiscard]] ValueRef emitFieldAddress(ValueRef objectAddress, Type objectType, std::string_view fieldName, i32 fieldIndex, Type fieldType);
        void emitStoreValue(ValueRef value, ValueRef address, Type type);
        [[nodiscard]] ValueRef emitElementAddress(ValueRef baseAddress, Type arrayType, ValueRef index, Type elementType);

        [[nodiscard]] ValueRef emitElementAccess(ValueRef receiverAddress, Type receiverType, ValueRef index, Type resultType, bool wantsElementAddress);
        [[nodiscard]] ValueRef emitElementAddressForReceiver(ValueRef receiverAddress, Type receiverType, ValueRef index);
        [[nodiscard]] ValueRef emitSliceIntrinsic(ValueRef receiverAddress, Type receiverType, Type sliceType);
        [[nodiscard]] ValueRef emitStringToCString(ValueRef receiverAddress, Type receiverType);
        [[nodiscard]] ValueRef emitMakeSlice(ValueRef baseAddress, ValueRef length, Type sliceType);
        [[nodiscard]] ValueRef emitDynamicArrayStorage(ValueRef destinationAddress, Type arrayType, i32 count);
        void emitArrayAdd(ValueRef receiverAddress, ValueRef value, Type receiverType);
        void emitArrayRemove(ValueRef receiverAddress, ValueRef index, Type receiverType);
        [[nodiscard]] ValueRef emitArrayCopy(ValueRef sourceAddress, Type arrayType, bool reserveNulByte = false);

        void emitStringLiteralIntoAddress(ValueRef destinationAddress, std::string_view escaped);
        void emitDynamicArrayDescriptor(ValueRef destinationAddress, Type arrayType, ValueRef dataPointer, i32 length, i32 capacity);
        [[nodiscard]] ValueRef emitCStringEquality(BinaryOperatorKind operation, ValueRef left, ValueRef right);
        [[nodiscard]] ValueRef emitDefensiveRefCopy(ValueRef address, Type valueType);

        [[nodiscard]] ValueRef emitWidened(ValueRef value, Type sourceType, Type targetType);
        [[nodiscard]] ValueRef emitIntToFloat(ValueRef value, Type sourceType, Type targetType);
        [[nodiscard]] ValueRef emitBinary(BinaryOperatorKind binaryOperator, ValueRef left, ValueRef right, Type resultType, Type operandType);
        [[nodiscard]] ValueRef emitUnary(UnaryOperatorKind unaryOperator, ValueRef operand, Type resultType);
        [[nodiscard]] ValueRef emitBitwiseIntrinsic(IntrinsicKind intrinsicKind, const std::vector<ValueRef>& arguments, Type resultType);
        [[nodiscard]] ValueRef emitCall(FunctionId functionId, std::vector<ValueRef> arguments, Type resultType);

        [[nodiscard]] ShortCircuitFrame beginShortCircuit(bool isLogicalAnd, ValueRef leftValue);
        [[nodiscard]] ValueRef endShortCircuit(const ShortCircuitFrame& frame, ValueRef rightValue);
        void discardShortCircuit(const ShortCircuitFrame& frame);

        [[nodiscard]] IfFrame beginIf(ValueRef condition, bool hasElse);
        void endIfThen(IfFrame& frame);
        void endIf(IfFrame& frame);

        [[nodiscard]] WhileFrame beginWhile();
        void beginWhileBody(WhileFrame& frame, ValueRef condition);
        void endWhile(WhileFrame& frame);

        void emitBreak();
        void emitSkip();

        void emitGlobalConstant(std::string_view name, Type type, ConstantValue value);
        void emitGlobalReference(std::string_view name, Type type, std::string_view targetName);
        void emitConstructedGlobal(std::string_view name, Type type);
        [[nodiscard]] ValueRef emitGlobalAddress(const std::string& name);
        [[nodiscard]] ValueRef readGlobal(const std::string& name);
        void emitGlobalAssignment(const std::string& name, ValueRef value);
        [[nodiscard]] ValueRef beginGlobalInitConstruction(std::string_view name, Type type);
        void resumeGlobalInitEffects();
        void suspendGlobalInit();

        void recordExternDefinition(Type functionType);
        void recordCalledPreludeFunction(Type functionType);

        void recordDefaultArgumentFold(u16 fileId, u32 defaultNode, FoldValue value);
        [[nodiscard]] ValueRef emitDefaultArgument(u16 fileId, u32 defaultNode, Type defaultType, Type parameterType);
        [[nodiscard]] ValueRef localOrGlobalAddress(u32 nameId, std::string_view name);

        void recordEnumFieldConstant(i32 enumTypeId, std::string fieldName, ConstantValue constant, std::optional<FoldValue> folded);
        [[nodiscard]] const EnumFieldConstant* tryGetEnumFieldConstant(i32 enumTypeId, const std::string& fieldName) const;
        [[nodiscard]] std::optional<ConstantValue> makeEnumConstantValue(Type baseType, i32 value) const;
        [[nodiscard]] ValueRef emitEnumMemberConstant(Type enumType, const std::string& enumName, const std::string& fieldName, const ConstantValue& underlying);

        void finishModule();

    private:
        [[nodiscard]] bool canEmit() const noexcept;
        void computeEntryPointFlags();
        void registerExternDefinition(const FunctionDefinition& definition);
        [[nodiscard]] bool canEmitWith(std::initializer_list<ValueRef> operands);
        [[nodiscard]] ValueRef elementBaseAddress(ValueRef receiverAddress, Type receiverType, Type elementType);
        [[nodiscard]] ValueRef allocateLocalSlot(std::string localName, Type valueType, ValueRef initialValue);
        [[nodiscard]] std::vector<u32> sortedDefinedLocalIds(const LocalsMap& locals) const;
        [[nodiscard]] bool installPreludeFunction(Type functionType);
        void registerBuiltinTypes();
        void registerRequiredExterns();
        void installCompletedFunctions();
        void installRequiredPreludeFunctions();
        void installGlobalInit();
        void registerPendingExterns();
        void addRequiredPreludeTypes();
        void addTypeDeclarations();
        void registerArrayTypes();
        void addEnumDeclarations();
        [[nodiscard]] std::string_view nameLexeme(u32 nameId) const noexcept;
        void mergeLocals(BasicBlock& block, const std::vector<IncomingLocals>& incomingValues);

        SemanticContext& m_semanticContext;
        bool m_emissionFailed = false;
        Module& m_module;
        LocalsMap m_locals;
        const std::vector<std::string_view>* m_nameLexemes = nullptr;
        // name ids of the current function's locals
        std::vector<u32> m_addressTakenLocals;
        std::vector<LoopContext> m_loopContexts;
        std::map<std::pair<i32, std::string>, EnumFieldConstant> m_enumFieldConstants;
        std::unordered_map<std::string, Type> m_globalTypes;
        std::vector<Type> m_pendingExternDefinitions;
        std::vector<Type> m_requiredPreludeFunctions;
        std::unordered_set<i32> m_queuedPreludeFunctionIds;
        bool m_isPreludePass = false;
        std::vector<Type> m_currentCalledPreludeFunctions;
        std::unordered_set<i32> m_currentCalledPreludeFunctionIds;
        std::unordered_map<u64, FoldValue> m_parameterDefaultFolds;

        struct GlobalInitState
        {
            std::optional<Function> function{};
            i32 nextTemporaryId = 0;
            i32 nextLocalSlotId = 0;
            i32 nextBlockId = 0;
        };
        GlobalInitState m_globalInit;
        TemporaryId m_nextTemporaryId = 0;
        LocalSlotId m_nextLocalSlotId = 0;
        BlockId m_nextBlockId = 0;
        std::optional<Function> m_pendingFunction{};
        std::vector<Function> m_completedFunctions;
        Function* m_currentFunction = nullptr;
        BasicBlock* m_currentBlock = nullptr;
        Type m_currentReturnType = Type::Void();
        bool m_entryPointFlagsComputed = false;
        bool m_emitEntryPoint = false;
    };
}
