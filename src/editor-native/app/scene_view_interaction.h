#pragma once

#include "core/types.h"
#include "editor-native/app/editor_input.h"
#include "editor-native/app/input_routing.h"
#include "editor-native/model/transform_gizmo_math.h"
#include "editor-native/view/scene_view.h"

#include <optional>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;
class EditorUi;

struct ViewportContextClick {
    bool tracking = false;
    Vec2 start{};
};

float uiPixelScaleX();
float uiPixelScaleY();
ViewportRect viewportRectFromDocument(Rml::ElementDocument* document);
ViewportRect elementContentRectFromDocument(Rml::ElementDocument* document, const char* id);
ViewportRect resolveSpriteAnimationCanvasContentRect(Rml::ElementDocument* document);
ViewportRect resolveSpriteAnimationPreviewContentRect(Rml::ElementDocument* document);
ViewportRect resolveTilesetEditorCanvasContentRect(Rml::ElementDocument* document);
void syncEditorOverlayViewport(Rml::ElementDocument* document);

void routeViewportInput(EditorCoordinator& coordinator, const SceneViewportProjection& projection,
                        const RmlInputResult& rml, bool contextMenuHit);
void routeGlobalEscape(EditorCoordinator& coordinator,
                       TransformInteractionState& transform);
/** Tilemap/tool Escape only — no transform state available at the call site. */
inline void routeGlobalEscape(EditorCoordinator& coordinator) {
    TransformInteractionState unused;
    routeGlobalEscape(coordinator, unused);
}
void routeViewportPickDrag(EditorCoordinator& coordinator, const SceneViewportProjection& projection,
                           const RmlInputResult& rml, const SceneFrameSnapshot& frame,
                           TransformInteractionState& transform,
                           bool contextMenuHit);
void routeViewportContextMenu(EditorCoordinator& coordinator, EditorUi& ui,
                              const SceneViewportProjection& projection, const RmlInputResult& rml,
                              ViewportContextClick& click,
                              std::optional<Vec2>& pendingSpawnPosition,
                              bool contextMenuHit);

/** Hover handle for the current selection (None when no scale grips / miss). */
TransformHandle hoverTransformHandle(const EditorCoordinator& coordinator,
                                     const SceneViewportProjection& projection,
                                     const SceneFrameSnapshot& frame,
                                     Vec2 screenMouse);

} // namespace ArtCade::EditorNative
