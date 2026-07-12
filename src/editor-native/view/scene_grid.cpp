#include "editor-native/view/scene_grid.h"

#include "editor-native/model/project_document.h"

#include <cmath>

namespace ArtCade::EditorNative {

namespace {

std::string normalizedActiveLayerId(const ProjectDocument& document, const EditorState& editorState,
                                    const SceneId& sceneId) {
    const SceneDef* scene = document.findScene(sceneId);
    if (!scene) return {};
    std::string active;
    const auto it = editorState.sceneViews.find(sceneId);
    if (it != editorState.sceneViews.end()) active = it->second.activeLayerId;
    if (active.empty() || !document.hasLayer(sceneId, active)) active = scene->defaultLayerId;
    return active;
}

const EditorSceneViewState& sceneViewStateOrDefault(const EditorState& editorState,
                                                    const SceneId& sceneId) {
    static const EditorSceneViewState kDefault{};
    const auto it = editorState.sceneViews.find(sceneId);
    return it == editorState.sceneViews.end() ? kDefault : it->second;
}

} // namespace

bool selectionSupportsTilemapEditing(const ProjectDocument& document,
                                     const EditorState& editorState,
                                     const SceneId& sceneId) {
    if (sceneId.empty() || sceneId != editorState.activeSceneId) return false;
    if (editorState.selection.primaryEntity == INVALID_ENTITY) return false;

    const SceneInstanceDef* instance =
        document.findInstanceInScene(sceneId, editorState.selection.primaryEntity);
    if (instance == nullptr || !instance->tilemap.has_value()) return false;

    const std::string activeLayer = normalizedActiveLayerId(document, editorState, sceneId);
    return document.effectiveLayerId(sceneId, *instance) == activeLayer
        && !document.isInstanceLayerLocked(sceneId, *instance);
}

SceneGridDefinition worldAuthoringGrid(const EditorSceneViewState& viewState) {
    float size = viewState.gridCellSize;
    if (!std::isfinite(size) || size <= 0.0f) size = SceneGridDefaults::kCellSize;
    return SceneGridDefinition{
        SceneGridKind::World,
        Vec2{size, size},
        Vec2{0.0f, 0.0f},
    };
}

std::optional<SceneGridDefinition> tilemapCellGrid(const ProjectDocument& document,
                                                   const EditorState& editorState,
                                                   const SceneId& sceneId) {
    if (!selectionSupportsTilemapEditing(document, editorState, sceneId)) return std::nullopt;
    const SceneInstanceDef* inst =
        document.findInstanceInScene(sceneId, editorState.selection.primaryEntity);
    if (!inst || !inst->tilemap.has_value()) return std::nullopt;
    const Vec2& cellSize = inst->tilemap->cellSize;
    return SceneGridDefinition{
        SceneGridKind::Tilemap,
        cellSize,
        inst->transform.position,
    };
}

SceneGridDefinition viewportDisplayGrid(const ProjectDocument& document,
                                        const EditorState& editorState,
                                        const SceneId& sceneId) {
    if (isTilemapTool(editorState.activeTool)) {
        if (const std::optional<SceneGridDefinition> tilemap =
                tilemapCellGrid(document, editorState, sceneId)) {
            return *tilemap;
        }
    }
    return worldAuthoringGrid(sceneViewStateOrDefault(editorState, sceneId));
}

int visualGridStrideForZoom(float cellSize, float zoom) {
    if (!std::isfinite(cellSize) || cellSize <= 0.0f
        || !std::isfinite(zoom) || zoom <= 0.0f) {
        return 1;
    }

    constexpr float kMinScreenStepPx = 8.0f;
    int stride = 1;
    while (cellSize * static_cast<float>(stride) * zoom < kMinScreenStepPx
           && stride < 1024) {
        stride *= 2;
    }
    return stride;
}

Vec2 snapWorldPositionToGrid(Vec2 worldPosition, const SceneGridDefinition& grid) {
    if (!std::isfinite(grid.cellSize.x) || !std::isfinite(grid.cellSize.y)
        || grid.cellSize.x <= 0.0f || grid.cellSize.y <= 0.0f) {
        return worldPosition;
    }
    const auto snapAxis = [&](float value, float origin, float cellSize) {
        return origin + std::round((value - origin) / cellSize) * cellSize;
    };
    return Vec2{
        snapAxis(worldPosition.x, grid.origin.x, grid.cellSize.x),
        snapAxis(worldPosition.y, grid.origin.y, grid.cellSize.y),
    };
}

} // namespace ArtCade::EditorNative
