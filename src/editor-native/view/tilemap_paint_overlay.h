#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"
#include "editor-native/view/scene_view_camera.h"

namespace ArtCade::EditorNative {

class ProjectDocument;

// Draws the hovered-cell highlight for the active paint tool (Eraser gets a
// distinct color) on top of the persistent Scene View render. With the Brush
// and a multi-tile stamp selected, the outline covers the whole N x M
// footprint the next click would paint. Pure drawing, no input handling -
// reads TilemapEditorState directly since, unlike SceneView::render(), this
// overlay only exists while a paint tool is active and genuinely needs
// workspace state (the hovered cell, the stamp) that has no place in a
// document-pure SceneFrameSnapshot. `effectiveTool` is the coordinator's
// effectiveTilemapTool() (persistent selection or the momentary right-click
// Eraser override).
void drawTilemapPaintOverlay(const ProjectDocument& document, const TilemapEditorState& tilemapEditor,
                             EditorTool effectiveTool,
                             const SceneId& sceneId, EntityId entityId,
                             const ViewportRect& rect, const EditorSceneViewState& view, Vec2 worldSize);

} // namespace ArtCade::EditorNative
