#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/IR/BlockLabel.h>
#include <Caracal/IR/ConstantValue.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/FunctionBuilder.h>
#include <Caracal/IR/Instruction.h>
#include <Caracal/IR/IfFrame.h>
#include <Caracal/IR/LocalsMap.h>
#include <Caracal/IR/Module.h>
#include <Caracal/IR/PhiInput.h>
#include <Caracal/IR/ShortCircuitFrame.h>
#include <Caracal/IR/ValueRef.h>
#include <Caracal/IR/WhileFrame.h>
#include <Caracal/Optimization/OperatorFolding.h>
#include <Caracal/Semantic/FunctionDefinition.h>
#include <Caracal/Semantic/Parameter.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Semantic/Type.h>
#include <Caracal/Syntax/BinaryOperatorKind.h>
#include <Caracal/Syntax/UnaryOperatorKind.h>
#include <Caracal/Text/TransparentStringHash.h>

#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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

    public:
        struct EnumFieldConstant
        {
            ConstantValue constant;
            std::optional<FoldValue> folded;
        };

        IREmitter(SemanticContext& semanticContext, Module& module);

        CARACAL_DELETE_COPY_DELETE_MOVE(IREmitter)

        [[nodiscard]] static ConstantValue constantFromFold(const FoldValue& value) noexcept;
        [[nodiscard]] bool emissionFailed() const noexcept { return m_emissionFailed; }
        void recordEmissionFailure() noexcept;
        void beginFunction(const FunctionDefinition& definition, SourceLine sourceLine);
        void endFunction();
        [[nodiscard]] SourceLine& currentSourceLine() noexcept { return m_currentSourceLine; }
        void recordSourceLines() noexcept { m_functionBuilder.recordSourceLinesFrom(&m_currentSourceLine); }
        void storeLocalsInSlots() noexcept { m_storesLocalsInSlots = true; }
        [[nodiscard]] ValueRef emitConstant(ConstantValue value, Type type);
        [[nodiscard]] ValueRef emitFoldedConstant(const FoldValue& value, Type type);
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
        void emitRefLocalDeclaration(u32 nameId, std::string_view name, Type refType, ValueRef address);
        [[nodiscard]] ValueRef addressOfLocal(u32 nameId);
        [[nodiscard]] ValueRef emitRefValue(u32 nameId, Type valueType);
        [[nodiscard]] ValueRef emitLoad(ValueRef address, Type valueType);

        [[nodiscard]] ValueRef allocateAggregateSlot(u32 nameId, std::string_view name, Type type);
        [[nodiscard]] ValueRef allocateTempAggregateSlot(std::string_view name, Type type);
        [[nodiscard]] ValueRef spillToTempSlot(ValueRef value, Type valueType);
        [[nodiscard]] ValueRef emitFieldAddress(ValueRef objectAddress, Type objectType, i32 fieldIndex, Type fieldType);
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
        [[nodiscard]] ValueRef emitBitwiseIntrinsic(IntrinsicKind intrinsicKind, std::span<const ValueRef> arguments, Type resultType);
        [[nodiscard]] ValueRef emitCall(FunctionId functionId, std::span<const ValueRef> arguments, Type resultType);

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
        [[nodiscard]] ValueRef emitGlobalAddress(std::string_view name);
        [[nodiscard]] ValueRef readGlobal(std::string_view name);
        void emitGlobalAssignment(std::string_view name, ValueRef value);
        void emitGlobalReferenceInitialization(std::string_view name, ValueRef targetAddress);
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
        static constexpr u32 NoBlockIndex = std::numeric_limits<u32>::max();
        static constexpr u32 NoModuleNameId = std::numeric_limits<u32>::max();

        [[nodiscard]] bool canEmit() const noexcept;
        void computeEntryPointFlags();
        void registerExternDefinition(const FunctionDefinition& definition);
        [[nodiscard]] bool canEmitWith(std::initializer_list<ValueRef> operands);
        [[nodiscard]] ValueRef elementBaseAddress(ValueRef receiverAddress, Type receiverType, Type elementType);
        [[nodiscard]] ValueRef allocateLocalSlot(u32 moduleNameId, Type valueType, ValueRef initialValue, i32 variable);
        [[nodiscard]] u32 moduleNameIdOf(u32 checkerNameId, std::string_view name);
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
        void mergeLocals(u32 blockIndex, const std::vector<IncomingLocals>& incomingValues);
        [[nodiscard]] ValueRef emitGlobalStorageAddress(std::string_view name, Type type);

        [[nodiscard]] TemporaryId nextTemporaryId() noexcept;
        ValueRef appendValueInstruction(const Instruction& instruction);
        void appendInstruction(const Instruction& instruction);
        [[nodiscard]] ValueRef appendBinaryInstruction(InstructionKind kind, ValueRef left, ValueRef right, Type type);
        [[nodiscard]] ValueRef appendComparisonInstruction(InstructionKind kind, ValueRef left, ValueRef right, Type type, Type operandType);
        [[nodiscard]] ValueRef appendUnaryInstruction(InstructionKind kind, ValueRef operand, Type type);
        void appendStoreInstruction(ValueRef value, ValueRef address, Type type);
        [[nodiscard]] ValueRef appendCallInstruction(FunctionId functionId, std::span<const ValueRef> arguments, Type resultType);
        u32 appendPhiInstruction(u32 blockIndex, TemporaryId resultId, Type type, std::span<const PhiInput> inputs);
        [[nodiscard]] u32 addBlock(BlockId id, BlockLabel label);
        [[nodiscard]] BlockId currentBlockId() const noexcept;
        [[nodiscard]] bool currentBlockFallsThrough() const noexcept;
        void setJump(u32 blockIndex, BlockId targetBlockId);
        void setBranch(u32 blockIndex, ValueRef condition, BlockId trueBlockId, BlockId falseBlockId);

        SemanticContext& m_semanticContext;
        bool m_emissionFailed = false;
        Module& m_module;
        LocalsMap m_locals;
        const std::vector<std::string_view>* m_nameLexemes = nullptr;
        std::vector<u32> m_moduleNameIdByCheckerNameId;
        std::vector<u32> m_addressTakenLocals;
        bool m_storesLocalsInSlots = false;
        std::vector<LoopContext> m_loopContexts;
        std::map<std::pair<i32, std::string>, EnumFieldConstant> m_enumFieldConstants;
        std::unordered_map<std::string, Type, TransparentStringHash, std::equal_to<>> m_globalTypes;
        std::vector<Type> m_pendingExternDefinitions;
        std::vector<Type> m_requiredPreludeFunctions;
        std::unordered_set<i32> m_queuedPreludeFunctionIds;
        bool m_isPreludePass = false;
        std::vector<Type> m_currentCalledPreludeFunctions;
        std::unordered_set<i32> m_currentCalledPreludeFunctionIds;
        std::unordered_map<u64, FoldValue> m_parameterDefaultFolds;
        std::vector<TemporaryId> m_operandBuffer;

        struct GlobalInitState
        {
            std::optional<Function> function{};
            FunctionBuilder builder;
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
        SourceLine m_currentSourceLine;
        FunctionBuilder m_functionBuilder;
        FunctionBuilder* m_currentBuilder = nullptr;
        u32 m_currentBlockIndex = NoBlockIndex;
        Type m_currentReturnType = Type::Void();
        bool m_entryPointFlagsComputed = false;
        bool m_emitEntryPoint = false;
    };
}
