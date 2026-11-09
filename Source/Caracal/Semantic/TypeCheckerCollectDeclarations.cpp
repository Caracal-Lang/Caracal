#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Constants.h>
#include <Caracal/Profiling.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/TokenBuffer.h>
#include <Caracal/Text/AsciiName.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Caracal
{
    // __caracal_test_<file name>_<test name>
    [[nodiscard]] static std::string TestFunctionBaseName(const std::filesystem::path& filePath, std::string_view testName)
    {
        auto name = std::string(TestFunctionNamePrefix);
        const auto stem = filePath.stem().u8string();
        name.append(stem.begin(), stem.end());
        if (!stem.empty() && !testName.empty())
        {
            name.push_back('_');
        }
        name += testName;
        replaceNonAsciiCharacters(name);
        return name;
    }

    void TypeChecker::collectDeclarations()
    {
        CARACAL_ZONE_NAMED("check.collectDeclarations");
        for (auto* unit : m_units)
        {
            collectUnitDeclarations(*unit);
        }
    }

    void TypeChecker::collectUnitDeclarations(CompilationUnit& unit)
    {
        m_pendingAnnotations.clear();
        for (const auto index : collectRootStatements(unit.nodeBuffer))
        {
            const auto kind = unit.nodeBuffer.kind(index);
            if (kind == NodeKind::AnnotationNode)
            {
                m_pendingAnnotations.push_back(index);
                continue;
            }

            switch (kind)
            {
                case NodeKind::ConstantDeclaration:
                case NodeKind::InitConstantDeclaration:
                {
                    m_globalConstantDeclarations.push_back(NodeIndex{ unit.fileId, index });
                    break;
                }
                case NodeKind::VariableDeclaration:
                {
                    for (const auto annotation : m_pendingAnnotations)
                    {
                        m_diagnostics.addUnexpectedAnnotationTargetError(
                            unit.source,
                            annotationLocation(unit, annotation));
                    }
                    break;
                }
                case NodeKind::EnumDefinitionStatement:
                {
                    collectEnumDeclaration(unit, index);
                    break;
                }
                case NodeKind::TypeDefinitionStatement:
                {
                    collectTypeDeclaration(unit, index);
                    break;
                }
                case NodeKind::FunctionDefinitionStatement:
                {
                    collectFunctionDeclaration(unit, index);
                    break;
                }
                case NodeKind::TestDefinitionStatement:
                {
                    collectTestDeclaration(unit, index);
                    break;
                }
                default:
                {
                    break;
                }
            }

            m_pendingAnnotations.clear();
        }
    }

    void TypeChecker::collectFunctionDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto functionName = nameOf(unit, index);
        const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, index);
        auto parameters = collectParameterNames(unit, functionLayout.parameters);
        auto& functionDefinition = m_module.createFunction(functionName, std::move(parameters), std::vector<Type>());
        functionDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, functionDefinition.type());
        m_functionDeclarations.push_back(NodeIndex{ unit.fileId, index });

        if (functionName == TestsStubName && m_testsStubDeclaration.index == InvalidNodeIndex)
        {
            m_testsStubDeclaration = NodeIndex{ unit.fileId, index };
        }
    }

    void TypeChecker::collectTestDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto baseName = TestFunctionBaseName(unit.source->filePath, escapedStringContent(unit, index));

        // we need to append a number in case the name is already in use
        auto functionName = baseName;
        for (auto suffix = 1; m_module.tryGetOverloadSet(functionName) != nullptr; ++suffix)
        {
            functionName = baseName + "_" + std::to_string(suffix);
        }

        auto& functionDefinition = m_module.createFunction(functionName, std::vector<Parameter>(), std::vector<Type>());
        functionDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, functionDefinition.type());
        m_testDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    void TypeChecker::reportDuplicateTypeDeclaration(const CompilationUnit& unit, u32 index, std::string_view name, Type existingType)
    {
        auto otherSource = SourceTextSharedPtr{};
        auto otherLocation = std::optional<SourceLocation>{};
        auto otherDefinitionNode = NodeIndex{};
        if (existingType.kind() == TypeKind::Enum)
        {
            otherDefinitionNode = m_module.getEnumDefinition(existingType).definitionNode();
        }
        else if (existingType.kind() == TypeKind::Type)
        {
            otherDefinitionNode = m_module.getTypeDefinition(existingType).definitionNode();
        }
        locateDefinitionName(
            otherDefinitionNode,
            otherSource,
            otherLocation);

        m_diagnostics.addDuplicateTypeDeclarationError(
            unit.source,
            nameLocation(unit, index),
            std::string(name),
            otherSource,
            otherLocation);
    }

    void TypeChecker::collectEnumDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto enumName = nameOf(unit, index);
        const auto existingType = m_module.tryGetTypeByName(enumName);
        if (existingType != Type::Undefined())
        {
            reportDuplicateTypeDeclaration(unit, index, enumName, existingType);
            return;
        }

        auto& enumDefinition = m_module.createEnum(enumName);
        enumDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, enumDefinition.type());
        m_enumDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    void TypeChecker::collectBuiltinTypeDeclaration(const CompilationUnit& unit, u32 index)
    {
        const auto typeName = nameOf(unit, index);
        const auto builtinType = m_module.tryGetTypeByName(typeName);
        if (builtinType == Type::Undefined())
        {
            const auto description = extractPendingBuiltinTypeDescription(unit);
            auto& typeDefinition = m_module.createBuiltinTypeFromDescription(typeName, description);
            typeDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
            m_module.setNodeType(NodeIndex{ unit.fileId, index }, typeDefinition.type());
            m_typeDeclarations.push_back(NodeIndex{ unit.fileId, index });
            return;
        }

        if (builtinType.kind() != TypeKind::Builtin || builtinType.id() < 0)
        {
            m_diagnostics.addNotABuiltinTypeError(
                unit.source,
                nameLocation(unit, index),
                std::string(typeName));
            return;
        }

        const auto& existingDefinition = m_module.getTypeDefinition(builtinType);
        if (existingDefinition.type() != Type::Undefined())
        {
            auto otherSource = SourceTextSharedPtr{};
            auto otherLocation = std::optional<SourceLocation>{};
            locateDefinitionName(
                existingDefinition.definitionNode(),
                otherSource,
                otherLocation);

            m_diagnostics.addDuplicateBuiltinTypeBindingError(
                unit.source,
                nameLocation(unit, index),
                std::string(typeName),
                otherLocation);
            return;
        }

        auto& typeDefinition = m_module.bindBuiltinTypeDefinition(builtinType);
        typeDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, builtinType);
        m_typeDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    void TypeChecker::collectTypeDeclaration(const CompilationUnit& unit, u32 index)
    {
        if (pendingAnnotationsContain(unit, BuiltinAnnotationName))
        {
            collectBuiltinTypeDeclaration(unit, index);
            return;
        }

        const auto typeName = nameOf(unit, index);
        const auto existingType = m_module.tryGetTypeByName(typeName);
        if (existingType != Type::Undefined())
        {
            reportDuplicateTypeDeclaration(unit, index, typeName, existingType);
            return;
        }

        auto& typeDefinition = m_module.createType(typeName);
        typeDefinition.setDefinitionNode(NodeIndex{ unit.fileId, index });
        m_module.setNodeType(NodeIndex{ unit.fileId, index }, typeDefinition.type());
        m_typeDeclarations.push_back(NodeIndex{ unit.fileId, index });
    }

    std::vector<u32> TypeChecker::collectRootStatements(const NodeBuffer& buffer) const
    {
        std::vector<u32> roots;
        auto index = buffer.nodeCount();
        while (index > 0)
        {
            const auto root = index - 1;
            roots.push_back(root);
            index -= buffer.subtreeSize(root);
        }

        std::reverse(roots.begin(), roots.end());
        return roots;
    }

    std::vector<Parameter> TypeChecker::collectParameterNames(const CompilationUnit& unit, u32 parametersNode)
    {
        std::vector<Parameter> parameters;
        NodeLayout::collectChildren(unit.nodeBuffer, parametersNode, m_childrenScratch);
        for (const auto parameterIndex : m_childrenScratch)
        {
            if (NodeLayout::isVariadicParameter(unit.nodeBuffer, parameterIndex))
            {
                parameters.emplace_back("varargs", Type::Undefined());
            }
            else
            {
                parameters.emplace_back(nameOf(unit, parameterIndex), Type::Undefined());
            }
        }

        return parameters;
    }

    BuiltinTypeDescription TypeChecker::extractPendingBuiltinTypeDescription(const CompilationUnit& unit) const
    {
        BuiltinTypeDescription description{};
        const auto& buffer = unit.nodeBuffer;
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        std::vector<u32> arguments;
        for (const auto annotationIndex : m_pendingAnnotations)
        {
            if (nameOf(unit, annotationIndex) != BuiltinAnnotationName)
            {
                continue;
            }

            NodeLayout::collectChildren(buffer, annotationIndex, arguments);
            for (const auto argumentIndex : arguments)
            {
                if (buffer.kind(argumentIndex) != NodeKind::NamedArgument)
                {
                    continue;
                }

                const auto argumentName = nameOf(unit, argumentIndex);
                const auto valueIndex = NodeLayout::namedArgumentValue(argumentIndex);
                if (argumentName == KindAnnotationArgumentName && buffer.kind(valueIndex) == NodeKind::NameExpression)
                {
                    const auto kindName = nameOf(unit, valueIndex);
                    if (kindName == BuiltinKindFloatName)
                    {
                        description.kind = BuiltinTypeKind::Float;
                    }
                    else if (kindName == BuiltinKindBoolName)
                    {
                        description.kind = BuiltinTypeKind::Bool;
                    }
                    else if (kindName == BuiltinKindPointerName)
                    {
                        description.kind = BuiltinTypeKind::Pointer;
                    }
                }
                else if (argumentName == BitsAnnotationArgumentName && buffer.kind(valueIndex) == NodeKind::NumberLiteral)
                {
                    const auto parsed = tryParseI32Literal(nameOf(unit, valueIndex));
                    if (parsed.has_value())
                    {
                        description.bits = parsed.value();
                    }
                }
                else if (argumentName == SignedAnnotationArgumentName && buffer.kind(valueIndex) == NodeKind::BoolLiteral)
                {
                    description.isSigned = tokens.getToken(static_cast<i32>(buffer.mainToken(valueIndex))).kind == TokenKind::TrueKeyword;
                }
            }
        }

        return description;
    }

    void TypeChecker::resolveWellKnownTypes()
    {
        m_module.refreshWellKnownTypes();
        m_defaultIntegerType = m_module.tryGetTypeByName(m_options.defaultIntegerType);
        m_defaultFloatingType = m_module.tryGetTypeByName(m_options.defaultFloatingType);
        m_defaultEnumBaseType = m_module.tryGetTypeByName(m_options.defaultEnumBaseType);
    }
}
