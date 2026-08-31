#include "SliceParameterPromotionOld.h"

#include <Caracal/Constants.h>
#include <Caracal/Syntax/ArrayLiteral.h>
#include <Caracal/Syntax/AssignmentStatement.h>
#include <Caracal/Syntax/BinaryExpression.h>
#include <Caracal/Syntax/BlockNode.h>
#include <Caracal/Syntax/ConstantDeclaration.h>
#include <Caracal/Syntax/ExpressionStatement.h>
#include <Caracal/Syntax/FunctionCallExpression.h>
#include <Caracal/Syntax/GroupingExpression.h>
#include <Caracal/Syntax/IfStatement.h>
#include <Caracal/Syntax/MemberAccessExpression.h>
#include <Caracal/Syntax/MethodDefinitionStatement.h>
#include <Caracal/Syntax/NameExpression.h>
#include <Caracal/Syntax/ReturnStatement.h>
#include <Caracal/Syntax/UnaryExpression.h>
#include <Caracal/Syntax/VariableDeclaration.h>
#include <Caracal/Syntax/WhileStatement.h>

#include <unordered_map>
#include <utility>

namespace Caracal
{
    struct SliceParameterUsage
    {
        bool promotable = true;
        std::vector<std::pair<Type, size_t>> forwards;
    };

    static u64 MakeSliceParameterKey(Type functionType, size_t parameterIndex)
    {
        return (static_cast<u64>(static_cast<u32>(functionType.id())) << 32) | static_cast<u64>(parameterIndex);
    }

    static size_t ImplicitThisOffset(const FunctionDefinition& functionDefinition)
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

    struct PromotionCandidate
    {
        Type functionType;
        size_t parameterIndex;
        bool promotable;
        std::vector<std::pair<Type, size_t>> forwards;
    };

    static void AnalyzeSliceParameterUses(const Expression* expression, const std::string& name, const std::vector<Parameter>& enclosingParameters, SemanticContext& module, SliceParameterUsage& usage);

