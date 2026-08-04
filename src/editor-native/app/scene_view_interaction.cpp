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

void routeViewportInput(EditorCoordinator& coordinator, const SceneViewportProjection& projection,
                        const RmlInputResult& rml, bool contextMenuHit) {
    // Inside the viewport region we are not over a panel; a focused text field
    // still blocks the viewport (prompt §19 / §24.16).
    const ViewportInputContext ctx{
        projection.visibleRect.contains(GetMouseX(), GetMouseY()),
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
    // precise than centre-zoom for level design). Both cameras route through
    // resolveSceneViewportProjection (never makeSceneViewCamera directly), so
    // "before" and "after" can never disagree with render/pick/drag/paint on
    // what the camera is.
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        const Vec2 mouse{static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY())};
        const Vec2 worldBefore = screenToWorld(projection.camera, mouse);
        const EditorSceneViewState before = coordinator.sceneView(active);
        coordinator.apply(SetViewportZoomIntent{active, before.zoom * (1.0f + wheel * 0.1f)});
        const SceneViewportProjection afterProjection = resolveSceneViewportProjection(
            projection.visibleRect, projection.cameraAnchorRect,
            coordinator.sceneView(active), worldSize);
        const Vec2 worldAfter = screenToWorld(afterProjection.camera, mouse);
        coordinator.apply(PanViewportIntent{
            active, {worldBefore.x - worldAfter.x, worldBefore.y - worldAfter.y}});
    }

    // Pan: middle-mouse, Space + left-mouse, or left-drag while the Pan tool is
    // active (toolbar). Right button stays free for the context menu / Create Here
    // (Select tool only — routeViewportContextMenu). Tool-pan is Edit-only so a
    // leftover Pan tool cannot steal Play left-clicks from gameplay focus.
    const bool spacePan = IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool toolPan = !coordinator.isPlaying()
        && coordinator.state().activeTool == EditorTool::Pan
        && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || spacePan || toolPan) {
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

// Escape is a keyboard-wide gesture, not scoped to whichever panel/viewport
// currently has mouse focus, so it is arbitrated once per frame here rather
// than inside any single input-routing module. Escape only ever cancels the
// current operation - it never touches the entity selection, Photoshop-
// style (Esc aborts a crop/marquee/transform; deselecting is always a
// separate, deliberate action - Select > Deselect, never overloaded onto
// Esc). Overloading it here too was tried and reverted: Escape gets pressed
// reflexively for unrelated reasons (dismiss a popup, back out of a drag),
// and losing the selection as a surprise side effect broke that reflex.
// Deselecting now lives only in the Inspector's own breadcrumb
// (data-action="deselect-entity", inspector_panel.cpp) - a dedicated,
// always-visible affordance that never collides with anything else.
// Exactly one level fires per press: (0) cancel an active transform gizmo
// gesture (preview discarded, selection kept); (1) cancel a pending tilemap
// gesture; (2) if nothing was pending and a tilemap tool is active, fall back
// to Select. None of these levels touch ProjectDocument/dirty/undo/selection
// except the tool Intent in (2).
void routeGlobalEscape(EditorCoordinator& coordinator,
                       TransformInteractionState& transform) {
    if (transform.active) {
        cancelTransformInteraction(transform);
        return;
    }
    if (coordinator.cancelPendingTilemapGesture()) return;
    if (isTilemapTool(coordinator.state().activeTool)) {
        coordinator.apply(SetActiveToolIntent{EditorTool::Select});
    }
}

TransformHandle hoverTransformHandle(const EditorCoordinator& coordinator,
                                     const SceneViewportProjection& projection,
                                     const SceneFrameSnapshot& frame,
                                     Vec2 screenMouse) {
    if (coordinator.isPlaying()) return TransformHandle::None;
    if (coordinator.state().activeTool != EditorTool::Select) return TransformHandle::None;
    const EntityId selected = coordinator.selection().primaryEntity;
    if (selected == INVALID_ENTITY) return TransformHandle::None;
    if (coordinator.selection().hasObjectType()) return TransformHandle::None;

    const SceneId active = coordinator.state().activeSceneId;
    const auto geometry =
        resolveInstanceTransformGeometry(coordinator.document(), frame, active, selected);
    if (!geometry || !geometry->supportsScale) return TransformHandle::None;

    const SceneInstanceDef* inst =
        coordinator.document().findInstanceInScene(active, selected);
    if (!inst || coordinator.document().isInstanceLayerLocked(active, *inst)) {
        return TransformHandle::None;
    }

    return hitTestTransformHandle(
        geometry->transform, geometry->supportsScale, projection.camera, screenMouse);
}

namespace {

bool gizmoAllowedForInstance(const EditorCoordinator& coordinator,
                             const SceneId& sceneId,
                             EntityId entityId) {
    if (coordinator.isPlaying()) return false;
    if (coordinator.state().activeTool != EditorTool::Select) return false;
    if (coordinator.selection().hasObjectType()) return false;
    const SceneInstanceDef* inst =
        coordinator.document().findInstanceInScene(sceneId, entityId);
    if (!inst) return false;
    if (coordinator.document().isInstanceLayerLocked(sceneId, *inst)) return false;
    return resolveTransformGizmoCapabilities(coordinator.document(), sceneId, entityId).canMove;
}

void updateTransformPreview(TransformInteractionState& transform,
                            const EditorCoordinator& coordinator,
                            const SceneViewportProjection& projection) {
    if (!transform.active) return;
    const Vec2 mouse{static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY())};
    const Vec2 world = screenToWorld(projection.camera, mouse);
    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

    if (transform.handle == TransformHandle::Body) {
        Transform moved = moveTransformFromPointer(transform, world);
        moved.position = applySceneGridSnap(coordinator, transform.sceneId, moved.position);
        transform.previewTransform = moved;
    } else {
        TransformResizeSnap snap;
        if (coordinator.sceneView(transform.sceneId).gridSnapEnabled) {
            const SceneGridDefinition grid =
                worldAuthoringGrid(coordinator.sceneView(transform.sceneId));
            snap.enabled = true;
            snap.origin = grid.origin;
            snap.cellSize = grid.cellSize;
        }
        transform.previewTransform =
            resizeTransformFromHandle(transform, world, shift,
                                      snap.enabled ? &snap : nullptr);
    }
}

} // namespace

