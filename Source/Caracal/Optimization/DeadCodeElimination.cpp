#include <Caracal/Optimization/DeadCodeElimination.h>
#include <Caracal/Optimization/DefinitionUseTable.h>
#include <Caracal/Profiling.h>

#include <Caracal/IR/Function.h>
#include <Caracal/IR/Instruction.h>

#include <vector>

namespace Caracal
{
    enum class InstructionResult : u8
    {
        None,
        Value,
        RemovableValue,
    };

    template<typename TUseOperand>
    static InstructionResult VisitInstruction(const Function& function, u32 instructionIndex, TUseOperand&& useOperand)
    {
        switch (function.kindAt(instructionIndex))
        {
            case InstructionKind::Parameter:
            {
                return InstructionResult::Value;
            }
            case InstructionKind::Constant:
            case InstructionKind::AddressOf:
            case InstructionKind::AddressOfGlobal:
            case InstructionKind::SizeOf:
            {
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::FieldAddress:
            case InstructionKind::LoadValue:
            case InstructionKind::ValueNegation:
            case InstructionKind::LogicalNegation:
            case InstructionKind::BitNot:
            case InstructionKind::IntToFloat:
            case InstructionKind::IntWiden:
            case InstructionKind::FloatWiden:
            {
                useOperand(function.aAt(instructionIndex));
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::ElementAddress:
            case InstructionKind::Add:
            case InstructionKind::Subtract:
            case InstructionKind::Multiply:
            case InstructionKind::Divide:
            case InstructionKind::Equal:
            case InstructionKind::NotEqual:
            case InstructionKind::LessThan:
            case InstructionKind::LessOrEqual:
            case InstructionKind::GreaterThan:
            case InstructionKind::GreaterOrEqual:
            case InstructionKind::BitAnd:
            case InstructionKind::BitOr:
            case InstructionKind::BitXor:
            case InstructionKind::ShiftLeft:
            case InstructionKind::ShiftRight:
            {
                useOperand(function.aAt(instructionIndex));
                useOperand(function.bAt(instructionIndex));
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::MakeSlice:
            {
                useOperand(function.aAt(instructionIndex));
                useOperand(function.bAt(instructionIndex));
                return InstructionResult::Value;
            }
            case InstructionKind::StoreValue:
            case InstructionKind::ArrayAdd:
            case InstructionKind::ArrayRemove:
            {
                useOperand(function.aAt(instructionIndex));
                useOperand(function.bAt(instructionIndex));
                return InstructionResult::None;
            }
            case InstructionKind::ArrayCopy:
            {
                useOperand(function.aAt(instructionIndex));
                return InstructionResult::Value;
            }
            case InstructionKind::Call:
            {
                for (const auto argument : function.operands(static_cast<u32>(function.bAt(instructionIndex)), static_cast<u32>(function.cAt(instructionIndex))))
                {
                    useOperand(argument);
                }
                return InstructionResult::Value;
            }
            case InstructionKind::CallVoid:
            {
                for (const auto argument : function.operands(static_cast<u32>(function.bAt(instructionIndex)), static_cast<u32>(function.cAt(instructionIndex))))
                {
                    useOperand(argument);
                }
                return InstructionResult::None;
            }
            case InstructionKind::Phi:
            {
                for (const auto& input : function.phiInputsAt(instructionIndex))
                {
                    useOperand(input.value);
                }
                return InstructionResult::RemovableValue;
            }
            case InstructionKind::Branch:
            case InstructionKind::ReturnValue:
            {
                useOperand(function.aAt(instructionIndex));
                return InstructionResult::None;
            }
            default:
            {
                return InstructionResult::None;
            }
        }
    }

    static void ResetDefinitionUseTable(DefinitionUseTable& table, size_t temporaryCount)
    {
        table.useCounts.assign(temporaryCount, 0);
        table.definitionInstructions.assign(temporaryCount, DefinitionUseTable::NoDefinitionInstruction);
    }

    static void BuildDefinitionUseTable(const Function& function, DefinitionUseTable& table)
    {
        CARACAL_ZONE_DETAIL("dce.buildDefinitionUseTable");
        ResetDefinitionUseTable(table, static_cast<size_t>(function.temporaryCount()));
        const auto countUse = [&table](TemporaryId operand)
        {
            if (operand >= 0)
            {
                table.useCounts[operand]++;
            }
        };

        const auto instructionCount = function.instructionCount();
        for (u32 instructionIndex = 0; instructionIndex < instructionCount; ++instructionIndex)
        {
            if (VisitInstruction(function, instructionIndex, countUse) == InstructionResult::RemovableValue)
            {
                table.definitionInstructions[function.resultIdAt(instructionIndex)] = instructionIndex;
            }
        }
    }

    static void RemoveDeadInstructions(Function& function, DefinitionUseTable& table)
    {
        CARACAL_ZONE_DETAIL("dce.removeDeadInstructions");
        std::vector<TemporaryId> worklist{};
        for (size_t id = 0; id < table.definitionInstructions.size(); ++id)
        {
            if (table.definitionInstructions[id] != DefinitionUseTable::NoDefinitionInstruction && table.useCounts[id] == 0)
            {
                worklist.push_back(static_cast<TemporaryId>(id));
            }
        }

        const auto releaseUse = [&table, &worklist](TemporaryId operand)
        {
            if (operand < 0)
            {
                return;
            }

            table.useCounts[operand]--;
            if (table.useCounts[operand] == 0 && table.definitionInstructions[operand] != DefinitionUseTable::NoDefinitionInstruction)
            {
                worklist.push_back(operand);
            }
        };

        while (!worklist.empty())
        {
            const auto deadId = worklist.back();
            worklist.pop_back();
            const auto instructionIndex = table.definitionInstructions[deadId];

            // we need to revisit to find more unused instructions
            VisitInstruction(function, instructionIndex, releaseUse);
            function.removeInstruction(instructionIndex);
        }
    }

    void eliminateDeadCode(Module& module) noexcept
    {
        CARACAL_ZONE_NAMED("deadCodeElimination");
        DefinitionUseTable table{};
        for (auto& function : module.functions())
        {
            BuildDefinitionUseTable(function, table);
            RemoveDeadInstructions(function, table);
        }

        if (auto* globalInit = module.tryGetGlobalInitMutable(); globalInit != nullptr)
        {
            BuildDefinitionUseTable(*globalInit, table);
            RemoveDeadInstructions(*globalInit, table);
        }
    }
}
