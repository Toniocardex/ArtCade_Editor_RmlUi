#pragma once

#include "core/types.h"
#include "editor-native/app/editor_input.h"
#include "editor-native/app/input_routing.h"
#include "editor-native/view/scene_view.h"

#include <optional>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;
class EditorUi;

struct ViewportDrag {
    bool     active = false;
    EntityId entity = INVALID_ENTITY;
    Vec2     startMouseWorld{};
    Vec2     startEntityPos{};
};

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
void routeGlobalEscape(EditorCoordinator& coordinator);
void routeViewportPickDrag(EditorCoordinator& coordinator, const SceneViewportProjection& projection,
                           const RmlInputResult& rml, ViewportDrag& drag,
                           bool contextMenuHit);
void routeViewportContextMenu(EditorCoordinator& coordinator, EditorUi& ui,
                              const SceneViewportProjection& projection, const RmlInputResult& rml,
                              ViewportContextClick& click,
                              std::optional<Vec2>& pendingSpawnPosition,
                              bool contextMenuHit);
std::optional<Vec2> dragPreviewPosition(const EditorCoordinator& coordinator,
                                        const SceneViewportProjection& projection,
                                        const ViewportDrag& drag);

} // namespace ArtCade::EditorNative
