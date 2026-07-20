#include "editor-native/app/scene_view_interaction.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/hierarchy_actions.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/scene_view.h"
#include "editor-native/view/scene_view_camera.h"

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <raylib.h>

#include <algorithm>
#include <optional>
#include <string>

namespace ArtCade::EditorNative {

float uiPixelScaleX() {
    const int sw = GetScreenWidth();
    return sw > 0 ? static_cast<float>(GetRenderWidth()) / static_cast<float>(sw) : 1.f;
}
float uiPixelScaleY() {
    const int sh = GetScreenHeight();
    return sh > 0 ? static_cast<float>(GetRenderHeight()) / static_cast<float>(sh) : 1.f;
}

// The viewport / canvas elements are laid out in RmlUi's physical-pixel space;
// raylib drawing and pick/drag use logical pixels, so convert once here.
ViewportRect elementContentRectFromDocument(Rml::ElementDocument* document, const char* id) {
    ViewportRect rect;
    if (!document || !id) return rect;
    const float sx = uiPixelScaleX();
    const float sy = uiPixelScaleY();
    if (Rml::Element* el = document->GetElementById(id)) {
        const Rml::Vector2f off = el->GetAbsoluteOffset(Rml::BoxArea::Content);
        const Rml::Vector2f size = el->GetBox().GetSize(Rml::BoxArea::Content);
        rect.x = static_cast<int>(off.x / sx);
        rect.y = static_cast<int>(off.y / sy);
        rect.width  = static_cast<int>(size.x / sx);
        rect.height = static_cast<int>(size.y / sy);
    }
    return rect;
}

ViewportRect viewportRectFromDocument(Rml::ElementDocument* document) {
    // Content box (not border) so the pick/draw rect matches the transparent
    // hole the user sees — same convention as animation/tileset canvases.
    return elementContentRectFromDocument(document, "viewport");
}

ViewportRect resolveSpriteAnimationCanvasContentRect(Rml::ElementDocument* document) {
    return elementContentRectFromDocument(document, "animation-sprite-canvas");
}

ViewportRect resolveSpriteAnimationPreviewContentRect(Rml::ElementDocument* document) {
    return elementContentRectFromDocument(document, "animation-preview-canvas");
}

ViewportRect resolveTilesetEditorCanvasContentRect(Rml::ElementDocument* document) {
    return elementContentRectFromDocument(document, "tileset-canvas");
}

void syncEditorOverlayViewport(Rml::ElementDocument* document) {
    if (!document) return;
    document->SetProperty("position", "absolute");
    document->SetProperty("left", "0px");
    document->SetProperty("top", "0px");
    document->SetProperty("width", std::to_string(GetRenderWidth()) + "px");
    document->SetProperty("height", std::to_string(GetRenderHeight()) + "px");
    document->SetProperty("background-color", "#0e0e10");
}

void routeViewportInput(EditorCoordinator& coordinator, const ViewportRect& rect,
                        const RmlInputResult& rml, bool contextMenuHit) {
    // Inside the viewport region we are not over a panel; a focused text field
    // still blocks the viewport (prompt §19 / §24.16).
    const ViewportInputContext ctx{
        rect.contains(GetMouseX(), GetMouseY()),
        /*rmlConsumedEvent*/ contextMenuHit,
        rml.textFocus,
        /*rmlPopupOpen*/ false,
    };
    if (!shouldViewportReceiveInput(ctx)) return;

    const PlaySession* playSession = coordinator.playSession();
    const SceneId active = playSession ? playSession->sceneId()
                                       : coordinator.state().activeSceneId;
    const Vec2 worldSize = playSession
        ? playSession->scene().worldSize
        : (coordinator.document().findScene(active)
               ? coordinator.document().findScene(active)->worldSize
               : Vec2{});

    // Zoom under the cursor: keep the world point beneath the mouse fixed (more
    // precise than centre-zoom for level design). All workspace, single camera.
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        const Vec2 mouse{static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY())};
        const EditorSceneViewState before = coordinator.sceneView(active);
        const Vec2 worldBefore =
            screenToWorld(makeSceneViewCamera(rect, before, worldSize), mouse);
        coordinator.apply(SetViewportZoomIntent{active, before.zoom * (1.0f + wheel * 0.1f)});
        const EditorSceneViewState after = coordinator.sceneView(active);
        const Vec2 worldAfter =
            screenToWorld(makeSceneViewCamera(rect, after, worldSize), mouse);
        coordinator.apply(PanViewportIntent{
            active, {worldBefore.x - worldAfter.x, worldBefore.y - worldAfter.y}});
    }

    // Pan: middle-mouse, or Space + left-mouse. The right button is left free for
    // the context menu / Create Here.
    const bool spacePan = IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || spacePan) {
        const float zoom = coordinator.sceneView(active).zoom;
        const Vector2 d = GetMouseDelta();
        coordinator.apply(PanViewportIntent{active, {-d.x / zoom, -d.y / zoom}});
    }
}


