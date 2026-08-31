#include <Caracal/Debug/ParseTreePrinter.h>

#include <Caracal/Syntax/ArrayTypeNameNode.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/MethodDefinitionStatement.h>
#include <Caracal/Syntax/UnaryExpression.h>
#include <Caracal/Text/StringBuilder.h>

#include <string>
#include <vector>

namespace Caracal
{
    [[nodiscard]] static UnaryOperatorKind ConvertToUnaryOperator(TokenKind kind) noexcept
    {
        switch (kind)
        {
            case TokenKind::Bang:
            {
                return UnaryOperatorKind::LogicalNegation;
            }
            case TokenKind::Minus:
            {
                return UnaryOperatorKind::ValueNegation;
            }
            case TokenKind::RefKeyword:
            {
                return UnaryOperatorKind::ReferenceOf;
            }
            default:
            {
                return UnaryOperatorKind::Invalid;
            }
        }
    }

    [[nodiscard]] static BinaryOperatorKind ConvertToBinaryOperator(TokenKind kind) noexcept
    {
        switch (kind)
        {
            case TokenKind::Dot:
            {
                return BinaryOperatorKind::MemberAccess;
            }
            case TokenKind::PercentPlus:
            {
                return BinaryOperatorKind::AdditionWrapping;
            }
            case TokenKind::PercentMinus:
            {
                return BinaryOperatorKind::SubtractionWrapping;
            }
            case TokenKind::PercentStar:
            {
                return BinaryOperatorKind::MultiplicationWrapping;
            }
            case TokenKind::Slash:
            {
                return BinaryOperatorKind::Division;
            }
            case TokenKind::EqualEqual:
            {
                return BinaryOperatorKind::Equal;
            }
            case TokenKind::BangEqual:
            {
                return BinaryOperatorKind::NotEqual;
            }
            case TokenKind::LessThan:
            {
                return BinaryOperatorKind::LessThan;
            }
            case TokenKind::LessThanEqual:
            {
                return BinaryOperatorKind::LessOrEqual;
            }
            case TokenKind::GreaterThan:
            {
                return BinaryOperatorKind::GreaterThan;
            }
            case TokenKind::GreaterThanEqual:
            {
                return BinaryOperatorKind::GreaterOrEqual;
            }
            case TokenKind::AndKeyword:
            {
                return BinaryOperatorKind::LogicalAnd;
            }
            case TokenKind::OrKeyword:
            {
                return BinaryOperatorKind::LogicalOr;
            }
            default:
            {
                return BinaryOperatorKind::Invalid;
            }
        }
    }

    static void WriteIndentedTypeName(StringBuilder& builder, SemanticContext* module, Type type, std::string_view prefix = "Type: ")
    {
        builder.appendIndented(prefix);

        if (module == nullptr)
        {
            builder.appendLine("?");
            return;
        }

        if (type.isReference())
        {
            builder.append("ref ");
            type = type.toValue();
        }

        if (type.kind() == TypeKind::Enum)
        {
            const auto& enumDefinition = module->getEnumDefinition(type);
            auto& name = enumDefinition.name();
            auto baseType = enumDefinition.baseType();

            builder
                .append(name)
                .append("(")
                .append(module->getNameByType(baseType))
                .appendLine(")");
            return;
        }
        else if (type.kind() == TypeKind::Type)
        {
            const auto& typeDefinition = module->getTypeDefinition(type);
            auto& name = typeDefinition.name();

            builder
                .appendLine(name);
            return;
        }
        else if (type.kind() == TypeKind::Function || type.kind() == TypeKind::Method || type.kind() == TypeKind::Constructor)
        {
            const auto& functionDefinition = module->getFunctionDefinition(type);
            const auto& parameters = functionDefinition.parameters();
            const auto& returnTypes = functionDefinition.returnTypes();

            builder.append("(");

            for (size_t i = 0; i < parameters.size(); i++)
            {
                auto parameter = parameters[i].type();
                if (parameter.isReference())
                {
                    builder.append("ref ");
                }
                builder.append(module->getNameByType(parameter));
                if (i < parameters.size() - 1)
                    builder.append(", ");
            }

            builder.append(") -> ");
            if (returnTypes.empty())
            {
                builder.appendLine("void");
                return;
            }

            for (size_t i = 0; i < returnTypes.size(); i++)
            {
                auto returnType = returnTypes[i];
                builder.append(module->getNameByType(returnType));
                if (i < returnTypes.size() - 1)
                    builder.append(", ");
            }
            builder.appendLine("");
            return;
        }

        auto name = module->getNameByType(type);
        builder.appendLine(name);
    }

