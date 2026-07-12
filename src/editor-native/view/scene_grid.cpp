#include "editor-native/view/scene_grid.h"

#include "editor-native/model/project_document.h"
#include "editor-native/view/scene_view_camera.h"

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

constexpr std::size_t kToolbarContextNameMaxChars = 14;
constexpr const char kUtf8Ellipsis[] = "\xE2\x80\xA6";

std::size_t utf8SequenceLength(unsigned char lead) {
    if ((lead & 0x80) == 0) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

std::size_t utf8CodePointCount(const std::string& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t len =
            utf8SequenceLength(static_cast<unsigned char>(text[i]));
        if (i + len > text.size()) break;
        i += len;
        ++count;
    }
    return count;
}

// Truncate on Unicode code-point boundaries so accented names (e.g. "città")
// never split a multibyte sequence mid-character.
std::string truncateWithEllipsis(const std::string& text, std::size_t maxChars) {
    if (maxChars == 0) return {};
    if (utf8CodePointCount(text) <= maxChars) return text;
    if (maxChars == 1) return kUtf8Ellipsis;

    std::size_t codePoints = 0;
    std::size_t byteEnd = 0;
    for (std::size_t i = 0; i < text.size() && codePoints < maxChars - 1;) {
        const std::size_t len =
            utf8SequenceLength(static_cast<unsigned char>(text[i]));
        if (i + len > text.size()) break;
        i += len;
        byteEnd = i;
        ++codePoints;
    }
    return text.substr(0, byteEnd) + kUtf8Ellipsis;
}

long formatWorldAxis(float value) {
    return static_cast<long>(std::lround(value));
}

} // namespace

TilemapCellCoord worldPositionToTilemapCell(Vec2 world, Vec2 origin, Vec2 cellSize) {
    return TilemapCellCoord{
        static_cast<int>(std::floor((world.x - origin.x) / cellSize.x)),
        static_cast<int>(std::floor((world.y - origin.y) / cellSize.y)),
    };
}

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

SceneGridPresentation makeSceneGridPresentation(const ProjectDocument& document,
                                                const EditorState& editorState,
                                                const SceneId& sceneId) {
    const SceneGridDefinition display = viewportDisplayGrid(document, editorState, sceneId);
    SceneGridPresentation pres;
    pres.kind = display.kind;
    pres.cellSize = display.cellSize;

    if (display.kind == SceneGridKind::Tilemap && isTilemapTool(editorState.activeTool)) {
        const SceneInstanceDef* inst =
            document.findInstanceInScene(sceneId, editorState.selection.primaryEntity);
        pres.contextName = inst ? inst->instanceName : std::string();
        pres.toolbarContextName = truncateWithEllipsis(pres.contextName, kToolbarContextNameMaxChars);
        pres.toolbarTooltip = "Tilemap grid from \"" + pres.contextName + "\"";
        pres.sourceEntityId = editorState.selection.primaryEntity;
        pres.sizeEditable = false;
    } else {
        pres.kind = SceneGridKind::World;
        pres.contextName = "World";
        pres.toolbarContextName = "World";
        pres.toolbarTooltip = {};
        pres.sourceEntityId = std::nullopt;
        pres.sizeEditable = true;
        pres.cellSize = worldAuthoringGrid(sceneViewStateOrDefault(editorState, sceneId)).cellSize;
    }
    return pres;
}

ViewportPointerReadout makePointerReadout(Vec2 screenPosition,
                                          const SceneViewCamera& camera,
                                          const ProjectDocument& document,
                                          const EditorState& editorState,
                                          const SceneId& sceneId) {
    ViewportPointerReadout result;
    result.valid = true;
    result.worldPosition = screenToWorld(camera, screenPosition);

    const SceneGridDefinition grid = viewportDisplayGrid(document, editorState, sceneId);
    if (grid.kind == SceneGridKind::Tilemap && isTilemapTool(editorState.activeTool)) {
        result.tilemapCell =
            worldPositionToTilemapCell(result.worldPosition, grid.origin, grid.cellSize);
        result.tilemapEntityId = editorState.selection.primaryEntity;
    }
    return result;
}

std::string formatViewportPointerReadout(const ViewportPointerReadout& readout) {
    if (!readout.valid) return {};
    std::string text = "World "
                     + std::to_string(formatWorldAxis(readout.worldPosition.x)) + ", "
                     + std::to_string(formatWorldAxis(readout.worldPosition.y));
    if (readout.tilemapCell.has_value()) {
        text += "  \xC2\xB7  Cell "
              + std::to_string(readout.tilemapCell->cellX) + ", "
              + std::to_string(readout.tilemapCell->cellY);
    }
    return text;
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
