#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"
#include "editor-native/view/scene_view_camera.h"

namespace ArtCade::EditorNative {

class ProjectDocument;

// Draws the hovered-cell highlight for the active paint tool (Eraser gets a
// distinct color) on top of the persistent Scene View render. Pure drawing,
// no input handling - reads TilemapEditorState directly since, unlike
// SceneView::render(), this overlay only exists while a paint tool is
// active and genuinely needs workspace state (the hovered cell) that has no
// place in a document-pure SceneFrameSnapshot.
void drawTilemapPaintOverlay(const ProjectDocument& document, const TilemapEditorState& tilemapEditor,
                             const SceneId& sceneId, EntityId entityId,
                             const ViewportRect& rect, const EditorSceneViewState& view, Vec2 worldSize);

} // namespace ArtCade::EditorNative
