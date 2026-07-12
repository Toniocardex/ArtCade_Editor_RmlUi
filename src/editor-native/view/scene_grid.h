#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"

#include <optional>

namespace ArtCade::EditorNative {

class ProjectDocument;

enum class SceneGridKind {
    World,
    Tilemap,
};

struct SceneGridDefinition {
    SceneGridKind kind = SceneGridKind::World;
    Vec2          cellSize{SceneGridDefaults::kCellSize, SceneGridDefaults::kCellSize};
    Vec2          origin{};
};

// Scene authoring: workspace grid at world origin. Used for entity drag, spawn,
// gizmo snap — never the contextual viewport resolver.
SceneGridDefinition worldAuthoringGrid(const EditorSceneViewState& viewState);

// Tilemap cell grid for the current editable selection. Empty when the selection
// does not support tilemap editing (same predicate as reconcileTilemapEditingContext).
std::optional<SceneGridDefinition> tilemapCellGrid(const ProjectDocument& document,
                                                   const EditorState& editorState,
                                                   const SceneId& sceneId);

// Viewport overlay: tilemap grid while a tilemap tool is active on an editable
// target; otherwise the world authoring grid.
SceneGridDefinition viewportDisplayGrid(const ProjectDocument& document,
                                      const EditorState& editorState,
                                      const SceneId& sceneId);

bool selectionSupportsTilemapEditing(const ProjectDocument& document,
                                     const EditorState& editorState,
                                     const SceneId& sceneId);

int  visualGridStrideForZoom(float cellSize, float zoom);
Vec2 snapWorldPositionToGrid(Vec2 worldPosition, const SceneGridDefinition& grid);

} // namespace ArtCade::EditorNative
