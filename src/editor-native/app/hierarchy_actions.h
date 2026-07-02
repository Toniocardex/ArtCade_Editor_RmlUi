#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_operation_result.h"
#include "editor-native/model/editor_state.h"
#include "editor-native/view/scene_view_camera.h"

namespace ArtCade::EditorNative {

class EditorCoordinator;
class ProjectDocument;

// =============================================================================
// Hierarchy actions — the editorial operations wired from the Hierarchy panel,
// kept UI-free so they are unit-testable without RmlUi (like inspector_commit).
//
// Each action reads authoritative state through const queries, builds exactly
// one EditorCommand, and forwards it to the coordinator. It never mutates the
// ProjectDocument or the selection directly, never decides which panels refresh
// (the coordinator accumulates invalidation), and produces at most one command.
// When a precondition is not met it returns a failure WITHOUT running a command,
// so nothing is mutated and nothing is invalidated.
// =============================================================================

// ---- Identity allocation (deterministic; consults the authoritative doc) ----

/**
 * Next free instance id for @p sceneId: one past the largest id already placed
 * in that scene (>= 1, never INVALID_ENTITY). Per-scene uniqueness matches what
 * ProjectValidator enforces; there is no duplicate counter held anywhere.
 */
EntityId nextAvailableEntityId(const ProjectDocument& document, const SceneId& sceneId);

/** A scene id of the form "scene-N" not already present in @p document. */
SceneId makeUniqueSceneId(const ProjectDocument& document);

// ---- Actions ----------------------------------------------------------------

struct SpawnPositionOptions {
    bool  snapToGrid = false;
    float gridSize = 48.0f;
    float edgeMargin = 16.0f;
};

/** Normalize an explicit world-space spawn point: optional grid snap, then scene clamp. */
Vec2 normalizeSpawnPosition(Vec2 worldPosition, Vec2 sceneSize,
                            SpawnPositionOptions options = {});

/** Default toolbar placement: the current visible viewport centre in world space. */
Vec2 defaultSpawnPosition(const ViewportRect& viewport,
                          const EditorSceneViewState& view,
                          Vec2 sceneSize,
                          SpawnPositionOptions options = {});

/** Create a new, uniquely-id'd scene. Does not change the active scene. */
EditorOperationResult addScene(EditorCoordinator& coordinator);

/** Delete @p sceneId. The coordinator reconciles the workspace afterwards. */
EditorOperationResult deleteScene(EditorCoordinator& coordinator, const SceneId& sceneId);

/** Make @p sceneId the gameplay start scene (no-op if it already is). */
EditorOperationResult setStartScene(EditorCoordinator& coordinator, const SceneId& sceneId);

/**
 * "+Entity": place a brand-new, independent object — a new object type plus its
 * first instance. Does not change the selection.
 */
EditorOperationResult addEntityAt(EditorCoordinator& coordinator, Vec2 spawnPosition);
EditorOperationResult addEntity(EditorCoordinator& coordinator);

/**
 * "+Instance": place another instance of the SELECTED entity's object type. The
 * new instance gets a fresh EntityId and its own Transform but reuses the chosen
 * ObjectTypeId, so the object-type-owned components (sprite, collider, movement)
 * are intentionally shared — no ObjectTypeDef is duplicated and no component is
 * copied. Reuses the existing CreateEntityCommand (no second command), then
 * selects the new instance via intent (workspace state, not in the undo history).
 * No selection / unknown type / no scene → failure without mutation.
 */
EditorOperationResult addInstanceOfSelectedTypeAt(EditorCoordinator& coordinator,
                                                  Vec2 spawnPosition);
EditorOperationResult addInstanceOfSelectedType(EditorCoordinator& coordinator);

/** Delete the selected instance from the active scene. No selection → no-op. */
EditorOperationResult deleteSelectedEntity(EditorCoordinator& coordinator);

} // namespace ArtCade::EditorNative