    static void AnalyzeSliceParameterCallArguments(const FunctionCallExpression* call, Type calleeType, size_t parameterOffset, const std::string& name, const std::vector<Parameter>& enclosingParameters, SemanticContext& module, SliceParameterUsage& usage)
    {
        const auto& arguments = call->arguments();
        size_t positionalIndex = 0;
        for (size_t index = 0; index < arguments.size(); ++index)
        {
            const auto& argument = arguments[index];
            const auto* argumentValue = stripGroupings(argument.value().get());
            const auto isBareParameter = argumentValue != nullptr
                && argumentValue->kind() == NodeKind::NameExpression
                && static_cast<const NameExpression*>(argumentValue)->name() == name;
            if (!isBareParameter)
            {
                AnalyzeSliceParameterUses(argument.value().get(), name, enclosingParameters, module, usage);
                if (!argument.isNamed())
                {
                    positionalIndex++;
                }
                continue;
            }

            // a bare parameter argument forwards the qualifier question to the callee's parameter
            if (calleeType == Type::Undefined())
            {
                usage.promotable = false;
                if (!argument.isNamed())
                {
                    positionalIndex++;
                }
                continue;
            }

            if (argument.isNamed())
            {
                // named arguments resolve by parameter name
                const auto& calleeParameters = module.getFunctionDefinition(calleeType).parameters();
                auto resolved = false;
                for (size_t j = 0; j < calleeParameters.size(); ++j)
                {
                    if (calleeParameters[j].name() == argument.name())
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

    static void AnalyzeSliceParameterUses(const Expression* expression, const std::string& name, const std::vector<Parameter>& enclosingParameters, SemanticContext& module, SliceParameterUsage& usage)
    {
        if (expression == nullptr)
        {
            return;
        }

        switch (expression->kind())
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
                // a bare appearance outside of the allowed shapes blocks promotion
                if (static_cast<const NameExpression*>(expression)->name() == name)
                {
                    usage.promotable = false;
                }
                return;
            }
            case NodeKind::ArrayLiteral:
            {
                for (const auto& element : static_cast<const ArrayLiteral*>(expression)->elements())
                {
                    AnalyzeSliceParameterUses(element.get(), name, enclosingParameters, module, usage);
                }
                return;
            }
            case NodeKind::GroupingExpression:
            {
                AnalyzeSliceParameterUses(static_cast<const GroupingExpression*>(expression)->expression().get(), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::UnaryExpression:
            {
                AnalyzeSliceParameterUses(static_cast<const UnaryExpression*>(expression)->expression().get(), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::MemberAccessExpression:
            {
                AnalyzeSliceParameterUses(static_cast<const MemberAccessExpression*>(expression)->expression().get(), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::FunctionCallExpression:
            {
                const auto* call = static_cast<const FunctionCallExpression*>(expression);
                AnalyzeSliceParameterCallArguments(call, module.tryGetFunctionTypeByName(call->nameExpression()->name()), 0, name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::BinaryExpression:
            {
                const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
                if (binaryExpression->binaryOperator() == BinaryOperatorKind::MemberAccess)
                {
                    const auto* receiver = stripGroupings(binaryExpression->leftExpression().get());
                    const auto receiverIsParameter = receiver != nullptr
                        && receiver->kind() == NodeKind::NameExpression
                        && static_cast<const NameExpression*>(receiver)->name() == name;
                    if (receiverIsParameter)
                    {
                        const auto* member = binaryExpression->rightExpression().get();
                        if (member->kind() == NodeKind::NameExpression
                            && static_cast<const NameExpression*>(member)->name() == ArrayLengthMemberName)
                        {
                            return;
                        }

                        if (member->kind() == NodeKind::FunctionCallExpression)
                        {
                            const auto* call = static_cast<const FunctionCallExpression*>(member);
                            if (call->nameExpression()->name() == "at")
                            {
                                AnalyzeSliceParameterCallArguments(call, Type::Undefined(), 0, name, enclosingParameters, module, usage);
                                return;
                            }
                        }

                        usage.promotable = false;
                        return;
                    }

                    AnalyzeSliceParameterUses(binaryExpression->leftExpression().get(), name, enclosingParameters, module, usage);
                    if (binaryExpression->rightExpression()->kind() == NodeKind::FunctionCallExpression)
                    {
                        // method callees resolve for type-name receivers (static calls) and enclosing parameters
                        const auto* methodCall = static_cast<const FunctionCallExpression*>(binaryExpression->rightExpression().get());
                        auto calleeType = Type::Undefined();
                        size_t parameterOffset = 0;
                        if (receiver != nullptr && receiver->kind() == NodeKind::NameExpression)
                        {
                            const auto& receiverName = static_cast<const NameExpression*>(receiver)->name();
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
                                const auto methodType = module.getTypeDefinition(receiverType).tryGetMethodTypeByName(methodCall->nameExpression()->name());
                                if (methodType != Type::Undefined())
                                {
                                    calleeType = methodType;
                                    parameterOffset = ImplicitThisOffset(module.getFunctionDefinition(methodType));
                                }
                            }
                        }
                        AnalyzeSliceParameterCallArguments(methodCall, calleeType, parameterOffset, name, enclosingParameters, module, usage);
                    }
                    else
                    {
                        AnalyzeSliceParameterUses(binaryExpression->rightExpression().get(), name, enclosingParameters, module, usage);
                    }
                    return;
                }

                AnalyzeSliceParameterUses(binaryExpression->leftExpression().get(), name, enclosingParameters, module, usage);
                AnalyzeSliceParameterUses(binaryExpression->rightExpression().get(), name, enclosingParameters, module, usage);
                return;
            }
            default:
            {
                usage.promotable = false;
                return;
            }
        }
    }

    static void AnalyzeSliceParameterUses(const Statement* statement, const std::string& name, const std::vector<Parameter>& enclosingParameters, SemanticContext& module, SliceParameterUsage& usage)
    {
        if (statement == nullptr)
        {
            return;
        }

        switch (statement->kind())
        {
            case NodeKind::BlockNode:
            {
                for (const auto& child : static_cast<const BlockNode*>(statement)->statements())
                {
                    AnalyzeSliceParameterUses(child.get(), name, enclosingParameters, module, usage);
                }
                return;
            }
            case NodeKind::VariableDeclaration:
            {
                AnalyzeSliceParameterUses(static_cast<const VariableDeclaration*>(statement)->rightExpression().get(), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::ConstantDeclaration:
            {
                AnalyzeSliceParameterUses(static_cast<const ConstantDeclaration*>(statement)->rightExpression().get(), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::AssignmentStatement:
            {
                const auto* assignment = static_cast<const AssignmentStatement*>(statement);
                AnalyzeSliceParameterUses(assignment->leftExpression().get(), name, enclosingParameters, module, usage);
                AnalyzeSliceParameterUses(assignment->rightExpression().get(), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::ExpressionStatement:
            {
                AnalyzeSliceParameterUses(static_cast<const ExpressionStatement*>(statement)->expression().get(), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::IfStatement:
            {
                const auto* ifStatement = static_cast<const IfStatement*>(statement);
                AnalyzeSliceParameterUses(ifStatement->condition().get(), name, enclosingParameters, module, usage);
                AnalyzeSliceParameterUses(ifStatement->trueStatement().get(), name, enclosingParameters, module, usage);
                if (ifStatement->falseStatement().has_value())
                {
                    AnalyzeSliceParameterUses(ifStatement->falseStatement().value().get(), name, enclosingParameters, module, usage);
                }
                return;
            }
            case NodeKind::WhileStatement:
            {
                const auto* whileStatement = static_cast<const WhileStatement*>(statement);
                AnalyzeSliceParameterUses(whileStatement->condition().get(), name, enclosingParameters, module, usage);
                AnalyzeSliceParameterUses(whileStatement->trueStatement().get(), name, enclosingParameters, module, usage);
                return;
            }
            case NodeKind::ReturnStatement:
            {
                const auto& expression = static_cast<const ReturnStatement*>(statement)->expression();
                if (expression.has_value())
                {
                    AnalyzeSliceParameterUses(expression.value().get(), name, enclosingParameters, module, usage);
                }
                return;
            }
            case NodeKind::BreakStatement:
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
        Type functionType,
        const BlockNode* bodyNode,
        SemanticContext& module,
        std::vector<PromotionCandidate>& candidates,
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

            auto usage = SliceParameterUsage{};
            AnalyzeSliceParameterUses(static_cast<const Statement*>(bodyNode), parameters[i].name(), parameters, module, usage);
            if (!usage.promotable)
            {
                continue;
            }

            candidateIndexByKey.try_emplace(MakeSliceParameterKey(functionType, i), candidates.size());
            candidates.push_back(PromotionCandidate{ functionType, i, true, std::move(usage.forwards) });
        }
    }

    void promoteReadOnlySliceParametersOld(
        const std::vector<const FunctionDefinitionStatement*>& functionDeclarations,
        const std::vector<const TypeDefinitionStatement*>& typeDeclarations,
        SemanticContext& module)
    {
        auto candidates = std::vector<PromotionCandidate>{};
        auto candidateIndexByKey = std::unordered_map<u64, size_t>{};
        for (const auto* functionDefinitionStatement : functionDeclarations)
        {
            auto* statement = const_cast<FunctionDefinitionStatement*>(functionDefinitionStatement);
            if (statement->isExtern() || statement->type() == Type::Undefined())
            {
                continue;
            }

            CollectSlicePromotionCandidates(statement->type(), statement->bodyNode().get(), module, candidates, candidateIndexByKey);
        }

        for (const auto* typeDefinitionStatement : typeDeclarations)
        {
            if (typeDefinitionStatement->isBuiltin())
            {
                continue;
            }

            for (const auto& bodyStatement : typeDefinitionStatement->bodyNode()->statements())
            {
                if (bodyStatement->kind() != NodeKind::MethodDefinitionStatement)
                {
                    continue;
                }

                const auto* methodStatement = static_cast<const MethodDefinitionStatement*>(bodyStatement.get());
                if (methodStatement->specialFunctionType() == SpecialFunctionType::Constructor
                    || methodStatement->methodNameNode()->methodName() == "new"
                    || methodStatement->type() == Type::Undefined())
                {
                    continue;
                }

                CollectSlicePromotionCandidates(methodStatement->type(), methodStatement->bodyNode().get(), module, candidates, candidateIndexByKey);
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
                        // a callee parameter that is already immutable from the prelude is a safe target
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
            parameter = Parameter{ parameter.name(), module.getOrCreateArrayType(TypeKind::Slice, elementType, 0, true), parameter.defaultValue() };
            functionDefinition.setParameters(std::move(parameters));
        }
    }

}
