#include "editor-native/model/variable_references.h"

#include "editor-native/model/presentation_variable_refs.h"
#include "logic-core.h"

namespace ArtCade::EditorNative {
namespace {

TextBindingScope bindingScope(VariableScope scope) {
    return scope == VariableScope::Project ? TextBindingScope::Global
                                           : TextBindingScope::Local;
}

NumberVariableScope expressionScope(VariableScope scope) {
    return scope == VariableScope::Project ? NumberVariableScope::Global
                                           : NumberVariableScope::Local;
}

// Project scope reaches every board; Object scope only the owning type's own,
// which is the only place `$local:` can name that type's variables.
template <typename Doc, typename Fn>
void forEachBoardBlock(Doc& project,
                       VariableScope scope,
                       const ObjectTypeId* ownerTypeId,
                       Fn&& fn) {
    const auto visitType = [&](auto& type) {
        if (!type.logicBoard) return;
        for (auto& rule : type.logicBoard->rules) {
            fn(rule.trigger);
            for (auto& clause : rule.conditions) fn(clause.block);
            for (auto& branch : rule.branches) {
                for (auto& clause : branch.conditions) fn(clause.block);
                for (auto& action : branch.actions) fn(action);
            }
        }
    };
    if (scope == VariableScope::Object) {
        if (!ownerTypeId) return;
        const auto it = project.objectTypes.find(*ownerTypeId);
        if (it == project.objectTypes.end()) return;
        visitType(it->second);
        return;
    }
    for (auto& [unused, type] : project.objectTypes) {
        (void)unused;
        visitType(type);
    }
}

// Every place a LogicValue can hold an expression: the Number arm and both
// components of a Vec2 (ADR-0028).
template <typename Value, typename Fn>
void forEachExpression(Value& value, Fn&& fn) {
    if (auto* expression = std::get_if<NumberExpression>(&value)) {
        fn(*expression);
        return;
    }
    if (auto* vec2 = std::get_if<LogicVec2Value>(&value)) {
        fn(vec2->x);
        fn(vec2->y);
    }
}

} // namespace

VariableReferenceCounts countVariableReferences(
    const ProjectDoc& project,
    VariableScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& key) {
    VariableReferenceCounts counts;
    const NumberVariableScope wanted = expressionScope(scope);
    forEachBoardBlock(project, scope, ownerTypeId, [&](const LogicBlockDef& block) {
        for (const LogicPropertyDef& property : block.properties) {
            if (scope == VariableScope::Project) {
                if (const auto* ref = std::get_if<LogicVariableReference>(&property.value);
                    ref && ref->id == key) {
                    ++counts.logicProperties;
                }
            }
            forEachExpression(property.value, [&](const NumberExpression& expression) {
                forEachNumberVariableExpression(
                    expression, [&](const NumberVariableExpression& node) {
                        if (node.scope == wanted && node.variableId == key)
                            ++counts.expressions;
                    });
            });
        }
    });
    const VariablePresentationReferenceSummary presentation =
        countPresentationVariableReferences(project, bindingScope(scope), ownerTypeId, key);
    counts.textBindings  = presentation.textReferences;
    counts.gaugeBindings = presentation.gaugeReferences;
    return counts;
}

void renameVariableReferences(
    ProjectDoc& project,
    VariableScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& from,
    const GameVariableId& to) {
    const NumberVariableScope wanted = expressionScope(scope);
    forEachBoardBlock(project, scope, ownerTypeId, [&](LogicBlockDef& block) {
        for (LogicPropertyDef& property : block.properties) {
            if (scope == VariableScope::Project) {
                if (auto* ref = std::get_if<LogicVariableReference>(&property.value);
                    ref && ref->id == from) {
                    ref->id = to;
                }
            }
            forEachExpression(property.value, [&](NumberExpression& expression) {
                forEachNumberVariableExpression(
                    expression, [&](NumberVariableExpression& node) {
                        if (node.scope == wanted && node.variableId == from)
                            node.variableId = to;
                    });
            });
        }
    });
    renamePresentationVariableReferences(
        project, bindingScope(scope), ownerTypeId, from, to);
}

bool variableReferencesRequireDifferentType(
    const ProjectDoc& project,
    VariableScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& key,
    GameVariableDefinition::Type nextType) {
    bool mismatch = false;
    const NumberVariableScope wanted = expressionScope(scope);
    forEachBoardBlock(project, scope, ownerTypeId, [&](const LogicBlockDef& block) {
        if (mismatch) return;
        const auto required = Logic::requiredVariableType(block.typeId);
        for (const LogicPropertyDef& property : block.properties) {
            if (scope == VariableScope::Project && required && *required != nextType) {
                if (const auto* ref = std::get_if<LogicVariableReference>(&property.value);
                    ref && ref->id == key) {
                    mismatch = true;
                    return;
                }
            }
            // An expression node can only mean a number, whatever block it
            // sits in, so it pins the variable to Number on its own.
            if (nextType == GameVariableDefinition::Type::Number) continue;
            forEachExpression(property.value, [&](const NumberExpression& expression) {
                forEachNumberVariableExpression(
                    expression, [&](const NumberVariableExpression& node) {
                        if (node.scope == wanted && node.variableId == key)
                            mismatch = true;
                    });
            });
            if (mismatch) return;
        }
    });
    if (mismatch) return true;
    return presentationReferencesRequireDifferentType(
        project, bindingScope(scope), ownerTypeId, key, nextType);
}

} // namespace ArtCade::EditorNative
