#include <Caracal/Semantic/TypeChecker.h>

#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Syntax/NodeLayout.h>
#include <Caracal/Syntax/NodeSpans.h>
#include <Caracal/Syntax/TokenBuffer.h>

namespace Caracal
{
    [[nodiscard]] static const SourceLocation& TokenLocationAt(const TokenBuffer& tokens, u32 tokenIndex) noexcept
    {
        return tokens.getSourceLocation(tokens.getToken(static_cast<i32>(tokenIndex)));
    }

    static bool IsTerminatingStatement(const NodeBuffer& buffer, u32 index)
    {
        switch (buffer.kind(index))
        {
            case NodeKind::ReturnStatement:
            case NodeKind::BreakStatement:
            case NodeKind::SkipStatement:
            {
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    void TypeChecker::typeCheckIfStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto ifLayout = NodeLayout::ifStatement(unit.nodeBuffer, index);

        const auto checkedCondition = typeCheckExpression(unit, ifLayout.condition, ExpressionContext{});
        auto conditionType = coerceConditionType(checkedCondition.type, unit, ifLayout.condition);
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolIfConditionError(
                unit.source,
                NodeSpans::expressionLocation(unit.nodeBuffer, tokens, ifLayout.condition),
                formatTypeName(m_module, conditionType));
        }

        auto ifFrame = m_emitter.beginIf(materializeValue(checkedCondition), ifLayout.hasFalse);
        typeCheckStatementInNewScope(unit, ifLayout.trueStatement);
        m_emitter.endIfThen(ifFrame);

        if (ifLayout.hasFalse)
        {
            typeCheckStatementInNewScope(unit, ifLayout.falseStatement);
        }
        m_emitter.endIf(ifFrame);
    }

    void TypeChecker::typeCheckTrailingIfStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto trailingIfLayout = NodeLayout::trailingIf(unit.nodeBuffer, index);

        const auto checkedCondition = typeCheckExpression(unit, trailingIfLayout.condition, ExpressionContext{});
        auto conditionType = coerceConditionType(checkedCondition.type, unit, trailingIfLayout.condition);
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolIfConditionError(
                unit.source,
                NodeSpans::expressionLocation(unit.nodeBuffer, tokens, trailingIfLayout.condition),
                formatTypeName(m_module, conditionType));
        }

