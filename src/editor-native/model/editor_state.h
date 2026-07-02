#pragma once

#include "core/types.h"
#include "editor-native/model/selection_state.h"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace SceneViewLimits {
constexpr float kZoomMin = 0.1f;
constexpr float kZoomMax = 8.0f;
} // namespace SceneViewLimits

namespace SceneGridDefaults {
constexpr float kCellSize = 48.0f;
} // namespace SceneGridDefaults

// Sheet zoom multiplies the fit-to-canvas base scale, so 1.0 = "fit view".
namespace SpriteSheetViewLimits {
constexpr float kZoomMin = 0.25f;
constexpr float kZoomMax = 16.0f;
} // namespace SpriteSheetViewLimits

/** Per-scene editor camera. Stored by SceneId, not the gameplay camera. */
struct EditorSceneViewState {
    Vec2  pan{};
    float zoom = 1.0f;
    // True once the one-time auto-fit has run for this scene. Lives here (not an
    // app-side registry) so it shares the single sceneViews lifecycle: cleared by
    // replaceProject, pruned with a deleted scene, reset for a fresh scene.
    bool  initialized = false;
    // Layer workspace state (never persisted, never dirties): the layer new
    // entities go into, and the layers hidden in the Edit viewport only.
    std::string                     activeLayerId;
    std::unordered_set<std::string> hiddenLayerIds;
    // Scene View workspace controls. Grid visibility and snap are intentionally
    // independent: hiding the grid never disables snapping.
    bool gridVisible = true;
    bool gridSnapEnabled = false;
    float gridCellSize = SceneGridDefaults::kCellSize;
};

struct SpriteAnimationEditorState {
    std::optional<AssetId> openAssetId;
    std::optional<std::string> selectedClipId;
    std::size_t selectedFrameIndex = 0;
    int sliceFrameWidth = 32;
    int sliceFrameHeight = 32;
    int sliceColumns = 1;
    int sliceRows = 1;
    int sliceMargin = 0;
    int sliceSpacing = 0;
    float sheetZoom = 1.0f;
    Vec2 sheetPan{};
    bool previewPlaying = false;
    float previewElapsed = 0.0f;
    std::size_t previewFrameIndex = 0;
};

enum class EditorTool {
    Select,
    Pan,
};

// Shared workspace state. This is not saved into the project file.
struct EditorState {
    // Workspace focus only. This is NOT the gameplay start scene.
    SceneId activeSceneId;
    SelectionState selection;
    EditorTool activeTool = EditorTool::Select;
    std::unordered_map<SceneId, EditorSceneViewState> sceneViews;
    SpriteAnimationEditorState spriteAnimationEditor;
};

inline float clampZoom(float v) {
    return std::clamp(v, SceneViewLimits::kZoomMin, SceneViewLimits::kZoomMax);
}

inline float clampSheetZoom(float v) {
    return std::clamp(v, SpriteSheetViewLimits::kZoomMin, SpriteSheetViewLimits::kZoomMax);
}

} // namespace ArtCade::EditorNative
