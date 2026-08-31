#include <Caracal/Syntax/NodeLayout.h>

#include <algorithm>
#include <vector>

namespace Caracal
{
    [[nodiscard]] static bool IsTypeNameKind(NodeKind kind)
    {
        return kind == NodeKind::TypeNameNode || kind == NodeKind::ArrayTypeNameNode;
    }

    [[nodiscard]] static std::string FormatProblem(u32 index, NodeKind kind, const std::string& problem)
    {
        return "node " + std::to_string(index) + " (" + stringify(kind) + "): " + problem;
    }

    bool verifyNodeLayout(const NodeBuffer& buffer, std::string& failureMessage)
    {
        std::vector<u32> children;

        for (u32 index = 0; index < buffer.nodeCount(); index++)
        {
            const auto kind = buffer.kind(index);
            const auto subtreeSize = buffer.subtreeSize(index);

            if (subtreeSize < 1 || subtreeSize > index + 1)
            {
                failureMessage = FormatProblem(index, kind, "subtree size " + std::to_string(subtreeSize) + " leaves the buffer");
                return false;
            }

            // the child subtrees must fill the space before the node without gaps or overlap
            const auto begin = index - subtreeSize + 1;
            children.clear();
            auto position = index;
            while (position > begin)
            {
                const auto child = position - 1;
                const auto childSize = buffer.subtreeSize(child);
                if (childSize < 1 || childSize > child - begin + 1)
                {
                    failureMessage = FormatProblem(index, kind, "child " + std::to_string(child) + " overruns the subtree");
                    return false;
                }

                children.push_back(child);
                position = child - childSize + 1;
            }
            std::reverse(children.begin(), children.end());
            const auto childCount = static_cast<u32>(children.size());

            auto minimumChildren = 0U;
            auto maximumChildren = ~0U;
            switch (kind)
            {
                case NodeKind::Unknown:
                case NodeKind::Error:
                case NodeKind::BreakStatement:
                case NodeKind::SkipStatement:
                case NodeKind::NameExpression:
                case NodeKind::DiscardLiteral:
                case NodeKind::BoolLiteral:
                case NodeKind::StringLiteral:
                case NodeKind::TypeNameNode:
                case NodeKind::MethodNameNode:
                {
                    maximumChildren = 0;
                    break;
                }
                case NodeKind::ReturnStatement:
                case NodeKind::EnumFieldDeclaration:
                {
                    maximumChildren = 1;
                    break;
                }
                case NodeKind::ReturnTypesNode:
                {
                    maximumChildren = 1;
                    if (childCount == 1 && !IsTypeNameKind(buffer.kind(children[0])))
                    {
                        failureMessage = FormatProblem(index, kind, "the return type child is not a type name");
                        return false;
                    }
                    break;
                }
                case NodeKind::NumberLiteral:
                {
                    maximumChildren = 1;
                    if (childCount == 1 && !IsTypeNameKind(buffer.kind(children[0])))
                    {
                        failureMessage = FormatProblem(index, kind, "the suffix child is not a type name");
                        return false;
                    }
                    break;
                }
                case NodeKind::ExpressionStatement:
                case NodeKind::GroupingExpression:
                case NodeKind::UnaryExpression:
                case NodeKind::MemberAccessExpression:
                case NodeKind::NamedArgument:
                {
                    minimumChildren = 1;
                    maximumChildren = 1;
                    break;
                }
                case NodeKind::AssignmentStatement:
                case NodeKind::BinaryExpression:
                case NodeKind::TrailingIfStatement:
                case NodeKind::WhileStatement:
                {
                    minimumChildren = 2;
                    maximumChildren = 2;
                    break;
                }
                case NodeKind::InitConstantDeclaration:
                {
                    minimumChildren = 2;
                    maximumChildren = 2;
                    if (childCount == 2 && !IsTypeNameKind(buffer.kind(children[1])))
                    {
                        failureMessage = FormatProblem(index, kind, "the init type child is not a type name");
                        return false;
                    }
                    break;
                }
                case NodeKind::ConstantDeclaration:
                case NodeKind::VariableDeclaration:
                case NodeKind::TypeFieldDeclaration:
                {
                    minimumChildren = 2;
                    maximumChildren = 3;
                    if (childCount == 3 && !IsTypeNameKind(buffer.kind(children[1])))
                    {
                        failureMessage = FormatProblem(index, kind, "the explicit type child is not a type name");
                        return false;
                    }
                    break;
                }
                case NodeKind::IfStatement:
                {
                    minimumChildren = 2;
                    maximumChildren = 3;
                    break;
                }
                case NodeKind::ParameterNode:
                {
                    minimumChildren = 1;
                    maximumChildren = 2;
                    if (childCount >= 1 && !IsTypeNameKind(buffer.kind(children[0])))
                    {
                        failureMessage = FormatProblem(index, kind, "the parameter type child is not a type name");
                        return false;
                    }
                    break;
                }
                case NodeKind::ArrayTypeNameNode:
                {
                    minimumChildren = 1;
                    maximumChildren = 2;
                    if (childCount >= 1 && !IsTypeNameKind(buffer.kind(children[0])))
                    {
                        failureMessage = FormatProblem(index, kind, "the element type child is not a type name");
                        return false;
                    }
                    if (childCount == 2 && buffer.kind(children[1]) != NodeKind::NumberLiteral)
                    {
                        failureMessage = FormatProblem(index, kind, "the length child is not a number literal");
                        return false;
                    }
                    break;
                }
                case NodeKind::TypeDefinitionStatement:
                {
                    minimumChildren = 1;
                    maximumChildren = 2;
                    if (childCount >= 1 && buffer.kind(children[childCount - 1]) != NodeKind::BlockNode)
                    {
                        failureMessage = FormatProblem(index, kind, "the body child is not a block");
                        return false;
                    }
                    if (childCount == 2 && buffer.kind(children[0]) != NodeKind::ParametersNode)
                    {
                        failureMessage = FormatProblem(index, kind, "the constructor parameters child is not a parameters node");
                        return false;
                    }
                    break;
                }
                case NodeKind::FunctionDefinitionStatement:
                {
                    minimumChildren = 3;
                    maximumChildren = 3;
                    if (childCount == 3
                        && (buffer.kind(children[0]) != NodeKind::ParametersNode
                            || buffer.kind(children[1]) != NodeKind::ReturnTypesNode
                            || buffer.kind(children[2]) != NodeKind::BlockNode))
                    {
                        failureMessage = FormatProblem(index, kind, "expected parameters, return types and block children");
                        return false;
                    }
                    break;
                }
                case NodeKind::MethodDefinitionStatement:
                {
                    minimumChildren = 4;
                    maximumChildren = 4;
                    if (childCount == 4
                        && (buffer.kind(children[0]) != NodeKind::MethodNameNode
                            || buffer.kind(children[1]) != NodeKind::ParametersNode
                            || buffer.kind(children[2]) != NodeKind::ReturnTypesNode
                            || buffer.kind(children[3]) != NodeKind::BlockNode))
                    {
                        failureMessage = FormatProblem(index, kind, "expected method name, parameters, return types and block children");
                        return false;
                    }
                    break;
                }
                case NodeKind::EnumDefinitionStatement:
                {
                    for (u32 child = 0; child < childCount; child++)
                    {
                        const auto childKind = buffer.kind(children[child]);
                        if (child == 0 && IsTypeNameKind(childKind))
                        {
                            continue;
                        }
                        if (childKind != NodeKind::EnumFieldDeclaration)
                        {
                            failureMessage = FormatProblem(index, kind, "unexpected enum child kind " + stringify(childKind));
                            return false;
                        }
                    }
                    break;
                }
                case NodeKind::ParametersNode:
                {
                    for (u32 child = 0; child < childCount; child++)
                    {
                        if (buffer.kind(children[child]) != NodeKind::ParameterNode)
                        {
                            failureMessage = FormatProblem(index, kind, "unexpected parameters child kind " + stringify(buffer.kind(children[child])));
                            return false;
                        }
                    }
                    break;
                }
                case NodeKind::FunctionCallExpression:
                {
                    minimumChildren = 1;
                    if (childCount >= 1 && buffer.kind(children[0]) != NodeKind::NameExpression)
                    {
                        failureMessage = FormatProblem(index, kind, "the callee child is not a name expression");
                        return false;
                    }
                    break;
                }
                case NodeKind::ReturnTypeNode:
                case NodeKind::BlockNode:
                case NodeKind::AnnotationNode:
                case NodeKind::ArrayLiteral:
                {
                    break;
                }
            }

            if (childCount < minimumChildren || childCount > maximumChildren)
            {
                failureMessage = FormatProblem(index, kind, "has " + std::to_string(childCount) + " children");
                return false;
            }
        }

        failureMessage.clear();
        return true;
    }
}
