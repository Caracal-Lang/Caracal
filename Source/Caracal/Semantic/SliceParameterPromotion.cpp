#include <Caracal/Semantic/SliceParameterPromotion.h>

#include <Caracal/CompilationContext.h>
#include <Caracal/Constants.h>
#include <Caracal/Semantic/Parameter.h>
#include <Caracal/Semantic/SemanticContext.h>
#include <Caracal/Syntax/NodeIndex.h>
#include <Caracal/Syntax/NodeLayout.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Caracal
{
    struct SlicePromotionUsage
    {
        bool promotable = true;
        std::vector<std::pair<Type, size_t>> forwards;
    };

    struct SlicePromotionCandidate
    {
        Type functionType;
        size_t parameterIndex;
        bool promotable;
        std::vector<std::pair<Type, size_t>> forwards;
    };

    [[nodiscard]] static u32 CallNameExpressionIndex(const NodeBuffer& buffer, u32 callIndex)
    {
        return NodeLayout::subtreeBegin(buffer, callIndex);
    }

    [[nodiscard]] static u64 MakeSliceParameterKey(Type functionType, size_t parameterIndex)
    {
        return (static_cast<u64>(static_cast<u32>(functionType.id())) << 32) | static_cast<u64>(parameterIndex);
    }

    [[nodiscard]] static size_t ImplicitThisOffset(const FunctionDefinition& functionDefinition)
    {
        const auto functionType = functionDefinition.functionType();
        if (functionType == FunctionType::SynthesizedConstructor
            || functionType == FunctionType::PublicMethod
            || functionType == FunctionType::PrivateMethod)
        {
            return 1;
        }

        if (functionType == FunctionType::Intrinsic
            && !functionDefinition.parameters().empty()
            && functionDefinition.parameters().front().name() == ImplicitThisName)
        {
            return 1;
        }

        return 0;
    }

    static void AnalyzeSliceParameterUsesInExpression(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 index,
        const std::string& name,
        const std::vector<Parameter>& enclosingParameters,
        SemanticContext& module,
        SlicePromotionUsage& usage);

    static void AnalyzeSliceParameterCallArguments(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 callIndex,
        Type calleeType,
        size_t parameterOffset,
        const std::string& name,
        const std::vector<Parameter>& enclosingParameters,
        SemanticContext& module,
        SlicePromotionUsage& usage)
    {
        const auto& buffer = unit.nodeBuffer;
        auto children = std::vector<u32>{};
        NodeLayout::collectChildren(buffer, callIndex, children);

        // arguments start at 1
        size_t positionalIndex = 0;
        for (size_t childIndex = 1; childIndex < children.size(); ++childIndex)
        {
            const auto argument = children[childIndex];
            const auto isNamed = buffer.kind(argument) == NodeKind::NamedArgument;
            auto value = argument;
            if (isNamed)
            {
                value = NodeLayout::namedArgumentValue(argument);
            }

            const auto strippedValue = NodeLayout::stripGroupings(buffer, value);
            const auto isBareParameter = 
                (buffer.kind(strippedValue) == NodeKind::NameExpression
                && context.getLexeme(unit.fileId, buffer.mainToken(strippedValue)) == name);
            if (!isBareParameter)
            {
                AnalyzeSliceParameterUsesInExpression(context, unit, value, name, enclosingParameters, module, usage);
                if (!isNamed)
                {
                    positionalIndex++;
                }
                continue;
            }

            if (calleeType == Type::Undefined())
            {
                usage.promotable = false;
                if (!isNamed)
                {
                    positionalIndex++;
                }
                continue;
            }

            if (isNamed)
            {
                // named arguments resolve by parameter name
                const auto argumentName = context.getLexeme(unit.fileId, buffer.mainToken(argument));
                const auto& calleeParameters = module.getFunctionDefinition(calleeType).parameters();
                auto resolved = false;
                for (size_t j = 0; j < calleeParameters.size(); ++j)
                {
                    if (calleeParameters[j].name() == argumentName)
                    {
                        usage.forwards.emplace_back(calleeType, j);
                        resolved = true;
                        break;
                    }
                }

                if (!resolved)
                {
                    usage.promotable = false;
                }
                continue;
            }

            usage.forwards.emplace_back(calleeType, positionalIndex + parameterOffset);
            positionalIndex++;
        }
    }

    static void AnalyzeSliceParameterUsesInExpression(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 index,
        const std::string& name,
        const std::vector<Parameter>& enclosingParameters,
        SemanticContext& module,
        SlicePromotionUsage& usage)
    {
        const auto& buffer = unit.nodeBuffer;
        switch (buffer.kind(index))
        {
            case NodeKind::NumberLiteral:
            case NodeKind::StringLiteral:
            case NodeKind::BoolLiteral:
            case NodeKind::DiscardLiteral:
            {
                return;
            }
            case NodeKind::NameExpression:
            {
                // dont promote to immutable, we dont check if we write through them
                if (context.getLexeme(unit.fileId, buffer.mainToken(index)) == name)
                {
                    usage.promotable = false;
                }
                return;
            }
            case NodeKind::ArrayLiteral:
            {
                auto elements = std::vector<u32>{};
                NodeLayout::collectChildren(buffer, index, elements);
                for (const auto element : elements)
                {
                    AnalyzeSliceParameterUsesInExpression(context, unit, element, name, enclosingParameters, module, usage);
                }
                return;
            }
            case NodeKind::GroupingExpression:
            {
                AnalyzeSliceParameterUsesInExpression(context, unit, NodeLayout::groupedExpression(index), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::UnaryExpression:
            {
                AnalyzeSliceParameterUsesInExpression(context, unit, NodeLayout::unaryOperand(index), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::MemberAccessExpression:
            {
                AnalyzeSliceParameterUsesInExpression(context, unit, NodeLayout::memberExpression(index), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::FunctionCallExpression:
            {
                const auto calleeName = context.getLexeme(unit.fileId, buffer.mainToken(CallNameExpressionIndex(buffer, index)));
                AnalyzeSliceParameterCallArguments(context, unit, index, module.tryGetFunctionTypeByName(calleeName), 0, name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::BinaryExpression:
            {
                const auto binaryLayout = NodeLayout::binary(buffer, index);
                const auto& tokens = context.tokenBuffer(unit.fileId);
                const auto operatorKind = tokens.getToken(static_cast<i32>(buffer.mainToken(index))).kind;
                if (operatorKind == TokenKind::Dot)
                {
                    const auto receiver = NodeLayout::stripGroupings(buffer, binaryLayout.left);
                    const auto receiverIsParameter = (buffer.kind(receiver) == NodeKind::NameExpression
                        && context.getLexeme(unit.fileId, buffer.mainToken(receiver)) == name);
                    if (receiverIsParameter)
                    {
                        const auto member = binaryLayout.right;
                        if (buffer.kind(member) == NodeKind::NameExpression
                            && context.getLexeme(unit.fileId, buffer.mainToken(member)) == ArrayLengthMemberName)
                        {
                            return;
                        }

                        if (buffer.kind(member) == NodeKind::FunctionCallExpression)
                        {
                            if (context.getLexeme(unit.fileId, buffer.mainToken(CallNameExpressionIndex(buffer, member))) == "at")
                            {
                                AnalyzeSliceParameterCallArguments(context, unit, member, Type::Undefined(), 0, name, enclosingParameters, module, usage);
                                return;
                            }
                        }

                        usage.promotable = false;
                        return;
                    }

                    AnalyzeSliceParameterUsesInExpression(context, unit, binaryLayout.left, name, enclosingParameters, module, usage);
                    if (buffer.kind(binaryLayout.right) == NodeKind::FunctionCallExpression)
                    {
                        // method callees resolve for type-name receivers (static calls) and enclosing parameters
                        auto calleeType = Type::Undefined();
                        size_t parameterOffset = 0;
                        if (buffer.kind(receiver) == NodeKind::NameExpression)
                        {
                            const auto receiverName = context.getLexeme(unit.fileId, buffer.mainToken(receiver));
                            auto receiverType = module.tryGetTypeByName(receiverName);
                            if (receiverType == Type::Undefined())
                            {
                                for (const auto& enclosingParameter : enclosingParameters)
                                {
                                    if (enclosingParameter.name() == receiverName)
                                    {
                                        receiverType = enclosingParameter.type().toValue();
                                        break;
                                    }
                                }
                            }

                            if (receiverType != Type::Undefined() && receiverType.kind() == TypeKind::Type)
                            {
                                const auto methodType = module.getTypeDefinition(receiverType).tryGetMethodTypeByName(context.getLexeme(unit.fileId, buffer.mainToken(CallNameExpressionIndex(buffer, binaryLayout.right))));
                                if (methodType != Type::Undefined())
                                {
                                    calleeType = methodType;
                                    parameterOffset = ImplicitThisOffset(module.getFunctionDefinition(methodType));
                                }
                            }
                        }
                        AnalyzeSliceParameterCallArguments(context, unit, binaryLayout.right, calleeType, parameterOffset, name, enclosingParameters, module, usage);
                    }
                    else
                    {
                        AnalyzeSliceParameterUsesInExpression(context, unit, binaryLayout.right, name, enclosingParameters, module, usage);
                    }
                    return;
                }

                AnalyzeSliceParameterUsesInExpression(context, unit, binaryLayout.left, name, enclosingParameters, module, usage);
                AnalyzeSliceParameterUsesInExpression(context, unit, binaryLayout.right, name, enclosingParameters, module, usage);
                return;
            }
            default:
            {
                usage.promotable = false;
                return;
            }
        }
    }

    static void AnalyzeSliceParameterUsesInStatement(
        const CompilationContext& context,
        const CompilationUnit& unit,
        u32 index,
        const std::string& name,
        const std::vector<Parameter>& enclosingParameters,
        SemanticContext& module,
        SlicePromotionUsage& usage)
    {
        const auto& buffer = unit.nodeBuffer;
        switch (buffer.kind(index))
        {
            case NodeKind::BlockNode:
            {
                auto children = std::vector<u32>{};
                NodeLayout::collectChildren(buffer, index, children);
                for (const auto child : children)
                {
                    AnalyzeSliceParameterUsesInStatement(context, unit, child, name, enclosingParameters, module, usage);
                }
                return;
            }
            case NodeKind::VariableDeclaration:
            {
                AnalyzeSliceParameterUsesInExpression(context, unit, NodeLayout::declaration(buffer, index).right, name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::ConstantDeclaration:
            {
                AnalyzeSliceParameterUsesInExpression(context, unit, NodeLayout::declaration(buffer, index).right, name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::AssignmentStatement:
            {
                const auto assignmentLayout = NodeLayout::assignment(buffer, index);
                AnalyzeSliceParameterUsesInExpression(context, unit, assignmentLayout.target, name, enclosingParameters, module, usage);
                AnalyzeSliceParameterUsesInExpression(context, unit, assignmentLayout.value, name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::ExpressionStatement:
            {
                AnalyzeSliceParameterUsesInExpression(context, unit, NodeLayout::statementExpression(index), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::IfStatement:
            {
                const auto ifLayout = NodeLayout::ifStatement(buffer, index);
                AnalyzeSliceParameterUsesInExpression(context, unit, ifLayout.condition, name, enclosingParameters, module, usage);
                AnalyzeSliceParameterUsesInStatement(context, unit, ifLayout.trueStatement, name, enclosingParameters, module, usage);
                if (ifLayout.hasFalse)
                {
                    AnalyzeSliceParameterUsesInStatement(context, unit, ifLayout.falseStatement, name, enclosingParameters, module, usage);
                }
                return;
            }
            case NodeKind::TrailingIfStatement:
            {
                const auto trailingIfLayout = NodeLayout::trailingIf(buffer, index);
                AnalyzeSliceParameterUsesInExpression(context, unit, trailingIfLayout.condition, name, enclosingParameters, module, usage);
                AnalyzeSliceParameterUsesInStatement(context, unit, trailingIfLayout.statement, name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::WhileStatement:
            {
                const auto whileLayout = NodeLayout::whileStatement(buffer, index);
                AnalyzeSliceParameterUsesInExpression(context, unit, whileLayout.condition, name, enclosingParameters, module, usage);
                AnalyzeSliceParameterUsesInStatement(context, unit, whileLayout.body, name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::ReturnStatement:
            {
                const auto value = NodeLayout::optionalChild(buffer, index);
                if (value.hasChild)
                {
                    AnalyzeSliceParameterUsesInExpression(context, unit, value.child, name, enclosingParameters, module, usage);
                }
                return;
            }
            case NodeKind::AnnotationNode:
            case NodeKind::BreakStatement:
            case NodeKind::InitConstantDeclaration:
            case NodeKind::SkipStatement:
            {
                return;
            }
            default:
            {
                usage.promotable = false;
                return;
            }
        }
    }

    static void CollectSlicePromotionCandidates(
        const CompilationContext& context,
        const CompilationUnit& unit,
        Type functionType,
        u32 bodyIndex,
        SemanticContext& module,
        std::vector<SlicePromotionCandidate>& candidates,
        std::unordered_map<u64, size_t>& candidateIndexByKey)
    {
        const auto& functionDefinition = module.getFunctionDefinition(functionType);
        if (functionDefinition.symbolName().has_value())
        {
            return;
        }

        const auto& parameters = functionDefinition.parameters();
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            const auto parameterType = parameters[i].type();
            if (parameterType.kind() != TypeKind::Slice
                || parameterType.isReference()
                || module.isImmutableSlice(parameterType))
            {
                continue;
            }

            auto usage = SlicePromotionUsage{};
            AnalyzeSliceParameterUsesInStatement(context, unit, bodyIndex, parameters[i].name(), parameters, module, usage);
            if (!usage.promotable)
            {
                continue;
            }

            candidateIndexByKey.try_emplace(MakeSliceParameterKey(functionType, i), candidates.size());
            candidates.push_back(SlicePromotionCandidate{ functionType, i, true, std::move(usage.forwards) });
        }
    }

    void promoteReadOnlySliceParameters(
        const CompilationContext& context,
        SemanticContext& module,
        const std::vector<NodeIndex>& functionDeclarations,
        const std::vector<NodeIndex>& typeDeclarations)
    {
        auto candidates = std::vector<SlicePromotionCandidate>{};
        auto candidateIndexByKey = std::unordered_map<u64, size_t>{};
        auto annotations = std::vector<u32>{};
        for (const auto functionDeclaration : functionDeclarations)
        {
            const auto& unit = context.units()[functionDeclaration.fileId];

            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, functionDeclaration.index, annotations);
            const auto isExtern = NodeLayout::annotationsContain(unit.nodeBuffer, context.tokenBuffer(unit.fileId), annotations, ExternAnnotationName);
            const auto functionType = module.typeOfNode(functionDeclaration);
            if (isExtern || functionType == Type::Undefined())
            {
                continue;
            }

            const auto functionLayout = NodeLayout::functionDefinition(unit.nodeBuffer, functionDeclaration.index);
            CollectSlicePromotionCandidates(context, unit, functionType, functionLayout.body, module, candidates, candidateIndexByKey);
        }

        for (const auto typeDeclaration : typeDeclarations)
        {
            const auto& unit = context.units()[typeDeclaration.fileId];
            NodeLayout::collectAnnotationsBefore(unit.nodeBuffer, typeDeclaration.index, annotations);
            if (NodeLayout::annotationsContain(unit.nodeBuffer, context.tokenBuffer(unit.fileId), annotations, BuiltinAnnotationName))
            {
                continue;
            }

            const auto typeLayout = NodeLayout::typeDefinition(unit.nodeBuffer, typeDeclaration.index);
            auto bodyStatements = std::vector<u32>{};
            NodeLayout::collectChildren(unit.nodeBuffer, typeLayout.body, bodyStatements);
            for (const auto bodyStatement : bodyStatements)
            {
                if (unit.nodeBuffer.kind(bodyStatement) != NodeKind::MethodDefinitionStatement)
                {
                    continue;
                }

                const auto methodLayout = NodeLayout::methodDefinition(unit.nodeBuffer, bodyStatement);
                const auto methodType = module.typeOfNode(NodeIndex{ unit.fileId, bodyStatement });
                if (unit.nodeBuffer.methodNameIsNew(context.tokenBuffer(unit.fileId), methodLayout.methodName)
                    || methodType == Type::Undefined())
                {
                    continue;
                }

                CollectSlicePromotionCandidates(context, unit, methodType, methodLayout.body, module, candidates, candidateIndexByKey);
            }
        }

        auto changed = true;
        while (changed)
        {
            changed = false;
            for (auto& candidate : candidates)
            {
                if (!candidate.promotable)
                {
                    continue;
                }

                for (const auto& [calleeType, calleeIndex] : candidate.forwards)
                {
                    auto forwardIsPromotable = false;
                    if (const auto found = candidateIndexByKey.find(MakeSliceParameterKey(calleeType, calleeIndex)); found != candidateIndexByKey.end())
                    {
                        forwardIsPromotable = candidates[found->second].promotable;
                    }
                    else
                    {
                        const auto& calleeParameters = module.getFunctionDefinition(calleeType).parameters();
                        forwardIsPromotable = calleeIndex < calleeParameters.size()
                            && module.isImmutableSlice(calleeParameters[calleeIndex].type());
                    }

                    if (!forwardIsPromotable)
                    {
                        candidate.promotable = false;
                        changed = true;
                        break;
                    }
                }
            }
        }

        for (const auto& candidate : candidates)
        {
            if (!candidate.promotable)
            {
                continue;
            }

            auto& functionDefinition = module.getFunctionDefinition(candidate.functionType);
            auto parameters = functionDefinition.parameters();
            auto& parameter = parameters[candidate.parameterIndex];
            const auto elementType = module.getArrayElementType(parameter.type());

            auto promoted = Parameter{ parameter.name(), module.getOrCreateArrayType(TypeKind::Slice, elementType, 0, true), parameter.defaultValue() };
            promoted.setDefaultValueNode(parameter.defaultValueNode());
            parameter = std::move(promoted);
            functionDefinition.setParameters(std::move(parameters));
        }
    }
}
