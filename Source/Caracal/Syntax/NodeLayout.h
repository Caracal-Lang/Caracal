#pragma once

#include <Caracal/API.h>
#include <Caracal/Defines.h>
#include <Caracal/Syntax/NodeBuffer.h>
#include <Caracal/Syntax/TokenBuffer.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace Caracal
{
    // children are the sibling subtrees before a node, last child at index - 1
    // children before that move backwards by subtree size
    namespace NodeLayout
    {
        [[nodiscard]] inline u32 subtreeBegin(const NodeBuffer& buffer, u32 index)
        {
            return index - buffer.subtreeSize(index) + 1;
        }

        [[nodiscard]] inline u32 lastChild(u32 index)
        {
            return index - 1;
        }

        [[nodiscard]] inline u32 previousSibling(const NodeBuffer& buffer, u32 child)
        {
            return child - buffer.subtreeSize(child);
        }

        // appends the children in source order
        inline void appendChildren(const NodeBuffer& buffer, u32 index, std::vector<u32>& out)
        {
            const auto first = out.size();
            const auto begin = index - buffer.subtreeSize(index) + 1;
            auto position = index;
            while (position > begin)
            {
                const auto child = position - 1;
                out.push_back(child);
                position = child - buffer.subtreeSize(child) + 1;
            }

            std::reverse(out.begin() + first, out.end());
        }

        inline void collectChildren(const NodeBuffer& buffer, u32 index, std::vector<u32>& out)
        {
            out.clear();
            appendChildren(buffer, index, out);
        }

        [[nodiscard]] inline u32 onlyChild(u32 index)
        {
            return index - 1;
        }

        [[nodiscard]] inline u32 statementExpression(u32 index)
        {
            return onlyChild(index);
        }

        [[nodiscard]] inline u32 groupedExpression(u32 index)
        {
            return onlyChild(index);
        }

        [[nodiscard]] inline u32 unaryOperand(u32 index)
        {
            return onlyChild(index);
        }

        [[nodiscard]] inline u32 memberExpression(u32 index)
        {
            return onlyChild(index);
        }

        [[nodiscard]] inline u32 namedArgumentValue(u32 index)
        {
            return onlyChild(index);
        }

        struct OptionalChildNodeLayout
        {
            u32 child;
            bool hasChild;
        };

        [[nodiscard]] inline OptionalChildNodeLayout optionalChild(const NodeBuffer& buffer, u32 index)
        {
            OptionalChildNodeLayout layout{};
            layout.hasChild = buffer.subtreeSize(index) > 1;
            if (layout.hasChild)
            {
                layout.child = lastChild(index);
            }

            return layout;
        }

        struct BinaryNodeLayout
        {
            u32 left;
            u32 right;
        };

        [[nodiscard]] inline BinaryNodeLayout binary(const NodeBuffer& buffer, u32 index)
        {
            BinaryNodeLayout layout{};
            layout.right = lastChild(index);
            layout.left = previousSibling(buffer, layout.right);
            return layout;
        }

        struct AssignmentNodeLayout
        {
            u32 target;
            u32 value;
        };

        [[nodiscard]] inline AssignmentNodeLayout assignment(const NodeBuffer& buffer, u32 index)
        {
            AssignmentNodeLayout layout{};
            layout.value = lastChild(index);
            layout.target = previousSibling(buffer, layout.value);
            return layout;
        }

        // ConstantDeclaration, VariableDeclaration and TypeFieldDeclaration:
        struct DeclarationNodeLayout
        {
            u32 left;
            u32 explicitType;
            u32 right;
            bool hasExplicitType;
        };

        [[nodiscard]] inline DeclarationNodeLayout declaration(const NodeBuffer& buffer, u32 index)
        {
            DeclarationNodeLayout layout{};
            const auto begin = subtreeBegin(buffer, index);
            layout.right = lastChild(index);
            const auto second = previousSibling(buffer, layout.right);
            if (subtreeBegin(buffer, second) == begin)
            {
                layout.left = second;
            }
            else
            {
                layout.hasExplicitType = true;
                layout.explicitType = second;
                layout.left = previousSibling(buffer, second);
            }

            return layout;
        }

        struct InitConstantNodeLayout
        {
            u32 left;
            u32 initType;
        };

        [[nodiscard]] inline InitConstantNodeLayout initConstant(const NodeBuffer& buffer, u32 index)
        {
            InitConstantNodeLayout layout{};
            layout.initType = lastChild(index);
            layout.left = previousSibling(buffer, layout.initType);
            return layout;
        }

        struct IfNodeLayout
        {
            u32 condition;
            u32 trueStatement;
            u32 falseStatement;
            bool hasFalse;
        };

        [[nodiscard]] inline IfNodeLayout ifStatement(const NodeBuffer& buffer, u32 index)
        {
            IfNodeLayout layout{};
            const auto begin = subtreeBegin(buffer, index);
            const auto last = lastChild(index);
            const auto second = previousSibling(buffer, last);
            if (subtreeBegin(buffer, second) == begin)
            {
                layout.condition = second;
                layout.trueStatement = last;
            }
            else
            {
                layout.hasFalse = true;
                layout.falseStatement = last;
                layout.trueStatement = second;
                layout.condition = previousSibling(buffer, second);
            }

            return layout;
        }

        struct TrailingIfNodeLayout
        {
            u32 statement;
            u32 condition;
        };

        [[nodiscard]] inline TrailingIfNodeLayout trailingIf(const NodeBuffer& buffer, u32 index)
        {
            TrailingIfNodeLayout layout{};
            layout.condition = lastChild(index);
            layout.statement = previousSibling(buffer, layout.condition);
            return layout;
        }

        struct WhileNodeLayout
        {
            u32 condition;
            u32 body;
        };

        [[nodiscard]] inline WhileNodeLayout whileStatement(const NodeBuffer& buffer, u32 index)
        {
            WhileNodeLayout layout{};
            layout.body = lastChild(index);
            layout.condition = previousSibling(buffer, layout.body);
            return layout;
        }

        struct FunctionDefinitionNodeLayout
        {
            u32 parameters;
            u32 returnTypes;
            u32 body;
        };

        [[nodiscard]] inline FunctionDefinitionNodeLayout functionDefinition(const NodeBuffer& buffer, u32 index)
        {
            FunctionDefinitionNodeLayout layout{};
            layout.body = lastChild(index);
            layout.returnTypes = previousSibling(buffer, layout.body);
            layout.parameters = previousSibling(buffer, layout.returnTypes);
            return layout;
        }

        struct MethodDefinitionNodeLayout
        {
            u32 methodName;
            u32 parameters;
            u32 returnTypes;
            u32 body;
        };

        [[nodiscard]] inline MethodDefinitionNodeLayout methodDefinition(const NodeBuffer& buffer, u32 index)
        {
            MethodDefinitionNodeLayout layout{};
            layout.body = lastChild(index);
            layout.returnTypes = previousSibling(buffer, layout.body);
            layout.parameters = previousSibling(buffer, layout.returnTypes);
            layout.methodName = previousSibling(buffer, layout.parameters);
            return layout;
        }

        struct TypeDefinitionNodeLayout
        {
            u32 constructorParameters;
            u32 body;
            bool hasConstructorParameters;
        };

        [[nodiscard]] inline TypeDefinitionNodeLayout typeDefinition(const NodeBuffer& buffer, u32 index)
        {
            TypeDefinitionNodeLayout layout{};
            const auto begin = subtreeBegin(buffer, index);
            layout.body = lastChild(index);
            if (subtreeBegin(buffer, layout.body) != begin)
            {
                layout.hasConstructorParameters = true;
                layout.constructorParameters = previousSibling(buffer, layout.body);
            }

            return layout;
        }

        struct ParameterNodeLayout
        {
            u32 typeName;
            u32 defaultValue;
            bool hasDefaultValue;
        };

        [[nodiscard]] inline ParameterNodeLayout parameter(const NodeBuffer& buffer, u32 index)
        {
            ParameterNodeLayout layout{};
            const auto begin = subtreeBegin(buffer, index);
            const auto last = lastChild(index);
            if (subtreeBegin(buffer, last) == begin)
            {
                layout.typeName = last;
            }
            else
            {
                layout.hasDefaultValue = true;
                layout.defaultValue = last;
                layout.typeName = previousSibling(buffer, last);
            }

            return layout;
        }

        struct ArrayTypeNameNodeLayout
        {
            u32 elementType;
            u32 lengthLiteral;
            bool hasLength;
        };

        [[nodiscard]] inline ArrayTypeNameNodeLayout arrayTypeName(const NodeBuffer& buffer, u32 index)
        {
            ArrayTypeNameNodeLayout layout{};
            const auto begin = subtreeBegin(buffer, index);
            const auto last = lastChild(index);
            if (subtreeBegin(buffer, last) == begin)
            {
                layout.elementType = last;
            }
            else
            {
                layout.hasLength = true;
                layout.lengthLiteral = last;
                layout.elementType = previousSibling(buffer, last);
            }

            return layout;
        }

        [[nodiscard]] inline u32 stripGroupings(const NodeBuffer& buffer, u32 index) noexcept
        {
            auto stripped = index;
            while (buffer.kind(stripped) == NodeKind::GroupingExpression)
            {
                stripped = onlyChild(stripped);
            }

            return stripped;
        }

        [[nodiscard]] inline u32 firstChild(const NodeBuffer& buffer, u32 index)
        {
            const auto begin = subtreeBegin(buffer, index);
            auto child = lastChild(index);
            while (subtreeBegin(buffer, child) > begin)
            {
                child = previousSibling(buffer, child);
            }
            return child;
        }

        [[nodiscard]] inline bool isVariadicParameter(const NodeBuffer& buffer, u32 parameterIndex)
        {
            const auto layout = parameter(buffer, parameterIndex);
            return buffer.mainToken(parameterIndex) == buffer.mainToken(layout.typeName)
                && buffer.kind(layout.typeName) == NodeKind::TypeNameNode;
        }

        [[nodiscard]] inline bool isNamedArgument(const NodeBuffer& buffer, u32 argumentNode) noexcept
        {
            return buffer.kind(argumentNode) == NodeKind::NamedArgument;
        }

        [[nodiscard]] inline u32 argumentValue(const NodeBuffer& buffer, u32 argumentNode) noexcept
        {
            if (isNamedArgument(buffer, argumentNode))
            {
                return onlyChild(argumentNode);
            }
            return argumentNode;
        }

        // the annotations of a statement are its preceding siblings
        inline void collectAnnotationsBefore(const NodeBuffer& buffer, u32 statementIndex, std::vector<u32>& annotations)
        {
            annotations.clear();
            auto begin = subtreeBegin(buffer, statementIndex);
            while (begin > 0)
            {
                const auto previousRoot = begin - 1;
                if (buffer.kind(previousRoot) != NodeKind::AnnotationNode)
                {
                    break;
                }

                annotations.push_back(previousRoot);
                begin = subtreeBegin(buffer, previousRoot);
            }

            std::reverse(annotations.begin(), annotations.end());
        }

        [[nodiscard]] inline bool annotationsContain(const NodeBuffer& buffer, const TokenBuffer& tokens, const std::vector<u32>& annotations, std::string_view name)
        {
            for (const auto annotation : annotations)
            {
                if (tokens.getLexeme(tokens.getToken(static_cast<i32>(buffer.mainToken(annotation)))) == name)
                {
                    return true;
                }
            }

            return false;
        }
    }

    // failureMessage only contains the first failure
    [[nodiscard]] CARACAL_API bool verifyNodeLayout(const NodeBuffer& buffer, std::string& failureMessage);
}