// Edit-mode pick + transform gizmo: handle hit-test before entity pick; release
// commits one SetEntityTransformCommand. Motion is a local preview only.
void routeViewportPickDrag(EditorCoordinator& coordinator, const SceneViewportProjection& projection,
                           const RmlInputResult& rml, const SceneFrameSnapshot& frame,
                           TransformInteractionState& transform,
                           bool contextMenuHit) {
    if (coordinator.state().activeTool != EditorTool::Select) {
        cancelTransformInteraction(transform);
        return;
    }
    if (transform.active) {
        if (coordinator.isPlaying()
            || coordinator.state().activeSceneId != transform.sceneId
            || coordinator.selection().primaryEntity != transform.entityId
            || coordinator.selection().hasObjectType()
            || !IsWindowFocused()) {
            cancelTransformInteraction(transform);
        } else if (const SceneInstanceDef* inst = coordinator.document().findInstanceInScene(
                       transform.sceneId, transform.entityId)) {
            if (coordinator.document().isInstanceLayerLocked(transform.sceneId, *inst)) {
                cancelTransformInteraction(transform);
            }
        } else {
            cancelTransformInteraction(transform);
        }
    }
    const SceneId active = coordinator.state().activeSceneId;
    const SceneViewCamera& cam = projection.camera;
    const Vec2 mouse{static_cast<float>(GetMouseX()), static_cast<float>(GetMouseY())};

    if (transform.active && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        updateTransformPreview(transform, coordinator, projection);
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !IsKeyDown(KEY_SPACE)) {
        const ViewportInputContext ctx{projection.visibleRect.contains(GetMouseX(), GetMouseY()),
                                       /*rmlConsumedEvent*/ contextMenuHit, rml.textFocus,
                                       /*rmlPopupOpen*/ false};
        if (shouldViewportReceiveInput(ctx)) {
            const Vec2 world = screenToWorld(cam, mouse);
            const EntityId alreadySelected = coordinator.selection().primaryEntity;

            // 1) Scale handles on the already-selected instance win over pick.
            if (alreadySelected != INVALID_ENTITY
                && gizmoAllowedForInstance(coordinator, active, alreadySelected)) {
                if (const auto geometry = resolveInstanceTransformGeometry(
                        coordinator.document(), frame, active, alreadySelected)) {
                    const TransformHandle handle = hitTestTransformHandle(
                        geometry->transform, geometry->supportsScale, cam, mouse);
                    if (handle != TransformHandle::None) {
                        if (const SceneInstanceDef* inst =
                                coordinator.document().findInstanceInScene(
                                    active, alreadySelected)) {
                            transform = beginTransformInteraction(
                                active, alreadySelected, handle, inst->transform,
                                *geometry, world);
                            return;
                        }
                    }
                }
            }

            const Vec2 viewport{
                mouse.x - static_cast<float>(projection.visibleRect.x),
                mouse.y - static_cast<float>(projection.visibleRect.y),
            };
            EntityId picked = pickEntityAt(frame, ScenePickPoint{world, viewport});
            if (picked != INVALID_ENTITY) {
                const SceneInstanceDef* pickedInst =
                    coordinator.document().findInstanceInScene(active, picked);
                if (!pickedInst
                    || coordinator.document().isInstanceLayerLocked(active, *pickedInst)) {
                    picked = INVALID_ENTITY;
                }
            }
            coordinator.apply(SelectEntityIntent{picked});
            if (picked != INVALID_ENTITY
                && gizmoAllowedForInstance(coordinator, active, picked)) {
                if (const auto geometry = resolveInstanceTransformGeometry(
                        coordinator.document(), frame, active, picked)) {
                    if (const SceneInstanceDef* inst =
                            coordinator.document().findInstanceInScene(active, picked)) {
                        // Body move when clicking the transformable bounds, or
                        // any pick on the entity (existing drag behaviour).
                        transform = beginTransformInteraction(
                            active, picked, TransformHandle::Body, inst->transform,
                            *geometry, world);
                    }
                }
            } else {
                cancelTransformInteraction(transform);
            }
        }
    }

    if (transform.active && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        updateTransformPreview(transform, coordinator, projection);
        const AuthoredTransformPatch patch = transformPatchForRelease(transform);
        if (patch.position || patch.rotationRadians || patch.scale) {
            coordinator.execute(SetEntityTransformCommand{
                transform.sceneId, transform.entityId, patch});
        }
        cancelTransformInteraction(transform);
    }
}

