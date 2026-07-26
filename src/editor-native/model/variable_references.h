#pragma once

#include "core/types.h"

#include <cstddef>

namespace ArtCade::EditorNative {

/**
 * Which variable table a reference resolves against (ADR-0031 lexicon).
 * Project → ProjectDoc::globalVariables; Object → ObjectType::localVariables.
 */
enum class VariableScope { Project, Object };

/** One count per reference kind the walk knows about. */
struct VariableReferenceCounts {
    std::size_t logicProperties = 0;
    std::size_t expressions     = 0;
    std::size_t textBindings    = 0;
    std::size_t gaugeBindings   = 0;

    std::size_t total() const {
        return logicProperties + expressions + textBindings + gaugeBindings;
    }
};

/**
 * The single authority on what counts as a reference to a variable (ADR-0031).
 * Four kinds, and every caller gets all four:
 *
 *   LogicVariableReference     block property values
 *   NumberVariableExpression   recursively, inside expression and Vec2 values
 *   TextComponent::bindKey
 *   GaugeComponent::bindKey
 *
 * `ownerTypeId` is required for Object scope and ignored for Project scope.
 * Object-scope walks visit only the owning type's own board, because that is
 * the only board where `$local:` resolves to that type.
 *
 * Note that `LogicVariableReference` carries no scope of its own: block
 * property references always name a project variable, so Object scope never
 * counts, renames or type-checks them.
 */
VariableReferenceCounts countVariableReferences(
    const ProjectDoc& project,
    VariableScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& key);

/** Rewrite every reference in place. Instance overrides are not references. */
void renameVariableReferences(
    ProjectDoc& project,
    VariableScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& from,
    const GameVariableId& to);

/** True when a reference pins the variable to a type other than @p nextType. */
bool variableReferencesRequireDifferentType(
    const ProjectDoc& project,
    VariableScope scope,
    const ObjectTypeId* ownerTypeId,
    const GameVariableId& key,
    GameVariableDefinition::Type nextType);

} // namespace ArtCade::EditorNative
