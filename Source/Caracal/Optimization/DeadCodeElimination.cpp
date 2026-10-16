#include <Caracal/Optimization/DeadCodeElimination.h>
#include <Caracal/Optimization/DefinitionUseTable.h>
#include <Caracal/Profiling.h>

#include <Caracal/IR/AddInstruction.h>
#include <Caracal/IR/AddressOfFieldInstruction.h>
#include <Caracal/IR/AddressOfGlobalInstruction.h>
#include <Caracal/IR/AddressOfInstruction.h>
#include <Caracal/IR/BasicBlock.h>
#include <Caracal/IR/BranchIfTerminator.h>
#include <Caracal/IR/CallInstruction.h>
#include <Caracal/IR/CallVoidInstruction.h>
#include <Caracal/IR/ConstantInstruction.h>
#include <Caracal/IR/DivideInstruction.h>
#include <Caracal/IR/ElementAddressInstruction.h>
#include <Caracal/IR/EqualInstruction.h>
#include <Caracal/IR/Function.h>
#include <Caracal/IR/GreaterOrEqualInstruction.h>
#include <Caracal/IR/GreaterThanInstruction.h>
#include <Caracal/IR/IntToFloatInstruction.h>
#include <Caracal/IR/IntWidenInstruction.h>
#include <Caracal/IR/ArrayAddInstruction.h>
#include <Caracal/IR/ArrayCopyInstruction.h>
#include <Caracal/IR/ArrayRemoveInstruction.h>
#include <Caracal/IR/SizeOfInstruction.h>
#include <Caracal/IR/LessOrEqualInstruction.h>
#include <Caracal/IR/LessThanInstruction.h>
#include <Caracal/IR/LoadValueInstruction.h>
#include <Caracal/IR/BitAndInstruction.h>
#include <Caracal/IR/BitNotInstruction.h>
#include <Caracal/IR/BitOrInstruction.h>
#include <Caracal/IR/BitXorInstruction.h>
#include <Caracal/IR/LogicalNegationInstruction.h>
#include <Caracal/IR/ShiftLeftInstruction.h>
#include <Caracal/IR/ShiftRightInstruction.h>
#include <Caracal/IR/MakeSliceInstruction.h>
#include <Caracal/IR/MultiplyInstruction.h>
#include <Caracal/IR/NotEqualInstruction.h>
#include <Caracal/IR/ParameterInstruction.h>
#include <Caracal/IR/PhiInstruction.h>
#include <Caracal/IR/ReturnValueTerminator.h>
#include <Caracal/IR/StoreValueInstruction.h>
#include <Caracal/IR/SubtractInstruction.h>
#include <Caracal/IR/ValueNegationInstruction.h>

#include <vector>

namespace Caracal
{
    enum class InstructionResult : u8
    {
        None,
        Value,
        RemovableValue,
    };

    template<typename TInstruction, typename TUseOperand>
    [[nodiscard]] static InstructionResult VisitUnary(const Instruction& instruction, TUseOperand& useOperand)
    {
        const auto& unary = static_cast<const TInstruction&>(instruction);
        useOperand(unary.operandValue());
        return InstructionResult::RemovableValue;
    }

    template<typename TInstruction, typename TUseOperand>
    [[nodiscard]] static InstructionResult VisitBinary(const Instruction& instruction, TUseOperand& useOperand)
    {
        const auto& binary = static_cast<const TInstruction&>(instruction);
        useOperand(binary.leftValue());
        useOperand(binary.rightValue());
        return InstructionResult::RemovableValue;
    }