    class ParseTreePrinter
    {
    public:
        ParseTreePrinter(const NodeBuffer& buffer, const TokenBuffer& tokens, SemanticContext* module, u16 fileId)
            : m_buffer{ buffer }
            , m_tokens{ tokens }
            , m_module{ module }
            , m_fileId{ fileId }
            , m_builder{ 4 }
        {
        }

        [[nodiscard]] std::string run()
        {
            printStatementRange(0, m_buffer.nodeCount());
            return m_builder.toString();
        }

    private:
        [[nodiscard]] std::string_view lexeme(u32 tokenIndex) const
        {
            return m_tokens.getLexeme(m_tokens.getToken(static_cast<i32>(tokenIndex)));
        }

        // pushes the subtree roots in [begin, end) back to front, returns childrenStart
        // entries below stay stable while recursion pushes and pops above them
        [[nodiscard]] size_t collectChildren(u32 begin, u32 end)
        {
            const auto childrenStart = m_childrenStack.size();
            auto index = end;
            while (index > begin)
            {
                const auto root = index - 1;
                m_childrenStack.push_back(root);
                index -= m_buffer.subtreeSize(root);
            }

            return childrenStart;
        }

        [[nodiscard]] size_t collectChildrenOf(u32 index)
        {
            return collectChildren(index - m_buffer.subtreeSize(index) + 1, index);
        }

        // annotations are positional siblings before their statement
        void printStatementRange(u32 begin, u32 end)
        {
            const auto childrenStart = collectChildren(begin, end);
            for (auto position = m_childrenStack.size(); position > childrenStart; position--)
            {
                const auto child = m_childrenStack[position - 1];
                if (m_buffer.kind(child) == NodeKind::AnnotationNode)
                {
                    m_pendingAnnotations.push_back(child);
                }
                else
                {
                    printNode(child);
                    m_pendingAnnotations.clear();
                }
            }
            m_childrenStack.resize(childrenStart);
            m_pendingAnnotations.clear();
        }

        void printPendingAnnotations()
        {
            for (const auto annotation : m_pendingAnnotations)
            {
                printAnnotation(annotation);
            }
            m_pendingAnnotations.clear();
        }

        void printAnnotation(u32 index)
        {
            m_builder.appendIndentedLine("Annotation: {");
            m_builder.pushIndentation();

            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_buffer.mainToken(index)));

