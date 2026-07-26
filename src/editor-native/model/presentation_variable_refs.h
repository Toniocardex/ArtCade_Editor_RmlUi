#pragma once

#include "core/text-component-format.h"
#include "core/types.h"

#include <cstddef>
#include <string>

namespace ArtCade::EditorNative {

struct VariablePresentationReferenceSummary {
    std::size_t textReferences = 0;
    std::size_t gaugeReferences = 0;

    std::size_t total() const { return textReferences + gaugeReferences; }
};

VariablePresentationReferenceSummary countPresentationVariableReferences(
    const ProjectDoc& project,
    TextBindingScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& key);

void renamePresentationVariableReferences(
    ProjectDoc& project,
    TextBindingScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& from,
    const GameVariableId& to);

/** True when any Text/Gauge bind requires a different variable type. */
bool presentationReferencesRequireDifferentType(
    const ProjectDoc& project,
    TextBindingScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& key,
    GameVariableDefinition::Type nextType);

/** Resolve Edit-mode bound initial value for a Text/Gauge bindKey. */
std::optional<GameVariableValue> resolveEffectiveBoundInitialValue(
    const ProjectDoc& project,
    const SceneInstanceDef& instance,
    const EntityDef& objectType,
    const std::string& bindKey,
    const std::string& bindScope);

} // namespace ArtCade::EditorNative