    template<typename TUseOperand>
    static InstructionResult VisitInstruction(const Instruction& instruction, TUseOperand&& useOperand)
    {
        switch (instruction.kind())
        {
            case InstructionKind::Parameter:
            {
                return InstructionResult::Value;
            }
            case InstructionKind::Constant:
            {
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::AddressOf:
            {
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::AddressOfGlobal:
            {
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::FieldAddress:
            {
                const auto& fieldAddress = static_cast<const AddressOfFieldInstruction&>(instruction);
                useOperand(fieldAddress.objectAddress());
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::ElementAddress:
            {
                const auto& elementAddress = static_cast<const ElementAddressInstruction&>(instruction);
                useOperand(elementAddress.baseAddress());
                useOperand(elementAddress.index());
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::MakeSlice:
            {
                const auto& makeSlice = static_cast<const MakeSliceInstruction&>(instruction);
                useOperand(makeSlice.baseAddress());
                useOperand(makeSlice.length());
                return InstructionResult::Value;
            }
            case InstructionKind::LoadValue:
            {
                const auto& load = static_cast<const LoadValueInstruction&>(instruction);
                useOperand(load.address());
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::StoreValue:
            {
                const auto& store = static_cast<const StoreValueInstruction&>(instruction);
                useOperand(store.value());
                useOperand(store.address());
                return InstructionResult::None;
            }
            case InstructionKind::ValueNegation:
            {
                return VisitUnary<ValueNegationInstruction>(instruction, useOperand);
            }
            case InstructionKind::IntToFloat:
            {
                return VisitUnary<IntToFloatInstruction>(instruction, useOperand);
            }
            case InstructionKind::IntWiden:
            {
                return VisitUnary<IntWidenInstruction>(instruction, useOperand);
            }
            case InstructionKind::SizeOf:
            {
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::ArrayAdd:
            {
                const auto& arrayAdd = static_cast<const ArrayAddInstruction&>(instruction);
                useOperand(arrayAdd.descriptorAddress());
                useOperand(arrayAdd.value());
                return InstructionResult::None;
            }
            case InstructionKind::ArrayRemove:
            {
                const auto& arrayRemove = static_cast<const ArrayRemoveInstruction&>(instruction);
                useOperand(arrayRemove.descriptorAddress());
                useOperand(arrayRemove.index());
                return InstructionResult::None;
            }
            case InstructionKind::ArrayCopy:
            {
                const auto& arrayCopy = static_cast<const ArrayCopyInstruction&>(instruction);
                useOperand(arrayCopy.sourceAddress());
                return InstructionResult::Value;
            }
            case InstructionKind::LogicalNegation:
            {
                return VisitUnary<LogicalNegationInstruction>(instruction, useOperand);
            }
            case InstructionKind::Call:
            {
                const auto& call = static_cast<const CallInstruction&>(instruction);
                for (const auto& argument : call.arguments())
                {
                    useOperand(argument);
                }
                return InstructionResult::Value;
            }
            case InstructionKind::CallVoid:
            {
                for (const auto& argument : static_cast<const CallVoidInstruction&>(instruction).arguments())
                {
                    useOperand(argument);
                }
                return InstructionResult::None;
            }
            case InstructionKind::Add:
            {
                return VisitBinary<AddInstruction>(instruction, useOperand);
            }
            case InstructionKind::Subtract:
            {
                return VisitBinary<SubtractInstruction>(instruction, useOperand);
            }
            case InstructionKind::Multiply:
            {
                return VisitBinary<MultiplyInstruction>(instruction, useOperand);
            }
            case InstructionKind::Divide:
            {
                return VisitBinary<DivideInstruction>(instruction, useOperand);
            }
            case InstructionKind::Equal:
            {
                return VisitBinary<EqualInstruction>(instruction, useOperand);
            }
            case InstructionKind::NotEqual:
            {
                return VisitBinary<NotEqualInstruction>(instruction, useOperand);
            }
            case InstructionKind::LessThan:
            {
                return VisitBinary<LessThanInstruction>(instruction, useOperand);
            }
            case InstructionKind::LessOrEqual:
            {
                return VisitBinary<LessOrEqualInstruction>(instruction, useOperand);
            }
            case InstructionKind::GreaterThan:
            {
                return VisitBinary<GreaterThanInstruction>(instruction, useOperand);
            }
            case InstructionKind::GreaterOrEqual:
            {
                return VisitBinary<GreaterOrEqualInstruction>(instruction, useOperand);
            }
            case InstructionKind::BitAnd:
            {
                return VisitBinary<BitAndInstruction>(instruction, useOperand);
            }
            case InstructionKind::BitOr:
            {
                return VisitBinary<BitOrInstruction>(instruction, useOperand);
            }
            case InstructionKind::BitXor:
            {
                return VisitBinary<BitXorInstruction>(instruction, useOperand);
            }
            case InstructionKind::BitNot:
            {
                return VisitUnary<BitNotInstruction>(instruction, useOperand);
            }
            case InstructionKind::ShiftLeft:
            {
                const auto& shiftLeft = static_cast<const ShiftLeftInstruction&>(instruction);
                useOperand(shiftLeft.value());
                useOperand(shiftLeft.amount());
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::ShiftRight:
            {
                const auto& shiftRight = static_cast<const ShiftRightInstruction&>(instruction);
                useOperand(shiftRight.value());
                useOperand(shiftRight.amount());
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::Phi:
            {
                const auto& phi = static_cast<const PhiInstruction&>(instruction);
                for (const auto& input : phi.inputs())
                {
                    useOperand(input.value());
                }
                return InstructionResult::RemovableValue;
            }
            default:
            {
                return InstructionResult::None;
            }
        }
    }

    template<typename TUseOperand>
    static void VisitTerminator(const Terminator* terminator, TUseOperand&& useOperand)
    {
        if (terminator == nullptr)
        {
            return;
        }

        switch (terminator->kind())
        {
            case TerminatorKind::Branch:
            {
                useOperand(static_cast<const BranchIfTerminator*>(terminator)->condition());
                break;
            }
            case TerminatorKind::ReturnValue:
            {
                useOperand(static_cast<const ReturnValueTerminator*>(terminator)->value());
                break;
            }
            default:
            {
                break;
            }
        }
    }

    static void ResetDefinitionUseTable(DefinitionUseTable& table, size_t temporaryCount)
    {
        table.useCounts.assign(temporaryCount, 0);
        table.definitions.assign(temporaryCount, nullptr);
        table.definitionBlockIndexes.assign(temporaryCount, 0);
    }

    static void CountUse(DefinitionUseTable& table, ValueRef value)
    {
        // an empty ValueRef has the id -1
        if (value.id() < 0)
        {
            return;
        }

        table.useCounts[value.id()]++;
    }

    static void BuildDefinitionUseTable(const Function& function, DefinitionUseTable& table)
    {
        CARACAL_ZONE_DETAIL("dce.buildDefinitionUseTable");
        ResetDefinitionUseTable(table, static_cast<size_t>(function.temporaryCount()));
        const auto countUse = [&table](ValueRef operand)
        {
            CountUse(table, operand);
        };

        const auto& blocks = function.blocks();
        for (size_t blockIndex = 0; blockIndex < blocks.size(); ++blockIndex)
        {
            const auto& block = blocks[blockIndex];
            for (const auto& instruction : block->instructions())
            {
                if (VisitInstruction(*instruction, countUse) == InstructionResult::RemovableValue)
                {
                    const auto resultId = instruction->resultId();
                    table.definitions[resultId] = instruction.get();
                    table.definitionBlockIndexes[resultId] = blockIndex;
                }
            }

            VisitTerminator(block->terminator(), countUse);
        }
    }

    [[nodiscard]] static bool IsMarkedDead(const Instruction& instruction, const std::vector<bool>& deadIds)
    {
        const auto resultId = instruction.resultId();
        if (resultId < 0)
        {
            return false;
        }

        return deadIds[resultId];
    }

    // returns false when no instruction is dead
    [[nodiscard]] static bool MarkDeadIds(const Function& function, DefinitionUseTable& table, std::vector<bool>& deadIds, std::vector<bool>& blocksWithDeadInstructions)
    {
        CARACAL_ZONE_DETAIL("dce.markDeadIds");
        std::vector<TemporaryId> worklist{};
        for (size_t id = 0; id < table.definitions.size(); ++id)
        {
            if (table.definitions[id] != nullptr && table.useCounts[id] == 0)
            {
                worklist.push_back(static_cast<TemporaryId>(id));
            }
        }

        if (worklist.empty())
        {
            return false;
        }

        deadIds.assign(table.definitions.size(), false);
        blocksWithDeadInstructions.assign(function.blocks().size(), false);
        const auto releaseUse = [&table, &worklist](ValueRef operand)
        {
            // an empty ValueRef has the id -1
            const auto operandId = operand.id();
            if (operandId < 0)
            {
                return;
            }

            table.useCounts[operandId]--;
            if (table.useCounts[operandId] == 0 && table.definitions[operandId] != nullptr)
            {
                worklist.push_back(operandId);
            }
        };

        while (!worklist.empty())
        {
            const auto deadId = worklist.back();
            worklist.pop_back();
            deadIds[deadId] = true;
            blocksWithDeadInstructions[table.definitionBlockIndexes[deadId]] = true;

            // we need to revisit to find more unused instructions
            VisitInstruction(*table.definitions[deadId], releaseUse);
        }

        return true;
    }

    static void RemoveDeadInstructions(Function& function, const std::vector<bool>& deadIds, const std::vector<bool>& blocksWithDeadInstructions)
    {
        CARACAL_ZONE_DETAIL("dce.removeDeadInstructions");
        const auto& blocks = function.blocks();
        for (size_t blockIndex = 0; blockIndex < blocks.size(); ++blockIndex)
        {
            if (!blocksWithDeadInstructions[blockIndex])
            {
                continue;
            }

            blocks[blockIndex]->removeInstructions(
                [&deadIds](const Instruction& instruction)
                {
                    return IsMarkedDead(instruction, deadIds);
                });
        }
    }

    static void EliminateInFunction(Function& function, DefinitionUseTable& table)
    {
        std::vector<bool> deadIds{};
        std::vector<bool> blocksWithDeadInstructions{};
        if (!MarkDeadIds(function, table, deadIds, blocksWithDeadInstructions))
        {
            return;
        }

        RemoveDeadInstructions(function, deadIds, blocksWithDeadInstructions);
    }

    void eliminateDeadCode(Module& module) noexcept
    {
        CARACAL_ZONE_NAMED("deadCodeElimination");
        DefinitionUseTable table{};
        for (auto& function : module.functions())
        {
            BuildDefinitionUseTable(function, table);
            EliminateInFunction(function, table);
        }

        if (auto* globalInit = module.tryGetGlobalInitMutable(); globalInit != nullptr)
        {
            BuildDefinitionUseTable(*globalInit, table);
            EliminateInFunction(*globalInit, table);
        }
    }
}
