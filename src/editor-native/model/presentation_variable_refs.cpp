#include "editor-native/model/presentation_variable_refs.h"

namespace ArtCade::EditorNative {
namespace {

bool scopeMatches(const std::string& bindScope, TextBindingScope scope) {
    const auto parsed = textBindingScopeFromString(bindScope);
    return parsed && *parsed == scope;
}

bool keyMatches(const std::string& bindKey, const GameVariableId& key) {
    return !bindKey.empty() && bindKey == key;
}

template <typename Fn>
void forEachMatchingObjectType(ProjectDoc& project,
                               TextBindingScope scope,
                               const ObjectTypeId* ownerTypeId,
                               Fn&& fn) {
    if (scope == TextBindingScope::Local) {
        if (!ownerTypeId) return;
        const auto it = project.objectTypes.find(*ownerTypeId);
        if (it == project.objectTypes.end()) return;
        fn(it->second);
        return;
    }
    for (auto& [_, type] : project.objectTypes) {
        (void)_;
        fn(type);
    }
}

template <typename Fn>
void forEachMatchingObjectType(const ProjectDoc& project,
                               TextBindingScope scope,
                               const ObjectTypeId* ownerTypeId,
                               Fn&& fn) {
    if (scope == TextBindingScope::Local) {
        if (!ownerTypeId) return;
        const auto it = project.objectTypes.find(*ownerTypeId);
        if (it == project.objectTypes.end()) return;
        fn(it->second);
        return;
    }
    for (const auto& [_, type] : project.objectTypes) {
        (void)_;
        fn(type);
    }
}

} // namespace

VariablePresentationReferenceSummary countPresentationVariableReferences(
    const ProjectDoc& project,
    TextBindingScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& key) {
    VariablePresentationReferenceSummary summary;
    forEachMatchingObjectType(project, scope, ownerTypeId, [&](const EntityDef& type) {
        if (type.text && keyMatches(type.text->bindKey, key)
            && scopeMatches(type.text->bindScope, scope)) {
            ++summary.textReferences;
        }
        if (type.gauge && keyMatches(type.gauge->bindKey, key)
            && scopeMatches(type.gauge->bindScope, scope)) {
            ++summary.gaugeReferences;
        }
    });
    return summary;
}

void renamePresentationVariableReferences(
    ProjectDoc& project,
    TextBindingScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& from,
    const GameVariableId& to) {
    forEachMatchingObjectType(project, scope, ownerTypeId, [&](EntityDef& type) {
        if (type.text && keyMatches(type.text->bindKey, from)
            && scopeMatches(type.text->bindScope, scope)) {
            type.text->bindKey = to;
        }
        if (type.gauge && keyMatches(type.gauge->bindKey, from)
            && scopeMatches(type.gauge->bindScope, scope)) {
            type.gauge->bindKey = to;
        }
    });
}

bool presentationReferencesRequireDifferentType(
    const ProjectDoc& project,
    TextBindingScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& key,
    GameVariableDefinition::Type nextType) {
    bool mismatch = false;
    forEachMatchingObjectType(project, scope, ownerTypeId, [&](const EntityDef& type) {
        if (mismatch) return;
        if (type.text && keyMatches(type.text->bindKey, key)
            && scopeMatches(type.text->bindScope, scope)
            && !isTextFormatCompatibleWithVariableType(type.text->format, nextType)) {
            mismatch = true;
            return;
        }
        if (type.gauge && keyMatches(type.gauge->bindKey, key)
            && scopeMatches(type.gauge->bindScope, scope)
            && !isGaugeCompatibleWithVariableType(nextType)) {
            mismatch = true;
        }
    });
    return mismatch;
}

std::optional<GameVariableValue> resolveEffectiveBoundInitialValue(
    const ProjectDoc& project,
    const SceneInstanceDef& instance,
    const EntityDef& objectType,
    const std::string& bindKey,
    const std::string& bindScope) {
    if (bindKey.empty()) return std::nullopt;
    const auto scope = textBindingScopeFromString(bindScope);
    if (!scope) return std::nullopt;

    if (*scope == TextBindingScope::Global) {
        for (const GameVariableDefinition& def : project.globalVariables) {
            if (def.key == bindKey) return def.initialValue;
        }
        return std::nullopt;
    }

    const auto overrideIt = instance.localVariableOverrides.find(bindKey);
    if (overrideIt != instance.localVariableOverrides.end()) {
        return overrideIt->second;
    }
    for (const GameVariableDefinition& def : objectType.localVariables) {
        if (def.key == bindKey) return def.initialValue;
    }
    return std::nullopt;
}

} // namespace ArtCade::EditorNative