bool sceneSurfaceContains(const ViewportRect& rect, const SceneViewCamera& camera,
                          Vec2 worldSize, Vec2 screen) {
    const auto project = [&](Vec2 world) {
        return Vec2{
            (world.x - camera.target.x) * camera.zoom + camera.offset.x,
            (world.y - camera.target.y) * camera.zoom + camera.offset.y,
        };
    };
    const Vec2 a = project(Vec2{0.0f, 0.0f});
    const Vec2 b = project(worldSize);
    const float left = std::max(static_cast<float>(rect.x), std::min(a.x, b.x));
    const float right = std::min(static_cast<float>(rect.x + rect.width), std::max(a.x, b.x));
    const float top = std::max(static_cast<float>(rect.y), std::min(a.y, b.y));
    const float bottom = std::min(static_cast<float>(rect.y + rect.height), std::max(a.y, b.y));
    return screen.x >= left && screen.x < right && screen.y >= top && screen.y < bottom;
}

Vec2 applySceneGridSnap(const EditorCoordinator& coordinator, const SceneId& sceneId,
                        Vec2 worldPosition) {
    if (!coordinator.sceneView(sceneId).gridSnapEnabled) return worldPosition;
    return snapWorldPositionToGrid(
        worldPosition, worldAuthoringGrid(coordinator.sceneView(sceneId)));
}

std::optional<Vec2> dragPreviewPosition(const EditorCoordinator& coordinator,
                                        const ViewportRect& rect,
                                        const ViewportDrag& drag) {
    if (!drag.active) return std::nullopt;
    const SceneId active = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(active);
    if (!scene) return std::nullopt;

    const SceneViewCamera cam =
        makeSceneViewCamera(rect, coordinator.sceneView(active), scene->worldSize);
    const Vec2 cur = screenToWorld(cam, Vec2{static_cast<float>(GetMouseX()),
                                             static_cast<float>(GetMouseY())});
    const Vec2 d{cur.x - drag.startMouseWorld.x, cur.y - drag.startMouseWorld.y};
    return applySceneGridSnap(
        coordinator, active, Vec2{drag.startEntityPos.x + d.x, drag.startEntityPos.y + d.y});
}

// Escape is a keyboard-wide gesture, not scoped to whichever panel/viewport
// currently has mouse focus, so it is arbitrated once per frame here rather
// than inside any single input-routing module. Exactly one level fires per
// press: (1) cancel a pending tilemap gesture - tilemap_paint_input.cpp keeps
// only its own focus-loss trigger for the same shared primitive, never
// Escape-key polling itself; (2) if nothing was pending and an entity is
// selected, clear the selection in this same press - a paint tool owns
// viewport clicks while active (routeViewportPickDrag below), so clicking
// empty canvas can never deselect the way it does with Select; Escape is the
// only affordance back to the Scene Inspector, so it must not cost two
// presses. SelectEntityIntent{INVALID_ENTITY} already falls the active tool
// back to Select via reconcileTilemapEditingContext (nothing left to
// operate on), so there is no separate "switch to Select" step to sequence
// first; (3) only when nothing was selected but a tilemap tool is still
// somehow active (no target at all) does Escape fall back to Select alone.
// None of the three levels touches ProjectDocument/dirty/undo.
void routeGlobalEscape(EditorCoordinator& coordinator) {
    if (coordinator.cancelPendingTilemapGesture()) return;
    if (coordinator.selection().hasEntity()) {
        coordinator.apply(SelectEntityIntent{INVALID_ENTITY});
        return;
    }
    if (isTilemapTool(coordinator.state().activeTool)) {
        coordinator.apply(SetActiveToolIntent{EditorTool::Select});
    }
}

