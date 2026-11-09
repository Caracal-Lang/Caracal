#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Semantic/TypeCheckerInternal.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Caracal
{
    [[nodiscard]] static SourceLocation TypeNameSpan(const CompilationContext& context, const CompilationUnit& unit, u32 index)
    {
        return NodeSpans::typeNameSpan(unit.nodeBuffer, context.tokenBuffer(unit.fileId), index);
    }

    void TypeChecker::typeCheckFunctionSignatures()
    {
        CARACAL_ZONE_NAMED("check.functionSignatures");
        for (const auto functionDeclaration : m_functionDeclarations)
        {
            const auto& unit = unitFor(functionDeclaration);
            typeCheckFunctionSignature(unit, functionDeclaration.index);
        }

        reportInvalidOverloadSets();
        for (const auto functionDeclaration : m_functionDeclarations)
        {
            const auto& unit = unitFor(functionDeclaration);
            typeCheckFunctionDefaultValues(unit, functionDeclaration.index);
        }
    }

    void TypeChecker::typeCheckFunctionSignature(const CompilationUnit& unit, u32 index)
    {
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);

        pushScope(ScopeKind::Function);
        auto parameters = typeCheckParametersNode(unit, functionLayout.parameters, false);
        auto returns = typeCheckReturnTypesNode(unit, functionLayout.returnTypes);
        popScope(false);

        const auto functionName = nameOf(unit, index);
        auto functionType = m_module.typeOfNode(NodeIndex{ unit.fileId, index });
        if (functionType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& functionDefinition = m_module.getFunctionDefinition(functionType);
        auto annotations = std::vector<u32>{};
        NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, index, annotations);
        std::optional<std::string> symbolName;
        auto isRequiredExtern = false;
        const auto isExtern = validateCallableAnnotations(unit, annotations, symbolName, isRequiredExtern);
        if (isExtern && isRequiredExtern)
        {
            m_module.markExternRequired(functionType);
        }

        auto parameterNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, functionLayout.parameters, parameterNodes);
        const auto isVariadic = !parameterNodes.empty() && NodeLayout::isVariadicParameter(unit.nodeBuffer, parameterNodes.back());
        if (isVariadic && !isExtern)
        {
            m_diagnostics.addNonExternVariadicFunctionError(
                unit.source,
                ParameterSpan(m_compilationContext, unit, parameterNodes.back()),
                std::string(functionName));
        }
        functionDefinition.setParameters(std::move(parameters));
        functionDefinition.setReturnTypes(std::move(returns));
        functionDefinition.setIsVariadic(isVariadic);
        functionDefinition.setSymbolName(std::move(symbolName));
        functionDefinition.setIsExtern(isExtern);
    }

    Type TypeChecker::typeCheckTypeNameNode(const CompilationUnit& unit, u32 index)
    {
        if (unit.nodeBuffer.kind(index) == NodeKind::ArrayTypeNameNode)
        {
            return typeCheckArrayTypeNameNode(unit, index);
        }

        const auto name = nameOf(unit, index);
        auto type = m_module.tryGetTypeByName(name);
        if (type != Type::Undefined())
        {
            if (!m_isPreludePass)
            {
                m_module.markPreludeTypeRequired(type);
            }

            const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
            if (unit.nodeBuffer.isReference(tokens, index))
            {
                if (type.isReference())
                {
                    TODO("Add error diagnostics for already being a reference");
                    return Type::Undefined();
                }
                type = type.toReference();
            }
            // TODO nullable handling
            return recordType(unit, index, type);
        }
        else
        {
            m_diagnostics.addUnknownTypeError(unit.source, nameLocation(unit, index), std::string(name));
            return Type::Undefined();
        }
    }

    Type TypeChecker::typeCheckArrayTypeNameNode(const CompilationUnit& unit, u32 index)
    {
        const auto arrayTypeLayout = NodeLayout::arrayTypeName(unit.nodeBuffer, index);
        auto elementType = typeCheckTypeNameNode(unit, arrayTypeLayout.elementType);
        if (elementType == Type::Undefined())
        {
            return Type::Undefined();
        }

        // ownership rule: arrays cant store references or slices
        if (elementType.isReference() || elementType.kind() == TypeKind::Slice)
        {
            m_diagnostics.addReferenceOrSliceArrayElementError(
                unit.source,
                TypeNameSpan(m_compilationContext, unit, arrayTypeLayout.elementType));
        }
        else if (elementType.kind() == TypeKind::DynamicArray)
        {
            m_diagnostics.addDynamicArrayElementError(
                unit.source,
                TypeNameSpan(m_compilationContext, unit, arrayTypeLayout.elementType));
        }

        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        auto arrayType = Type::Undefined();
        switch (unit.nodeBuffer.arrayKind(tokens, index))
        {
            case ArrayTypeKind::Slice:
            {
                arrayType = m_module.getOrCreateArrayType(TypeKind::Slice, elementType, 0);
                break;
            }
            case ArrayTypeKind::Fixed:
            {
                if (!arrayTypeLayout.hasLength)
                {
                    return Type::Undefined();
                }

                const auto length = convertToI32(unit, arrayTypeLayout.lengthLiteral);
                arrayType = m_module.getOrCreateArrayType(TypeKind::FixedArray, elementType, length);
                break;
            }
            case ArrayTypeKind::Dynamic:
            {
                arrayType = m_module.getOrCreateArrayType(TypeKind::DynamicArray, elementType, 0);
                break;
            }
        }

        if (unit.nodeBuffer.isReference(tokens, index))
        {
            arrayType = arrayType.toReference();
        }

        return recordType(unit, index, arrayType);
    }

    void TypeChecker::typeCheckSourceTextDefault(const CompilationUnit& unit, u32 defaultIndex, std::span<const Parameter> earlierParameters)
    {
        const auto namedParameter = nameOf(unit, defaultIndex);
        for (const auto& parameter : earlierParameters)
        {
            if (parameter.name() == namedParameter)
            {
                return;
            }
        }

        m_diagnostics.addSourceTextMustReferToEarlierParameterError(unit.source, nameLocation(unit, defaultIndex), std::string(namedParameter));
    }

    void TypeChecker::typeCheckDefaultValue(const CompilationUnit& unit, u32 parameterIndex, Type parameterType, std::span<const Parameter> earlierParameters)
    {
        const auto parameterLayout = NodeLayout::parameter(unit.nodeBuffer, parameterIndex);
        auto defaultContext = ExpressionContext{};
        defaultContext.contextualNumberType = parameterType;
        const auto checkedDefault = typeCheckExpression(unit, parameterLayout.defaultValue, defaultContext);
        const auto defaultType = checkedDefault.type;
        if (checkedDefault.folded.has_value())
        {
            m_emitter.recordDefaultArgumentFold(unit.fileId, parameterLayout.defaultValue, checkedDefault.folded.value());
        }
        if (defaultType != Type::Undefined() && !isAssignableTo(defaultType, parameterType))
        {
            const auto location = ExpressionSourceLocation(m_compilationContext, unit, parameterLayout.defaultValue);
            if (!tryAddArrayLengthMismatchError(unit, location, parameterType, defaultType))
            {
                m_diagnostics.addDefaultParameterTypeMismatchError(
                    unit.source,
                    location,
                    formatTypeName(m_module, parameterType),
                    formatTypeName(m_module, defaultType));
            }
        }
        if (unit.nodeBuffer.kind(parameterLayout.defaultValue) == NodeKind::SourceTextDefault)
        {
            typeCheckSourceTextDefault(unit, parameterLayout.defaultValue, earlierParameters);
        }
    }

    void TypeChecker::typeCheckFunctionDefaultValues(const CompilationUnit& unit, u32 index)
    {
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);
        const auto& parameters = m_module.getFunctionDefinition(m_module.typeOfNode(NodeIndex{ unit.fileId, index })).parameters();
        auto parameterNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, functionLayout.parameters, parameterNodes);

        pushScope(ScopeKind::Function);
        for (size_t position = 0; position < parameterNodes.size(); ++position)
        {
            if (NodeLayout::parameter(unit.nodeBuffer, parameterNodes[position]).hasDefaultValue)
            {
                typeCheckDefaultValue(unit, parameterNodes[position], parameters[position].type(), std::span<const Parameter>(parameters.data(), position));
            }
        }
        popScope(false);
    }

    std::vector<Parameter> TypeChecker::typeCheckParametersNode(const CompilationUnit& unit, u32 index, bool checksDefaultValues)
    {
        std::vector<Parameter> parameters{};
        auto seenDefault = false;

        auto parameterNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, index, parameterNodes);

        // parameter names stay out of scope in this loop
        // default values cannot reference other parameters
        for (const auto parameterIndex : parameterNodes)
        {
            const auto parameterLayout = NodeLayout::parameter(unit.nodeBuffer, parameterIndex);
            const auto isVariadic = NodeLayout::isVariadicParameter(unit.nodeBuffer, parameterIndex);
            auto parameterName = std::string_view{};
            if (isVariadic)
            {
                parameterName = "varargs";
            }
            else
            {
                parameterName = nameOf(unit, parameterIndex);
            }

            auto parameterType = typeCheckTypeNameNode(unit, parameterLayout.typeName);
            if (parameterType == Type::Void())
            {
                m_diagnostics.addVoidParameterTypeError(
                    unit.source,
                    ParameterSpan(m_compilationContext, unit, parameterIndex),
                    std::string(parameterName));

                parameterType = Type::Undefined();
            }
            m_module.setNodeType(NodeIndex{ unit.fileId, parameterIndex }, parameterType);

            if (parameterLayout.hasDefaultValue)
            {
                if (checksDefaultValues)
                {
                    typeCheckDefaultValue(unit, parameterIndex, parameterType, parameters);
                }
                seenDefault = true;
            }
            else if (seenDefault && !isVariadic)
            {
                // once a parameter with a default value is seen, all following parameters must have defaults
                m_diagnostics.addNonTrailingDefaultParameterError(
                    unit.source,
                    ParameterSpan(m_compilationContext, unit, parameterIndex),
                    std::string(parameterName));
            }

            auto parameter = Parameter{ parameterName, parameterType, nullptr };
            if (parameterLayout.hasDefaultValue)
            {
                parameter.setDefaultValueNode(NodeIndex{ unit.fileId, parameterLayout.defaultValue });
            }
            parameters.push_back(std::move(parameter));
        }

        // finally add parameter names into the current scope
        for (const auto parameterIndex : parameterNodes)
        {
            auto nameId = 0u;
            if (NodeLayout::isVariadicParameter(unit.nodeBuffer, parameterIndex))
            {
                nameId = internName("varargs");
            }
            else
            {
                nameId = nameIdOf(unit, parameterIndex);
            }

            // a parameter can have the name of a global
            const auto* existing = tryGetBinding(nameId);
            if (existing == nullptr || existing->scopeIndex != static_cast<u32>(m_scopeFrames.size() - 1))
            {
                addVariableBinding(nameId, m_module.typeOfNode(NodeIndex{ unit.fileId, parameterIndex }), nameLocation(unit, parameterIndex), unit.source, VariableBindingKind::Parameter);
            }
            else
            {
                m_diagnostics.addDuplicateParameterDeclarationError(
                    unit.source,
                    nameLocation(unit, parameterIndex),
                    std::string(m_nameLexemes[nameId]),
                    existing->source,
                    existing->location);
            }
        }

        return parameters;
    }

    std::vector<Type> TypeChecker::typeCheckReturnTypesNode(const CompilationUnit& unit, u32 index)
    {
        std::vector<Type> types{};
        auto returnTypeNodes = std::vector<u32>{};
        NodeLayout::collectChildren(unit.nodeBuffer, index, returnTypeNodes);
        for (const auto returnTypeNode : returnTypeNodes)
        {
            auto returnType = typeCheckTypeNameNode(unit, returnTypeNode);
            // ownership rule: returning references or slices is not allowed
            if (returnType.isReference() || returnType.kind() == TypeKind::Slice)
            {
                m_diagnostics.addReferenceOrSliceReturnError(
                    unit.source,
                    TypeNameSpan(m_compilationContext, unit, returnTypeNode));
            }
            types.push_back(returnType);
        }
        return types;
    }

    i32 TypeChecker::convertToI32(const CompilationUnit& unit, u32 numberLiteralIndex)
    {
        const auto literalType = typeCheckNumberLiteral(unit, numberLiteralIndex, ExpressionContext{}).type;
        if (literalType == Type::Undefined() || literalType != m_module.wellKnown().i32)
        {
            return 0;
        }

        const auto parsedValue = tryParseNumberLiteralValue(nameOf(unit, numberLiteralIndex), literalType, m_module);
        if (!parsedValue.has_value())
        {
            m_diagnostics.addNumberLiteralOutOfRangeError(
                unit.source,
                nameLocation(unit, numberLiteralIndex),
                std::string(nameOf(unit, numberLiteralIndex)),
                formatTypeName(m_module, m_module.wellKnown().i32));
            return 0;
        }

        return std::get<i32>(parsedValue.value());
    }
}