            if (m_buffer.annotationHasParentheses(m_tokens, index))
            {
                const auto childrenStart = collectChildrenOf(index);
                printArgumentList(childrenStart);
                m_childrenStack.resize(childrenStart);
            }

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printArgumentList(size_t childrenStart)
        {
            m_builder
                .appendIndented("ArgumentsNode(")
                .append(std::to_string(m_childrenStack.size() - childrenStart))
                .appendLine("): {");
            m_builder.pushIndentation();

            for (auto position = m_childrenStack.size(); position > childrenStart; position--)
            {
                const auto argument = m_childrenStack[position - 1];
                if (m_buffer.kind(argument) == NodeKind::NamedArgument)
                {
                    m_builder.appendIndentedLine("NamedArgument: {");
                    m_builder.pushIndentation();

                    m_builder
                        .appendIndented("Name: ")
                        .appendLine(lexeme(m_buffer.mainToken(argument)));
                    printNode(NodeLayout::namedArgumentValue(argument));
                    m_builder.popIndentation();
                    m_builder.appendIndentedLine("}");
                }
                else
                {
                    printNode(argument);
                }
            }

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        [[nodiscard]] Type typeOf(u32 index) const
        {
            if (m_module == nullptr)
            {
                return Type::Undefined();
            }

            return m_module->typeOfNode(NodeIndex{ m_fileId, index });
        }

        void printBracedNode(const char* label, u32 index)
        {
            m_builder
                .appendIndented(label)
                .appendLine(": {");
            m_builder.pushIndentation();
            printNode(index);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void appendArrayDisplayName(std::string& name, u32 index)
        {
            if (m_buffer.kind(index) != NodeKind::ArrayTypeNameNode)
            {
                name.append(lexeme(m_buffer.mainToken(index)));
                return;
            }

            const auto arrayTypeLayout = NodeLayout::arrayTypeName(m_buffer, index);

            name.push_back('[');
            appendArrayDisplayName(name, arrayTypeLayout.elementType);

            if (arrayTypeLayout.hasLength)
            {
                name.append("; ");
                name.append(lexeme(m_buffer.mainToken(arrayTypeLayout.lengthLiteral)));
            }
            else if (m_buffer.arrayKind(m_tokens, index) == ArrayTypeKind::Dynamic)
            {
                name.append("; _");
            }

            name.push_back(']');
        }

        void printTypeName(u32 index)
        {
            if (m_buffer.kind(index) == NodeKind::ArrayTypeNameNode)
            {
                printArrayTypeName(index);
                return;
            }

            m_builder
                .appendIndented(stringify(NodeKind::TypeNameNode))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            m_builder
                .appendIndented("Lexeme: ")
                .appendLine(lexeme(m_buffer.mainToken(index)));
            if (m_buffer.isReference(m_tokens, index))
            {
                m_builder.appendIndentedLine("Ref: true");
            }
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printArrayTypeName(u32 index)
        {
            const auto arrayTypeLayout = NodeLayout::arrayTypeName(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::ArrayTypeNameNode))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));

            std::string displayName;
            appendArrayDisplayName(displayName, index);
            m_builder
                .appendIndented("Lexeme: ")
                .appendLine(displayName);
            if (m_buffer.isReference(m_tokens, index))
            {
                m_builder.appendIndentedLine("Ref: true");
            }

            const auto arrayKind = m_buffer.arrayKind(m_tokens, index);
            if (arrayKind == ArrayTypeKind::Fixed)
            {
                m_builder.appendIndentedLine("Kind: Fixed");
            }
            else if (arrayKind == ArrayTypeKind::Dynamic)
            {
                m_builder.appendIndentedLine("Kind: Dynamic");
            }
            else
            {
                m_builder.appendIndentedLine("Kind: Slice");
            }

            m_builder.appendIndentedLine("DataType: {");
            m_builder.pushIndentation();
            printTypeName(arrayTypeLayout.elementType);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");

            if (arrayTypeLayout.hasLength)
            {
                m_builder.appendIndentedLine("Length: {");
                m_builder.pushIndentation();
                printNode(arrayTypeLayout.lengthLiteral);
                m_builder.popIndentation();
                m_builder.appendIndentedLine("}");
            }

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printParameter(u32 index)
        {
            const auto parameterLayout = NodeLayout::parameter(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::ParameterNode))
                .appendLine(": {");
            m_builder.pushIndentation();

            if (NodeLayout::isVariadicParameter(m_buffer, index))
            {
                m_builder
                    .appendIndented("Name: ")
                    .appendLine("varargs");
            }
            else
            {
                m_builder
                    .appendIndented("Name: ")
                    .appendLine(lexeme(m_buffer.mainToken(index)));
            }
            printTypeName(parameterLayout.typeName);
            if (parameterLayout.hasDefaultValue)
            {
                m_builder.appendIndentedLine("DefaultValue: {");
                m_builder.pushIndentation();
                printNode(parameterLayout.defaultValue);
                m_builder.popIndentation();
                m_builder.appendIndentedLine("}");
            }

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printParameters(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            m_builder
                .appendIndented(stringify(NodeKind::ParametersNode))
                .append("(")
                .append(std::to_string(m_childrenStack.size() - childrenStart))
                .appendLine("): {");
            m_builder.pushIndentation();

            for (auto position = m_childrenStack.size(); position > childrenStart; position--)
            {
                printParameter(m_childrenStack[position - 1]);
            }
            m_childrenStack.resize(childrenStart);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printReturnTypes(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            m_builder
                .appendIndented(stringify(NodeKind::ReturnTypesNode))
                .append("(")
                .append(std::to_string(m_childrenStack.size() - childrenStart))
                .appendLine("): {");
            m_builder.pushIndentation();

            for (auto position = m_childrenStack.size(); position > childrenStart; position--)
            {
                printTypeName(m_childrenStack[position - 1]);
            }
            m_childrenStack.resize(childrenStart);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printBlock(u32 index)
        {
            const auto begin = index - m_buffer.subtreeSize(index) + 1;

            // the block subtree doesnt include annotations
            const auto childrenStart = collectChildren(begin, index);
            u32 statementCount = 0;
            for (auto position = m_childrenStack.size(); position > childrenStart; position--)
            {
                if (m_buffer.kind(m_childrenStack[position - 1]) != NodeKind::AnnotationNode)
                {
                    statementCount++;
                }
            }
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::BlockNode))
                .append("(")
                .append(std::to_string(statementCount))
                .appendLine("): {");
            m_builder.pushIndentation();

            printStatementRange(begin, index);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printMethodName(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::MethodNameNode))
                .appendLine(": {");
            m_builder.pushIndentation();

            m_builder.appendIndented("MethodName: ");
            if (m_buffer.methodHasTypeName(m_tokens, index))
            {
                m_builder
                    .append(lexeme(m_buffer.methodTypeNameToken(m_tokens, index)))
                    .append(".");
            }
            m_builder.appendLine(lexeme(m_buffer.mainToken(index)));

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printConstantDeclaration(u32 index)
        {
            m_pendingAnnotations.clear();
            const auto declarationLayout = NodeLayout::declaration(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::ConstantDeclaration))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            printBracedNode("Left", declarationLayout.left);
            if (declarationLayout.hasExplicitType)
            {
                m_builder.appendIndentedLine("ExplicitType: {");
                m_builder.pushIndentation();
                printTypeName(declarationLayout.explicitType);
                m_builder.popIndentation();
                m_builder.appendIndentedLine("}");
            }
            printBracedNode("Right", declarationLayout.right);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printInitConstantDeclaration(u32 index)
        {
            m_pendingAnnotations.clear();
            const auto initLayout = NodeLayout::initConstant(m_buffer, index);

            // TODO rename once the older printer is gone
            m_builder.appendIndentedLine("ConstantDeclaration: {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            printBracedNode("Left", initLayout.left);
            m_builder.appendIndentedLine("InitType: {");
            m_builder.pushIndentation();
            printTypeName(initLayout.initType);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printVariableDeclaration(u32 index)
        {
            m_pendingAnnotations.clear();
            const auto declarationLayout = NodeLayout::declaration(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::VariableDeclaration))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            printBracedNode("Left", declarationLayout.left);
            if (declarationLayout.hasExplicitType)
            {
                m_builder.appendIndentedLine("ExplicitType: {");
                m_builder.pushIndentation();
                printTypeName(declarationLayout.explicitType);
                m_builder.popIndentation();
                m_builder.appendIndentedLine("}");
            }
            printBracedNode("Right", declarationLayout.right);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printTypeFieldDeclaration(u32 index)
        {
            const auto declarationLayout = NodeLayout::declaration(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::TypeFieldDeclaration))
                .appendLine(": {");
            m_builder.pushIndentation();

            m_builder
                .appendIndented("IsConstant: ")
                .appendLine(m_buffer.isConstantTypeField(m_tokens, index) ? "true" : "false");
            printBracedNode("Left", declarationLayout.left);
            if (declarationLayout.hasExplicitType)
            {
                m_builder.appendIndentedLine("ExplicitType: {");
                m_builder.pushIndentation();
                printTypeName(declarationLayout.explicitType);
                m_builder.popIndentation();
                m_builder.appendIndentedLine("}");
            }
            printBracedNode("Right", declarationLayout.right);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printExpressionStatement(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::ExpressionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();
            printNode(NodeLayout::statementExpression(index));
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printAssignmentStatement(u32 index)
        {
            const auto assignmentLayout = NodeLayout::assignment(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::AssignmentStatement))
                .appendLine(": {");
            m_builder.pushIndentation();
            printBracedNode("Left", assignmentLayout.target);
            printBracedNode("Right", assignmentLayout.value);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printFunctionDefinitionStatement(u32 index)
        {
            const auto functionLayout = NodeLayout::functionDefinition(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::FunctionDefinitionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_buffer.mainToken(index)));
            printPendingAnnotations();
            printParameters(functionLayout.parameters);
            printReturnTypes(functionLayout.returnTypes);
            printBlock(functionLayout.body);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printEnumDefinitionStatement(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            auto hasBaseType = false;
            auto baseTypeNode = 0u;
            if (childCount > 0)
            {
                baseTypeNode = m_childrenStack[m_childrenStack.size() - 1];
                hasBaseType = m_buffer.kind(baseTypeNode) == NodeKind::TypeNameNode;
            }
            u32 fieldCount = static_cast<u32>(childCount);
            if (hasBaseType)
            {
                fieldCount--;
            }

            const auto enumType = typeOf(index);

            m_builder
                .appendIndented(stringify(NodeKind::EnumDefinitionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, enumType);
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_buffer.mainToken(index)));
            printPendingAnnotations();

            if (hasBaseType)
            {
                printTypeName(baseTypeNode);
            }

            const EnumDefinition* enumDefinition = nullptr;
            if (enumType.kind() == TypeKind::Enum)
            {
                enumDefinition = &m_module->getEnumDefinition(enumType);
            }

            m_builder
                .appendIndented("FieldNodes(")
                .append(std::to_string(fieldCount))
                .appendLine("): {");
            m_builder.pushIndentation();
            auto endPosition = m_childrenStack.size();
            if (hasBaseType)
            {
                endPosition--;
            }

            size_t nextField = 0;
            for (auto position = endPosition; position > childrenStart; position--)
            {
                const auto fieldNode = m_childrenStack[position - 1];
                const EnumField* field = nullptr;
                if (enumDefinition != nullptr)
                {
                    const auto& fields = enumDefinition->fields();
                    const auto fieldName = lexeme(m_buffer.mainToken(fieldNode));
                    auto isDuplicate = false;
                    for (size_t consumed = 0; consumed < nextField; consumed++)
                    {
                        if (fields[consumed].name() == fieldName)
                        {
                            isDuplicate = true;
                            break;
                        }
                    }

                    if (!isDuplicate && nextField < fields.size() && fields[nextField].name() == fieldName)
                    {
                        field = &fields[nextField];
                        nextField++;
                    }
                }
                printEnumFieldDeclaration(fieldNode, field);
            }
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");

            m_childrenStack.resize(childrenStart);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printEnumFieldDeclaration(u32 index, const EnumField* field)
        {
            m_builder
                .appendIndented(stringify(NodeKind::EnumFieldDeclaration))
                .appendLine(": {");
            m_builder.pushIndentation();
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_buffer.mainToken(index)));
            const auto value = NodeLayout::optionalChild(m_buffer, index);
            if (value.hasChild)
            {
                printNode(value.child);
            }

            if (field != nullptr && field->hasConstantValue())
            {
                m_builder
                    .appendIndented("Value: ")
                    .appendLine(std::to_string(field->value()));
            }
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printTypeDefinitionStatement(u32 index)
        {
            const auto typeLayout = NodeLayout::typeDefinition(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::TypeDefinitionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_buffer.mainToken(index)));
            printPendingAnnotations();
            if (typeLayout.hasConstructorParameters)
            {
                printParameters(typeLayout.constructorParameters);
            }
            printBlock(typeLayout.body);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printMethodDefinitionStatement(u32 index)
        {
            const auto methodLayout = NodeLayout::methodDefinition(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::MethodDefinitionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));

            const auto methodNameToken = m_buffer.mainToken(methodLayout.methodName);
            const auto methodNameKind = m_tokens.getToken(static_cast<i32>(methodNameToken)).kind;
            const auto hasTypeName = m_buffer.methodHasTypeName(m_tokens, methodLayout.methodName);
            if (!hasTypeName && methodNameKind == TokenKind::NewKeyword)
            {
                m_builder
                    .appendIndented("SpecialFunctionType: ")
                    .appendLine(stringify(SpecialFunctionType::Constructor));
            }
            auto modifier = MethodModifier::Public;
            if (hasTypeName)
            {
                modifier = MethodModifier::Static;
            }
            else if (methodNameKind == TokenKind::PrivateIdentifier)
            {
                modifier = MethodModifier::Private;
            }
            m_builder
                .appendIndented("Modifier: ")
                .appendLine(stringify(modifier));
            printMethodName(methodLayout.methodName);

            printPendingAnnotations();
            printParameters(methodLayout.parameters);
            printReturnTypes(methodLayout.returnTypes);
            printBlock(methodLayout.body);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printIfStatement(u32 index)
        {
            const auto ifLayout = NodeLayout::ifStatement(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::IfStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            printBracedNode("Condition", ifLayout.condition);
            printBracedNode("True", ifLayout.trueStatement);
            if (ifLayout.hasFalse)
            {
                printBracedNode("False", ifLayout.falseStatement);
            }

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printTrailingIfStatement(u32 index)
        {
            const auto trailingIfLayout = NodeLayout::trailingIf(m_buffer, index);

            // TODO rename once the old parser is gone
            m_builder.appendIndentedLine("IfStatement: {");
            m_builder.pushIndentation();

            printBracedNode("Condition", trailingIfLayout.condition);
            printBracedNode("True", trailingIfLayout.statement);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printWhileStatement(u32 index)
        {
            const auto whileLayout = NodeLayout::whileStatement(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::WhileStatement))
                .appendLine(": {");
            m_builder.pushIndentation();
            printBracedNode("Condition", whileLayout.condition);
            printBracedNode("True", whileLayout.body);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printReturnStatement(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::ReturnStatement))
                .appendLine(": {");
            m_builder.pushIndentation();
            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            const auto value = NodeLayout::optionalChild(m_buffer, index);
            if (value.hasChild)
            {
                printNode(value.child);
            }
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printGroupingExpression(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::GroupingExpression))
                .appendLine(": {");
            m_builder.pushIndentation();
            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            printNode(NodeLayout::groupedExpression(index));
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printUnaryExpression(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::UnaryExpression))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            const auto operatorKind = ConvertToUnaryOperator(m_tokens.getToken(static_cast<i32>(m_buffer.mainToken(index))).kind);
            m_builder
                .appendIndented("Operation: ")
                .appendLine(stringify(operatorKind));
            m_builder.appendIndentedLine("Expression: {");
            m_builder.pushIndentation();
            printNode(NodeLayout::unaryOperand(index));
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printBinaryExpression(u32 index)
        {
            const auto binaryLayout = NodeLayout::binary(m_buffer, index);

            m_builder
                .appendIndented(stringify(NodeKind::BinaryExpression))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            printBracedNode("Left", binaryLayout.left);
            auto operatorKind = ConvertToBinaryOperator(m_tokens.getToken(static_cast<i32>(m_buffer.mainToken(index))).kind);
            
            if (operatorKind == BinaryOperatorKind::MemberAccess
                && m_buffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression
                && typeOf(NodeLayout::subtreeBegin(m_buffer, binaryLayout.right)).kind() == TypeKind::Constructor)
            {
                operatorKind = BinaryOperatorKind::ConstructorCall;
            }
            m_builder
                .appendIndented("Operation: ")
                .appendLine(stringify(operatorKind));
            printBracedNode("Right", binaryLayout.right);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printNameExpression(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::NameExpression))
                .appendLine(": {");
            m_builder.pushIndentation();
            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_buffer.mainToken(index)));
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printFunctionCallExpression(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto nameExpression = m_childrenStack[m_childrenStack.size() - 1];

            m_builder
                .appendIndented(stringify(NodeKind::FunctionCallExpression))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            WriteIndentedTypeName(m_builder, m_module, typeOf(nameExpression), "Function type: ");
            printNode(nameExpression);

            // remove the name expression from the stack
            m_childrenStack.pop_back();
            printArgumentList(childrenStart);
            m_childrenStack.resize(childrenStart);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printMemberAccessExpression(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::MemberAccessExpression))
                .appendLine(": {");
            m_builder.pushIndentation();
            printNode(NodeLayout::memberExpression(index));
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printNumberLiteral(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::NumberLiteral))
                .append(": ")
                .appendLine(lexeme(m_buffer.mainToken(index)));
            const auto suffix = NodeLayout::optionalChild(m_buffer, index);
            if (suffix.hasChild)
            {
                printTypeName(suffix.child);
            }
        }

        void printArrayLiteral(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto elementCount = m_childrenStack.size() - childrenStart;

            m_builder
                .appendIndented(stringify(NodeKind::ArrayLiteral))
                .append("(")
                .append(std::to_string(elementCount))
                .appendLine("): {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            if (m_buffer.isDynamicArrayLiteral(m_tokens, index))
            {
                m_builder.appendIndentedLine("Dynamic: true");
            }
            for (auto position = m_childrenStack.size(); position > childrenStart; position--)
            {
                printNode(m_childrenStack[position - 1]);
            }
            m_childrenStack.resize(childrenStart);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printNode(u32 index)
        {
            const auto kind = m_buffer.kind(index);
            switch (kind)
            {
                case NodeKind::ConstantDeclaration:
                {
                    printConstantDeclaration(index);
                    break;
                }
                case NodeKind::InitConstantDeclaration:
                {
                    printInitConstantDeclaration(index);
                    break;
                }
                case NodeKind::VariableDeclaration:
                {
                    printVariableDeclaration(index);
                    break;
                }
                case NodeKind::TypeFieldDeclaration:
                {
                    printTypeFieldDeclaration(index);
                    break;
                }
                case NodeKind::ExpressionStatement:
                {
                    printExpressionStatement(index);
                    break;
                }
                case NodeKind::AssignmentStatement:
                {
                    printAssignmentStatement(index);
                    break;
                }
                case NodeKind::FunctionDefinitionStatement:
                {
                    printFunctionDefinitionStatement(index);
                    break;
                }
                case NodeKind::EnumDefinitionStatement:
                {
                    printEnumDefinitionStatement(index);
                    break;
                }
                case NodeKind::EnumFieldDeclaration:
                {
                    printEnumFieldDeclaration(index, nullptr);
                    break;
                }
                case NodeKind::TypeDefinitionStatement:
                {
                    printTypeDefinitionStatement(index);
                    break;
                }
                case NodeKind::MethodDefinitionStatement:
                {
                    printMethodDefinitionStatement(index);
                    break;
                }
                case NodeKind::IfStatement:
                {
                    printIfStatement(index);
                    break;
                }
                case NodeKind::TrailingIfStatement:
                {
                    printTrailingIfStatement(index);
                    break;
                }
                case NodeKind::WhileStatement:
                {
                    printWhileStatement(index);
                    break;
                }
                case NodeKind::BreakStatement:
                case NodeKind::SkipStatement:
                {
                    m_builder.appendIndentedLine(stringify(kind));
                    break;
                }
                case NodeKind::ReturnStatement:
                {
                    printReturnStatement(index);
                    break;
                }
                case NodeKind::GroupingExpression:
                {
                    printGroupingExpression(index);
                    break;
                }
                case NodeKind::UnaryExpression:
                {
                    printUnaryExpression(index);
                    break;
                }
                case NodeKind::BinaryExpression:
                {
                    printBinaryExpression(index);
                    break;
                }
                case NodeKind::NameExpression:
                {
                    printNameExpression(index);
                    break;
                }
                case NodeKind::FunctionCallExpression:
                {
                    printFunctionCallExpression(index);
                    break;
                }
                case NodeKind::MemberAccessExpression:
                {
                    printMemberAccessExpression(index);
                    break;
                }
                case NodeKind::DiscardLiteral:
                {
                    m_builder.appendIndentedLine("Discard: _");
                    break;
                }
                case NodeKind::BoolLiteral:
                case NodeKind::StringLiteral:
                {
                    m_builder
                        .appendIndented(stringify(kind))
                        .append(": ")
                        .appendLine(lexeme(m_buffer.mainToken(index)));
                    break;
                }
                case NodeKind::NumberLiteral:
                {
                    printNumberLiteral(index);
                    break;
                }
                case NodeKind::TypeNameNode:
                case NodeKind::ArrayTypeNameNode:
                {
                    printTypeName(index);
                    break;
                }
                case NodeKind::ArrayLiteral:
                {
                    printArrayLiteral(index);
                    break;
                }
                case NodeKind::BlockNode:
                {
                    printBlock(index);
                    break;
                }
                default:
                {
                    m_builder.appendIndentedLine("Missing NodeKind!!");
                    break;
                }
            }
        }

        const NodeBuffer& m_buffer;
        const TokenBuffer& m_tokens;
        SemanticContext* m_module;
        u16 m_fileId;
        StringBuilder m_builder;
        std::vector<u32> m_childrenStack;
        std::vector<u32> m_pendingAnnotations;
    };

    std::string prettyPrint(const NodeBuffer& buffer, const TokenBuffer& tokens, SemanticContext* module, u16 fileId)
    {
        ParseTreePrinter printer{ buffer, tokens, module, fileId };
        return printer.run();
    }
}