// Edit-mode pick + drag: press hit-tests and selects; release commits one move.
// Motion between press and release is shown as a local preview by the draw path,
// not as a stream of commands.
void routeViewportPickDrag(EditorCoordinator& coordinator, const ViewportRect& rect,
                           const RmlInputResult& rml, ViewportDrag& drag,
                           bool contextMenuHit) {
    // A paint tool owns viewport clicks while active - entity pick/drag must
    // not also claim them.
    if (coordinator.state().activeTool != EditorTool::Select) return;
    const SceneId active = coordinator.state().activeSceneId;
    // Hidden layers are not pickable: the snapshot the picker reads excludes them.
    const SceneFrameSnapshot frame = collectSceneFrameSnapshot(
        coordinator.document(), active, coordinator.selection().primaryEntity,
        coordinator.sceneView(active).hiddenLayerIds);
    const SceneViewCamera cam =
        makeSceneViewCamera(rect, coordinator.sceneView(active), frame.worldSize);
    const Vec2 mouse{static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY())};

    // Space + left-drag is a pan gesture, not a pick (handled by routeViewportInput).
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !IsKeyDown(KEY_SPACE)) {
        const ViewportInputContext ctx{rect.contains(GetMouseX(), GetMouseY()),
                                       /*rmlConsumedEvent*/ contextMenuHit, rml.textFocus,
                                       /*rmlPopupOpen*/ false};
        if (shouldViewportReceiveInput(ctx)) {
            const Vec2 world = screenToWorld(cam, mouse);
            EntityId picked = pickEntityAt(frame, world);
            // Locked layers stay non-pickable in the Scene View (Hierarchy can
            // still select them). Other layers are selectable here too: the
            // SelectEntityIntent below switches activeLayerId to match, same as
            // a Hierarchy click — rejecting them used to look like "click does
            // nothing" while Inspector stayed on the Scene.
            if (picked != INVALID_ENTITY) {
                const SceneInstanceDef* pickedInst =
                    coordinator.document().findInstanceInScene(active, picked);
                if (!pickedInst
                    || coordinator.document().isInstanceLayerLocked(active, *pickedInst)) {
                    picked = INVALID_ENTITY;
                }
            }
            coordinator.apply(SelectEntityIntent{picked});   // INVALID clears selection
            if (picked != INVALID_ENTITY) {
                if (const SceneInstanceDef* inst =
                        coordinator.document().findInstanceInScene(active, picked)) {
                    drag = ViewportDrag{true, picked, world, inst->transform.position};
                }
            }
        }
    }

    if (drag.active && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (const std::optional<Vec2> preview = dragPreviewPosition(coordinator, rect, drag)) {
            if (preview->x != drag.startEntityPos.x || preview->y != drag.startEntityPos.y) {
                coordinator.execute(SetEntityTransformCommand{
                    active, drag.entity, AuthoredTransformPatch{*preview}});
            }
        }
        drag = ViewportDrag{};
    }
}

void routeViewportContextMenu(EditorCoordinator& coordinator, EditorUi& ui,
                              const ViewportRect& rect, const RmlInputResult& rml,
                              ViewportContextClick& click,
                              std::optional<Vec2>& pendingSpawnPosition,
                              bool contextMenuHit) {
    if (coordinator.isPlaying()) {
        click = ViewportContextClick{};
        pendingSpawnPosition.reset();
        ui.hideContextMenus();
        return;
    }
    // A paint tool owns the viewport's right-click too (Eraser's right-click
    // shortcut in routeViewportTilemapPaint) - the entity-creation menu is a
    // Select-tool concept and must not fight it for the same gesture, mirroring
    // routeViewportPickDrag's own "a paint tool owns viewport clicks" guard.
    if (coordinator.state().activeTool != EditorTool::Select) {
        click = ViewportContextClick{};
        pendingSpawnPosition.reset();
        ui.hideContextMenus();
        return;
    }

    if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE))
        && !contextMenuHit) {
        ui.hideContextMenus();
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        ui.hideContextMenus();
        pendingSpawnPosition.reset();
        const ViewportInputContext ctx{rect.contains(GetMouseX(), GetMouseY()),
                                       /*rmlConsumedEvent*/ contextMenuHit, rml.textFocus,
                                       /*rmlPopupOpen*/ false};
        if (shouldViewportReceiveInput(ctx)) {
            click = ViewportContextClick{true, {GetMouseX() * 1.0f, GetMouseY() * 1.0f}};
        }
    }

    if (!click.tracking || !IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) return;

    const Vector2 end = GetMousePosition();
    const Vector2 delta{end.x - click.start.x, end.y - click.start.y};
    click = ViewportContextClick{};
    constexpr float kClickThresholdPx = 4.0f;
    if (delta.x * delta.x + delta.y * delta.y > kClickThresholdPx * kClickThresholdPx) {
        return;
    }

    const ViewportInputContext ctx{rect.contains(GetMouseX(), GetMouseY()),
                                   /*rmlConsumedEvent*/ contextMenuHit, rml.textFocus,
                                   /*rmlPopupOpen*/ false};
    if (!shouldViewportReceiveInput(ctx)) return;

    const SceneId& active = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(active);
    if (!scene) return;

    const SceneViewCamera camera = makeSceneViewCamera(rect, coordinator.sceneView(active),
                                                       scene->worldSize);
    const Vec2 mouse{static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY())};
    if (!sceneSurfaceContains(rect, camera, scene->worldSize, mouse)) return;

    SpawnPositionOptions options;
    options.edgeMargin = 0.0f;   // "Here" should mean the clicked world position.
    const Vec2 rawSpawn = screenToWorld(camera, mouse);
    pendingSpawnPosition = normalizeSpawnPosition(
        applySceneGridSnap(coordinator, active, rawSpawn), scene->worldSize, options);
    bool canCreateInstance = false;
    if (const SceneInstanceDef* selected = coordinator.document().findInstanceInScene(
            active, coordinator.selection().primaryEntity)) {
        canCreateInstance = coordinator.document().findObjectType(selected->objectTypeId) != nullptr;
    }
    ui.showViewportContextMenu(
        static_cast<int>(mouse.x * uiPixelScaleX()),
        static_cast<int>(mouse.y * uiPixelScaleY()),
        canCreateInstance);
}

} // namespace ArtCade::EditorNative
