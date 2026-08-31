#include "TypeCheckerOld.h"
#include <Caracal/Profiling.h>

#include <Caracal/Constants.h>
#include <Caracal/ScopedValue.h>
#include <Caracal/Semantic/ArgumentBinderOld.h>
#include <Caracal/Semantic/SliceParameterPromotionOld.h>
#include <Caracal/Diagnostics/DiagnosticFormatting.h>
#include <Caracal/Semantic/NumberLiteralParsing.h>
#include <Caracal/Semantic/OperatorRegistrationOld.h>
#include <Caracal/Syntax/BoolLiteral.h>
#include <Caracal/Syntax/BreakStatement.h>
#include <Caracal/Syntax/GroupingExpression.h>
#include <Caracal/Syntax/SkipStatement.h>
#include <Caracal/Syntax/StringLiteral.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace Caracal
{
    static bool ShouldIgnoreUnusedVariableWarning(std::string_view name)
    {
        return name == "_" || name == ImplicitThisName;
    }

    static bool IsInitConstantAssignmentSite(std::string_view functionName)
    {
        return functionName == EntryPointFunctionName || functionName == UserMainFunctionName;
    }

    static BuiltinTypeDescription ExtractBuiltinTypeDescription(const std::vector<AnnotationNodeUPtr>& annotations)
    {
        BuiltinTypeDescription description{};
        for (const auto& annotation : annotations)
        {
            if (annotation->kind() != AnnotationKind::Builtin)
            {
                continue;
            }

            for (const auto& argument : annotation->arguments())
            {
                if (!argument.isNamed())
                {
                    continue;
                }

                const auto* value = argument.value().get();
                if (argument.name() == KindAnnotationArgumentName && value->kind() == NodeKind::NameExpression)
                {
                    const auto& kindName = static_cast<const NameExpression*>(value)->name();
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
                else if (argument.name() == BitsAnnotationArgumentName && value->kind() == NodeKind::NumberLiteral)
                {
                    const auto parsed = tryParseI32Literal(static_cast<const NumberLiteral*>(value)->literalLexeme());
                    if (parsed.has_value())
                    {
                        description.bits = parsed.value();
                    }
                }
                else if (argument.name() == SignedAnnotationArgumentName && value->kind() == NodeKind::BoolLiteral)
                {
                    description.isSigned = static_cast<const BoolLiteral*>(value)->value();
                }
            }
        }

        return description;
    }

    TypeCheckerOld::TypeCheckerOld(
        CompilationContext& compilationContext,
        bool isPreludePass,
        DiagnosticsBag& diagnostics)
        : m_compilationContext{ compilationContext }
        , m_parseTrees(compilationContext.parseTreesFor(isPreludePass))
        , m_options{ compilationContext.options() }
        , m_isPreludePass{ isPreludePass }
        , m_module{ compilationContext.semanticContext() }
        , m_diagnostics{ diagnostics }
        , m_currentReturnType{ Type::Void() }
        , m_currentType{ Type::Undefined() }
        , m_scopes{}
    {
        m_scopes.emplace_back(std::make_unique<Scope>(nullptr, ScopeKind::Global));
    }

    bool TypeCheckerOld::typeCheck()
    {
        if (m_diagnostics.hasErrors())
        {
            return false;
        }

        collectDeclarations();
        resolveWellKnownTypes();
        collectMethodDeclarations();

        typeCheckFunctionSignatures();
        typeCheckTypeSignatures();
        promoteReadOnlySliceParametersOld(m_functionDeclarations, m_typeDeclarations, m_module);
        typeCheckEnumDefinitions();
        typeCheckGlobalConstants();
        typeCheckTypeFieldDefinitions();
        typeCheckFunctionDefinitions();
        typeCheckTypeMethodDefinitions();

        checkUninitializedInitConstants();

        return !m_diagnostics.hasErrors();
    }

    void TypeCheckerOld::checkUninitializedInitConstants()
    {
        for (const auto& [name, declaration] : m_initConstants)
        {
            if (m_initConstantAssignments.contains(name))
                continue;

            m_diagnostics.addUninitializedInitConstantError(declaration.source, declaration.location, name);
        }
    }

    void TypeCheckerOld::collectDeclarations()
    {
        for (const auto& parseTree : m_parseTrees)
        {
            for (const auto& statement : parseTree->statements())
            {
                switch (statement->kind())
                {
                    case NodeKind::ConstantDeclaration:
                    {
                        auto* constantDeclaration = static_cast<ConstantDeclaration*>(statement.get());
                        m_globalConstantDeclarations.push_back(constantDeclaration);

                        break;
                    }
                    case NodeKind::VariableDeclaration:
                    {
                        auto* variableDeclaration = static_cast<VariableDeclaration*>(statement.get());
                        if (!variableDeclaration->annotations().empty())
                        {
                            for (const auto& annotation : variableDeclaration->annotations())
                            {
                                m_diagnostics.addUnexpectedAnnotationTargetError(
                                    parseTree->tokens().source(),
                                    annotation->sourceLocation(parseTree->tokens()));
                            }
                        }

                        break;
                    }
                    case NodeKind::EnumDefinitionStatement:
                    {
                        auto* enumStatement = static_cast<EnumDefinitionStatement*>(statement.get());
                        const auto& enumName = enumStatement->name();
                        auto existingType = m_module.tryGetTypeByName(enumName);
                        if (existingType != Type::Undefined())
                        {
                            auto otherSource = SourceTextSharedPtr{};
                            auto otherLocation = std::optional<SourceLocation>{};
                            if (existingType.kind() == TypeKind::Enum)
                            {
                                auto* otherStatement = m_module.getEnumDefinition(existingType).statement();
                                if (otherStatement != nullptr)
                                {
                                    otherSource = m_compilationContext.source(otherStatement->nameToken());
                                    otherLocation = m_compilationContext.sourceLocation(otherStatement->nameToken());
                                }
                            }
                            else if (existingType.kind() == TypeKind::Type)
                            {
                                auto* otherStatement = m_module.getTypeDefinition(existingType).statement();
                                if (otherStatement != nullptr)
                                {
                                    otherSource = m_compilationContext.source(otherStatement->nameToken());
                                    otherLocation = m_compilationContext.sourceLocation(otherStatement->nameToken());
                                }
                            }

                            m_diagnostics.addDuplicateTypeDeclarationError(
                                parseTree->tokens().source(),
                                parseTree->tokens().getSourceLocation(enumStatement->nameToken()),
                                enumName,
                                otherSource,
                                otherLocation);
                            break;
                        }

                        auto& enumDefinition = m_module.createEnum(enumName, enumStatement);
                        enumStatement->setType(enumDefinition.type());
                        m_enumDeclarations.push_back(enumStatement);

                        break;
                    }
                    case NodeKind::TypeDefinitionStatement:
                    {
                        auto* typeStatement = static_cast<TypeDefinitionStatement*>(statement.get());
                        if (typeStatement->isBuiltin())
                        {
                            const auto builtinType = m_module.tryGetTypeByName(typeStatement->name());
                            if (builtinType == Type::Undefined())
                            {
                                const auto description = ExtractBuiltinTypeDescription(typeStatement->annotations());
                                auto& typeDefinition = m_module.createBuiltinTypeFromDescription(typeStatement->name(), description, typeStatement);
                                typeStatement->setType(typeDefinition.type());
                                m_typeDeclarations.push_back(typeStatement);
                                break;
                            }

                            if (builtinType.kind() != TypeKind::Builtin || builtinType.id() < 0)
                            {
                                m_diagnostics.addNotABuiltinTypeError(
                                    parseTree->tokens().source(),
                                    parseTree->tokens().getSourceLocation(typeStatement->nameToken()),
                                    std::string(typeStatement->name()));
                                break;
                            }

                            const auto& existingDefinition = m_module.getTypeDefinition(builtinType);
                            if (existingDefinition.type() != Type::Undefined())
                            {
                                auto otherLocation = std::optional<SourceLocation>{};
                                const auto* otherStatement = existingDefinition.statement();
                                if (otherStatement != nullptr)
                                {
                                    otherLocation = m_compilationContext.sourceLocation(otherStatement->nameToken());
                                }

                                m_diagnostics.addDuplicateBuiltinTypeBindingError(
                                    parseTree->tokens().source(),
                                    parseTree->tokens().getSourceLocation(typeStatement->nameToken()),
                                    std::string(typeStatement->name()),
                                    otherLocation);
                                break;
                            }

                            m_module.bindBuiltinTypeDefinition(builtinType, typeStatement);
                            typeStatement->setType(builtinType);
                            m_typeDeclarations.push_back(typeStatement);
                            break;
                        }

                        auto existingType = m_module.tryGetTypeByName(typeStatement->name());
                        if (existingType != Type::Undefined())
                        {
                            auto otherSource = SourceTextSharedPtr{};
                            auto otherLocation = std::optional<SourceLocation>{};
                            if (existingType.kind() == TypeKind::Enum)
                            {
                                auto* otherStatement = m_module.getEnumDefinition(existingType).statement();
                                if (otherStatement != nullptr)
                                {
                                    otherSource = m_compilationContext.source(otherStatement->nameToken());
                                    otherLocation = m_compilationContext.sourceLocation(otherStatement->nameToken());
                                }
                            }
                            else if (existingType.kind() == TypeKind::Type)
                            {
                                auto* otherStatement = m_module.getTypeDefinition(existingType).statement();
                                if (otherStatement != nullptr)
                                {
                                    otherSource = m_compilationContext.source(otherStatement->nameToken());
                                    otherLocation = m_compilationContext.sourceLocation(otherStatement->nameToken());
                                }
                            }

                            m_diagnostics.addDuplicateTypeDeclarationError(
                                parseTree->tokens().source(),
                                parseTree->tokens().getSourceLocation(typeStatement->nameToken()),
                                std::string(typeStatement->name()),
                                otherSource,
                                otherLocation);
                            break;
                        }

                        auto& typeDefinition = m_module.createType(typeStatement->name(), typeStatement);
                        typeStatement->setType(typeDefinition.type());
                        m_typeDeclarations.push_back(typeStatement);

                        break;
                    }
                    case NodeKind::FunctionDefinitionStatement:
                    {
                        auto* functionStatement = static_cast<FunctionDefinitionStatement*>(statement.get());
                        const auto& functionName = functionStatement->name();
                        auto existingFunctionType = m_module.tryGetFunctionTypeByName(functionName);
                        if (existingFunctionType != Type::Undefined())
                        {
                            auto otherSource = SourceTextSharedPtr{};
                            auto otherLocation = std::optional<SourceLocation>{};
                            auto* otherStatement = static_cast<const FunctionDefinitionStatement*>(m_module.getFunctionDefinition(existingFunctionType).statement());
                            if (otherStatement != nullptr)
                            {
                                otherSource = m_compilationContext.source(otherStatement->nameToken());
                                otherLocation = m_compilationContext.sourceLocation(otherStatement->nameToken());
                            }

                            m_diagnostics.addDuplicateFunctionDeclarationError(
                                parseTree->tokens().source(),
                                parseTree->tokens().getSourceLocation(functionStatement->nameToken()),
                                functionName,
                                otherSource,
                                otherLocation);
                            break;
                        }

                        std::vector<Parameter> parameters{};
                        const auto& parametersNodes = functionStatement->parametersNode()->parameters();
                        for (const auto& parameterNode : parametersNodes)
                        {
                            parameters.emplace_back(parameterNode->name(), Type::Undefined());
                        }

                        auto& functionDefinition = m_module.createFunction(functionName, std::move(parameters), std::vector<Type>(), functionStatement);
                        functionStatement->setType(functionDefinition.type());
                        m_functionDeclarations.push_back(functionStatement);

                        break;
                    }
                    default:
                    {
                        break;
                    }
                }
            }
        }
    }

    void TypeCheckerOld::resolveWellKnownTypes()
    {
        m_module.refreshWellKnownTypes();
        m_defaultIntegerType = m_module.tryGetTypeByName(m_options.defaultIntegerType);
        m_defaultFloatingType = m_module.tryGetTypeByName(m_options.defaultFloatingType);
        m_defaultEnumBaseType = m_module.tryGetTypeByName(m_options.defaultEnumBaseType);
    }

    void TypeCheckerOld::collectMethodDeclarations()
    {
        for (const auto* typeDefinitionStatement : m_typeDeclarations)
        {
            const auto typeType = m_module.tryGetTypeByName(typeDefinitionStatement->name());
            if (typeType == Type::Undefined())
            {
                TODO("This shouldn't happen");
            }

            auto& typeDefinition = m_module.getTypeDefinition(typeType);
            const auto& bodyStatements = typeDefinitionStatement->bodyNode()->statements();
            for (const auto& bodyStatement : bodyStatements)
            {
                if (bodyStatement->kind() != NodeKind::MethodDefinitionStatement)
                {
                    continue;
                }

                auto* methodStatement = static_cast<MethodDefinitionStatement*>(bodyStatement.get());
                const auto modifier = methodStatement->modifier();
                const auto& methodName = methodStatement->methodNameNode()->methodName();
                if (methodStatement->specialFunctionType() == SpecialFunctionType::Constructor || methodName == "new")
                {
                    const auto& tokens = m_compilationContext.tokenBuffer(typeDefinitionStatement->nameToken());
                    auto constructorLocation = tokens.getSourceLocation(typeDefinitionStatement->nameToken());
                    if (typeDefinitionStatement->constructorParameters().has_value())
                    {
                        constructorLocation = typeDefinitionStatement->constructorParameters().value()->sourceLocation(tokens);
                    }

                    m_diagnostics.addTypeDotNewDeclarationError(
                        tokens.source(),
                        tokens.getSourceLocation(methodStatement->methodNameNode()->methodNameToken()),
                        constructorLocation,
                        std::string(typeDefinitionStatement->name()));
                    continue;
                }

                std::vector<Parameter> declarationParameters{};
                if (modifier != MethodModifier::Static)
                {
                    declarationParameters.emplace_back(ImplicitThisName, typeDefinition.type().toReference());
                }

                const auto& parameterNodes = methodStatement->parametersNode()->parameters();
                for (const auto& parameterNode : parameterNodes)
                {
                    declarationParameters.emplace_back(parameterNode->name(), Type::Undefined());
                }

                auto methodType = typeDefinition.tryGetMethodTypeByName(methodName);
                if (methodType != Type::Undefined())
                {
                    const auto& tokens = m_compilationContext.tokenBuffer(typeDefinitionStatement->nameToken());
                    auto otherLocation = std::optional<SourceLocation>{};
                    const auto* otherStatement = static_cast<const MethodDefinitionStatement*>(m_module.getFunctionDefinition(methodType).statement());
                    if (otherStatement != nullptr)
                    {
                        otherLocation = tokens.getSourceLocation(otherStatement->methodNameNode()->methodNameToken());
                    }

                    m_diagnostics.addDuplicateMethodDeclarationError(
                        tokens.source(),
                        tokens.getSourceLocation(methodStatement->methodNameNode()->methodNameToken()),
                        methodName,
                        typeDefinition.name(),
                        otherLocation);
                    continue;
                }

                auto& methodDefinition = m_module.createMethod(typeDefinition, modifier, methodName, std::move(declarationParameters), std::vector<Type>(), methodStatement);
                methodStatement->setType(methodDefinition.type());
            }

            if (typeDefinitionStatement->isBuiltin())
            {
                continue;
            }

            std::vector<Parameter> constructorParameters{};
            constructorParameters.emplace_back(ImplicitThisName, typeType.toReference());

            if (typeDefinitionStatement->constructorParameters().has_value())
            {
                const auto& parameterNodes = typeDefinitionStatement->constructorParameters().value()->parameters();
                for (const auto& parameterNode : parameterNodes)
                {
                    constructorParameters.emplace_back(parameterNode->name(), Type::Undefined());
                }
            }

            static_cast<void>(m_module.createConstructor(typeDefinition, std::move(constructorParameters)));
        }
    }

    void TypeCheckerOld::typeCheckFunctionSignatures()
    {
        for (const auto* functionDefinitionStatement : m_functionDeclarations)
        {
            auto* statement = const_cast<FunctionDefinitionStatement*>(functionDefinitionStatement);
            typeCheckFunctionSignature(statement, m_compilationContext.tokenBuffer(statement->nameToken()));
        }
    }

    void TypeCheckerOld::typeCheckTypeSignatures()
    {
        for (const auto* typeDefinitionStatement : m_typeDeclarations)
        {
            auto* statement = const_cast<TypeDefinitionStatement*>(typeDefinitionStatement);
            typeCheckTypeSignature(statement, m_compilationContext.tokenBuffer(statement->nameToken()));
        }
    }

    void TypeCheckerOld::typeCheckGlobalConstants()
    {
        for (const auto* constantDeclaration : m_globalConstantDeclarations)
        {
            auto* statement = const_cast<ConstantDeclaration*>(constantDeclaration);
            typeCheckConstantDeclaration(statement, m_compilationContext.tokenBuffer(statement->firstColonToken()));
        }
    }

    void TypeCheckerOld::typeCheckFunctionDefinitions()
    {
        for (const auto* functionDefinitionStatement : m_functionDeclarations)
        {
            auto* statement = const_cast<FunctionDefinitionStatement*>(functionDefinitionStatement);
            typeCheckFunctionDefinitionStatement(statement, m_compilationContext.tokenBuffer(statement->nameToken()));
        }
    }

    void TypeCheckerOld::typeCheckEnumDefinitions()
    {
        for (const auto* enumDefinitionStatement : m_enumDeclarations)
        {
            auto* statement = const_cast<EnumDefinitionStatement*>(enumDefinitionStatement);
            typeCheckEnumDefinitionStatement(statement, m_compilationContext.tokenBuffer(statement->nameToken()));
        }
    }

    void TypeCheckerOld::typeCheckTypeFieldDefinitions()
    {
        for (const auto* typeDefinitionStatement : m_typeDeclarations)
        {
            auto* statement = const_cast<TypeDefinitionStatement*>(typeDefinitionStatement);
            typeCheckTypeFieldDefinition(statement, m_compilationContext.tokenBuffer(statement->nameToken()));
        }
    }

    void TypeCheckerOld::typeCheckTypeMethodDefinitions()
    {
        for (const auto* typeDefinitionStatement : m_typeDeclarations)
        {
            auto* statement = const_cast<TypeDefinitionStatement*>(typeDefinitionStatement);
            typeCheckTypeMethodDefinition(statement, m_compilationContext.tokenBuffer(statement->nameToken()));
        }
    }

    void TypeCheckerOld::typeCheckFunctionSignature(FunctionDefinitionStatement* statement, const TokenBuffer& tokens)
    {
        pushScope(ScopeKind::Function);
        auto parameters = typeCheckParametersNode(statement->parametersNode().get(), tokens);
        auto returns = typeCheckReturnTypesNode(statement->returnTypesNode().get(), tokens);
        popScope(false);

        const auto& functionName = statement->name();
        auto functionType = m_module.tryGetFunctionTypeByName(functionName);
        if (functionType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& functionDefinition = m_module.getFunctionDefinition(functionType);
        const auto& parameterNodes = statement->parametersNode()->parameters();
        std::optional<std::string> symbolName;
        auto isRequiredExtern = false;
        const auto isExtern = validateCallableAnnotations(statement->annotations(), tokens, symbolName, isRequiredExtern);
        if (isExtern && isRequiredExtern)
        {
            m_module.markExternRequired(functionType);
        }
        const auto isVariadic = !parameterNodes.empty() && parameterNodes.back()->isVariadic();
        if (isVariadic && !isExtern)
        {
            auto* variadicParameter = parameterNodes.back().get();
            m_diagnostics.addNonExternVariadicFunctionError(
                tokens.source(),
                variadicParameter->sourceLocation(tokens),
                functionName);
        }
        functionDefinition.setParameters(std::move(parameters));
        functionDefinition.setReturnTypes(std::move(returns));
        functionDefinition.setIsVariadic(isVariadic);
        functionDefinition.setSymbolName(std::move(symbolName));
    }

    void TypeCheckerOld::typeCheckTypeSignature(TypeDefinitionStatement* statement, const TokenBuffer& tokens)
    {
        auto typeType = statement->type();
        if (typeType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        validateTypeAnnotation(statement, tokens);

        auto& typeDefinition = m_module.getTypeDefinition(typeType);
        if (statement->isBuiltin())
        {
            if (statement->constructorParameters().has_value())
            {
                m_diagnostics.addBuiltinTypeConstructorIgnoredWarning(
                    tokens.source(),
                    statement->constructorParameters().value()->sourceLocation(tokens),
                    std::string(statement->name()));
            }
        }
        else
        {
            typeCheckConstructorSignature(statement, typeDefinition, typeType, tokens);
        }

        const auto& bodyStatements = statement->bodyNode()->statements();
        for (const auto& bodyStatement : bodyStatements)
        {
            if (bodyStatement->kind() != NodeKind::MethodDefinitionStatement)
            {
                continue;
            }

            const auto* methodStatement = static_cast<const MethodDefinitionStatement*>(bodyStatement.get());
            if (methodStatement->specialFunctionType() == SpecialFunctionType::Constructor || methodStatement->methodNameNode()->methodName() == "new")
            {
                continue;
            }

            if (methodStatement->type() == Type::Undefined())
            {
                continue;
            }

            typeCheckMethodSignature(methodStatement, typeDefinition, typeType, tokens);

            if (statement->isBuiltin())
            {
                validateBuiltinMethodOld(methodStatement, typeDefinition, typeType, tokens, m_module, m_diagnostics);
            }
            else
            {
                validateStaticMethodTypeName(methodStatement, typeDefinition, tokens);
                registerOperatorMethodOld(methodStatement, typeDefinition, typeType, m_module);
            }
        }
    }

    void TypeCheckerOld::validateStaticMethodTypeName(const MethodDefinitionStatement* methodStatement, TypeDefinition& typeDefinition, const TokenBuffer& tokens)
    {
        const auto& methodNameNode = methodStatement->methodNameNode();
        if (!methodNameNode->hasTypeName())
        {
            return;
        }

        if (methodNameNode->typeName().value() == typeDefinition.name())
        {
            return;
        }

        m_diagnostics.addStaticMethodTypeNameMismatchError(
            tokens.source(),
            tokens.getSourceLocation(methodNameNode->typeNameToken().value()),
            methodNameNode->typeName().value(),
            typeDefinition.name(),
            methodNameNode->methodName());
    }

    void TypeCheckerOld::typeCheckTypeFieldDefinition(TypeDefinitionStatement* statement, const TokenBuffer& tokens)
    {
        auto typeType = statement->type();
        if (typeType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        if (statement->isBuiltin())
        {
            for (const auto& definitionStatement : statement->bodyNode()->statements())
            {
                if (definitionStatement->kind() != NodeKind::TypeFieldDeclaration)
                {
                    continue;
                }

                auto* fieldStatement = static_cast<TypeFieldDeclaration*>(definitionStatement.get());
                m_diagnostics.addBuiltinTypeFieldIgnoredWarning(
                    tokens.source(),
                    fieldStatement->nameExpression()->sourceLocation(tokens),
                    fieldStatement->nameExpression()->name());
            }

            return;
        }

        pushScope(ScopeKind::Type);

        if (statement->constructorParameters().has_value())
        {
            static_cast<void>(typeCheckParametersNode(statement->constructorParameters().value().get(), tokens));
        }

        auto& typeDefinition = m_module.getTypeDefinition(typeType);
        auto fieldIndex = 0;
        const auto& definitionStatements = statement->bodyNode()->statements();
        for (auto& definitionStatement : definitionStatements)
        {
            if (definitionStatement->kind() != NodeKind::TypeFieldDeclaration)
            {
                continue;
            }

            typeCheckTypeFieldDeclaration(typeDefinition, static_cast<TypeFieldDeclaration*>(definitionStatement.get()), fieldIndex, tokens);
            ++fieldIndex;
        }

        popScope(true);
    }

    void TypeCheckerOld::typeCheckTypeMethodDefinition(TypeDefinitionStatement* statement, const TokenBuffer& tokens)
    {
        auto typeType = statement->type();
        if (typeType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        if (statement->isBuiltin())
        {
            for (const auto& bodyStatement : statement->bodyNode()->statements())
            {
                if (bodyStatement->kind() != NodeKind::MethodDefinitionStatement)
                {
                    continue;
                }

                auto* methodStatement = static_cast<MethodDefinitionStatement*>(bodyStatement.get());
                if (!methodStatement->bodyNode()->statements().empty())
                {
                    m_diagnostics.addBuiltinMethodBodyIgnoredWarning(
                        tokens.source(),
                        tokens.getSourceLocation(methodStatement->methodNameNode()->methodNameToken()),
                        methodStatement->methodNameNode()->methodName());
                }
            }

            return;
        }

        const ScopedValue<Type> currentTypeScope{ m_currentType, typeType };
        pushScope(ScopeKind::Type);

        auto& typeDefinition = m_module.getTypeDefinition(typeType);
        for (const auto& fieldDefinition : typeDefinition.fields())
        {
            currentScope()->addVariableBinding(
                fieldDefinition.name(),
                fieldDefinition.type(),
                getTypeFieldLocation(typeDefinition, fieldDefinition, tokens),
                tokens.source(),
                VariableBindingKind::Field);
        }

        const auto& definitionStatements = statement->bodyNode()->statements();
        for (auto& definitionStatement : definitionStatements)
        {
            if (definitionStatement->kind() != NodeKind::MethodDefinitionStatement)
            {
                continue;
            }

            auto* methodStatement = static_cast<MethodDefinitionStatement*>(definitionStatement.get());
            if (methodStatement->specialFunctionType() == SpecialFunctionType::Constructor || methodStatement->methodNameNode()->methodName() == "new")
            {
                continue;
            }

            if (methodStatement->type() == Type::Undefined())
            {
                continue;
            }

            typeCheckMethodDefinitionStatement(methodStatement, tokens);
        }

        popScope(false);
    }

    void TypeCheckerOld::typeCheckMethodSignature(const MethodDefinitionStatement* methodStatement, TypeDefinition& typeDefinition, Type typeType, const TokenBuffer& tokens)
    {
        const auto& methodName = methodStatement->methodNameNode()->methodName();

        pushScope(ScopeKind::Method);
        auto parameters = typeCheckParametersNode(methodStatement->parametersNode().get(), tokens);
        auto returns = typeCheckReturnTypesNode(methodStatement->returnTypesNode().get(), tokens);
        popScope(false);

        auto methodType = typeDefinition.tryGetMethodTypeByName(methodName);
        if (methodType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& methodDefinition = m_module.getFunctionDefinition(methodType);

        std::optional<std::string> symbolName;
        auto isRequiredExtern = false;
        const auto isExtern = validateCallableAnnotations(methodStatement->annotations(), tokens, symbolName, isRequiredExtern);
        if (isExtern && isRequiredExtern)
        {
            m_module.markExternRequired(methodType);
        }
        if (isExtern && !symbolName.has_value())
        {
            // methods need to specify the extern symbol name
            for (const auto& annotation : methodStatement->annotations())
            {
                if (annotation->kind() == AnnotationKind::Extern)
                {
                    m_diagnostics.addExternMethodRequiresSymbolError(tokens.source(), annotation->sourceLocation(tokens), methodDefinition.fullName());
                    break;
                }
            }
        }
        const auto& parameterNodes = methodStatement->parametersNode()->parameters();
        const auto isVariadic = !parameterNodes.empty() && parameterNodes.back()->isVariadic();
        if (isVariadic && !isExtern)
        {
            auto* variadicParameter = parameterNodes.back().get();
            m_diagnostics.addNonExternVariadicFunctionError(
                tokens.source(),
                variadicParameter->sourceLocation(tokens),
                methodName);
        }

        if (methodStatement->modifier() != MethodModifier::Static)
        {
            parameters.insert(parameters.begin(), Parameter{ ImplicitThisName, typeType.toReference() });
        }
        methodDefinition.setParameters(std::move(parameters));
        methodDefinition.setReturnTypes(std::move(returns));
        methodDefinition.setIsVariadic(isVariadic);
        methodDefinition.setSymbolName(std::move(symbolName));
    }

    void TypeCheckerOld::typeCheckConstructorSignature(const TypeDefinitionStatement* typeDefinitionStatement, TypeDefinition& typeDefinition, Type typeType, const TokenBuffer& tokens)
    {
        auto constructorType = typeDefinition.tryGetMethodTypeByName("new");
        if (constructorType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        std::vector<Parameter> constructorParameters{};
        constructorParameters.emplace_back(ImplicitThisName, typeType.toReference());

        if (typeDefinitionStatement->constructorParameters().has_value())
        {
            pushScope(ScopeKind::Type);
            auto declaredConstructorParameters = typeCheckParametersNode(typeDefinitionStatement->constructorParameters().value().get(), tokens);
            popScope(false);

            constructorParameters.insert(
                constructorParameters.end(),
                declaredConstructorParameters.begin(),
                declaredConstructorParameters.end());
        }

        auto& constructorDefinition = m_module.getFunctionDefinition(constructorType);
        constructorDefinition.setParameters(std::move(constructorParameters));
        constructorDefinition.setReturnTypes(std::vector<Type>{});
    }

    void TypeCheckerOld::typeCheckStatement(Statement* statement, const TokenBuffer& tokens)
    {
        switch (statement->kind())
        {
            case NodeKind::ConstantDeclaration:
            {
                typeCheckConstantDeclaration(static_cast<ConstantDeclaration*>(statement), tokens);
                break;
            }
            case NodeKind::VariableDeclaration:
            {
                typeCheckVariableDeclaration(static_cast<VariableDeclaration*>(statement), tokens);
                break;
            }
            case NodeKind::ExpressionStatement:
            {
                typeCheckExpressionStatement(static_cast<ExpressionStatement*>(statement), tokens);
                break;
            }
            case NodeKind::AssignmentStatement:
            {
                typeCheckAssignmentStatement(static_cast<AssignmentStatement*>(statement), tokens);
                break;
            }
            case NodeKind::FunctionDefinitionStatement:
            {
                typeCheckFunctionDefinitionStatement(static_cast<FunctionDefinitionStatement*>(statement), tokens);
                break;
            }
            case NodeKind::EnumDefinitionStatement:
            {
                typeCheckEnumDefinitionStatement(static_cast<EnumDefinitionStatement*>(statement), tokens);
                break;
            }
            case NodeKind::IfStatement:
            {
                typeCheckIfStatement(static_cast<IfStatement*>(statement), tokens);
                break;
            }
            case NodeKind::WhileStatement:
            {
                typeCheckWhileStatement(static_cast<WhileStatement*>(statement), tokens);
                break;
            }
            case NodeKind::ReturnStatement:
            {
                typeCheckReturnStatement(static_cast<ReturnStatement*>(statement), tokens);
                break;
            }
            case NodeKind::BlockNode:
            {
                typeCheckBlockNode(static_cast<BlockNode*>(statement), tokens);
                break;
            }
            default:
            {
                break;
            }
        }
    }

    [[nodiscard]] static bool ContainsRuntimeCall(const Expression* expression, const SemanticContext& module) noexcept
    {
        if (expression == nullptr)
        {
            return false;
        }

        switch (expression->kind())
        {
            case NodeKind::FunctionCallExpression:
            {
                const auto* call = static_cast<const FunctionCallExpression*>(expression);
                if (call->functionType() != Type::Undefined())
                {
                    const auto* definition = module.tryGetFunctionDefinition(call->functionType());
                    if (definition != nullptr && isBitwiseIntrinsicKind(definition->intrinsicKind()))
                    {
                        // foldable builtins are fine but arguments can force this into a runtime call
                        for (const auto& argument : call->arguments())
                        {
                            if (ContainsRuntimeCall(argument.value().get(), module))
                            {
                                return true;
                            }
                        }

                        return false;
                    }
                }

                return true;
            }
            case NodeKind::GroupingExpression:
            {
                return ContainsRuntimeCall(static_cast<const GroupingExpression*>(expression)->expression().get(), module);
            }
            case NodeKind::UnaryExpression:
            {
                return ContainsRuntimeCall(static_cast<const UnaryExpression*>(expression)->expression().get(), module);
            }
            case NodeKind::BinaryExpression:
            {
                const auto* binaryExpression = static_cast<const BinaryExpression*>(expression);
                return ContainsRuntimeCall(binaryExpression->leftExpression().get(), module)
                    || ContainsRuntimeCall(binaryExpression->rightExpression().get(), module);
            }
            case NodeKind::MemberAccessExpression:
            {
                return ContainsRuntimeCall(static_cast<const MemberAccessExpression*>(expression)->expression().get(), module);
            }
            case NodeKind::ArrayLiteral:
            {
                for (const auto& element : static_cast<const ArrayLiteral*>(expression)->elements())
                {
                    if (ContainsRuntimeCall(element.get(), module))
                    {
                        return true;
                    }
                }

                return false;
            }
            default:
            {
                return false;
            }
        }
    }

    [[nodiscard]] static bool IsConstructorCallExpression(const Expression* expression) noexcept
    {
        const auto* stripped = stripGroupings(expression);
        return stripped != nullptr
            && stripped->kind() == NodeKind::BinaryExpression
            && static_cast<const BinaryExpression*>(stripped)->binaryOperator() == BinaryOperatorKind::ConstructorCall;
    }

    void TypeCheckerOld::typeCheckConstantDeclaration(ConstantDeclaration* statement, const TokenBuffer& tokens)
    {
        if (statement->isGlobalConstant())
        {
            for (const auto& annotation : statement->annotations())
            {
                m_diagnostics.addUnexpectedAnnotationTargetError(
                    tokens.source(),
                    annotation->sourceLocation(tokens));
            }
        }

        if (statement->isInit())
        {
            auto declaredType = Type::Undefined();
            if (statement->explicitType().has_value())
            {
                declaredType = typeCheckTypeNameNode(statement->explicitType().value().get(), tokens);
            }

            const auto isValidInitConstant = statement->isGlobalConstant();
            auto* leftExpression = statement->leftExpression().get();
            if (leftExpression->kind() == NodeKind::NameExpression)
            {
                auto* nameExpression = static_cast<NameExpression*>(leftExpression);
                const auto& name = nameExpression->name();

                if (!isValidInitConstant)
                {
                    m_diagnostics.addNonGlobalInitConstantError(
                        tokens.source(),
                        nameExpression->sourceLocation(tokens),
                        name);
                }

                auto scope = currentScope();
                if (!scope->hasVariableBinding(name))
                {
                    scope->addVariableBinding(name, declaredType, nameExpression->sourceLocation(tokens), tokens.source(), VariableBindingKind::LocalConstant);

                    if (isValidInitConstant)
                    {
                        static_cast<void>(m_module.createInitConstant(name, declaredType));
                        m_initConstants.insert_or_assign(name, InitConstantDeclaration{ nameExpression->sourceLocation(tokens), tokens.source() });
                    }
                }
                else
                {
                    m_diagnostics.addDuplicateConstantDeclarationError(
                        tokens.source(),
                        nameExpression->sourceLocation(tokens),
                        name,
                        scope->tryGetVariableBindingSource(name),
                        scope->tryGetVariableBindingLocation(name));
                }
                nameExpression->setType(declaredType);
            }

            statement->setType(declaredType);
            return;
        }

        auto declaredType = Type::Undefined();
        if (statement->explicitType().has_value())
        {
            declaredType = typeCheckTypeNameNode(statement->explicitType().value().get(), tokens);
        }

        auto rightExpression = statement->rightExpression().get();
        auto rightType = Type::Undefined();
        {
            // use the explicit type for the initializer
            const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, contextualTypeForExpectedType(declaredType) };
            rightType = typeCheckExpression(rightExpression, tokens);
        }

        // the declared type wins for the binding, the initializer only has to be assignable to it
        auto bindingType = rightType;
        if (declaredType != Type::Undefined())
        {
            bindingType = declaredType;
        }

        auto leftExpression = statement->leftExpression().get();
        if (leftExpression->kind() == NodeKind::NameExpression)
        {
            auto nameExpression = static_cast<NameExpression*>(leftExpression);
            const auto& name = nameExpression->name();
            if (bindingType == Type::Void())
            {
                m_diagnostics.addCallReturnsNoValueError(tokens.source(), nameExpression->sourceLocation(tokens), name);
                nameExpression->setType(Type::Undefined());
                statement->setType(Type::Undefined());
                return;
            }

            if (statement->isGlobalConstant()
                && ContainsRuntimeCall(rightExpression, m_module)
                && !IsConstructorCallExpression(rightExpression))
            {
                m_diagnostics.addGlobalConstantWithCallError(
                    tokens.source(),
                    rightExpression->sourceLocation(tokens),
                    name);
            }

            auto scope = currentScope();
            if (!scope->hasVariableBinding(name))
            {
                scope->addVariableBinding(name, bindingType, nameExpression->sourceLocation(tokens), tokens.source(), VariableBindingKind::LocalConstant);

                if (statement->isGlobalConstant())
                {
                    // TODO maybe remove the return value from the function?
                    static_cast<void>(m_module.createConstant(name, rightExpression));
                }
            }
            else
            {
                m_diagnostics.addDuplicateConstantDeclarationError(
                    tokens.source(),
                    nameExpression->sourceLocation(tokens),
                    name,
                    scope->tryGetVariableBindingSource(name),
                    scope->tryGetVariableBindingLocation(name));
            }
            nameExpression->setType(bindingType);
        }
        else if (statement->isGlobalConstant() && leftExpression->kind() == NodeKind::DiscardLiteral)
        {
            // a discarded global still needs to evaluate for potential side effects
            m_module.addGlobalDiscardExpression(rightExpression);
        }

        if (declaredType != Type::Undefined() && rightType != Type::Undefined() && !isAssignableTo(rightType, declaredType))
        {
            const auto location = statement->rightExpression()->sourceLocation(tokens);
            if (!tryAddArrayLengthMismatchError(tokens, location, declaredType, rightType))
            {
                m_diagnostics.addExplicitConstantTypeMismatchError(
                    tokens.source(),
                    location,
                    formatTypeName(m_module, declaredType),
                    formatTypeName(m_module, rightType));
            }
        }

        statement->setType(bindingType);
    }

    void TypeCheckerOld::typeCheckVariableDeclaration(VariableDeclaration* statement, const TokenBuffer& tokens)
    {
        auto declaredType = Type::Undefined();
        if (statement->explicitType().has_value())
        {
            declaredType = typeCheckTypeNameNode(statement->explicitType().value().get(), tokens);
        }

        auto* rightExpression = statement->rightExpression().get();
        auto rightType = Type::Undefined();
        {
            // use the explicit type for the initializer
            const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, contextualTypeForExpectedType(declaredType) };
            rightType = typeCheckExpression(rightExpression, tokens);
        }

        // the declared type wins for the binding, the initializer only has to be assignable to it
        auto bindingType = rightType;
        if (declaredType != Type::Undefined())
        {
            bindingType = declaredType;
        }

        auto referencesConstant = false;
        const auto* strippedRight = stripGroupings(rightExpression);
        if (strippedRight->kind() == NodeKind::UnaryExpression)
        {
            const auto* unary = static_cast<const UnaryExpression*>(strippedRight);
            referencesConstant = unary->unaryOperator() == UnaryOperatorKind::ReferenceOf && unary->referencesConstant();
        }
        else if (strippedRight->kind() == NodeKind::NameExpression)
        {
            referencesConstant = currentScope()->variableReferencesConstant(static_cast<const NameExpression*>(strippedRight)->name());
        }
        else if (strippedRight->kind() == NodeKind::BinaryExpression)
        {
            // a slice() view borrows its receiver, a view of a constant stays constant
            const auto* binaryRight = static_cast<const BinaryExpression*>(strippedRight);
            if (binaryRight->binaryOperator() == BinaryOperatorKind::MemberAccess
                && binaryRight->rightExpression()->kind() == NodeKind::FunctionCallExpression
                && binaryRight->leftExpression()->kind() == NodeKind::NameExpression)
            {
                const auto* callExpression = static_cast<const FunctionCallExpression*>(binaryRight->rightExpression().get());
                if (callExpression->functionType() != Type::Undefined()
                    && m_module.getFunctionDefinition(callExpression->functionType()).intrinsicKind() == IntrinsicKind::ArraySlice)
                {
                    const auto& receiverName = static_cast<const NameExpression*>(binaryRight->leftExpression().get())->name();
                    referencesConstant = currentScope()->tryGetVariableBindingKind(receiverName) == VariableBindingKind::LocalConstant
                        || currentScope()->variableReferencesConstant(receiverName);
                }
            }
        }

        auto leftExpression = statement->leftExpression().get();
        if (leftExpression->kind() == NodeKind::NameExpression)
        {
            auto nameExpression = static_cast<NameExpression*>(leftExpression);
            const auto& name = nameExpression->name();
            if (bindingType == Type::Void())
            {
                m_diagnostics.addCallReturnsNoValueError(tokens.source(), nameExpression->sourceLocation(tokens), name);
                nameExpression->setType(Type::Undefined());
                statement->setType(Type::Undefined());
                return;
            }

            auto scope = currentScope();
            if (!scope->hasVariableBinding(name))
            {
                scope->addVariableBinding(name, bindingType, nameExpression->sourceLocation(tokens), tokens.source(), VariableBindingKind::LocalVariable, referencesConstant);
            }
            else
            {
                m_diagnostics.addDuplicateVariableDeclarationError(
                    tokens.source(),
                    nameExpression->sourceLocation(tokens),
                    name,
                    scope->tryGetVariableBindingSource(name),
                    scope->tryGetVariableBindingLocation(name));
            }
            nameExpression->setType(bindingType);
        }

        if (declaredType != Type::Undefined() && rightType != Type::Undefined() && !isAssignableTo(rightType, declaredType))
        {
            const auto location = statement->rightExpression()->sourceLocation(tokens);
            if (!tryAddArrayLengthMismatchError(tokens, location, declaredType, rightType))
            {
                m_diagnostics.addExplicitVariableTypeMismatchError(
                    tokens.source(),
                    location,
                    formatTypeName(m_module, declaredType),
                    formatTypeName(m_module, rightType));
            }
        }

        statement->setType(bindingType);
    }

    void TypeCheckerOld::typeCheckExpressionStatement(ExpressionStatement* statement, const TokenBuffer& tokens)
    {
        auto expressionType = typeCheckExpression(statement->expression().get(), tokens);
        statement->setType(expressionType);
    }

    bool TypeCheckerOld::receiverChainIsConstant(const Expression* expression, std::string& rootName, bool& referencesConstant)
    {
        const auto* stripped = stripGroupings(expression);
        if (stripped == nullptr)
        {
            return false;
        }

        if (stripped->kind() == NodeKind::NameExpression)
        {
            const auto& name = static_cast<const NameExpression*>(stripped)->name();
            if (currentScope()->tryGetVariableBindingKind(name) == VariableBindingKind::LocalConstant)
            {
                rootName = name;
                referencesConstant = false;
                return true;
            }

            if (currentScope()->variableReferencesConstant(name))
            {
                rootName = name;
                referencesConstant = true;
                return true;
            }

            return false;
        }

        if (stripped->kind() == NodeKind::BinaryExpression)
        {
            const auto* memberAccess = static_cast<const BinaryExpression*>(stripped);
            if (memberAccess->binaryOperator() != BinaryOperatorKind::MemberAccess
                || memberAccess->rightExpression()->kind() != NodeKind::NameExpression)
            {
                return false;
            }

            // a constant anywhere along the chain makes the whole tail unwritable
            if (receiverChainIsConstant(memberAccess->leftExpression().get(), rootName, referencesConstant))
            {
                return true;
            }

            auto receiverType = memberAccess->leftExpression()->type().toValue();
            if (receiverType.kind() != TypeKind::Type)
            {
                return false;
            }

            const auto& fieldName = static_cast<const NameExpression*>(memberAccess->rightExpression().get())->name();
            const auto& fieldDefinition = m_module.getTypeDefinition(receiverType).tryGetFieldByName(fieldName);
            if (fieldDefinition.type() != Type::Undefined() && fieldDefinition.isConstant())
            {
                rootName = fieldName;
                referencesConstant = false;
                return true;
            }
        }

        return false;
    }

    void TypeCheckerOld::typeCheckAssignmentStatement(AssignmentStatement* statement, const TokenBuffer& tokens)
    {
        auto* leftExpression = statement->leftExpression().get();
        auto leftType = typeCheckExpression(leftExpression, tokens);
        if (leftType.isReference())
        {
            leftType = leftType.toValue();
        }

        // global init constants are only allowed to be assigned once in caracalMain/main 
        if (leftExpression->kind() == NodeKind::NameExpression)
        {
            const auto& targetName = static_cast<const NameExpression*>(leftExpression)->name();
            if (m_initConstants.contains(targetName))
            {
                const auto location = leftExpression->sourceLocation(tokens);
                if (!IsInitConstantAssignmentSite(m_currentFunctionName))
                {
                    m_diagnostics.addAssignmentToInitConstantError(tokens.source(), location, targetName);
                }
                else if (const auto existing = m_initConstantAssignments.find(targetName); existing != m_initConstantAssignments.end())
                {
                    m_diagnostics.addInitConstantAlreadyInitializedError(tokens.source(), location, targetName, existing->second);
                }
                else
                {
                    m_initConstantAssignments.emplace(targetName, location);
                }
            }
            else if (currentScope()->tryGetVariableBindingKind(targetName) == VariableBindingKind::LocalConstant)
            {
                m_diagnostics.addAssignmentToConstantError(
                    tokens.source(),
                    leftExpression->sourceLocation(tokens),
                    targetName);
            }
            else if (currentScope()->variableReferencesConstant(targetName))
            {
                m_diagnostics.addAssignmentThroughConstantReferenceError(
                    tokens.source(),
                    leftExpression->sourceLocation(tokens),
                    targetName);
            }
        }
        else if (leftExpression->kind() == NodeKind::BinaryExpression)
        {
            const auto* memberAccess = static_cast<const BinaryExpression*>(leftExpression);
            if (memberAccess->binaryOperator() == BinaryOperatorKind::MemberAccess &&
                memberAccess->rightExpression()->kind() == NodeKind::NameExpression)
            {
                auto receiverType = memberAccess->leftExpression()->type();
                if (receiverType.isReference())
                {
                    receiverType = receiverType.toValue();
                }

                if (receiverType.kind() == TypeKind::Type && leftExpression->type() != Type::Undefined())
                {
                    auto& typeDefinition = m_module.getTypeDefinition(receiverType);
                    const auto& fieldName = static_cast<const NameExpression*>(memberAccess->rightExpression().get())->name();
                    const auto& fieldDefinition = typeDefinition.tryGetFieldByName(fieldName);
                    if (fieldDefinition.type() != Type::Undefined() && fieldDefinition.isConstant())
                    {
                        m_diagnostics.addAssignmentToConstantError(
                            tokens.source(),
                            leftExpression->sourceLocation(tokens),
                            fieldName);
                    }
                    else
                    {
                        // a constant receiver makes every field along the chain unwritable
                        auto rootName = std::string{};
                        auto rootReferencesConstant = false;
                        if (receiverChainIsConstant(memberAccess->leftExpression().get(), rootName, rootReferencesConstant))
                        {
                            if (rootReferencesConstant)
                            {
                                m_diagnostics.addAssignmentThroughConstantReferenceError(
                                    tokens.source(),
                                    leftExpression->sourceLocation(tokens),
                                    rootName);
                            }
                            else
                            {
                                m_diagnostics.addAssignmentToConstantError(
                                    tokens.source(),
                                    leftExpression->sourceLocation(tokens),
                                    rootName);
                            }
                        }
                    }
                }
            }
        }
        auto rightType = Type::Undefined();
        {
            // use the assigned variable's type for the value
            const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, contextualTypeForExpectedType(leftType) };
            rightType = typeCheckExpression(statement->rightExpression().get(), tokens);
        }
        if (rightType.isReference())
        {
            rightType = rightType.toValue();
        }

        if (leftType == Type::Discard())
            return;

        // either side being undefined means its error was already reported
        if (leftType == Type::Undefined() || rightType == Type::Undefined())
            return;

        if (!isAssignableTo(rightType, leftType))
        {
            const auto location = statement->rightExpression()->sourceLocation(tokens);
            if (!tryAddArrayLengthMismatchError(tokens, location, leftType, rightType))
            {
                m_diagnostics.addAssignmentTypeMismatchError(
                    tokens.source(),
                    location,
                    formatTypeName(m_module, leftType),
                    formatTypeName(m_module, rightType));
            }
        }
    }

    static bool ConditionIsLiteralTrue(const Expression* condition)
    {
        condition = stripGroupings(condition);
        return condition != nullptr && condition->kind() == NodeKind::BoolLiteral && static_cast<const BoolLiteral*>(condition)->value();
    }

    static bool ContainsLoopBreak(const Statement* statement)
    {
        switch (statement->kind())
        {
            case NodeKind::BreakStatement:
            {
                return true;
            }
            case NodeKind::BlockNode:
            {
                for (const auto& inner : static_cast<const BlockNode*>(statement)->statements())
                {
                    if (ContainsLoopBreak(inner.get()))
                    {
                        return true;
                    }
                }
                return false;
            }
            case NodeKind::IfStatement:
            {
                const auto* ifStatement = static_cast<const IfStatement*>(statement);
                if (ContainsLoopBreak(ifStatement->trueStatement().get()))
                {
                    return true;
                }

                return ifStatement->falseStatement().has_value() && ContainsLoopBreak(ifStatement->falseStatement().value().get());
            }
            default:
                return false;
        }
    }

    static bool StatementGuaranteesReturn(const Statement* statement)
    {
        switch (statement->kind())
        {
            case NodeKind::ReturnStatement:
            {
                return true;
            }
            case NodeKind::BlockNode:
            {
                for (const auto& inner : static_cast<const BlockNode*>(statement)->statements())
                {
                    if (StatementGuaranteesReturn(inner.get()))
                    {
                        return true;
                    }
                }
                return false;
            }
            case NodeKind::IfStatement:
            {
                const auto* ifStatement = static_cast<const IfStatement*>(statement);
                return ifStatement->falseStatement().has_value()
                    && StatementGuaranteesReturn(ifStatement->trueStatement().get())
                    && StatementGuaranteesReturn(ifStatement->falseStatement().value().get());
            }
            case NodeKind::WhileStatement:
            {
                const auto* whileStatement = static_cast<const WhileStatement*>(statement);
                return ConditionIsLiteralTrue(whileStatement->condition().get())
                    && !ContainsLoopBreak(whileStatement->trueStatement().get());
            }
            default:
                return false;
        }
    }

    static bool HasExternAnnotation(const std::vector<AnnotationNodeUPtr>& annotations)
    {
        for (const auto& annotation : annotations)
        {
            if (annotation->kind() == AnnotationKind::Extern)
            {
                return true;
            }
        }

        return false;
    }

    void TypeCheckerOld::typeCheckFunctionDefinitionStatement(FunctionDefinitionStatement* statement, const TokenBuffer& tokens)
    {
        const ScopedValue<Type> returnTypeScope{ m_currentReturnType, Type::Void() };
        auto parentScope = currentScope();
        pushScope(ScopeKind::Function);

        auto functionType = statement->type();
        if (functionType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& functionDefinition = m_module.getFunctionDefinition(functionType);
        auto& parameters = functionDefinition.parameters();
        const auto& parameterNodes = statement->parametersNode()->parameters();
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto location = std::optional<SourceLocation>{};
            if (i < parameterNodes.size())
            {
                location = parameterNodes[i]->sourceLocation(tokens);
            }
            currentScope()->addVariableBinding(parameters[i].name(), parameters[i].type(), location, tokens.source(), VariableBindingKind::Parameter);
        }

        auto& returnTypes = functionDefinition.returnTypes();
        if (returnTypes.size() == 1)
        {
            m_currentReturnType = returnTypes[0];
        }
        else if (returnTypes.size() > 1)
        {
            TODO("Handle multiple return types in function definition");
        }

        m_currentFunctionName = functionDefinition.fullName();
        typeCheckBlockNode(statement->bodyNode().get(), tokens);
        m_currentFunctionName.clear();

        if (!statement->isExtern()
            && m_currentReturnType != Type::Void()
            && m_currentReturnType != Type::Undefined()
            && !StatementGuaranteesReturn(statement->bodyNode().get()))
        {
            m_diagnostics.addMissingReturnError(
                tokens.source(),
                tokens.getSourceLocation(statement->nameToken()),
                functionDefinition.name(),
                formatTypeName(m_module, m_currentReturnType));
        }

        popScope(!statement->isExtern());
    }

    void TypeCheckerOld::typeCheckEnumDefinitionStatement(EnumDefinitionStatement* statement, const TokenBuffer& tokens)
    {
        const auto& enumName = statement->name();
        auto baseType = Type::Undefined();
        auto defaultBaseType = m_defaultEnumBaseType;
        auto enumType = statement->type();
        if (enumType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        auto& enumDefinition = m_module.getEnumDefinition(enumType);
        auto isFlag = false;
        auto stepValue = std::optional<i32>{};
        validateEnumAnnotation(statement, tokens, isFlag, stepValue);
        auto currentFieldValue = isFlag ? 1 : 0;
        auto step = stepValue.value_or(1);

        if (statement->baseType().has_value())
        {
            baseType = typeCheckTypeNameNode(statement->baseType().value().get(), tokens);
            enumDefinition.setBaseType(baseType);
        }

        const auto& fieldNodes = statement->fieldNodes();
        for (auto& fieldNode : fieldNodes)
        {
            const auto& fieldName = fieldNode->name();
            if (const auto* otherField = enumDefinition.tryGetFieldByName(fieldName); otherField != nullptr)
            {
                m_diagnostics.addDuplicateEnumFieldDeclarationError(
                    tokens.source(),
                    tokens.getSourceLocation(fieldNode->nameToken()),
                    fieldName,
                    otherField->location());
                continue;
            }

            const auto fieldLocation = tokens.getSourceLocation(fieldNode->nameToken());
            if (isFlag)
            {
                if (fieldNode->valueExpression().has_value())
                {
                    m_diagnostics.addFlagEnumExplicitValueError(
                        tokens.source(),
                        fieldNode->valueExpression().value()->sourceLocation(tokens),
                        fieldName);
                }

                enumDefinition.addField(fieldName, currentFieldValue, fieldLocation);
                fieldNode->setValue(currentFieldValue);

                if (currentFieldValue == 0)
                {
                    currentFieldValue = 1;
                }
                else
                {
                    currentFieldValue *= 2;
                }
            }
            else if (fieldNode->valueExpression().has_value())
            {
                auto expression = fieldNode->valueExpression().value().get();
                auto contextualType = defaultBaseType;
                if (baseType != Type::Undefined())
                {
                    contextualType = baseType;
                }
                else if (expression->kind() == NodeKind::StringLiteral)
                {
                    // TODO enum string values are cstrings for now until enums can handle any types
                    contextualType = m_module.wellKnown().cstring;
                }

                auto fieldValueType = Type::Undefined();
                {
                    const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, contextualType };
                    fieldValueType = typeCheckExpression(expression, tokens);
                }
                if (baseType == Type::Undefined())
                {
                    baseType = fieldValueType;
                    enumDefinition.setBaseType(baseType);
                }
                else if (fieldValueType != baseType)
                {
                    if (fieldValueType != Type::Undefined())
                    {
                        m_diagnostics.addEnumFieldValueTypeMismatchError(
                            tokens.source(),
                            expression->sourceLocation(tokens),
                            formatTypeName(m_module, baseType),
                            formatTypeName(m_module, fieldValueType));
                    }
                }

                if (expression->kind() == NodeKind::NumberLiteral)
                {
                    const auto value = tryConvertEnumFieldLiteralValue(*static_cast<NumberLiteral*>(expression));
                    if (value.has_value())
                    {
                        enumDefinition.addField(fieldName, value.value(), fieldLocation);
                        fieldNode->setValue(value.value());
                        currentFieldValue = value.value() + step;
                    }
                    else
                    {
                        enumDefinition.addField(fieldName, expression, fieldLocation);
                    }
                }
                else
                {
                    enumDefinition.addField(fieldName, expression, fieldLocation);
                }
            }
            else
            {
                enumDefinition.addField(fieldName, currentFieldValue, fieldLocation);
                fieldNode->setValue(currentFieldValue);
                currentFieldValue += step;
            }
        }

        if (enumDefinition.baseType() == Type::Undefined())
        {
            enumDefinition.setBaseType(defaultBaseType);
        }
    }

    void TypeCheckerOld::typeCheckTypeFieldDeclaration(TypeDefinition& typeDefinition, TypeFieldDeclaration* statement, i32 fieldIndex, const TokenBuffer& tokens)
    {
        const auto& fieldName = statement->nameExpression()->name();
        const auto& existingField = typeDefinition.tryGetFieldByName(fieldName);
        if (existingField.type() != Type::Undefined())
        {
            m_diagnostics.addDuplicateTypeFieldDeclarationError(
                tokens.source(),
                statement->nameExpression()->sourceLocation(tokens),
                fieldName,
                getTypeFieldLocation(typeDefinition, existingField, tokens));
            return;
        }

        auto fieldType = Type::Undefined();
        if (statement->explicitType().has_value())
        {
            fieldType = typeCheckTypeNameNode(statement->explicitType().value().get(), tokens);
        }

        auto fieldExpression = statement->rightExpression().get();
        auto expressionType = Type::Undefined();
        {
            // use the explicit field type for the initializer
            const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, contextualTypeForExpectedType(fieldType) };
            expressionType = typeCheckExpression(fieldExpression, tokens);
        }
        
        if (fieldType == Type::Undefined())
        {
            fieldType = expressionType;
        }
        else if (expressionType != Type::Undefined() && !isAssignableTo(expressionType, fieldType))
        {
            const auto location = fieldExpression->sourceLocation(tokens);
            if (!tryAddArrayLengthMismatchError(tokens, location, fieldType, expressionType))
            {
                m_diagnostics.addTypeFieldInitializerMismatchError(
                    tokens.source(),
                    location,
                    formatTypeName(m_module, fieldType),
                    formatTypeName(m_module, expressionType));
            }
        }

        if (fieldType == Type::Undefined())
        {
            return;
        }

        // ownership rule: we dont allow storing references or slices in types
        if (fieldType.isReference() || fieldType.kind() == TypeKind::Slice)
        {
            m_diagnostics.addReferenceOrSliceFieldError(
                tokens.source(),
                statement->nameExpression()->sourceLocation(tokens));
        }

        statement->nameExpression()->setType(fieldType);
        statement->setType(fieldType);
        typeDefinition.addField(fieldType, fieldName, fieldIndex, fieldExpression, statement->isConstant());
    }

    void TypeCheckerOld::typeCheckMethodDefinitionStatement(MethodDefinitionStatement* statement, const TokenBuffer& tokens)
    {
        const ScopedValue<Type> returnTypeScope{ m_currentReturnType, Type::Void() };
        auto methodType = statement->type();
        if (methodType == Type::Undefined())
        {
            TODO("This shouldn't happen");
        }

        if (statement->modifier() == MethodModifier::Static)
        {
            pushScope(ScopeKind::StaticMethod);
        }
        else
        {
            pushScope(ScopeKind::Method);
        }

        auto& methodDefinition = m_module.getFunctionDefinition(methodType);
        auto& parameters = methodDefinition.parameters();
        const auto& parameterNodes = statement->parametersNode()->parameters();
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            auto location = std::optional<SourceLocation>{};
            if (i < parameterNodes.size())
            {
                location = parameterNodes[i]->sourceLocation(tokens);
            }
            currentScope()->addVariableBinding(parameters[i].name(), parameters[i].type(), location, tokens.source(), VariableBindingKind::Parameter);
        }

        auto& returnTypes = methodDefinition.returnTypes();
        if (returnTypes.size() == 1)
        {
            m_currentReturnType = returnTypes[0];
        }
        else if (returnTypes.size() > 1)
        {
            TODO("Handle multiple return types in function definition");
        }

        m_currentFunctionName = methodDefinition.fullName();
        typeCheckBlockNode(statement->bodyNode().get(), tokens);
        m_currentFunctionName.clear();

        if (!HasExternAnnotation(statement->annotations())
            && m_currentReturnType != Type::Void()
            && m_currentReturnType != Type::Undefined()
            && !StatementGuaranteesReturn(statement->bodyNode().get()))
        {
            m_diagnostics.addMissingReturnError(
                tokens.source(),
                tokens.getSourceLocation(statement->methodNameNode()->methodNameToken()),
                methodDefinition.name(),
                formatTypeName(m_module, m_currentReturnType));
        }

        popScope(!statement->isExtern());
    }

    void TypeCheckerOld::typeCheckIfStatement(IfStatement* statement, const TokenBuffer& tokens)
    {
        auto conditionType = typeCheckExpression(statement->condition().get(), tokens);
        conditionType = coerceConditionType(conditionType, statement->condition().get());
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolIfConditionError(
                tokens.source(),
                statement->condition()->sourceLocation(tokens),
                formatTypeName(m_module, conditionType));
        }

        typeCheckStatement(statement->trueStatement().get(), tokens);

        if (statement->hasFalseBlock())
        {
            typeCheckStatement(statement->falseStatement().value().get(), tokens);
        }
    }

    void TypeCheckerOld::typeCheckWhileStatement(WhileStatement* statement, const TokenBuffer& tokens)
    {
        auto conditionType = typeCheckExpression(statement->condition().get(), tokens);
        conditionType = coerceConditionType(conditionType, statement->condition().get());
        if (conditionType != Type::Undefined() && conditionType != m_module.wellKnown().boolean)
        {
            m_diagnostics.addNonBoolWhileConditionError(
                tokens.source(),
                statement->condition()->sourceLocation(tokens),
                formatTypeName(m_module, conditionType));
        }

        typeCheckStatement(statement->trueStatement().get(), tokens);
    }

    void TypeCheckerOld::typeCheckReturnStatement(ReturnStatement* statement, const TokenBuffer& tokens)
    {
        const auto declaredReturnType = m_currentReturnType;

        if (statement->expression().has_value())
        {
            auto type = Type::Undefined();
            {
                const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, contextualTypeForExpectedType(declaredReturnType) };
                type = typeCheckExpression(statement->expression().value().get(), tokens);
            }
            if (type.isReference())
            {
                // returning a ref is now allowed, so we need to coerce it to a value
                type = type.toValue();
            }

            statement->setType(type);
            if (type == Type::Undefined())
            {
                return;
            }

            if (!isAssignableTo(type, declaredReturnType))
            {
                const auto location = statement->expression().value()->sourceLocation(tokens);
                if (!tryAddArrayLengthMismatchError(tokens, location, declaredReturnType, type))
                {
                    m_diagnostics.addReturnTypeMismatchError(
                        tokens.source(),
                        location,
                        formatTypeName(m_module, declaredReturnType),
                        formatTypeName(m_module, type));
                }
            }
        }
        else
        {
            statement->setType(Type::Void());

            if (declaredReturnType != Type::Void())
            {
                const auto returnKeywordLocation = tokens.getSourceLocation(statement->keywordToken());
                const auto semicolonLocation = tokens.getSourceLocation(statement->semicolonToken());
                m_diagnostics.addReturnTypeMismatchError(
                    tokens.source(),
                    SourceLocation{ returnKeywordLocation.startIndex, semicolonLocation.endIndex },
                    formatTypeName(m_module, declaredReturnType),
                    formatTypeName(m_module, Type::Void()));
            }
        }
    }

    Type TypeCheckerOld::typeCheckExpression(Expression* expression, const TokenBuffer& tokens)
    {
        switch (expression->kind())
        {
            case NodeKind::StringLiteral:
            {
                auto literalType = m_module.wellKnown().string;
                if (literalType == Type::Undefined()
                    || (m_contextualNumberType.has_value() && m_contextualNumberType.value() == m_module.wellKnown().cstring))
                {
                    // string literals are cstrings for FFI function calls
                    literalType = m_module.wellKnown().cstring;
                }

                if (!m_isPreludePass && literalType == m_module.wellKnown().string)
                {
                    m_module.markPreludeTypeRequired(literalType);
                }

                expression->setType(literalType);
                return expression->type();
            }
            case NodeKind::BoolLiteral:
            {
                expression->setType(m_module.wellKnown().boolean);
                return expression->type();
            }
            case NodeKind::NumberLiteral:
            {
                return typeCheckNumberLiteral(static_cast<NumberLiteral*>(expression), tokens);
            }
            case NodeKind::ArrayLiteral:
            {
                return typeCheckArrayLiteral(static_cast<ArrayLiteral*>(expression), tokens);
            }
            case NodeKind::GroupingExpression:
            {
                return typeCheckGroupingExpression(static_cast<GroupingExpression*>(expression), tokens);
            }
            case NodeKind::UnaryExpression:
            {
                return typeCheckUnaryExpressionExpression(static_cast<UnaryExpression*>(expression), tokens);
            }
            case NodeKind::BinaryExpression:
            {
                return typeCheckBinaryExpressionExpression(static_cast<BinaryExpression*>(expression), tokens);
            }
            case NodeKind::NameExpression:
            {
                return typeCheckNameExpression(static_cast<NameExpression*>(expression), tokens);
            }
            case NodeKind::FunctionCallExpression:
            {
                return typeCheckFunctionCallExpression(static_cast<FunctionCallExpression*>(expression), tokens);
            }
            case NodeKind::MemberAccessExpression:
            {
                return typeCheckMemberAccessExpression(static_cast<MemberAccessExpression*>(expression), tokens);
            }
            case NodeKind::DiscardLiteral:
            {
                return Type::Discard();
            }
            default:
            {
                TODO("Missing Expression!!");
            }
        }
        return Type::Undefined();
    }

    Type TypeCheckerOld::typeCheckGroupingExpression(GroupingExpression* groupingExpression, const TokenBuffer& tokens)
    {
        auto type = typeCheckExpression(groupingExpression->expression().get(), tokens);

        groupingExpression->setType(type);
        return type;
    }

    Type TypeCheckerOld::typeCheckUnaryExpressionExpression(UnaryExpression* unaryExpression, const TokenBuffer& tokens)
    {
        switch (unaryExpression->unaryOperator())
        {
            case UnaryOperatorKind::LogicalNegation:
            {
                auto type = typeCheckExpression(unaryExpression->expression().get(), tokens);

                const auto operandType = type.toValue();
                if (operandType != Type::Undefined()
                    && m_module.tryGetOperatorSignature(operandType, UnaryOperatorKind::LogicalNegation) == nullptr)
                {
                    m_diagnostics.addUnaryOperandTypeMismatchError(
                        tokens.source(),
                        unaryExpression->sourceLocation(tokens),
                        "!",
                        formatTypeName(m_module, operandType),
                        "a 'bool' operand");

                    unaryExpression->setType(Type::Undefined());
                    return Type::Undefined();
                }

                unaryExpression->setType(type);
                return type;
            }
            case UnaryOperatorKind::ValueNegation:
            {
                auto* operand = unaryExpression->expression().get();
                m_negatedLiteralSignConsumed = false;

                auto type = Type::Undefined();
                {
                    const ScopedValue<bool> negatedLiteralScope{ m_negatedLiteralContext, dynamic_cast<NumberLiteral*>(operand) != nullptr };
                    type = typeCheckExpression(operand, tokens);
                }

                if (m_negatedLiteralSignConsumed)
                {
                    unaryExpression->setSignFolded(true);
                    m_negatedLiteralSignConsumed = false;
                }

                const auto operandType = type.toValue();
                if (operandType != Type::Undefined()
                    && m_module.tryGetOperatorSignature(operandType, UnaryOperatorKind::ValueNegation) == nullptr)
                {
                    m_diagnostics.addUnaryOperandTypeMismatchError(
                        tokens.source(),
                        unaryExpression->sourceLocation(tokens),
                        "-",
                        formatTypeName(m_module, operandType),
                        "a numeric operand");

                    unaryExpression->setType(Type::Undefined());
                    return Type::Undefined();
                }

                unaryExpression->setType(type);
                return type;
            }
            case UnaryOperatorKind::ReferenceOf:
            {
                auto* operand = unaryExpression->expression().get();
                auto type = typeCheckExpression(operand, tokens);
                if (type.isReference())
                {
                    m_diagnostics.addAlreadyReferenceError(
                        tokens.source(),
                        unaryExpression->sourceLocation(tokens));
                    return Type::Undefined();
                }

                const auto* strippedOperand = stripGroupings(operand);
                const auto operandIsMemberAccess = strippedOperand->kind() == NodeKind::BinaryExpression
                    && static_cast<const BinaryExpression*>(strippedOperand)->binaryOperator() == BinaryOperatorKind::MemberAccess
                    && static_cast<const BinaryExpression*>(strippedOperand)->rightExpression()->kind() == NodeKind::NameExpression;
                if (strippedOperand->kind() != NodeKind::NameExpression && !operandIsMemberAccess)
                {
                    // cant return references to temporaries
                    m_diagnostics.addReferenceToNonVariableError(
                        tokens.source(),
                        unaryExpression->sourceLocation(tokens));
                    return Type::Undefined();
                }

                if (operandIsMemberAccess)
                {
                    auto rootName = std::string{};
                    auto rootReferencesConstant = false;
                    if (receiverChainIsConstant(strippedOperand, rootName, rootReferencesConstant))
                    {
                        unaryExpression->setReferencesConstant(true);
                    }
                }
                else if (const auto& name = static_cast<const NameExpression*>(strippedOperand)->name();
                    currentScope()->tryGetVariableBindingKind(name) == VariableBindingKind::LocalConstant)
                {
                    unaryExpression->setReferencesConstant(true);
                }

                auto referenceType = type.toReference();
                unaryExpression->setType(referenceType);
                return referenceType;
            }
            default:
            {
                TODO("Missing UnaryOperatorKind!!");
                return Type::Undefined();
            }
        }
    }

    Type TypeCheckerOld::typeCheckBinaryExpressionExpression(BinaryExpression* binaryExpression, const TokenBuffer& tokens)
    {
        switch (binaryExpression->binaryOperator())
        {
            case BinaryOperatorKind::MemberAccess:
            {
                auto leftType = typeCheckExpression(binaryExpression->leftExpression().get(), tokens);
                if (leftType.kind() == TypeKind::Enum)
                {
                    auto& enumDefinition = m_module.getEnumDefinition(leftType);
                    if (binaryExpression->rightExpression()->kind() == NodeKind::NameExpression)
                    {
                        auto* fieldNameExpression = static_cast<NameExpression*>(binaryExpression->rightExpression().get());
                        if (!enumDefinition.hasField(fieldNameExpression->name()))
                        {
                            m_diagnostics.addUnknownEnumFieldError(
                                tokens.source(),
                                fieldNameExpression->sourceLocation(tokens),
                                enumDefinition.name(),
                                fieldNameExpression->name());
                            return Type::Undefined();
                        }

                        binaryExpression->setType(leftType);
                        fieldNameExpression->setType(leftType);
                        return leftType;
                    }

                    m_diagnostics.addInvalidEnumMemberAccessError(
                        tokens.source(),
                        binaryExpression->rightExpression()->sourceLocation(tokens),
                        enumDefinition.name());
                    return Type::Undefined();
                }
                else if (leftType.kind() == TypeKind::Type)
                {
                    auto& typeDefinition = m_module.getTypeDefinition(leftType);

                    if (binaryExpression->rightExpression()->kind() == NodeKind::FunctionCallExpression)
                    {
                        auto functionCallExpression = static_cast<FunctionCallExpression*>(binaryExpression->rightExpression().get());
                        const auto& name = functionCallExpression->nameExpression()->name();

                        auto methodType = typeDefinition.tryGetMethodTypeByName(name);
                        if (methodType != Type::Undefined())
                        {
                            auto& methodDefinition = m_module.getFunctionDefinition(methodType);
                            if (methodDefinition.symbolName().has_value())
                            {
                                m_module.markExternRequired(methodType);
                            }

                            if (!m_isPreludePass)
                            {
                                m_module.markPreludeTypeRequired(leftType);
                                for (const auto callReturnType : methodDefinition.returnTypes())
                                {
                                    m_module.markPreludeTypeRequired(callReturnType);
                                }
                            }

                            if (methodDefinition.functionType() == FunctionType::PrivateMethod && m_currentType != leftType)
                            {
                                m_diagnostics.addPrivateMethodCallOutsideTypeError(
                                    tokens.source(),
                                    functionCallExpression->sourceLocation(tokens),
                                    name,
                                    typeDefinition.name());
                                return Type::Undefined();
                            }

                            if (!typeCheckCallArguments(functionCallExpression, methodDefinition, tokens))
                            {
                                return Type::Undefined();
                            }

                            const auto& returnTypes = methodDefinition.returnTypes();
                            Type returnType = Type::Void();
                            if (returnTypes.size() == 1)
                            {
                                returnType = returnTypes[0];
                            }
                            else if (returnTypes.size() > 1)
                            {
                                TODO("Handle multiple return types");
                            }

                            if (methodDefinition.functionType() == FunctionType::SynthesizedConstructor)
                            {
                                binaryExpression->setBinaryOperatorKind(BinaryOperatorKind::ConstructorCall);
                                binaryExpression->setType(leftType);
                                functionCallExpression->setType(leftType);
                                functionCallExpression->setFunctionType(methodType);
                                functionCallExpression->nameExpression()->setType(methodType);
                                return leftType;
                            }

                            binaryExpression->setType(returnType);
                            functionCallExpression->setType(returnType);
                            functionCallExpression->setFunctionType(methodType);
                            functionCallExpression->nameExpression()->setType(methodType);
                            return returnType;
                        }
                        else
                        {
                            m_diagnostics.addUnknownMethodError(
                                tokens.source(),
                                functionCallExpression->sourceLocation(tokens),
                                typeDefinition.name(),
                                name);
                            return Type::Undefined();
                        }
                    }
                    else if (binaryExpression->rightExpression()->kind() == NodeKind::NameExpression)
                    {
                        auto* fieldNameExpression = static_cast<NameExpression*>(binaryExpression->rightExpression().get());
                        const auto& fieldDefinition = typeDefinition.tryGetFieldByName(fieldNameExpression->name());
                        if (fieldDefinition.type() == Type::Undefined())
                        {
                            m_diagnostics.addUnknownFieldError(
                                tokens.source(),
                                fieldNameExpression->sourceLocation(tokens),
                                typeDefinition.name(),
                                fieldNameExpression->name());
                            return Type::Undefined();
                        }

                        // trying to access private fields isnt allowed
                        const auto& accessedFieldName = fieldNameExpression->name();
                        if (!accessedFieldName.empty() && accessedFieldName.front() == '_' && m_currentType != leftType)
                        {
                            m_diagnostics.addPrivateFieldAccessOutsideTypeError(
                                tokens.source(),
                                fieldNameExpression->sourceLocation(tokens),
                                accessedFieldName,
                                typeDefinition.name());
                            return Type::Undefined();
                        }

                        auto fieldType = fieldDefinition.type();
                        fieldNameExpression->setType(fieldType);
                        binaryExpression->setType(fieldType);
                        return fieldType;
                    }
                }
                else if (leftType.kind() == TypeKind::FixedArray
                    || leftType.kind() == TypeKind::DynamicArray
                    || leftType.kind() == TypeKind::Slice)
                {
                    const auto receiverType = leftType.toValue();
                    if (binaryExpression->rightExpression()->kind() == NodeKind::FunctionCallExpression)
                    {
                        auto* functionCallExpression = static_cast<FunctionCallExpression*>(binaryExpression->rightExpression().get());
                        const auto& name = functionCallExpression->nameExpression()->name();

                        auto methodType = m_module.tryGetOrCreateArrayIntrinsic(receiverType, name);
                        if (methodType == Type::Undefined())
                        {
                            if (name == "add" || name == "remove")
                            {
                                const auto elementType = m_module.getArrayElementType(receiverType);
                                const auto dynamicTypeName = "[" + formatTypeName(m_module, elementType) + "; _]";
                                m_diagnostics.addMethodRequiresDynamicArrayError(
                                    tokens.source(),
                                    functionCallExpression->sourceLocation(tokens),
                                    formatTypeName(m_module, receiverType),
                                    dynamicTypeName);
                            }
                            else
                            {
                                m_diagnostics.addUnknownMethodError(
                                    tokens.source(),
                                    functionCallExpression->sourceLocation(tokens),
                                    formatTypeName(m_module, receiverType),
                                    name);
                            }
                            return Type::Undefined();
                        }

                        auto& methodDefinition = m_module.getFunctionDefinition(methodType);
                        if ((methodDefinition.intrinsicKind() == IntrinsicKind::ArraySet
                            || methodDefinition.intrinsicKind() == IntrinsicKind::ArrayAdd
                            || methodDefinition.intrinsicKind() == IntrinsicKind::ArrayRemove))
                        {
                            if (m_module.isImmutableSlice(receiverType))
                            {
                                m_diagnostics.addMutatingMethodOnImmutableSliceError(
                                    tokens.source(),
                                    binaryExpression->leftExpression()->sourceLocation(tokens),
                                    name);
                                return Type::Undefined();
                            }

                            auto rootName = std::string{};
                            auto rootReferencesConstant = false;
                            if (receiverChainIsConstant(binaryExpression->leftExpression().get(), rootName, rootReferencesConstant))
                            {
                                if (rootReferencesConstant)
                                {
                                    m_diagnostics.addMutatingMethodThroughConstantReferenceError(
                                        tokens.source(),
                                        binaryExpression->leftExpression()->sourceLocation(tokens),
                                        rootName,
                                        name);
                                }
                                else
                                {
                                    m_diagnostics.addMutatingMethodOnConstantError(
                                        tokens.source(),
                                        binaryExpression->leftExpression()->sourceLocation(tokens),
                                        rootName,
                                        name);
                                }
                            }
                        }

                        if (!typeCheckCallArguments(functionCallExpression, methodDefinition, tokens))
                        {
                            return Type::Undefined();
                        }

                        const auto& returnTypes = methodDefinition.returnTypes();
                        auto returnType = Type::Void();
                        if (returnTypes.size() == 1)
                        {
                            returnType = returnTypes[0];
                        }

                        binaryExpression->setType(returnType);
                        functionCallExpression->setType(returnType);
                        functionCallExpression->setFunctionType(methodType);
                        functionCallExpression->nameExpression()->setType(methodType);
                        return returnType;
                    }
                    else if (binaryExpression->rightExpression()->kind() == NodeKind::NameExpression)
                    {
                        auto* memberNameExpression = static_cast<NameExpression*>(binaryExpression->rightExpression().get());
                        if (memberNameExpression->name() == ArrayLengthMemberName)
                        {
                            const auto lengthType = m_module.wellKnown().i32;
                            memberNameExpression->setType(lengthType);
                            binaryExpression->setType(lengthType);
                            return lengthType;
                        }

                        m_diagnostics.addUnknownFieldError(
                            tokens.source(),
                            memberNameExpression->sourceLocation(tokens),
                            formatTypeName(m_module, receiverType),
                            memberNameExpression->name());
                        return Type::Undefined();
                    }
                }
                else if (leftType.kind() == TypeKind::Builtin
                    && binaryExpression->rightExpression()->kind() == NodeKind::FunctionCallExpression)
                {
                    auto* functionCallExpression = static_cast<FunctionCallExpression*>(binaryExpression->rightExpression().get());
                    const auto& name = functionCallExpression->nameExpression()->name();

                    // only intrinsic methods are callable on builtin types, operator declarations are just signatures
                    auto& typeDefinition = m_module.getTypeDefinition(leftType);
                    const auto methodType = typeDefinition.tryGetMethodTypeByName(name);
                    auto isIntrinsic = false;
                    if (methodType != Type::Undefined())
                    {
                        isIntrinsic = m_module.getFunctionDefinition(methodType).functionType() == FunctionType::Intrinsic;
                    }

                    if (!isIntrinsic)
                    {
                        m_diagnostics.addUnknownMethodError(
                            tokens.source(),
                            functionCallExpression->sourceLocation(tokens),
                            formatTypeName(m_module, leftType.toValue()),
                            name);
                        return Type::Undefined();
                    }

                    auto& methodDefinition = m_module.getFunctionDefinition(methodType);
                    if (!typeCheckCallArguments(functionCallExpression, methodDefinition, tokens))
                    {
                        return Type::Undefined();
                    }

                    const auto& returnTypes = methodDefinition.returnTypes();
                    auto returnType = Type::Void();
                    if (returnTypes.size() == 1)
                    {
                        returnType = returnTypes[0];
                    }

                    binaryExpression->setType(returnType);
                    functionCallExpression->setType(returnType);
                    functionCallExpression->setFunctionType(methodType);
                    functionCallExpression->nameExpression()->setType(methodType);
                    return returnType;
                }

                if (leftType != Type::Undefined())
                {
                    m_diagnostics.addInvalidMemberAccessReceiverError(
                        tokens.source(),
                        binaryExpression->sourceLocation(tokens),
                        formatTypeName(m_module, leftType));
                }
                return Type::Undefined();
            }
            case BinaryOperatorKind::AdditionWrapping:
            case BinaryOperatorKind::SubtractionWrapping:
            case BinaryOperatorKind::MultiplicationWrapping:
            case BinaryOperatorKind::Division:
            {
                auto leftType = typeCheckExpression(binaryExpression->leftExpression().get(), tokens);
                if (leftType.isReference())
                {
                    leftType = leftType.toValue();
                }
                auto rightType = typeCheckExpression(binaryExpression->rightExpression().get(), tokens);
                if (rightType.isReference())
                {
                    rightType = rightType.toValue();
                }

                // TODO we need to be able look up the resulting type for a binary expression, 
                // for now we'll just make sure left and right have the same type and use that one
                if (leftType != rightType)
                {
                    if (leftType != Type::Undefined() && rightType != Type::Undefined())
                    {
                        m_diagnostics.addArithmeticOperandTypeMismatchError(
                            tokens.source(),
                            tokens.getSourceLocation(binaryExpression->binaryOperatorToken()),
                            formatBinaryOperator(binaryExpression->binaryOperator()),
                            formatTypeName(m_module, leftType),
                            formatTypeName(m_module, rightType));
                    }

                    binaryExpression->setType(Type::Undefined());
                    return Type::Undefined();
                }

                const auto* operatorSignature = m_module.tryGetOperatorSignature(leftType, binaryExpression->binaryOperator());
                if (operatorSignature == nullptr)
                {
                    m_diagnostics.addBinaryOperandTypeMismatchError(
                        tokens.source(),
                        tokens.getSourceLocation(binaryExpression->binaryOperatorToken()),
                        formatBinaryOperator(binaryExpression->binaryOperator()),
                        formatTypeName(m_module, leftType),
                        "numeric operands");

                    binaryExpression->setType(Type::Undefined());
                    return Type::Undefined();
                }

                binaryExpression->setType(operatorSignature->resultType);
                return operatorSignature->resultType;
            }
            case BinaryOperatorKind::Equal:
            case BinaryOperatorKind::NotEqual:
            case BinaryOperatorKind::LessThan:
            case BinaryOperatorKind::LessOrEqual:
            case BinaryOperatorKind::GreaterThan:
            case BinaryOperatorKind::GreaterOrEqual:
            case BinaryOperatorKind::LogicalAnd:
            case BinaryOperatorKind::LogicalOr:
            {
                auto leftType = typeCheckExpression(binaryExpression->leftExpression().get(), tokens);
                if (leftType.isReference())
                {
                    leftType = leftType.toValue();
                }
                auto rightType = typeCheckExpression(binaryExpression->rightExpression().get(), tokens);
                if (rightType.isReference())
                {
                    rightType = rightType.toValue();
                }

                if (!areComparableTypes(leftType, rightType))
                {
                    if (leftType != Type::Undefined() && rightType != Type::Undefined())
                    {
                        m_diagnostics.addComparisonOperandTypeMismatchError(
                            tokens.source(),
                            tokens.getSourceLocation(binaryExpression->binaryOperatorToken()),
                            formatBinaryOperator(binaryExpression->binaryOperator()),
                            formatTypeName(m_module, leftType),
                            formatTypeName(m_module, rightType));
                    }

                    binaryExpression->setType(Type::Undefined());
                    return Type::Undefined();
                }

                const auto operation = binaryExpression->binaryOperator();

                auto lookupType = leftType;
                if (lookupType.kind() == TypeKind::Enum)
                {
                    lookupType = m_module.getEnumDefinition(lookupType).baseType();
                }

                const auto* operatorSignature = m_module.tryGetOperatorSignature(lookupType, operation);
                if (operatorSignature == nullptr)
                {
                    const auto* expectedOperands = "numeric operands";
                    if (operation == BinaryOperatorKind::LogicalAnd || operation == BinaryOperatorKind::LogicalOr)
                    {
                        expectedOperands = "'bool' operands";
                    }
                    else if (operation == BinaryOperatorKind::Equal || operation == BinaryOperatorKind::NotEqual)
                    {
                        expectedOperands = "operands that can be compared for equality";
                    }

                    m_diagnostics.addBinaryOperandTypeMismatchError(
                        tokens.source(),
                        tokens.getSourceLocation(binaryExpression->binaryOperatorToken()),
                        formatBinaryOperator(operation),
                        formatTypeName(m_module, leftType),
                        expectedOperands);

                    binaryExpression->setType(Type::Undefined());
                    return Type::Undefined();
                }

                auto resultType = operatorSignature->resultType;
                binaryExpression->setType(resultType);
                return resultType;
            }
            default:
            {
                TODO("Missing BinaryOperatornKind!!");
                return Type::Undefined();
            }
        }

        return Type::Undefined();
    }

    Type TypeCheckerOld::typeCheckNameExpression(NameExpression* expression, const TokenBuffer& tokens)
    {
        const auto& name = expression->name();
        auto optionalVariableType = currentScope()->tryGetVariableBinding(name);
        if (optionalVariableType.has_value())
        {
            const auto isLeadingDotRoot = m_leadingDotRootPending;
            m_leadingDotRootPending = false;
            if (!isLeadingDotRoot && currentScope()->tryGetVariableBindingKind(name) == VariableBindingKind::Field)
            {
                m_diagnostics.addFieldAccessMissingDotError(tokens.source(), expression->sourceLocation(tokens), name);
                return Type::Undefined();
            }

            static_cast<void>(currentScope()->markVariableBindingRead(name));
            auto type = optionalVariableType.value();
            expression->setType(type);
            return type;
        }

        auto type = m_module.tryGetTypeByName(name);
        if (type != Type::Undefined())
        {
            expression->setType(type);
            return type;
        }

        m_diagnostics.addUnknownNameError(tokens.source(), expression->sourceLocation(tokens), name);

        return Type::Undefined();
    }

    Type TypeCheckerOld::typeCheckFunctionCallExpression(FunctionCallExpression* functionCallExpression, const TokenBuffer& tokens)
    {
        const auto& name = functionCallExpression->nameExpression()->name();
        auto functionType = m_module.tryGetFunctionTypeByName(name);
        if (functionType == Type::Undefined())
        {
            if (m_currentType != Type::Undefined())
            {
                auto& typeDefinition = m_module.getTypeDefinition(m_currentType);
                if (typeDefinition.tryGetMethodTypeByName(name) != Type::Undefined())
                {
                    m_diagnostics.addMethodCallMissingDotError(
                        tokens.source(),
                        functionCallExpression->sourceLocation(tokens),
                        name,
                        typeDefinition.name());
                    return Type::Undefined();
                }
            }

            m_diagnostics.addUnknownFunctionError(
                tokens.source(),
                functionCallExpression->sourceLocation(tokens),
                name);
            return Type::Undefined();
        }

        return typeCheckResolvedFunctionCall(functionCallExpression, functionType, tokens);
    }

    Type TypeCheckerOld::typeCheckResolvedFunctionCall(FunctionCallExpression* functionCallExpression, Type functionType, const TokenBuffer& tokens)
    {
        const auto& functionDefinition = m_module.getFunctionDefinition(functionType);

        // extern declarations only reach codegen when a call requires them
        if (functionDefinition.symbolName().has_value())
        {
            m_module.markExternRequired(functionType);
        }

        if (!m_isPreludePass)
        {
            for (const auto callReturnType : functionDefinition.returnTypes())
            {
                m_module.markPreludeTypeRequired(callReturnType);
            }
        }

        if (!typeCheckCallArguments(functionCallExpression, functionDefinition, tokens))
        {
            return Type::Undefined();
        }

        const auto& returnTypes = functionDefinition.returnTypes();
        Type returnType = Type::Void();
        if (returnTypes.size() == 1)
        {
            returnType = returnTypes[0];
        }
        else if (returnTypes.size() > 1)
        {
            TODO("Handle multiple return types");
        }

        functionCallExpression->setType(returnType);
        functionCallExpression->setFunctionType(functionType);
        functionCallExpression->nameExpression()->setType(functionType);
        return returnType;
    }

    Type TypeCheckerOld::typeCheckMemberAccessExpression(MemberAccessExpression* memberAccessExpression, const TokenBuffer& tokens)
    {
        if (currentScope()->kind() == ScopeKind::StaticMethod)
        {
            m_diagnostics.addMemberAccessInStaticMethodError(
                tokens.source(),
                memberAccessExpression->sourceLocation(tokens),
                m_currentFunctionName);

            memberAccessExpression->setType(Type::Undefined());
            return Type::Undefined();
        }

        if (m_currentType == Type::Undefined())
        {
            m_diagnostics.addMemberAccessInDefaultParameterError(
                tokens.source(),
                memberAccessExpression->sourceLocation(tokens));

            memberAccessExpression->setType(Type::Undefined());
            return Type::Undefined();
        }

        auto& typeDefinition = m_module.getTypeDefinition(m_currentType);
        if (memberAccessExpression->expression()->kind() == NodeKind::FunctionCallExpression)
        {
            auto* functionCallExpression = static_cast<FunctionCallExpression*>(memberAccessExpression->expression().get());
            const auto& methodName = functionCallExpression->nameExpression()->name();
            const auto methodType = typeDefinition.tryGetMethodTypeByName(methodName);
            if (methodType == Type::Undefined())
            {
                m_diagnostics.addUnknownMethodError(
                    tokens.source(),
                    functionCallExpression->sourceLocation(tokens),
                    typeDefinition.name(),
                    methodName);

                memberAccessExpression->setType(Type::Undefined());
                return Type::Undefined();
            }

            const auto type = typeCheckResolvedFunctionCall(functionCallExpression, methodType, tokens);
            memberAccessExpression->setType(type);
            return type;
        }

        m_leadingDotRootPending = true;
        auto type = typeCheckExpression(memberAccessExpression->expression().get(), tokens);
        m_leadingDotRootPending = false;
        memberAccessExpression->setType(type);

        return type;
    }

    bool TypeCheckerOld::typeCheckCallArguments(
        FunctionCallExpression* functionCallExpression,
        const FunctionDefinition& functionDefinition,
        const TokenBuffer& tokens)
    {
        const auto& parameterTypes = functionDefinition.parameters();
        auto isVariadic = functionDefinition.isVariadic();
        auto parameterCount = (isVariadic ? parameterTypes.size() - 1 : parameterTypes.size());
        // static intrinsics like i32.bitAnd have no receiver, array intrinsics carry an explicit .this parameter
        const bool hasImplicitThis =
            functionDefinition.functionType() == FunctionType::SynthesizedConstructor ||
            functionDefinition.functionType() == FunctionType::PublicMethod ||
            functionDefinition.functionType() == FunctionType::PrivateMethod ||
            (functionDefinition.functionType() == FunctionType::Intrinsic
                && !functionDefinition.parameters().empty()
                && functionDefinition.parameters().front().name() == ImplicitThisName);
        const size_t parameterOffset = hasImplicitThis ? 1 : 0;
        if (parameterCount < parameterOffset)
        {
            TODO("This shouldn't happen");
            return false;
        }

        auto binding = bindCallArgumentsOld(parameterTypes, parameterOffset, isVariadic, *functionCallExpression, functionDefinition.name(), m_diagnostics, tokens);
        if (!binding.ok)
        {
            return false;
        }

        for (const auto& argument : functionCallExpression->arguments())
        {
            // use the bound parameter type for the argument if needed, variadic arguments have no expected type
            auto* argumentExpression = argument.value().get();
            auto contextualType = std::optional<Type>{};
            auto isVariadicArgument = true;
            for (size_t i = 0; i < binding.ordered.size(); ++i)
            {
                if (binding.ordered[i] != argumentExpression)
                {
                    continue;
                }

                contextualType = contextualTypeForExpectedType(parameterTypes[i + parameterOffset].type());
                isVariadicArgument = false;
                break;
            }

            // variadic arguments of extern functions go to C, string literals there must stay cstring
            if (isVariadicArgument
                && m_module.tryGetExternFunctionByFullName(functionDefinition.fullName()) != Type::Undefined())
            {
                contextualType = m_module.wellKnown().cstring;
            }

            const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, contextualType };
            static_cast<void>(typeCheckExpression(argumentExpression, tokens));
        }

        const auto expectedArgumentCount = parameterCount - parameterOffset;
        std::vector<ArgumentTypeMismatchInfo> argumentTypeMismatches{};
        for (size_t i = 0; i < binding.ordered.size(); ++i)
        {
            const auto* argument = binding.ordered[i];
            const auto argumentType = argument->type();
            const auto expectedType = parameterTypes[i + parameterOffset].type();
            if (!isAssignableTo(argumentType, expectedType))
            {
                argumentTypeMismatches.push_back(ArgumentTypeMismatchInfo{
                    argument->sourceLocation(tokens),
                    static_cast<i32>(i + 1),
                    formatTypeName(m_module, expectedType),
                    formatTypeName(m_module, argumentType),
                    });
            }
        }

        for (size_t j = 0; j < binding.variadic.size(); ++j)
        {
            const auto* argument = binding.variadic[j];
            const auto argumentType = argument->type();
            if (argumentType == Type::Undefined() || argumentType == Type::Void())
            {
                m_diagnostics.addInvalidVariadicArgumentTypeError(
                    tokens.source(),
                    argument->sourceLocation(tokens),
                    functionDefinition.name(),
                    static_cast<i32>(expectedArgumentCount + j + 1),
                    formatTypeName(m_module, argumentType));
                return false;
            }
        }

        if (!argumentTypeMismatches.empty())
        {
            const auto argumentLocation = functionCallExpression->argumentsLocation(tokens);
            m_diagnostics.addArgumentTypeMismatchError(
                tokens.source(),
                argumentLocation,
                functionDefinition.name(),
                argumentTypeMismatches);
            return false;
        }

        functionCallExpression->setBoundArguments(std::move(binding.ordered), std::move(binding.variadic));
        return true;
    }

    Type TypeCheckerOld::typeCheckNumberLiteral(NumberLiteral* literal, const TokenBuffer& tokens)
    {
        auto numberType = Type::Undefined();
        auto parsedValue = std::optional<NumberLiteral::ParsedValue>{};
        if (literal->explicitType().has_value())
        {
            numberType = typeCheckTypeNameNode(literal->explicitType().value().get(), tokens);
        }
        else
        {
            const auto& lexeme = literal->literalLexeme();
            const auto isFloatingLiteral = lexeme.find('.') != std::string_view::npos;
            if (m_contextualNumberType.has_value())
            {
                const auto contextualType = m_contextualNumberType.value();
                const auto* contextualDescription = m_module.tryGetBuiltinTypeDescription(contextualType);
                if (!isFloatingLiteral && contextualDescription != nullptr && contextualDescription->kind == BuiltinTypeKind::Int)
                {
                    numberType = contextualType;
                }
            }

            if (numberType == Type::Undefined() && isFloatingLiteral)
            {
                numberType = m_defaultFloatingType;
            }
            else if (numberType == Type::Undefined())
            {
                numberType = m_defaultIntegerType;
            }
        }

        const auto* numberDescription = m_module.tryGetBuiltinTypeDescription(numberType);
        if (numberType != Type::Undefined()
            && numberDescription != nullptr
            && (numberDescription->kind == BuiltinTypeKind::Int || numberDescription->kind == BuiltinTypeKind::Float))
        {
            parsedValue = tryParseNumberLiteralValue(literal->literalLexeme(), numberType, m_module);
            if (!parsedValue.has_value() && m_negatedLiteralContext
                && numberDescription->kind == BuiltinTypeKind::Int && numberDescription->isSigned && numberDescription->bits == 32)
            {
                const auto negatedValue = tryParseNegatedI32Literal(literal->literalLexeme());
                if (negatedValue.has_value())
                {
                    parsedValue = NumberLiteral::ParsedValue{ negatedValue.value() };
                    m_negatedLiteralSignConsumed = true;
                }
            }

            if (!parsedValue.has_value())
            {
                m_diagnostics.addNumberLiteralOutOfRangeError(
                    tokens.source(),
                    literal->sourceLocation(tokens),
                    literal->literalLexeme(),
                    formatTypeName(m_module, numberType));
                numberType = Type::Undefined();
            }
        }

        literal->setParsedValue(std::move(parsedValue));
        literal->setType(numberType);
        return numberType;
    }

    Type TypeCheckerOld::typeCheckArrayLiteral(ArrayLiteral* literal, const TokenBuffer& tokens)
    {
        if (literal->elements().empty())
        {
            // an explicit dynamic array type provides the element type for an empty dynamic literal
            if (literal->isDynamic() && m_contextualNumberType.has_value()
                && m_contextualNumberType.value().kind() == TypeKind::DynamicArray)
            {
                const auto arrayType = m_contextualNumberType.value().toValue();
                literal->setType(arrayType);
                return arrayType;
            }

            m_diagnostics.addEmptyArrayLiteralError(
                tokens.source(),
                literal->sourceLocation(tokens));
            literal->setType(Type::Undefined());
            return Type::Undefined();
        }

        // an enclosing array type provides the element type, everything else provides none
        auto contextualElementType = std::optional<Type>{};
        if (m_contextualNumberType.has_value())
        {
            const auto contextualType = m_contextualNumberType.value();
            if (contextualType.kind() == TypeKind::FixedArray
                || contextualType.kind() == TypeKind::DynamicArray
                || contextualType.kind() == TypeKind::Slice)
            {
                contextualElementType = m_module.getArrayElementType(contextualType);
            }
        }

        auto elementType = Type::Undefined();
        auto hasElementError = false;
        auto reportedElementMismatch = false;
        {
            const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, contextualElementType };
            for (const auto& element : literal->elements())
            {
                const auto type = typeCheckExpression(element.get(), tokens);
                if (type == Type::Undefined())
                {
                    hasElementError = true;
                    continue;
                }

                if (elementType == Type::Undefined())
                {
                    elementType = type;
                }
                else if (type != elementType)
                {
                    if (!reportedElementMismatch)
                    {
                        m_diagnostics.addArrayElementTypeMismatchError(
                            tokens.source(),
                            element->sourceLocation(tokens),
                            formatTypeName(m_module, elementType),
                            formatTypeName(m_module, type));
                        reportedElementMismatch = true;
                    }
                    hasElementError = true;
                }
            }
        }

        if (hasElementError || elementType == Type::Undefined())
        {
            literal->setType(Type::Undefined());
            return Type::Undefined();
        }

        // ownership rule: arrays cant store references or slices
        if (elementType.isReference() || elementType.kind() == TypeKind::Slice)
        {
            m_diagnostics.addReferenceOrSliceArrayElementError(
                tokens.source(),
                literal->sourceLocation(tokens));
        }
        else if (elementType.kind() == TypeKind::DynamicArray)
        {
            m_diagnostics.addDynamicArrayElementError(
                tokens.source(),
                literal->sourceLocation(tokens));
        }

        auto arrayType = Type::Undefined();
        if (literal->isDynamic())
        {
            arrayType = m_module.getOrCreateArrayType(TypeKind::DynamicArray, elementType, 0);
        }
        else
        {
            const auto length = static_cast<i32>(literal->elements().size());
            arrayType = m_module.getOrCreateArrayType(TypeKind::FixedArray, elementType, length);
        }
        literal->setType(arrayType);
        return arrayType;
    }

    Type TypeCheckerOld::typeCheckTypeNameNode(TypeNameNode* typeNameNode, const TokenBuffer& tokens)
    {
        if (typeNameNode->kind() == NodeKind::ArrayTypeNameNode)
        {
            return typeCheckArrayTypeNameNode(static_cast<ArrayTypeNameNode*>(typeNameNode), tokens);
        }

        const auto& name = typeNameNode->name();
        auto type = m_module.tryGetTypeByName(name);
        if (type != Type::Undefined())
        {
            if (!m_isPreludePass)
            {
                m_module.markPreludeTypeRequired(type);
            }

            if (typeNameNode->isReference())
            {
                if (type.isReference())
                {
                    TODO("Add error diagnostics for already being a reference");
                    return Type::Undefined();
                }
                type = type.toReference();
            }
            // TODO nullable handling
            typeNameNode->setType(type);
            return type;
        }
        else
        {
            m_diagnostics.addUnknownTypeError(tokens.source(), tokens.getSourceLocation(typeNameNode->nameToken()), name);
            return Type::Undefined();
        }
    }

    Type TypeCheckerOld::typeCheckArrayTypeNameNode(ArrayTypeNameNode* arrayTypeNameNode, const TokenBuffer& tokens)
    {
        auto elementType = typeCheckTypeNameNode(arrayTypeNameNode->elementType().get(), tokens);
        if (elementType == Type::Undefined())
        {
            return Type::Undefined();
        }

        // ownership rule: arrays cant store references or slices
        if (elementType.isReference() || elementType.kind() == TypeKind::Slice)
        {
            m_diagnostics.addReferenceOrSliceArrayElementError(
                tokens.source(),
                arrayTypeNameNode->elementType()->sourceLocation(tokens));
        }
        else if (elementType.kind() == TypeKind::DynamicArray)
        {
            m_diagnostics.addDynamicArrayElementError(
                tokens.source(),
                arrayTypeNameNode->elementType()->sourceLocation(tokens));
        }

        auto arrayType = Type::Undefined();
        switch (arrayTypeNameNode->arrayKind())
        {
            case ArrayTypeKind::Slice:
            {
                arrayType = m_module.getOrCreateArrayType(TypeKind::Slice, elementType, 0);
                break;
            }
            case ArrayTypeKind::Fixed:
            {
                auto* lengthLiteral = arrayTypeNameNode->lengthLiteral().get();
                if (lengthLiteral == nullptr)
                {
                    return Type::Undefined();
                }

                i32 length = 0;
                {
                    const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, std::nullopt };
                    length = convertToI32(lengthLiteral, tokens);
                }
                arrayType = m_module.getOrCreateArrayType(TypeKind::FixedArray, elementType, length);
                break;
            }
            case ArrayTypeKind::Dynamic:
            {
                arrayType = m_module.getOrCreateArrayType(TypeKind::DynamicArray, elementType, 0);
                break;
            }
        }

        if (arrayTypeNameNode->isReference())
        {
            arrayType = arrayType.toReference();
        }

        arrayTypeNameNode->setType(arrayType);
        return arrayType;
    }

    std::vector<Parameter> TypeCheckerOld::typeCheckParametersNode(ParametersNode* parametersNode, const TokenBuffer& tokens)
    {
        std::vector<Parameter> parameters{};
        auto seenDefault = false;

        // first loop doesnt put the parameter names into the scope, so that default values cannot reference other parameters
        for (const auto& parameterNode : parametersNode->parameters())
        {
            auto parameterType = typeCheckTypeNameNode(parameterNode->typeName().get(), tokens);
            if (parameterType == Type::Void())
            {
                m_diagnostics.addVoidParameterTypeError(
                    tokens.source(),
                    parameterNode->sourceLocation(tokens),
                    parameterNode->name());

                parameterType = Type::Undefined();
            }
            parameterNode->setType(parameterType);

            const Expression* defaultValue = nullptr;
            if (parameterNode->hasDefault())
            {
                auto* defaultExpression = parameterNode->defaultValue().get();
                auto defaultType = Type::Undefined();
                {
                    const ScopedValue<std::optional<Type>> contextualScope{ m_contextualNumberType, parameterType };
                    defaultType = typeCheckExpression(defaultExpression, tokens);
                }
                if (defaultType != Type::Undefined() && !isAssignableTo(defaultType, parameterType))
                {
                    const auto location = defaultExpression->sourceLocation(tokens);
                    if (!tryAddArrayLengthMismatchError(tokens, location, parameterType, defaultType))
                    {
                        m_diagnostics.addDefaultParameterTypeMismatchError(
                            tokens.source(),
                            location,
                            formatTypeName(m_module, parameterType),
                            formatTypeName(m_module, defaultType));
                    }
                }
                defaultValue = defaultExpression;
                seenDefault = true;
            }
            else if (seenDefault && !parameterNode->isVariadic())
            {
                // once a parameter with a default value is seen, all following parameters must have defaults
                m_diagnostics.addNonTrailingDefaultParameterError(
                    tokens.source(),
                    parameterNode->sourceLocation(tokens),
                    parameterNode->name());
            }

            parameters.push_back(Parameter{ parameterNode->name(), parameterType, defaultValue });
        }

        // finally add parameter names into the current scope
        for (const auto& parameterNode : parametersNode->parameters())
        {
            const auto& name = parameterNode->name();
            auto scope = currentScope();
            if (!scope->hasVariableBinding(name))
            {
                scope->addVariableBinding(name, parameterNode->type(), tokens.getSourceLocation(parameterNode->nameToken()), tokens.source(), VariableBindingKind::Parameter);
            }
            else
            {
                m_diagnostics.addDuplicateParameterDeclarationError(
                    tokens.source(),
                    tokens.getSourceLocation(parameterNode->nameToken()),
                    name,
                    scope->tryGetVariableBindingSource(name),
                    scope->tryGetVariableBindingLocation(name));
            }
        }

        return parameters;
    }

    std::vector<Type> TypeCheckerOld::typeCheckReturnTypesNode(ReturnTypesNode* returnTypesNode, const TokenBuffer& tokens)
    {
        std::vector<Type> types{};
        for (const auto& returnTypeNode : returnTypesNode->returnTypes())
        {
            auto returnType = typeCheckTypeNameNode(returnTypeNode.get(), tokens);
            // ownership rule: returning references or slices is not allowed because they could outlive the values
            if (returnType.isReference() || returnType.kind() == TypeKind::Slice)
            {
                m_diagnostics.addReferenceOrSliceReturnError(
                    tokens.source(),
                    returnTypeNode->sourceLocation(tokens));
            }
            types.push_back(returnType);
        }
        return types;
    }

    i32 TypeCheckerOld::convertToI32(NumberLiteral* literal, const TokenBuffer& tokens)
    {
        auto literalType = typeCheckNumberLiteral(literal, tokens);
        if (literalType == Type::Undefined() || literalType != m_module.wellKnown().i32)
        {
            return 0;
        }

        if (!literal->hasParsedValue())
        {
            m_diagnostics.addNumberLiteralOutOfRangeError(
                tokens.source(),
                literal->sourceLocation(tokens),
                literal->literalLexeme(),
                formatTypeName(m_module, m_module.wellKnown().i32));
            return 0;
        }

        return std::get<i32>(literal->parsedValue().value());
    }

    Type TypeCheckerOld::coerceConditionType(Type conditionType, Expression* conditionExpression)
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
                conditionExpression->setType(m_module.wellKnown().boolean);
                return m_module.wellKnown().boolean;
            }
        }

        return conditionType;
    }

    bool TypeCheckerOld::isAssignableTo(Type sourceType, Type targetType) const noexcept
    {
        if (sourceType == targetType)
        {
            return true;
        }

        // assigning mutable to immutable slice is allowed
        // assigning immutable to mutable slice is not allowed
        if (sourceType.kind() == TypeKind::Slice && targetType.kind() == TypeKind::Slice
            && !sourceType.isReference() && !targetType.isReference()
            && m_module.isImmutableSlice(targetType) && !m_module.isImmutableSlice(sourceType)
            && m_module.getArrayElementType(sourceType) == m_module.getArrayElementType(targetType))
        {
            return true;
        }

        // numbers are only allowed to be assigned to a bigger type and only same signedness
        if (sourceType.isBaseType() && targetType.isBaseType())
        {
            const auto* sourceDescription = m_module.tryGetBuiltinTypeDescription(sourceType);
            const auto* targetDescription = m_module.tryGetBuiltinTypeDescription(targetType);
            if (sourceDescription != nullptr && targetDescription != nullptr
                && sourceDescription->kind == targetDescription->kind
                && sourceDescription->bits < targetDescription->bits)
            {
                if (sourceDescription->kind == BuiltinTypeKind::Int)
                {
                    return sourceDescription->isSigned == targetDescription->isSigned;
                }
                if (sourceDescription->kind == BuiltinTypeKind::Float)
                {
                    return true;
                }
            }
        }

        return false;
    }

    std::optional<Type> TypeCheckerOld::contextualTypeForExpectedType(Type expectedType) const noexcept
    {
        if (expectedType.kind() == TypeKind::FixedArray
            || expectedType.kind() == TypeKind::DynamicArray
            || expectedType.kind() == TypeKind::Slice)
        {
            return expectedType;
        }

        const auto* description = m_module.tryGetBuiltinTypeDescription(expectedType);
        if (description != nullptr && description->kind == BuiltinTypeKind::Int)
        {
            return expectedType;
        }

        // string literals stay cstrings for FFI
        if (expectedType == m_module.wellKnown().cstring)
        {
            return expectedType;
        }

        return std::nullopt;
    }

    bool TypeCheckerOld::tryAddArrayLengthMismatchError(const TokenBuffer& tokens, const SourceLocation& location, Type expectedType, Type actualType)
    {
        if (expectedType.kind() != TypeKind::FixedArray || actualType.kind() != TypeKind::FixedArray)
        {
            return false;
        }

        if (!expectedType.isBaseType() || !actualType.isBaseType())
        {
            return false;
        }

        if (m_module.getArrayElementType(expectedType) != m_module.getArrayElementType(actualType))
        {
            return false;
        }

        m_diagnostics.addArrayLengthMismatchError(
            tokens.source(),
            location,
            formatTypeName(m_module, expectedType),
            m_module.getArrayLength(expectedType),
            m_module.getArrayLength(actualType));
        return true;
    }

    bool TypeCheckerOld::areComparableTypes(Type leftType, Type rightType)
    {
        if (leftType == rightType)
        {
            return true;
        }

        if (leftType.kind() == TypeKind::Enum)
        {
            auto& enumDefinition = m_module.getEnumDefinition(leftType);
            if (enumDefinition.baseType() == rightType)
            {
                return true;
            }
        }

        if (rightType.kind() == TypeKind::Enum)
        {
            auto& enumDefinition = m_module.getEnumDefinition(rightType);
            if (enumDefinition.baseType() == leftType)
            {
                return true;
            }
        }

        return false;
    }

    static bool IsTerminatingStatement(const Statement* statement)
    {
        switch (statement->kind())
        {
            case NodeKind::ReturnStatement:
            case NodeKind::BreakStatement:
            case NodeKind::SkipStatement:
                return true;
            default:
                return false;
        }
    }

    static std::optional<SourceLocation> DeadStatementLocation(const Statement* statement, const TokenBuffer& tokens)
    {
        switch (statement->kind())
        {
            case NodeKind::ReturnStatement:
                return tokens.getSourceLocation(static_cast<const ReturnStatement*>(statement)->keywordToken());
            case NodeKind::BreakStatement:
                return tokens.getSourceLocation(static_cast<const BreakStatement*>(statement)->keywordToken());
            case NodeKind::SkipStatement:
                return tokens.getSourceLocation(static_cast<const SkipStatement*>(statement)->keywordToken());
            case NodeKind::IfStatement:
                return tokens.getSourceLocation(static_cast<const IfStatement*>(statement)->ifKeyword());
            case NodeKind::WhileStatement:
                return tokens.getSourceLocation(static_cast<const WhileStatement*>(statement)->whileKeyword());
            case NodeKind::ExpressionStatement:
                return static_cast<const ExpressionStatement*>(statement)->expression()->sourceLocation(tokens);
            case NodeKind::AssignmentStatement:
                return static_cast<const AssignmentStatement*>(statement)->leftExpression()->sourceLocation(tokens);
            case NodeKind::ConstantDeclaration:
                return static_cast<const ConstantDeclaration*>(statement)->leftExpression()->sourceLocation(tokens);
            case NodeKind::VariableDeclaration:
                return static_cast<const VariableDeclaration*>(statement)->leftExpression()->sourceLocation(tokens);
            default:
                return std::nullopt;
        }
    }

    void TypeCheckerOld::typeCheckBlockNode(BlockNode* body, const TokenBuffer& tokens)
    {
        auto reachedTerminator = false;
        auto warnedUnreachable = false;
        for (const auto& statement : body->statements())
        {
            if (reachedTerminator && !warnedUnreachable)
            {
                if (const auto location = DeadStatementLocation(statement.get(), tokens); location.has_value())
                {
                    m_diagnostics.addUnreachableCodeWarning(tokens.source(), location.value());
                    warnedUnreachable = true;
                }
            }

            typeCheckStatement(statement.get(), tokens);

            if (IsTerminatingStatement(statement.get()))
            {
                reachedTerminator = true;
            }
        }
    }

    void TypeCheckerOld::pushScope(ScopeKind kind)
    {
        auto parent = m_scopes.back().get();
        if (m_scopePool.empty())
        {
            m_scopes.emplace_back(std::make_unique<Scope>(parent, kind));
        }
        else
        {
            m_scopePool.back()->reset(parent, kind);
            m_scopes.emplace_back(std::move(m_scopePool.back()));
            m_scopePool.pop_back();
        }
    }

    void TypeCheckerOld::emitUnusedVariableWarnings(const Scope& scope)
    {
        if (m_diagnostics.hasErrors())
        {
            return;
        }

        std::vector<std::pair<std::string_view, const VariableBinding*>> unused;
        for (const auto& [name, binding] : scope.variableBindings())
        {
            if (binding.wasRead || ShouldIgnoreUnusedVariableWarning(name) || !binding.location.has_value())
            {
                continue;
            }

            if (binding.kind != VariableBindingKind::LocalVariable && binding.kind != VariableBindingKind::LocalConstant && binding.kind != VariableBindingKind::Parameter)
            {
                continue;
            }

            unused.emplace_back(name, &binding);
        }

        std::sort(unused.begin(), unused.end(), [](const auto& left, const auto& right)
        {
            return left.second->location.value().startIndex < right.second->location.value().startIndex;
        });

        for (const auto& [name, binding] : unused)
        {
            if (binding->kind == VariableBindingKind::Parameter)
            {
                m_diagnostics.addUnusedParameterWarning(binding->source, binding->location.value(), std::string(name));
            }
            else if (binding->kind == VariableBindingKind::LocalConstant)
            {
                m_diagnostics.addUnusedLocalConstantWarning(binding->source, binding->location.value(), std::string(name));
            }
            else
            {
                m_diagnostics.addUnusedLocalVariableWarning(binding->source, binding->location.value(), std::string(name));
            }
        }
    }

    void TypeCheckerOld::popScope(bool emitUnusedWarnings)
    {
        if (emitUnusedWarnings)
        {
            emitUnusedVariableWarnings(*m_scopes.back());
        }

        m_scopePool.push_back(std::move(m_scopes.back()));
        m_scopes.pop_back();
        if (m_scopes.size() == 0)
        {
            TODO("Popped too many scopes");
        }
    }

    Scope* TypeCheckerOld::currentScope() const noexcept
    {
        return m_scopes.back().get();
    }
}