void routeViewportContextMenu(EditorCoordinator& coordinator, EditorUi& ui,
                              const SceneViewportProjection& projection, const RmlInputResult& rml,
                              ViewportContextClick& click,
                              std::optional<Vec2>& pendingSpawnPosition,
                              bool contextMenuHit) {
    const ViewportRect& rect = projection.visibleRect;
    if (coordinator.isPlaying()) {
        click = ViewportContextClick{};
        pendingSpawnPosition.reset();
        ui.hideContextMenus();
        return;
    }

    // Dismiss open menus on a real click that missed them. This must run for
    // every Edit-mode tool: Hierarchy/Assets row menus are side-panel UI and
    // stay valid while Brush/Eraser/etc. is active. Never force-close them
    // every frame just because a paint tool owns the viewport — that made the
    // Hierarchy "⌄" menu flash open then shut immediately after processFrame
    // deferred its show (menu opens at end of frame N, this path killed it at
    // the start of frame N+1).
    if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)
         || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
        && !contextMenuHit) {
        ui.hideContextMenus();
        // Inspector in-flow dropdowns only when the click is in the Scene View;
        // dismissing on every shell click would collapse Binding→Variable in the
        // same frame the Binding pick opens it.
        if (rect.contains(GetMouseX(), GetMouseY())) {
            ui.dismissInspectorTransientMenus();
        }
    }

    // A paint tool owns the viewport's right-click too (Eraser's right-click
    // shortcut in routeViewportTilemapPaint) - the entity-creation menu is a
    // Select-tool concept and must not fight it for the same gesture, mirroring
    // routeViewportPickDrag's own "a paint tool owns viewport clicks" guard.
    if (coordinator.state().activeTool != EditorTool::Select) {
        click = ViewportContextClick{};
        pendingSpawnPosition.reset();
        return;
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
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

    const SceneViewCamera& camera = projection.camera;
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
