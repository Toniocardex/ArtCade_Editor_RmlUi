#include "editor-native/view/scene_grid.h"

#include <cmath>

namespace ArtCade::EditorNative {

SceneGridDefinition makeSceneGridDefinition() {
    return SceneGridDefinition{SceneGridDefaults::kCellSize, Vec2{0.0f, 0.0f}};
}

SceneGridDefinition makeSceneGridDefinition(const EditorSceneViewState& viewState) {
    if (!std::isfinite(viewState.gridCellSize) || viewState.gridCellSize <= 0.0f)
        return makeSceneGridDefinition();
    return SceneGridDefinition{viewState.gridCellSize, Vec2{0.0f, 0.0f}};
}

int visualGridStrideForZoom(const SceneGridDefinition& grid, float zoom) {
    if (!std::isfinite(grid.cellSize) || grid.cellSize <= 0.0f
        || !std::isfinite(zoom) || zoom <= 0.0f) {
        return 1;
    }

    constexpr float kMinScreenStepPx = 8.0f;
    int stride = 1;
    while (grid.cellSize * static_cast<float>(stride) * zoom < kMinScreenStepPx
           && stride < 1024) {
        stride *= 2;
    }
    return stride;
}

Vec2 snapWorldPositionToGrid(Vec2 worldPosition, const SceneGridDefinition& grid) {
    if (!std::isfinite(grid.cellSize) || grid.cellSize <= 0.0f) return worldPosition;
    const auto snapAxis = [&](float value, float origin) {
        return origin + std::round((value - origin) / grid.cellSize) * grid.cellSize;
    };
    return Vec2{
        snapAxis(worldPosition.x, grid.origin.x),
        snapAxis(worldPosition.y, grid.origin.y),
    };
}

} // namespace ArtCade::EditorNative
