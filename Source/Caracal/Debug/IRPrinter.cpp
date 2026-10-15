#include <Caracal/Debug/IRPrinter.h>
#include <Caracal/IR/AddInstruction.h>
#include <Caracal/IR/AddressOfInstruction.h>
#include <Caracal/IR/AddressOfFieldInstruction.h>
#include <Caracal/IR/AddressOfGlobalInstruction.h>
#include <Caracal/IR/AllocateLocalInstruction.h>
#include <Caracal/IR/BranchIfTerminator.h>
#include <Caracal/IR/CallInstruction.h>
#include <Caracal/IR/CallVoidInstruction.h>
#include <Caracal/IR/ConstantInstruction.h>
#include <Caracal/IR/DivideInstruction.h>
#include <Caracal/IR/ElementAddressInstruction.h>
#include <Caracal/IR/EqualInstruction.h>
#include <Caracal/IR/ExternFunction.h>
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
#include <Caracal/IR/ReturnTerminator.h>
#include <Caracal/IR/ReturnValueTerminator.h>
#include <Caracal/IR/StoreValueInstruction.h>
#include <Caracal/IR/SubtractInstruction.h>
#include <Caracal/IR/JumpTerminator.h>
#include <Caracal/IR/ValueNegationInstruction.h>

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
                m_builder.append(", ");

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
            prettyPrintBlock(function, *block);
        }
    }

    void IRPrinter::prettyPrintBlock(const Function& function, const BasicBlock& block)
    {
        m_builder.append("block ");
        appendBlockLabel(function, block.id());
        m_builder.appendLine(":");
        m_builder.pushIndentation();

        for (const auto& instruction : block.instructions())
            prettyPrintInstruction(function, *instruction);

        if (block.hasTerminator())
            prettyPrintTerminator(function, *block.terminator());

        m_builder.popIndentation();
    }

    void IRPrinter::prettyPrintInstruction(const Function& function, const Instruction& instruction)
    {
        switch (instruction.kind())
        {
            case InstructionKind::Parameter:
            {
                const auto& parameter = static_cast<const ParameterInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ parameter.resultId() });
                m_builder
                    .append(" = parameter ")
                    .append(std::to_string(parameter.parameterIndex()))
                    .append(" : ");
                appendType(parameter.parameter().type());
                m_builder
                    .append(" \"")
                    .append(parameter.parameter().name())
                    .appendLine("\"");
                break;
            }
            case InstructionKind::Constant:
            {
                const auto& constant = static_cast<const ConstantInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ constant.resultId() });
                m_builder
                    .append(" = const ")
                    .append(FormatConstantValue(m_module, constant.value(), constant.type()))
                    .append(" : ");
                appendType(constant.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::AllocateLocal:
            {
                const auto& allocateLocal = static_cast<const AllocateLocalInstruction&>(instruction);
                m_builder.appendIndented("");
                appendSlot(LocalSlotRef{ allocateLocal.slotId() });
                m_builder.append(" = allocate_local : ");
                appendType(allocateLocal.type());
                m_builder
                    .append(" \"")
                    .append(allocateLocal.localName())
                    .appendLine("\"");
                break;
            }
            case InstructionKind::AddressOf:
            {
                const auto& addressOf = static_cast<const AddressOfInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ addressOf.resultId() });
                m_builder.append(" = address_of ");
                appendSlot(addressOf.local());
                m_builder.append(" : ");
                appendType(addressOf.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::AddressOfGlobal:
            {
                const auto& addressOfGlobal = static_cast<const AddressOfGlobalInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ addressOfGlobal.resultId() });
                m_builder.append(" = address_of_global @");
                m_builder.append(addressOfGlobal.name());
                m_builder.append(" : ");
                appendType(addressOfGlobal.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::FieldAddress:
            {
                const auto& fieldAddress = static_cast<const AddressOfFieldInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ fieldAddress.resultId() });
                m_builder.append(" = address_of_field ");
                appendValue(fieldAddress.objectAddress());
                m_builder
                    .append(".")
                    .append(std::to_string(fieldAddress.fieldIndex()))
                    .append(" : ");
                appendType(fieldAddress.type());
                m_builder
                    .append(" \"")
                    .append(fieldAddress.fieldName())
                    .appendLine("\"");
                break;
            }
            case InstructionKind::ElementAddress:
            {
                const auto& elementAddress = static_cast<const ElementAddressInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ elementAddress.resultId() });
                m_builder.append(" = element_address ");
                appendValue(elementAddress.baseAddress());
                m_builder.append("[");
                appendValue(elementAddress.index());
                m_builder.append("] : ");
                appendType(elementAddress.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::MakeSlice:
            {
                const auto& makeSlice = static_cast<const MakeSliceInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ makeSlice.resultId() });
                m_builder.append(" = make_slice ");
                appendValue(makeSlice.baseAddress());
                m_builder.append(", ");
                appendValue(makeSlice.length());
                m_builder.append(" : ");
                appendType(makeSlice.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ArrayAdd:
            {
                const auto& arrayAdd = static_cast<const ArrayAddInstruction&>(instruction);
                m_builder.appendIndented("array_add ");
                appendValue(arrayAdd.descriptorAddress());
                m_builder.append(", ");
                appendValue(arrayAdd.value());
                m_builder.append(" : ");
                appendType(arrayAdd.arrayType());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ArrayRemove:
            {
                const auto& arrayRemove = static_cast<const ArrayRemoveInstruction&>(instruction);
                m_builder.appendIndented("array_remove ");
                appendValue(arrayRemove.descriptorAddress());
                m_builder.append(", ");
                appendValue(arrayRemove.index());
                m_builder.append(" : ");
                appendType(arrayRemove.arrayType());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ArrayCopy:
            {
                const auto& arrayCopy = static_cast<const ArrayCopyInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ arrayCopy.resultId() });
                m_builder.append(" = array_copy ");
                appendValue(arrayCopy.sourceAddress());
                m_builder.append(" : ");
                appendType(arrayCopy.arrayType());
                if (arrayCopy.reserveNulByte())
                {
                    m_builder.append(" nul");
                }
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::LoadValue:
            {
                const auto& load = static_cast<const LoadValueInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ load.resultId() });
                m_builder.append(" = load_value ");
                appendValue(load.address());
                m_builder.append(" : ");
                appendType(load.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::StoreValue:
            {
                const auto& store = static_cast<const StoreValueInstruction&>(instruction);
                m_builder.appendIndented("store_value ");
                appendValue(store.value());
                m_builder.append(", ");
                appendValue(store.address());
                m_builder.append(" : ");
                appendType(store.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::IntToFloat:
            {
                const auto& conversion = static_cast<const IntToFloatInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ conversion.resultId() });
                m_builder.append(" = int_to_float ");
                appendValue(conversion.operandValue());
                m_builder.append(" : ");
                appendType(conversion.sourceType());
                m_builder.append(" -> ");
                appendType(conversion.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::IntWiden:
            {
                const auto& conversion = static_cast<const IntWidenInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ conversion.resultId() });
                m_builder.append(" = int_widen ");
                appendValue(conversion.operandValue());
                m_builder.append(" : ");
                appendType(conversion.sourceType());
                m_builder.append(" -> ");
                appendType(conversion.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::SizeOf:
            {
                const auto& sizeOf = static_cast<const SizeOfInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ sizeOf.resultId() });
                m_builder.append(" = size_of ");
                appendType(sizeOf.measuredType());
                m_builder.append(" : ");
                appendType(sizeOf.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ValueNegation:
            {
                const auto& valueNegation = static_cast<const ValueNegationInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ valueNegation.resultId() });
                m_builder.append(" = value_negate ");
                appendValue(valueNegation.operandValue());
                m_builder.append(" : ");
                appendType(valueNegation.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::LogicalNegation:
            {
                const auto& logicalNegation = static_cast<const LogicalNegationInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ logicalNegation.resultId() });
                m_builder.append(" = logical_negate ");
                appendValue(logicalNegation.operandValue());
                m_builder.append(" : ");
                appendType(logicalNegation.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Call:
            {
                const auto& call = static_cast<const CallInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ call.resultId() });
                m_builder.append(" = call ").append(ResolveFunctionName(m_module, call.functionId())).append("(");
                for (size_t index = 0; index < call.arguments().size(); ++index)
                {
                    if (index > 0)
                        m_builder.append(", ");

                    appendValue(call.arguments()[index]);
                }
                m_builder.append(") : ");
                appendType(call.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::CallVoid:
            {
                const auto& call = static_cast<const CallVoidInstruction&>(instruction);
                m_builder.appendIndented("");
                m_builder.append("call ").append(ResolveFunctionName(m_module, call.functionId())).append("(");
                for (size_t index = 0; index < call.arguments().size(); ++index)
                {
                    if (index > 0)
                        m_builder.append(", ");

                    appendValue(call.arguments()[index]);
                }
                m_builder.appendLine(")");
                break;
            }
            case InstructionKind::Add:
            {
                const auto& add = static_cast<const AddInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ add.resultId() });
                m_builder.append(" = add ");
                appendValue(add.leftValue());
                m_builder.append(", ");
                appendValue(add.rightValue());
                m_builder.append(" : ");
                appendType(add.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Subtract:
            {
                const auto& subtract = static_cast<const SubtractInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ subtract.resultId() });
                m_builder.append(" = sub ");
                appendValue(subtract.leftValue());
                m_builder.append(", ");
                appendValue(subtract.rightValue());
                m_builder.append(" : ");
                appendType(subtract.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Multiply:
            {
                const auto& multiply = static_cast<const MultiplyInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ multiply.resultId() });
                m_builder.append(" = multiply ");
                appendValue(multiply.leftValue());
                m_builder.append(", ");
                appendValue(multiply.rightValue());
                m_builder.append(" : ");
                appendType(multiply.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Divide:
            {
                const auto& divide = static_cast<const DivideInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ divide.resultId() });
                m_builder.append(" = divide ");
                appendValue(divide.leftValue());
                m_builder.append(", ");
                appendValue(divide.rightValue());
                m_builder.append(" : ");
                appendType(divide.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Equal:
            {
                const auto& equal = static_cast<const EqualInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ equal.resultId() });
                m_builder.append(" = equal ");
                appendValue(equal.leftValue());
                m_builder.append(", ");
                appendValue(equal.rightValue());
                m_builder.append(" : ");
                appendType(equal.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::NotEqual:
            {
                const auto& notEqual = static_cast<const NotEqualInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ notEqual.resultId() });
                m_builder.append(" = not_equal ");
                appendValue(notEqual.leftValue());
                m_builder.append(", ");
                appendValue(notEqual.rightValue());
                m_builder.append(" : ");
                appendType(notEqual.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::LessThan:
            {
                const auto& lessThan = static_cast<const LessThanInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ lessThan.resultId() });
                m_builder.append(" = less_than ");
                appendValue(lessThan.leftValue());
                m_builder.append(", ");
                appendValue(lessThan.rightValue());
                m_builder.append(" : ");
                appendType(lessThan.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::LessOrEqual:
            {
                const auto& lessOrEqual = static_cast<const LessOrEqualInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ lessOrEqual.resultId() });
                m_builder.append(" = less_or_equal ");
                appendValue(lessOrEqual.leftValue());
                m_builder.append(", ");
                appendValue(lessOrEqual.rightValue());
                m_builder.append(" : ");
                appendType(lessOrEqual.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::GreaterThan:
            {
                const auto& greaterThan = static_cast<const GreaterThanInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ greaterThan.resultId() });
                m_builder.append(" = greater_than ");
                appendValue(greaterThan.leftValue());
                m_builder.append(", ");
                appendValue(greaterThan.rightValue());
                m_builder.append(" : ");
                appendType(greaterThan.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::GreaterOrEqual:
            {
                const auto& greaterOrEqual = static_cast<const GreaterOrEqualInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ greaterOrEqual.resultId() });
                m_builder.append(" = greater_or_equal ");
                appendValue(greaterOrEqual.leftValue());
                m_builder.append(", ");
                appendValue(greaterOrEqual.rightValue());
                m_builder.append(" : ");
                appendType(greaterOrEqual.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::BitAnd:
            {
                const auto& bitAnd = static_cast<const BitAndInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ bitAnd.resultId() });
                m_builder.append(" = bit_and ");
                appendValue(bitAnd.leftValue());
                m_builder.append(", ");
                appendValue(bitAnd.rightValue());
                m_builder.append(" : ");
                appendType(bitAnd.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::BitOr:
            {
                const auto& bitOr = static_cast<const BitOrInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ bitOr.resultId() });
                m_builder.append(" = bit_or ");
                appendValue(bitOr.leftValue());
                m_builder.append(", ");
                appendValue(bitOr.rightValue());
                m_builder.append(" : ");
                appendType(bitOr.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::BitXor:
            {
                const auto& bitXor = static_cast<const BitXorInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ bitXor.resultId() });
                m_builder.append(" = bit_xor ");
                appendValue(bitXor.leftValue());
                m_builder.append(", ");
                appendValue(bitXor.rightValue());
                m_builder.append(" : ");
                appendType(bitXor.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::BitNot:
            {
                const auto& bitNot = static_cast<const BitNotInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ bitNot.resultId() });
                m_builder.append(" = bit_not ");
                appendValue(bitNot.operandValue());
                m_builder.append(" : ");
                appendType(bitNot.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ShiftLeft:
            {
                const auto& shiftLeft = static_cast<const ShiftLeftInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ shiftLeft.resultId() });
                m_builder.append(" = shift_left ");
                appendValue(shiftLeft.value());
                m_builder.append(", ");
                appendValue(shiftLeft.amount());
                m_builder.append(" : ");
                appendType(shiftLeft.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::ShiftRight:
            {
                const auto& shiftRight = static_cast<const ShiftRightInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ shiftRight.resultId() });
                m_builder.append(" = shift_right ");
                appendValue(shiftRight.value());
                m_builder.append(", ");
                appendValue(shiftRight.amount());
                m_builder.append(" : ");
                appendType(shiftRight.type());
                m_builder.appendLine("");
                break;
            }
            case InstructionKind::Phi:
            {
                const auto& phi = static_cast<const PhiInstruction&>(instruction);
                m_builder.appendIndented("");
                appendValue(ValueRef{ phi.resultId() });
                m_builder.append(" = phi ");

                const auto& phiInputs = phi.inputs();
                for (size_t index = 0; index < phiInputs.size(); ++index)
                {
                    if (index > 0)
                    {
                        m_builder.append(", ");
                    }

                    const auto& input = phiInputs[index];
                    m_builder.append("[");
                    appendBlockLabel(function, input.blockId());
                    m_builder.append(": ");
                    appendValue(input.value());
                    m_builder.append("]");
                }

                m_builder.append(" : ");
                appendType(phi.type());
                m_builder.appendLine("");
                break;
            }
            default:
            {
                m_builder
                    .appendIndented("<unknown instruction #")
                    .append(std::to_string(static_cast<i32>(instruction.kind())))
                    .appendLine(">");
                break;
            }
        }
    }

    void IRPrinter::prettyPrintTerminator(const Function& function, const Terminator& terminator)
    {
        switch (terminator.kind())
        {
            case TerminatorKind::Jump:
            {
                const auto& jump = static_cast<const JumpTerminator&>(terminator);
                m_builder.appendIndented("jump ");
                appendBlockLabel(function, jump.targetBlockId());
                m_builder.appendLine("");
                break;
            }
            case TerminatorKind::Branch:
            {
                const auto& branch = static_cast<const BranchIfTerminator&>(terminator);
                m_builder.appendIndented("branch if ");
                appendValue(branch.condition());
                m_builder.append(", ");
                appendBlockLabel(function, branch.trueBlockId());
                m_builder.append(", ");
                appendBlockLabel(function, branch.falseBlockId());
                m_builder.appendLine("");
                break;
            }
            case TerminatorKind::Return:
            {
                m_builder.appendIndentedLine("return");
                break;
            }
            case TerminatorKind::ReturnValue:
            {
                const auto& ret = static_cast<const ReturnValueTerminator&>(terminator);
                m_builder.appendIndented("return ");
                appendValue(ret.value());
                m_builder.appendLine("");
                break;
            }
            case TerminatorKind::Unreachable:
            {
                m_builder.appendIndentedLine("unreachable");
                break;
            }
            default:
            {
                m_builder
                    .appendIndented("<unknown terminator #")
                    .append(std::to_string(static_cast<i32>(terminator.kind())))
                    .appendLine(">");
                break;
            }
        }
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

    void IRPrinter::appendBlockLabel(const Function& function, BlockId blockId)
    {
        const auto* block = function.tryGetBlock(blockId);
        if (block != nullptr)
        {
            m_builder
                .append(block->label())
                .append("(")
                .append(std::to_string(block->id()))
                .append(")");
            return;
        }

        m_builder.append("unknown");
    }
}
