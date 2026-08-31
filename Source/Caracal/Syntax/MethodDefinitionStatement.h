#pragma once

#include <Caracal/API.h>
#include <Caracal/Syntax/Statement.h>
#include <Caracal/Syntax/Token.h>
#include <Caracal/Syntax/MethodNameNode.h>
#include <Caracal/Syntax/ParametersNode.h>
#include <Caracal/Syntax/ReturnTypesNode.h>
#include <Caracal/Syntax/BlockNode.h>
#include <Caracal/Syntax/AnnotationNode.h>
#include <Caracal/Syntax/MethodModifier.h>

#include <vector>

namespace Caracal
{
    enum class SpecialFunctionType
    {
        None,
        Constructor,
        Destructor
    };

    class CARACAL_API MethodDefinitionStatement : public Statement
    {
    public:
        MethodDefinitionStatement(
            const Token& keywordToken,
            MethodNameNodeUPtr&& methodNameNode,
            ParametersNodeUPtr&& parametersNode,
            ReturnTypesNodeUPtr&& returnTypesNode,
            BlockNodeUPtr&& bodyNode,
            MethodModifier modifier,
            SpecialFunctionType specialFunctionType,
            std::vector<AnnotationNodeUPtr>&& annotations);

        CARACAL_DELETE_COPY_DEFAULT_MOVE(MethodDefinitionStatement)

        [[nodiscard]] const Token& keywordToken() const noexcept { return m_keywordToken; }
        [[nodiscard]] const MethodNameNodeUPtr& methodNameNode() const noexcept { return m_methodNameNode; }
        [[nodiscard]] const ParametersNodeUPtr& parametersNode() const noexcept { return m_parametersNode; }
        [[nodiscard]] const ReturnTypesNodeUPtr& returnTypesNode() const noexcept { return m_returnTypesNode; }
        [[nodiscard]] const BlockNodeUPtr& bodyNode() const noexcept { return m_bodyNode; }
        [[nodiscard]] MethodModifier modifier() const noexcept { return m_modifier; }
        [[nodiscard]] SpecialFunctionType specialFunctionType() const noexcept { return m_specialFunctionType; }
        [[nodiscard]] const std::vector<AnnotationNodeUPtr>& annotations() const noexcept { return m_annotations; }
        [[nodiscard]] bool isExtern() const noexcept;


    private:
        Token m_keywordToken;
        MethodNameNodeUPtr m_methodNameNode;
        ParametersNodeUPtr m_parametersNode;
        ReturnTypesNodeUPtr m_returnTypesNode;
        BlockNodeUPtr m_bodyNode;
        MethodModifier m_modifier;
        SpecialFunctionType m_specialFunctionType;
        std::vector<AnnotationNodeUPtr> m_annotations;
    };

    [[nodiscard]] CARACAL_API std::string stringify(MethodModifier modifier);
    [[nodiscard]] CARACAL_API std::string stringify(SpecialFunctionType specialFunctionType);
}
