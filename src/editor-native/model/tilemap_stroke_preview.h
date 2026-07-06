#pragma once

#include "editor-native/model/editor_state.h"
#include "editor-native/model/scene_frame_snapshot.h"

namespace ArtCade::EditorNative {

class ProjectDocument;

// Overlays a PendingTileStroke's uncommitted changes onto an already-
// collected SceneFrameSnapshot, so the Scene View shows "what the tilemap
// would look like if this stroke committed" without ProjectDocument ever
// being touched. Rule: if the pending stroke contains a cell, show its
// `after` value (nullopt removes the drawn cell - Eraser preview);
// otherwise leave the snapshot's already-collected (committed) value
// untouched. Mirrors the drag-preview pattern already applied in-place to a
// SceneFrameSnapshot in editor_app.cpp's render block, rather than
// threading an overlay parameter into collectSceneFrameSnapshot itself or a
// second EditorState-aware renderer (that pattern exists for the Tileset
// Editor specifically because that canvas is a separate zoom/pan space,
// which the Scene View is not).
void applyPendingTilemapStrokePreview(SceneFrameSnapshot& snapshot,
                                      const ProjectDocument& document,
                                      const TilemapEditorState& tilemapEditor);

} // namespace ArtCade::EditorNative
