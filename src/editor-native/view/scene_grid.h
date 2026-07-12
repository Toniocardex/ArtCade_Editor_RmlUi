#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"
#include "editor-native/model/tilemap_chunk_math.h"
#include "editor-native/view/scene_view_camera.h"

#include <optional>
#include <string>

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

// Derived toolbar projection — never stored in EditorState or ProjectDocument.
struct SceneGridPresentation {
    SceneGridKind kind = SceneGridKind::World;

    std::string contextName;
    std::string toolbarContextName;
    std::string toolbarTooltip;
    Vec2        cellSize{SceneGridDefaults::kCellSize, SceneGridDefaults::kCellSize};

    std::optional<EntityId> sourceEntityId;

    bool sizeEditable = true;
};

// Derived pointer readout — never stored in EditorState or ProjectDocument.
struct ViewportPointerReadout {
    bool valid = false;

    Vec2 worldPosition{};

    std::optional<TilemapCellCoord> tilemapCell;
    std::optional<EntityId>         tilemapEntityId;
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

SceneGridPresentation makeSceneGridPresentation(const ProjectDocument& document,
                                              const EditorState& editorState,
                                              const SceneId& sceneId);

ViewportPointerReadout makePointerReadout(Vec2 screenPosition,
                                         const SceneViewCamera& camera,
                                         const ProjectDocument& document,
                                         const EditorState& editorState,
                                         const SceneId& sceneId);

std::string formatViewportPointerReadout(const ViewportPointerReadout& readout);

TilemapCellCoord worldPositionToTilemapCell(Vec2 world, Vec2 origin, Vec2 cellSize);

bool selectionSupportsTilemapEditing(const ProjectDocument& document,
                                     const EditorState& editorState,
                                     const SceneId& sceneId);

int  visualGridStrideForZoom(float cellSize, float zoom);
Vec2 snapWorldPositionToGrid(Vec2 worldPosition, const SceneGridDefinition& grid);

} // namespace ArtCade::EditorNative
