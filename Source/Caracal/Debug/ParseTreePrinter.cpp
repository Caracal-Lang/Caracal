#include <Caracal/Debug/ParseTreePrinter.h>

#include <Caracal/Syntax/ArrayTypeNameNode.h>
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
        ParseTreePrinter(const NodeBuffer& tree, const TokenBuffer& tokens, SemanticContext* module)
            : m_tree{ tree }
            , m_tokens{ tokens }
            , m_module{ module }
            , m_builder{ 4 }
        {
        }

        [[nodiscard]] std::string run()
        {
            printStatementRange(0, m_tree.nodeCount());
            return m_builder.toString();
        }

    private:
        [[nodiscard]] std::string_view lexeme(u32 tokenIndex) const
        {
            return m_tokens.getLexeme(m_tokens.getToken(static_cast<i32>(tokenIndex)));
        }

        // collects the subtree roots inside [begin, end) back to front onto the
        // scratch stack and returns the childrenStart, entries below stay stable while
        // recursion pushes and pops above them
        [[nodiscard]] size_t collectChildren(u32 begin, u32 end)
        {
            const auto childrenStart = m_childrenStack.size();
            auto index = end;
            while (index > begin)
            {
                const auto root = index - 1;
                m_childrenStack.push_back(root);
                index -= m_tree.subtreeSize(root);
            }

            return childrenStart;
        }

        [[nodiscard]] size_t collectChildrenOf(u32 index)
        {
            return collectChildren(index - m_tree.subtreeSize(index) + 1, index);
        }

        // annotations are positional siblings before their statement
        void printStatementRange(u32 begin, u32 end)
        {
            const auto childrenStart = collectChildren(begin, end);
            for (auto position = m_childrenStack.size(); position > childrenStart; position--)
            {
                const auto child = m_childrenStack[position - 1];
                if (m_tree.kind(child) == NodeKind::AnnotationNode)
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
                .appendLine(lexeme(m_tree.mainToken(index)));

            if (m_tree.annotationHasParentheses(m_tokens, index))
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
                if (m_tree.kind(argument) == NodeKind::NamedArgument)
                {
                    m_builder.appendIndentedLine("NamedArgument: {");
                    m_builder.pushIndentation();

                    m_builder
                        .appendIndented("Name: ")
                        .appendLine(lexeme(m_tree.mainToken(argument)));
                    printNode(argument - 1);
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
            switch (m_tree.kind(index))
            {
                case NodeKind::DiscardLiteral:
                {
                    return Type::Discard();
                }
                case NodeKind::GroupingExpression:
                case NodeKind::UnaryExpression:
                case NodeKind::MemberAccessExpression:
                case NodeKind::ConstantDeclaration:
                case NodeKind::VariableDeclaration:
                {
                    return typeOf(index - 1);
                }
                default:
                {
                    return Type::Undefined();
                }
            }
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
            if (m_tree.kind(index) != NodeKind::ArrayTypeNameNode)
            {
                name.append(lexeme(m_tree.mainToken(index)));
                return;
            }

            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            const auto elementType = m_childrenStack[m_childrenStack.size() - 1];
            u32 lengthLiteral = 0;
            auto hasLength = false;
            if (childCount == 2)
            {
                lengthLiteral = m_childrenStack[m_childrenStack.size() - 2];
                hasLength = m_tree.kind(lengthLiteral) == NodeKind::NumberLiteral;
            }
            m_childrenStack.resize(childrenStart);

            name.push_back('[');
            appendArrayDisplayName(name, elementType);

            if (hasLength)
            {
                name.append("; ");
                name.append(lexeme(m_tree.mainToken(lengthLiteral)));
            }
            else if (m_tree.arrayKind(m_tokens, index) == ArrayTypeKind::Dynamic)
            {
                name.append("; _");
            }

            name.push_back(']');
        }

        void printTypeName(u32 index)
        {
            if (m_tree.kind(index) == NodeKind::ArrayTypeNameNode)
            {
                printArrayTypeName(index);
                return;
            }

            m_builder
                .appendIndented(stringify(NodeKind::TypeNameNode))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());
            m_builder
                .appendIndented("Lexeme: ")
                .appendLine(lexeme(m_tree.mainToken(index)));
            if (m_tree.isReference(m_tokens, index))
            {
                m_builder.appendIndentedLine("Ref: true");
            }
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printArrayTypeName(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            const auto elementType = m_childrenStack[m_childrenStack.size() - 1];
            u32 lengthLiteral = 0;
            auto hasLength = false;
            if (childCount == 2)
            {
                lengthLiteral = m_childrenStack[m_childrenStack.size() - 2];
                hasLength = true;
            }
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::ArrayTypeNameNode))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());

            std::string displayName;
            appendArrayDisplayName(displayName, index);
            m_builder
                .appendIndented("Lexeme: ")
                .appendLine(displayName);
            if (m_tree.isReference(m_tokens, index))
            {
                m_builder.appendIndentedLine("Ref: true");
            }

            const auto arrayKind = m_tree.arrayKind(m_tokens, index);
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
            printTypeName(elementType);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");

            if (hasLength)
            {
                m_builder.appendIndentedLine("Length: {");
                m_builder.pushIndentation();
                printNode(lengthLiteral);
                m_builder.popIndentation();
                m_builder.appendIndentedLine("}");
            }

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printParameter(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            const auto typeName = m_childrenStack[m_childrenStack.size() - 1];
            u32 defaultValue = 0;
            auto hasDefault = false;
            if (childCount == 2)
            {
                defaultValue = m_childrenStack[m_childrenStack.size() - 2];
                hasDefault = true;
            }
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::ParameterNode))
                .appendLine(": {");
            m_builder.pushIndentation();

            if (m_tree.mainToken(index) == m_tree.mainToken(typeName)
                && m_tree.kind(typeName) == NodeKind::TypeNameNode)
            {
                m_builder
                    .appendIndented("Name: ")
                    .appendLine("varargs");
            }
            else
            {
                m_builder
                    .appendIndented("Name: ")
                    .appendLine(lexeme(m_tree.mainToken(index)));
            }
            printTypeName(typeName);
            if (hasDefault)
            {
                m_builder.appendIndentedLine("DefaultValue: {");
                m_builder.pushIndentation();
                printNode(defaultValue);
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
            const auto begin = index - m_tree.subtreeSize(index) + 1;

            // the block subtree doesnt include annotations
            const auto childrenStart = collectChildren(begin, index);
            u32 statementCount = 0;
            for (auto position = m_childrenStack.size(); position > childrenStart; position--)
            {
                if (m_tree.kind(m_childrenStack[position - 1]) != NodeKind::AnnotationNode)
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
            const auto mainToken = m_tree.mainToken(index);
            if (m_tree.methodHasTypeName(m_tokens, index))
            {
                m_builder
                    .append(lexeme(mainToken - 2))
                    .append(".");
            }
            m_builder.appendLine(lexeme(mainToken));

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printConstantDeclaration(u32 index)
        {
            m_pendingAnnotations.clear();
            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            const auto left = m_childrenStack[m_childrenStack.size() - 1];
            const auto right = m_childrenStack[childrenStart];
            u32 middle = 0;
            if (childCount == 3)
            {
                middle = m_childrenStack[m_childrenStack.size() - 2];
            }
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::ConstantDeclaration))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            printBracedNode("Left", left);
            if (childCount == 3)
            {
                m_builder.appendIndentedLine("ExplicitType: {");
                m_builder.pushIndentation();
                printTypeName(middle);
                m_builder.popIndentation();
                m_builder.appendIndentedLine("}");
            }
            printBracedNode("Right", right);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printInitConstantDeclaration(u32 index)
        {
            m_pendingAnnotations.clear();
            const auto childrenStart = collectChildrenOf(index);
            const auto left = m_childrenStack[m_childrenStack.size() - 1];
            const auto initType = m_childrenStack[childrenStart];
            m_childrenStack.resize(childrenStart);

            // TODO rename once the older printer is gone
            m_builder.appendIndentedLine("ConstantDeclaration: {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            printBracedNode("Left", left);
            m_builder.appendIndentedLine("InitType: {");
            m_builder.pushIndentation();
            printTypeName(initType);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printVariableDeclaration(u32 index)
        {
            m_pendingAnnotations.clear();
            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            const auto left = m_childrenStack[m_childrenStack.size() - 1];
            const auto right = m_childrenStack[childrenStart];
            u32 middle = 0;
            if (childCount == 3)
            {
                middle = m_childrenStack[m_childrenStack.size() - 2];
            }
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::VariableDeclaration))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, typeOf(index));
            printBracedNode("Left", left);
            if (childCount == 3)
            {
                m_builder.appendIndentedLine("ExplicitType: {");
                m_builder.pushIndentation();
                printTypeName(middle);
                m_builder.popIndentation();
                m_builder.appendIndentedLine("}");
            }
            printBracedNode("Right", right);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printTypeFieldDeclaration(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            const auto left = m_childrenStack[m_childrenStack.size() - 1];
            const auto right = m_childrenStack[childrenStart];
            u32 middle = 0;
            if (childCount == 3)
            {
                middle = m_childrenStack[m_childrenStack.size() - 2];
            }
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::TypeFieldDeclaration))
                .appendLine(": {");
            m_builder.pushIndentation();

            m_builder
                .appendIndented("IsConstant: ")
                .appendLine(m_tree.isConstantTypeField(m_tokens, index) ? "true" : "false");
            printBracedNode("Left", left);
            if (childCount == 3)
            {
                m_builder.appendIndentedLine("ExplicitType: {");
                m_builder.pushIndentation();
                printTypeName(middle);
                m_builder.popIndentation();
                m_builder.appendIndentedLine("}");
            }
            printBracedNode("Right", right);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printExpressionStatement(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::ExpressionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();
            printNode(index - 1);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printAssignmentStatement(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto left = m_childrenStack[m_childrenStack.size() - 1];
            const auto right = m_childrenStack[childrenStart];
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::AssignmentStatement))
                .appendLine(": {");
            m_builder.pushIndentation();
            printBracedNode("Left", left);
            printBracedNode("Right", right);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printFunctionDefinitionStatement(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto parameters = m_childrenStack[m_childrenStack.size() - 1];
            const auto returnTypes = m_childrenStack[m_childrenStack.size() - 2];
            const auto body = m_childrenStack[m_childrenStack.size() - 3];
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::FunctionDefinitionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_tree.mainToken(index)));
            printPendingAnnotations();
            printParameters(parameters);
            printReturnTypes(returnTypes);
            printBlock(body);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printEnumDefinitionStatement(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            const auto first = m_childrenStack[m_childrenStack.size() - 1];
            const auto hasBaseType = m_tree.kind(first) == NodeKind::TypeNameNode;
            u32 fieldCount = static_cast<u32>(childCount);
            if (hasBaseType)
            {
                fieldCount--;
            }

            m_builder
                .appendIndented(stringify(NodeKind::EnumDefinitionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_tree.mainToken(index)));
            printPendingAnnotations();

            if (hasBaseType)
            {
                printTypeName(first);
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
            for (auto position = endPosition; position > childrenStart; position--)
            {
                printNode(m_childrenStack[position - 1]);
            }
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");

            m_childrenStack.resize(childrenStart);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printEnumFieldDeclaration(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::EnumFieldDeclaration))
                .appendLine(": {");
            m_builder.pushIndentation();
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_tree.mainToken(index)));
            if (m_tree.subtreeSize(index) > 1)
            {
                printNode(index - 1);
            }
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printTypeDefinitionStatement(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            const auto body = m_childrenStack[childrenStart];
            u32 constructorParameters = 0;
            auto hasConstructorParameters = false;
            if (childCount == 2)
            {
                constructorParameters = m_childrenStack[m_childrenStack.size() - 1];
                hasConstructorParameters = true;
            }
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::TypeDefinitionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_tree.mainToken(index)));
            printPendingAnnotations();
            if (hasConstructorParameters)
            {
                printParameters(constructorParameters);
            }
            printBlock(body);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printMethodDefinitionStatement(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto methodName = m_childrenStack[m_childrenStack.size() - 1];
            const auto parameters = m_childrenStack[m_childrenStack.size() - 2];
            const auto returnTypes = m_childrenStack[m_childrenStack.size() - 3];
            const auto body = m_childrenStack[m_childrenStack.size() - 4];
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::MethodDefinitionStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());

            const auto methodNameToken = m_tree.mainToken(methodName);
            const auto methodNameKind = m_tokens.getToken(static_cast<i32>(methodNameToken)).kind;
            const auto hasTypeName = m_tree.methodHasTypeName(m_tokens, methodName);
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
            printMethodName(methodName);

            printPendingAnnotations();
            printParameters(parameters);
            printReturnTypes(returnTypes);
            printBlock(body);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printIfStatement(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto childCount = m_childrenStack.size() - childrenStart;
            const auto condition = m_childrenStack[m_childrenStack.size() - 1];
            const auto trueStatement = m_childrenStack[m_childrenStack.size() - 2];
            u32 falseStatement = 0;
            auto hasFalse = false;
            if (childCount == 3)
            {
                falseStatement = m_childrenStack[childrenStart];
                hasFalse = true;
            }
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::IfStatement))
                .appendLine(": {");
            m_builder.pushIndentation();

            printBracedNode("Condition", condition);
            printBracedNode("True", trueStatement);
            if (hasFalse)
            {
                printBracedNode("False", falseStatement);
            }

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printTrailingIfStatement(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto trueStatement = m_childrenStack[m_childrenStack.size() - 1];
            const auto condition = m_childrenStack[childrenStart];
            m_childrenStack.resize(childrenStart);

            // TODO rename once the old parser is gone
            m_builder.appendIndentedLine("IfStatement: {");
            m_builder.pushIndentation();

            printBracedNode("Condition", condition);
            printBracedNode("True", trueStatement);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printWhileStatement(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto condition = m_childrenStack[m_childrenStack.size() - 1];
            const auto trueStatement = m_childrenStack[childrenStart];
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::WhileStatement))
                .appendLine(": {");
            m_builder.pushIndentation();
            printBracedNode("Condition", condition);
            printBracedNode("True", trueStatement);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printReturnStatement(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::ReturnStatement))
                .appendLine(": {");
            m_builder.pushIndentation();
            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());
            if (m_tree.subtreeSize(index) > 1)
            {
                printNode(index - 1);
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
            printNode(index - 1);
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
            const auto operatorKind = ConvertToUnaryOperator(m_tokens.getToken(static_cast<i32>(m_tree.mainToken(index))).kind);
            m_builder
                .appendIndented("Operation: ")
                .appendLine(stringify(operatorKind));
            m_builder.appendIndentedLine("Expression: {");
            m_builder.pushIndentation();
            printNode(index - 1);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printBinaryExpression(u32 index)
        {
            const auto childrenStart = collectChildrenOf(index);
            const auto left = m_childrenStack[m_childrenStack.size() - 1];
            const auto right = m_childrenStack[childrenStart];
            m_childrenStack.resize(childrenStart);

            m_builder
                .appendIndented(stringify(NodeKind::BinaryExpression))
                .appendLine(": {");
            m_builder.pushIndentation();

            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());
            printBracedNode("Left", left);
            const auto operatorKind = ConvertToBinaryOperator(m_tokens.getToken(static_cast<i32>(m_tree.mainToken(index))).kind);
            m_builder
                .appendIndented("Operation: ")
                .appendLine(stringify(operatorKind));
            printBracedNode("Right", right);

            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printNameExpression(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::NameExpression))
                .appendLine(": {");
            m_builder.pushIndentation();
            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());
            m_builder
                .appendIndented("Name: ")
                .appendLine(lexeme(m_tree.mainToken(index)));
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

            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());
            WriteIndentedTypeName(m_builder, m_module, Type::Undefined(), "Function type: ");
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
            printNode(index - 1);
            m_builder.popIndentation();
            m_builder.appendIndentedLine("}");
        }

        void printNumberLiteral(u32 index)
        {
            m_builder
                .appendIndented(stringify(NodeKind::NumberLiteral))
                .append(": ")
                .appendLine(lexeme(m_tree.mainToken(index)));
            if (m_tree.subtreeSize(index) > 1)
            {
                printTypeName(index - 1);
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

            WriteIndentedTypeName(m_builder, m_module, Type::Undefined());
            if (m_tree.isDynamicArrayLiteral(m_tokens, index))
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
            const auto kind = m_tree.kind(index);
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
                    printEnumFieldDeclaration(index);
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
                        .appendLine(lexeme(m_tree.mainToken(index)));
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

        const NodeBuffer& m_tree;
        const TokenBuffer& m_tokens;
        SemanticContext* m_module;
        StringBuilder m_builder;
        std::vector<u32> m_childrenStack;
        std::vector<u32> m_pendingAnnotations;
    };

    std::string prettyPrint(const NodeBuffer& buffer, const TokenBuffer& tokens, SemanticContext* module)
    {
        ParseTreePrinter printer{ buffer, tokens, module };
        return printer.run();
    }
}
