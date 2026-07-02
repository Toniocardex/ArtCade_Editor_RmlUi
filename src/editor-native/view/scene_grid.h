#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"

namespace ArtCade::EditorNative {

struct SceneGridDefinition {
    float cellSize = SceneGridDefaults::kCellSize;
    Vec2  origin{};
};

SceneGridDefinition makeSceneGridDefinition();
SceneGridDefinition makeSceneGridDefinition(const EditorSceneViewState& viewState);
int visualGridStrideForZoom(const SceneGridDefinition& grid, float zoom);
Vec2 snapWorldPositionToGrid(Vec2 worldPosition, const SceneGridDefinition& grid);

} // namespace ArtCade::EditorNative