        auto ifFrame = m_emitter.beginIf(materializeValue(checkedCondition), false);
        typeCheckStatement(unit, trailingIfLayout.statement);
        m_emitter.endIfThen(ifFrame);
        m_emitter.endIf(ifFrame);
    }

    void TypeChecker::typeCheckWhileStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto whileLayout = NodeLayout::whileStatement(unit.nodeBuffer, index);

        auto whileFrame = m_emitter.beginWhile();
        const auto checkedCondition = typeCheckExpression(unit, whileLayout.condition, ExpressionContext{});
        auto conditionType = coerceConditionType(checkedCondition.type, unit, whileLayout.condition);
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolWhileConditionError(
                unit.source,
                NodeSpans::expressionLocation(unit.nodeBuffer, tokens, whileLayout.condition),
                formatTypeName(m_module, conditionType));
        }

        m_emitter.beginWhileBody(whileFrame, materializeValue(checkedCondition));
        typeCheckStatementInNewScope(unit, whileLayout.body);
        m_emitter.endWhile(whileFrame);
    }

    void TypeChecker::typeCheckReturnStatement(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto declaredReturnType = m_currentReturnType;

        const auto value = NodeLayout::optionalChild(unit.nodeBuffer, index);
        if (value.hasChild)
        {
            // a constructor call builds the object in a temp slot and returns that
            auto pushedTempDestination = false;
            if (!currentScopeIsGlobal())
            {
                const auto constructedType = tryResolveConstructedType(unit, NodeLayout::stripGroupings(unit.nodeBuffer, value.child));
                if (constructedType != Type::Undefined())
                {
                    const auto destination = m_emitter.allocateTempAggregateSlot("constructed", constructedType);
                    if (destination.id() >= 0)
                    {
                        pushConstructorDestination(unit, value.child, destination, true);
                        pushedTempDestination = true;
                    }
                }
            }

            auto checkedValue = CheckedValue{ Type::Undefined() };
            {
                auto context = ExpressionContext{};
                context.contextualNumberType = contextualTypeForExpectedType(declaredReturnType);
                checkedValue = typeCheckExpression(unit, value.child, context);
            }
            if (pushedTempDestination)
            {
                m_constructorDestinations.pop_back();
            }
            auto type = checkedValue.type;
            if (type.isReference())
            {
                // returning a ref is not allowed, so we need to coerce it to a value
                type = type.toValue();
            }

            recordType(unit, index, type);
            if (type == Type::Undefined())
            {
                return;
            }

            if (!isAssignableTo(type, declaredReturnType))
            {
                const auto location = NodeSpans::expressionLocation(unit.nodeBuffer, tokens, value.child);
                if (!tryAddArrayLengthMismatchError(unit, location, declaredReturnType, type))
                {
                    m_diagnostics.addReturnTypeMismatchError(
                        unit.source,
                        location,
                        formatTypeName(m_module, declaredReturnType),
                        formatTypeName(m_module, type));
                }
            }

            auto returnedValue = materializeExpressionValue(unit, value.child, checkedValue);
            if (unit.nodeBuffer.kind(NodeLayout::stripGroupings(unit.nodeBuffer, value.child)) != NodeKind::NameExpression)
            {
                returnedValue = copyValueIfNeeded(unit, value.child, returnedValue, type);
            }
            else if (returnedNameNeedsCopy(unit, value.child, type))
            {
                returnedValue = emitCopyOfValue(returnedValue, type);
            }
            m_emitter.emitReturnValue(m_emitter.emitWidened(returnedValue, type.toValue(), declaredReturnType.toValue()));
        }
        else
        {
            recordType(unit, index, Type::Void());
            m_emitter.emitReturn();

            if (declaredReturnType != Type::Void())
            {
                const auto keywordToken = unit.nodeBuffer.mainToken(index);
                const auto semicolonToken = NodeSpans::findCloserToken(tokens, keywordToken + 1, TokenKind::Semicolon);
                const auto& returnKeywordLocation = TokenLocationAt(tokens, keywordToken);
                const auto& semicolonLocation = TokenLocationAt(tokens, semicolonToken);
                m_diagnostics.addReturnTypeMismatchError(
                    unit.source,
                    SourceLocation{ returnKeywordLocation.startIndex, semicolonLocation.endIndex },
                    formatTypeName(m_module, declaredReturnType),
                    formatTypeName(m_module, Type::Void()));
            }
        }
    }

    void TypeChecker::typeCheckStatementInNewScope(const CompilationUnit& unit, u32 index)
    {
        pushScope(currentScopeKind());
        if (unit.nodeBuffer.kind(index) == NodeKind::BlockNode)
        {
            typeCheckBlockNode(unit, index);
        }
        else
        {
            typeCheckStatement(unit, index);
        }

        // endIf merges the branches after this removal
        // a local of both branches otherwise gets a phi
        for (auto position = static_cast<size_t>(m_scopeFrames.back().boundNamesStart); position < m_boundNames.size(); ++position)
        {
            m_emitter.removeLocal(m_boundNames[position]);
        }
        popScope(true);
    }

    void TypeChecker::typeCheckBlockNode(const CompilationUnit& unit, u32 index)
    {
        const auto& tokens = m_compilationContext.tokenBuffer(unit.fileId);
        const auto childrenStart = m_childrenStack.size();
        NodeLayout::appendChildren(unit.nodeBuffer, index, m_childrenStack);
        const auto childrenEnd = m_childrenStack.size();

        auto reachedTerminator = false;
        auto warnedUnreachable = false;
        for (auto position = childrenStart; position < childrenEnd; ++position)
        {
            const auto child = m_childrenStack[position];
            if (unit.nodeBuffer.kind(child) == NodeKind::AnnotationNode)
            {
                continue;
            }

            if (reachedTerminator && !warnedUnreachable)
            {
                if (const auto location = NodeSpans::deadStatementLocation(unit.nodeBuffer, tokens, child); location.has_value())
                {
                    m_diagnostics.addUnreachableCodeWarning(unit.source, location.value());
                    warnedUnreachable = true;
                }
            }

            typeCheckStatement(unit, child);

            if (IsTerminatingStatement(unit.nodeBuffer, child))
            {
                reachedTerminator = true;
            }
        }

        m_childrenStack.resize(childrenStart);
    }

    Type TypeChecker::coerceConditionType(Type conditionType, const CompilationUnit& unit, u32 conditionIndex)
    {
        if (conditionType == m_module.wellKnown().boolean)
        {
            return conditionType;
        }

        if (conditionType.kind() == TypeKind::Enum)
        {
            auto& enumDefinition = m_module.getEnumDefinition(conditionType);
            if (enumDefinition.baseType() == m_module.wellKnown().boolean)
            {
                recordType(unit, conditionIndex, m_module.wellKnown().boolean);
                return m_module.wellKnown().boolean;
            }
        }

        return conditionType;
    }
}
