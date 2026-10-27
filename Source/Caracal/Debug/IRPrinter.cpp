#include <Caracal/Debug/IRPrinter.h>
#include <Caracal/IR/ExternFunction.h>

#include <algorithm>
#include <charconv>
#include <string_view>
#include <string>
#include <type_traits>
#include <variant>

namespace Caracal
{
    static const std::string& ResolveFunctionName(const Module& module, FunctionId functionId)
    {
        static const std::string unknownFunctionName = "<unknown-function>";

        const auto* functionName = module.tryGetFunctionName(functionId);
        if (functionName == nullptr)
            return unknownFunctionName;

        return *functionName;
    }

    static std::string FormatLiteralData(const Module& module, const ConstantValue::LiteralData& value, Type type)
    {
        return std::visit([&module, type](const auto& payload) -> std::string
            {
                using Payload = std::decay_t<decltype(payload)>;

                const auto formatStringLiteral = [&module, type](std::string_view text) -> std::string
                    {
                        std::string formatted = "\"";
                        formatted.reserve(text.size() + 2);
                        for (const auto character : text)
                        {
                            switch (character)
                            {
                                case '\\':
                                    formatted += "\\\\";
                                    break;
                                case '"':
                                    formatted += "\\\"";
                                    break;
                                case '\n':
                                    formatted += "\\n";
                                    break;
                                case '\r':
                                    formatted += "\\r";
                                    break;
                                case '\t':
                                    formatted += "\\t";
                                    break;
                                case '\0':
                                    formatted += "\\0";
                                    break;
                                default:
                                    formatted += character;
                                    break;
                            }
                        }

                        if (type == module.wellKnown().cstring)
                            formatted += "\\0";

                        formatted += '"';
                        return formatted;
                    };

                if constexpr (std::is_same_v<Payload, bool>)
                {
                    if (payload)
                    {
                        return "true";
                    }
                    else
                    {
                        return "false";
                    }
                }

                if constexpr (std::is_integral_v<Payload> && !std::is_same_v<Payload, bool>)
                    return std::to_string(payload);

                if constexpr (std::is_same_v<Payload, f32> || std::is_same_v<Payload, f64>)
                {
                    char buffer[40]{};
                    const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), payload, std::chars_format::fixed, 6);
                    if (error != std::errc{})
                        return "<invalid-f32>";

                    auto text = std::string(buffer, end);
                    const auto dotIndex = text.find('.');
                    if (dotIndex == std::string::npos)
                    {
                        text += ".0";
                    }

                    return text;
                }

                if constexpr (std::is_same_v<Payload, std::string>)
                    return formatStringLiteral(payload);

                return "<unknown-constant>";
            }, value);
    }

    static std::string FormatLiteralConstantValue(const Module& module, const ConstantValue& value, Type type)
    {
        if (const auto* literalData = value.tryGetLiteralData())
            return FormatLiteralData(module, *literalData, type);

        if (const auto* enumConstant = value.tryGetEnumConstant())
            return enumConstant->enumName + "." + enumConstant->fieldName;

        if (const auto* aggregate = value.tryGetAggregate())
        {
            std::string formatted = "[";
            for (size_t index = 0; index < aggregate->size(); ++index)
            {
                if (index > 0)
                {
                    formatted += ", ";
                }

                formatted += FormatLiteralConstantValue(module, (*aggregate)[index], Type::Undefined());
            }

            formatted += "]";
            return formatted;
        }

        return "<unknown-constant>";
    }

    static std::string FormatConstantValue(const Module& module, const ConstantValue& value, Type type)
    {
        if (const auto* enumConstant = value.tryGetEnumConstant())
        {
            auto underlyingType = type;
            if (const auto* enumDeclaration = module.tryGetEnum(enumConstant->enumType))
                underlyingType = enumDeclaration->baseType();

            return enumConstant->enumName + "." + enumConstant->fieldName + " (= "
                + FormatLiteralData(module, enumConstant->underlyingValue, underlyingType) + ")";
        }

        return FormatLiteralConstantValue(module, value, type);
    }

    static std::string_view ResolveParameterName(const std::vector<std::string>& parameterNames, size_t index) noexcept
    {
        static constexpr std::string_view unnamedParameter = "_";
        if (index >= parameterNames.size())
            return unnamedParameter;

        if (parameterNames[index].empty())
            return unnamedParameter;

        return parameterNames[index];
    }

    IRPrinter::IRPrinter(const Module& module, i32 indentation)
        : m_module{ module }
        , m_builder{ indentation }
    {
    }

    std::string IRPrinter::prettyPrint()
    {
        bool hasPrintedDeclaration = false;
        const auto appendDeclarationSeparator = [&]()
            {
                if (hasPrintedDeclaration)
                    m_builder.appendLine("");

                hasPrintedDeclaration = true;
            };

        for (const auto& enumDeclaration : m_module.enums())
        {
            appendDeclarationSeparator();
            prettyPrintEnumDeclaration(enumDeclaration);
        }

        for (const auto& typeDeclaration : m_module.types())
        {
            appendDeclarationSeparator();
            prettyPrintTypeDeclaration(typeDeclaration);
        }

        // globals are single-line declarations, so print them as one group without blank lines between them
        const bool hasGlobals = !m_module.globalConstants().empty()
            || !m_module.globalReferences().empty()
            || !m_module.constructedGlobals().empty();
        if (hasGlobals)
        {
            appendDeclarationSeparator();

            for (const auto& globalDeclaration : m_module.globalConstants())
                prettyPrintGlobalConstantDeclaration(globalDeclaration);

            for (const auto& globalReference : m_module.globalReferences())
                prettyPrintGlobalReferenceDeclaration(globalReference);

            for (const auto& constructedGlobal : m_module.constructedGlobals())
                prettyPrintConstructedGlobalDeclaration(constructedGlobal);
        }

        for (const auto& function : m_module.externFunctions())
        {
            appendDeclarationSeparator();
            prettyPrintExternFunction(function);
        }

        for (const auto& function : m_module.functions())
        {
            appendDeclarationSeparator();
            prettyPrintFunction(function);
        }

        if (const auto* globalInit = m_module.tryGetGlobalInit())
        {
            appendDeclarationSeparator();
            prettyPrintFunction(*globalInit);
        }

        return m_builder.toString();
    }

    void IRPrinter::prettyPrintEnumDeclaration(const EnumDeclaration& enumDeclaration)
    {
        m_builder
            .append("enum ")
            .append(enumDeclaration.name())
            .append(" : ");
        appendType(enumDeclaration.baseType());
        m_builder.appendLine("");

        m_builder.pushIndentation();
        for (const auto& field : enumDeclaration.fields())
        {
            m_builder
                .appendIndented("")
                .append(field.name)
                .append(" = ")
                .append(FormatLiteralConstantValue(m_module, field.value, enumDeclaration.baseType()))
                .appendLine("");
        }
        m_builder.popIndentation();
    }

    void IRPrinter::prettyPrintTypeDeclaration(const TypeDeclaration& typeDeclaration)
    {
        m_builder
            .append("type ")
            .append(typeDeclaration.name())
            .appendLine("");

        m_builder.pushIndentation();
        for (const auto& field : typeDeclaration.fields())
        {
            m_builder.appendIndented("");
            if (field.isConstant)
            {
                m_builder.append("const ");
            }

            m_builder.append(field.name).append(": ");
            appendType(field.type);
            m_builder.appendLine("");
        }
        m_builder.popIndentation();
    }

    void IRPrinter::prettyPrintGlobalConstantDeclaration(const GlobalConstantDeclaration& globalDeclaration)
    {
        m_builder
            .append("global ")
            .append(globalDeclaration.name())
            .append(" : ");
        appendType(globalDeclaration.type());
        m_builder
            .append(" = ")
            .append(FormatLiteralConstantValue(m_module, globalDeclaration.value(), globalDeclaration.type()))
            .appendLine("");
    }

    void IRPrinter::prettyPrintGlobalReferenceDeclaration(const GlobalReferenceDeclaration& globalReference)
    {
        m_builder
            .append("global ")
            .append(globalReference.name())
            .append(" : ");
        appendType(globalReference.type());
        m_builder
            .append(" = @")
            .append(globalReference.targetName())
            .appendLine("");
    }

    void IRPrinter::prettyPrintConstructedGlobalDeclaration(const ConstructedGlobalDeclaration& globalDeclaration)
    {
        m_builder
            .append("global ")
            .append(globalDeclaration.name())
            .append(" : ");
        appendType(globalDeclaration.type());
        m_builder.appendLine("");
    }

    void IRPrinter::prettyPrintExternFunction(const ExternFunction& function)
    {
        m_builder
            .append("extern ")
            .append(function.name())
            .append("(");

        const auto& parameters = function.parameters();
        for (size_t index = 0; index < parameters.size(); ++index)
        {
            if (index > 0)
            {
                m_builder.append(", ");
            }

            m_builder
                .append("%")
                .append(std::to_string(index))
                .append(": ");
            appendType(parameters[index].type());
            m_builder
                .append(" \"")
                .append(parameters[index].name())
                .append("\"");
        }

        m_builder.append(") ");
        appendType(function.returnType());
        if (function.symbolName().has_value())
        {
            m_builder.append(" symbol \"").append(function.symbolName().value()).append("\"");
        }
        m_builder.appendLine("");
    }

    void IRPrinter::appendType(Type type)
    {
        const auto baseType = type.toBaseType();
        if (type.isReference())
            m_builder.append("ref ");

        if (baseType == Type::Void())
            m_builder.append("void");
        else if (baseType == Type::Discard())
            m_builder.append("discard");
        else if (baseType == Type::Undefined())
            m_builder.append("undefined");
        else if (baseType == Type::Function())
            m_builder.append("function");
        else if (baseType == Type::CVariadic())
            m_builder.append("...");
        else if (const auto* typeName = m_module.tryGetTypeName(baseType))
            m_builder.append(*typeName);
        else
            m_builder.append("type#").append(std::to_string(baseType.id()));

        if (type.isOptional())
            m_builder.append("?");
    }

    void IRPrinter::appendValue(ValueRef value)
    {
        m_builder
            .append("%")
            .append(std::to_string(value.id()));
    }

    void IRPrinter::appendSlot(LocalSlotRef slot)
    {
        m_builder
            .append("%slot")
            .append(std::to_string(slot.id()));
    }

    void IRPrinter::prettyPrintFunction(const Function& function)
    {
        m_builder
            .append("define ")
            .append(function.name())
            .append("(");

        const auto& parameters = function.parameters();
        for (size_t index = 0; index < parameters.size(); ++index)
        {
            if (index > 0)
            {
                m_builder.append(", ");
            }

            m_builder
                .append("%")
                .append(std::to_string(index))
                .append(": ");
            appendType(parameters[index].type());
            m_builder
                .append(" \"")
                .append(parameters[index].name())
                .append("\"");
        }

        m_builder.append(") ");
        appendType(function.returnType());
        m_builder.appendLine("");

        for (const auto& block : function.blocks())
        {
            m_builder.append("block ");
            appendBlockLabel(function, block.id);
            m_builder.appendLine(":");
            m_builder.pushIndentation();
            for (auto instructionIndex = block.instructionStart; instructionIndex < block.instructionStart + block.instructionCount; ++instructionIndex)
            {
                prettyPrintInstruction(function, function.instructionAt(instructionIndex));
            }
            m_builder.popIndentation();
        }
    }

    void IRPrinter::appendResult(const Instruction& instruction)
    {
        m_builder.appendIndented("");
        appendValue(ValueRef{ instruction.resultId });
    }

    void IRPrinter::appendOneOperand(const Instruction& instruction, std::string_view name)
    {
        appendResult(instruction);
        m_builder.append(" = ").append(std::string(name)).append(" ");
        appendValue(ValueRef{ instruction.a });
        m_builder.append(" : ");
        appendType(instruction.type);
        m_builder.appendLine("");
    }

    void IRPrinter::appendTwoOperands(const Instruction& instruction, std::string_view name)
    {
        appendResult(instruction);
        m_builder.append(" = ").append(std::string(name)).append(" ");
        appendValue(ValueRef{ instruction.a });
        m_builder.append(", ");
        appendValue(ValueRef{ instruction.b });
        m_builder.append(" : ");
        appendType(instruction.type);
        m_builder.appendLine("");
    }

    void IRPrinter::prettyPrintInstruction(const Function& function, const Instruction& instruction)
    {
        switch (instruction.kind)
        {
            case InstructionKind::Nop:
            {
                break;
            }
            case InstructionKind::Parameter:
            {
                appendResult(instruction);
                m_builder
                    .append(" = parameter ")
                    .append(std::to_string(instruction.a))
                    .append(" : ");
                appendType(instruction.type);
                m_builder
                    .append(" \"")
                    .append(function.parameters()[static_cast<size_t>(instruction.a)].name())
                    .appendLine("\"");
                break;
            }
            case InstructionKind::Constant:
            {
                const auto constant = m_module.constantValue(instruction);
                appendResult(instruction);
                m_builder
                    .append(" = const ")
                    .append(FormatConstantValue(m_module, constant, instruction.type))
                    .append(" : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::AllocateLocal:
            {
                m_builder.appendIndented("");
                appendSlot(LocalSlotRef{ instruction.a });
                m_builder.append(" = allocate_local : ");
                appendType(instruction.type);
                m_builder
                    .append(" \"")
                    .append(m_module.names()[static_cast<size_t>(instruction.b)])
                    .appendLine("\"");
                break;
            }
            case InstructionKind::AddressOf:
            {
                appendResult(instruction);
                m_builder.append(" = address_of ");
                appendSlot(LocalSlotRef{ instruction.a });
                m_builder.append(" : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::AddressOfGlobal:
            {
                appendResult(instruction);
                m_builder
                    .append(" = address_of_global @")
                    .append(m_module.names()[static_cast<size_t>(instruction.a)])
                    .append(" : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::FieldAddress:
            {
                appendResult(instruction);
                m_builder.append(" = address_of_field ");
                appendValue(ValueRef{ instruction.a });
                m_builder
                    .append(".")
                    .append(std::to_string(instruction.b))
                    .append(" : ");
                appendType(instruction.type);
                m_builder
                    .append(" \"")
                    .append(std::string(m_module.fieldName(instruction.secondType(), instruction.b)))
                    .appendLine("\"");
                break;
            }
            case InstructionKind::ElementAddress:
            {
                appendResult(instruction);
                m_builder.append(" = element_address ");
                appendValue(ValueRef{ instruction.a });
                m_builder.append("[");
                appendValue(ValueRef{ instruction.b });
                m_builder.append("] : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::MakeSlice:
            {
                appendResult(instruction);
                m_builder.append(" = make_slice ");
                appendValue(ValueRef{ instruction.a });
                m_builder.append(", ");
                appendValue(ValueRef{ instruction.b });
                m_builder.append(" : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ArrayAdd:
            {
                m_builder.appendIndented("array_add ");
                appendValue(ValueRef{ instruction.a });
                m_builder.append(", ");
                appendValue(ValueRef{ instruction.b });
                m_builder.append(" : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ArrayRemove:
            {
                m_builder.appendIndented("array_remove ");
                appendValue(ValueRef{ instruction.a });
                m_builder.append(", ");
                appendValue(ValueRef{ instruction.b });
                m_builder.append(" : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ArrayCopy:
            {
                appendResult(instruction);
                m_builder.append(" = array_copy ");
                appendValue(ValueRef{ instruction.a });
                m_builder.append(" : ");
                appendType(instruction.type);
                if (instruction.d != 0)
                {
                    m_builder.append(" nul");
                }
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::LoadValue:
            {
                appendOneOperand(instruction, "load_value");
                break;
            }
            case InstructionKind::StoreValue:
            {
                m_builder.appendIndented("store_value ");
                appendValue(ValueRef{ instruction.a });
                m_builder.append(", ");
                appendValue(ValueRef{ instruction.b });
                m_builder.append(" : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::IntToFloat:
            case InstructionKind::IntWiden:
            case InstructionKind::FloatWiden:
            {
                appendResult(instruction);
                if (instruction.kind == InstructionKind::IntToFloat)
                {
                    m_builder.append(" = int_to_float ");
                }
                else if (instruction.kind == InstructionKind::IntWiden)
                {
                    m_builder.append(" = int_widen ");
                }
                else
                {
                    m_builder.append(" = float_widen ");
                }
                appendValue(ValueRef{ instruction.a });
                m_builder.append(" : ");
                appendType(instruction.secondType());
                m_builder.append(" -> ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::SizeOf:
            {
                appendResult(instruction);
                m_builder.append(" = size_of ");
                appendType(instruction.secondType());
                m_builder.append(" : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ValueNegation:
            {
                appendOneOperand(instruction, "value_negate");
                break;
            }
            case InstructionKind::LogicalNegation:
            {
                appendOneOperand(instruction, "logical_negate");
                break;
            }
            case InstructionKind::BitNot:
            {
                appendOneOperand(instruction, "bit_not");
                break;
            }
            case InstructionKind::Call:
            case InstructionKind::CallVoid:
            {
                m_builder.appendIndented("");
                if (instruction.kind == InstructionKind::Call)
                {
                    appendValue(ValueRef{ instruction.resultId });
                    m_builder.append(" = ");
                }
                m_builder.append("call ").append(ResolveFunctionName(m_module, instruction.a)).append("(");
                const auto arguments = function.operands(static_cast<u32>(instruction.b), static_cast<u32>(instruction.c));
                for (size_t index = 0; index < arguments.size(); ++index)
                {
                    if (index > 0)
                    {
                        m_builder.append(", ");
                    }

                    appendValue(ValueRef{ arguments[index] });
                }
                if (instruction.kind == InstructionKind::Call)
                {
                    m_builder.append(") : ");
                    appendType(instruction.type);
                    m_builder.appendLine("");
                }
                else
                {
                    m_builder.appendLine(")");
                }
                break;
            }
            case InstructionKind::Add:
            {
                appendTwoOperands(instruction, "add");
                break;
            }
            case InstructionKind::Subtract:
            {
                appendTwoOperands(instruction, "sub");
                break;
            }
            case InstructionKind::Multiply:
            {
                appendTwoOperands(instruction, "multiply");
                break;
            }
            case InstructionKind::Divide:
            {
                appendTwoOperands(instruction, "divide");
                break;
            }
            case InstructionKind::Equal:
            {
                appendTwoOperands(instruction, "equal");
                break;
            }
            case InstructionKind::NotEqual:
            {
                appendTwoOperands(instruction, "not_equal");
                break;
            }
            case InstructionKind::LessThan:
            {
                appendTwoOperands(instruction, "less_than");
                break;
            }
            case InstructionKind::LessOrEqual:
            {
                appendTwoOperands(instruction, "less_or_equal");
                break;
            }
            case InstructionKind::GreaterThan:
            {
                appendTwoOperands(instruction, "greater_than");
                break;
            }
            case InstructionKind::GreaterOrEqual:
            {
                appendTwoOperands(instruction, "greater_or_equal");
                break;
            }
            case InstructionKind::BitAnd:
            {
                appendTwoOperands(instruction, "bit_and");
                break;
            }
            case InstructionKind::BitOr:
            {
                appendTwoOperands(instruction, "bit_or");
                break;
            }
            case InstructionKind::BitXor:
            {
                appendTwoOperands(instruction, "bit_xor");
                break;
            }
            case InstructionKind::ShiftLeft:
            {
                appendTwoOperands(instruction, "shift_left");
                break;
            }
            case InstructionKind::ShiftRight:
            {
                appendTwoOperands(instruction, "shift_right");
                break;
            }
            case InstructionKind::Phi:
            {
                appendResult(instruction);
                m_builder.append(" = phi ");
                const auto inputs = function.phiInputs(instruction);
                for (size_t index = 0; index < inputs.size(); ++index)
                {
                    if (index > 0)
                    {
                        m_builder.append(", ");
                    }

                    m_builder.append("[");
                    appendBlockLabel(function, inputs[index].blockId);
                    m_builder.append(": ");
                    appendValue(ValueRef{ inputs[index].value });
                    m_builder.append("]");
                }
                m_builder.append(" : ");
                appendType(instruction.type);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Jump:
            {
                m_builder.appendIndented("jump ");
                appendBlockLabel(function, instruction.a);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Branch:
            {
                m_builder.appendIndented("branch if ");
                appendValue(ValueRef{ instruction.a });
                m_builder.append(", ");
                appendBlockLabel(function, instruction.b);
                m_builder.append(", ");
                appendBlockLabel(function, instruction.c);
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Return:
            {
                m_builder.appendIndentedLine("return");
                break;
            }
            case InstructionKind::ReturnValue:
            {
                m_builder.appendIndented("return ");
                appendValue(ValueRef{ instruction.a });
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Unreachable:
            {
                m_builder.appendIndentedLine("unreachable");
                break;
            }
            default:
            {
                m_builder
                    .appendIndented("<unknown instruction #")
                    .append(std::to_string(static_cast<i32>(instruction.kind)))
                    .appendLine(">");
                break;
            }
        }
    }

    void IRPrinter::appendBlockLabel(const Function& function, BlockId blockId)
    {
        for (const auto& block : function.blocks())
        {
            if (block.id == blockId)
            {
                m_builder
                    .append(std::string(blockLabelText(block.label)))
                    .append("(")
                    .append(std::to_string(block.id))
                    .append(")");
                return;
            }
        }

        m_builder.append("unknown");
    }
}
