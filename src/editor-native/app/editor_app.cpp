#include "editor-native/app/editor_app.h"

#include "editor-native/app/asset_import.h"
#include "editor-native/app/confirm_dialog.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/editor_input.h"
#include "editor-native/app/file_dialog.h"
#include "editor-native/app/hierarchy_actions.h"
#include "editor-native/app/input_routing.h"
#include "editor-native/app/new_project_transaction.h"
#include "editor-native/app/project_file.h"
#include "editor-native/app/rml_host.h"
#include "editor-native/app/sprite_animation_canvas_input.h"
#include "editor-native/app/sprite_animation_preview_renderer.h"
#include "editor-native/app/tile_palette_renderer.h"
#include "editor-native/app/tilemap_paint_input.h"
#include "editor-native/app/tileset_editor_canvas_input.h"
#include "editor-native/app/tileset_editor_renderer.h"
#include "editor-native/app/unsaved_guard.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/commands/tileset_commands.h"
#include "editor-native/commands/sprite_animation_commands.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_animation_slicing.h"
#include "editor-native/model/tilemap_stroke_preview.h"
#include "editor-native/model/tilemap_validation.h"
#include "editor-native/model/tileset_slicing.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/scene_view.h"
#include "editor-native/view/texture_cache.h"
#include "editor-native/view/tilemap_paint_overlay.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Box.h>

#include <raylib.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// raylib (5.0) has no public way to cancel a requested window close, so we reset
// GLFW's flag directly to keep the app open when the user picks Cancel in the
// unsaved-changes guard. NOTE: on Windows raylib's GetWindowHandle() returns the
// native HWND, not the GLFWwindow* this call needs — glfwGetCurrentContext()
// returns the GLFW window owning the main thread's GL context, which is
// raylib's one and only window.
extern "C" void  glfwSetWindowShouldClose(void* window, int value);
extern "C" void* glfwGetCurrentContext(void);

#if defined(_WIN32)
// DWM caption theming, declared by hand: including <windows.h> here clashes
// with raylib symbol names (CloseWindow, ShowCursor, ...). Cosmetic calls —
// on Windows builds that predate an attribute they fail and are ignored.
extern "C" __declspec(dllimport) long __stdcall DwmSetWindowAttribute(
    void* hwnd, unsigned long attribute, const void* value, unsigned long size);
#pragma comment(lib, "dwmapi")
#endif

namespace ArtCade::EditorNative {

namespace {

// HiDPI bridge: RmlUi runs in physical framebuffer pixels (GetRenderWidth),
// while raylib's drawing and mouse stay in logical pixels (GetScreenWidth) —
// raylib applies the DPI scale itself via screenScale / SetMouseScale. The
// factor is 1.0 on a 100% display, so this is a no-op there.
float uiPixelScaleX() {
    const int sw = GetScreenWidth();
    return sw > 0 ? static_cast<float>(GetRenderWidth()) / static_cast<float>(sw) : 1.f;
}
float uiPixelScaleY() {
    const int sh = GetScreenHeight();
    return sh > 0 ? static_cast<float>(GetRenderHeight()) / static_cast<float>(sh) : 1.f;
}

// The viewport element is laid out in RmlUi's physical-pixel space; the raylib
// scene renderer and pick/drag hit-testing both work in logical pixels, so the
// rect is converted physical -> logical here once at the boundary.
ViewportRect viewportRectFromDocument(Rml::ElementDocument* document) {
    ViewportRect rect;
    if (!document) return rect;
    const float sx = uiPixelScaleX();
    const float sy = uiPixelScaleY();
    if (Rml::Element* vp = document->GetElementById("viewport")) {
        const Rml::Vector2f off = vp->GetAbsoluteOffset();
        rect.x = static_cast<int>(off.x / sx);
        rect.y = static_cast<int>(off.y / sy);
        rect.width  = static_cast<int>(vp->GetClientWidth()  / sx);
        rect.height = static_cast<int>(vp->GetClientHeight() / sy);
    }
    return rect;
}

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

ViewportRect resolveSpriteAnimationCanvasContentRect(Rml::ElementDocument* document) {
    return elementContentRectFromDocument(document, "animation-sprite-canvas");
}

ViewportRect resolveSpriteAnimationPreviewContentRect(Rml::ElementDocument* document) {
    return elementContentRectFromDocument(document, "animation-preview-canvas");
}

ViewportRect resolveTilesetEditorCanvasContentRect(Rml::ElementDocument* document) {
    return elementContentRectFromDocument(document, "tileset-canvas");
}

void syncAnimationDocumentViewport(Rml::ElementDocument* document) {
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

// Transient viewport drag state — local presentation only (prompt §3: "valori
// temporanei durante un drag"). It never enters ProjectDocument; the single
// SetEntityPositionCommand is issued once, on release.
struct ViewportDrag {
    bool     active = false;
    EntityId entity = INVALID_ENTITY;
    Vec2     startMouseWorld{};
    Vec2     startEntityPos{};
};

struct ViewportContextClick {
    bool    tracking = false;
    Vector2 start{};
};

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
// Escape-key polling itself; (2) if nothing was pending and a tilemap tool is
// active, fall back to Select; (3) if the tool was already Select and an
// entity is selected, clear the selection (the scene's own Inspector, then
// visible again, is the existing fallback - no new code needed for that
// part). None of the three levels touches ProjectDocument/dirty/undo.
void routeGlobalEscape(EditorCoordinator& coordinator) {
    if (coordinator.cancelPendingTilemapGesture()) return;
    if (isTilemapTool(coordinator.state().activeTool)) {
        coordinator.apply(SetActiveToolIntent{EditorTool::Select});
        return;
    }
    if (coordinator.selection().hasEntity()) {
        coordinator.apply(SelectEntityIntent{INVALID_ENTITY});
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
            // A locked layer's instances, and any instance outside the active
            // layer, are not pickable by clicking the Scene View - both stay
            // selectable from the Hierarchy instead (which also switches the
            // active layer to match, see SelectEntityIntent), treated the same
            // as picking nothing so a click on one clears the selection rather
            // than starting a drag or silently switching the active layer out
            // from under the user.
            if (picked != INVALID_ENTITY) {
                const SceneInstanceDef* pickedInst =
                    coordinator.document().findInstanceInScene(active, picked);
                const bool onOtherLayer = pickedInst
                    && coordinator.document().effectiveLayerId(active, *pickedInst)
                           != coordinator.activeLayerId(active);
                if (!pickedInst
                    || coordinator.document().isInstanceLayerLocked(active, *pickedInst)
                    || onOtherLayer) {
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
                coordinator.execute(SetEntityPositionCommand{active, drag.entity, *preview});
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
            click = ViewportContextClick{true, GetMousePosition()};
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

std::filesystem::path editorResourceRoot() {
    return std::filesystem::path(GetApplicationDirectory()) / "resources";
}

void applyWindowIcon(const std::filesystem::path& resourceRoot) {
    const std::string iconPath = (resourceRoot / "app-icon.png").string();
    Image icon = LoadImage(iconPath.c_str());
    if (!icon.data) {
        TraceLog(LOG_WARNING, "[editor] failed to load window icon: %s", iconPath.c_str());
        return;
    }
    SetWindowIcon(icon);
    UnloadImage(icon);
}

std::filesystem::path resolveImageAssetPath(const std::filesystem::path& resourceRoot,
                                            const std::string& sourcePath) {
    if (sourcePath.empty()) return {};
    const PathConfinementResult resolved = resolvePathInsideRoot(
        resourceRoot, std::filesystem::u8path(sourcePath));
    return resolved.ok ? resolved.value : std::filesystem::path{};
}

std::unordered_map<AssetId, TextureRequest> textureRequestsFor(
    const ProjectDoc& doc, const std::filesystem::path& resourceRoot) {
    std::unordered_map<AssetId, TextureRequest> out;
    for (const ImageAssetDef& asset : doc.imageAssets) {
        out.emplace(asset.assetId, TextureRequest{
            asset.assetId,
            resolveImageAssetPath(resourceRoot, asset.sourcePath),
        });
    }
    return out;
}

std::unordered_map<AssetId, TextureRequest> textureRequestsFor(
    const PlayAssetCatalogSnapshot& catalog, const std::filesystem::path& resourceRoot) {
    std::unordered_map<AssetId, TextureRequest> out;
    for (const auto& [assetId, asset] : catalog.imageAssets) {
        out.emplace(assetId, TextureRequest{
            assetId,
            resolveImageAssetPath(resourceRoot, asset.sourcePath),
        });
    }
    return out;
}

std::filesystem::path normalizeProjectSavePath(std::filesystem::path destination) {
    if (destination.extension().empty()) {
        destination.replace_extension(".artcade-project");
    }
    const std::filesystem::path fileName = destination.filename();
    const std::string stem = destination.stem().string();
    if (stem.empty() || destination.parent_path().filename() == stem) {
        return destination;
    }
    return destination.parent_path() / stem / fileName;
}

std::filesystem::path suggestedProjectSavePath(const ProjectDoc& doc) {
    std::string stem = doc.projectName.empty() ? std::string("Untitled") : doc.projectName;
    for (char& c : stem) {
        switch (c) {
            case '<': case '>': case ':': case '"': case '/': case '\\':
            case '|': case '?': case '*':
                c = '_';
                break;
            default:
                break;
        }
    }
    if (stem.empty()) stem = "Untitled";
    return std::filesystem::path(stem + ".artcade-project");
}

bool copyAssetsForSaveAs(const std::filesystem::path& previousRoot,
                         const std::filesystem::path& nextRoot,
                         std::string& error) {
    if (previousRoot.empty() || previousRoot == nextRoot) return true;

    const PathConfinementResult sourceResult =
        resolvePathInsideRoot(previousRoot, "assets");
    if (!sourceResult.ok) {
        error = "Could not resolve existing assets folder: " + sourceResult.error;
        return false;
    }
    const std::filesystem::path& source = sourceResult.value;
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) return true;
    if (ec) {
        error = "Could not inspect existing assets folder: " + ec.message();
        return false;
    }

    // Validate every source entry before recursive copy. The iterator does not
    // opt into following directory symlinks; resolving each lexical relative
    // path additionally rejects Windows junctions/reparse points that lead out
    // of previousRoot before std::filesystem::copy can traverse them.
    std::filesystem::recursive_directory_iterator it{
        source, std::filesystem::directory_options::none, ec};
    const std::filesystem::recursive_directory_iterator end;
    if (ec) {
        error = "Could not inspect existing assets tree: " + ec.message();
        return false;
    }
    for (; it != end;) {
        const std::filesystem::path relative = it->path().lexically_relative(previousRoot);
        const PathConfinementResult entry = resolvePathInsideRoot(previousRoot, relative);
        if (!entry.ok) {
            error = "Assets tree contains an unsafe path: " + entry.error;
            return false;
        }
        it.increment(ec);
        if (ec) {
            error = "Could not inspect existing assets tree: " + ec.message();
            return false;
        }
    }

    std::filesystem::create_directories(nextRoot, ec);
    if (ec) {
        error = "Could not create project folder: " + ec.message();
        return false;
    }

    const PathConfinementResult destinationResult =
        resolvePathInsideRoot(nextRoot, "assets");
    if (!destinationResult.ok) {
        error = "Could not resolve destination assets folder: " + destinationResult.error;
        return false;
    }
    const std::filesystem::path& destination = destinationResult.value;
    std::filesystem::copy(source, destination,
                          std::filesystem::copy_options::recursive
                        | std::filesystem::copy_options::skip_symlinks
                        | std::filesystem::copy_options::overwrite_existing,
                          ec);
    if (ec) {
        error = "Could not copy assets folder: " + ec.message();
        return false;
    }
    return true;
}

} // namespace

int EditorApp::run(int argc, char** argv) {
    // Optional one-shot screenshot mode: "--shot <path>" renders a few frames,
    // captures the framebuffer and exits. Used to verify the shell renders.
    // "--shot-project <file.artcade>" opens a project first, and "--shot-anim"
    // additionally opens the Sprite Animation Editor on its first animation
    // asset, so the overlay can be smoke-tested without interaction.
    std::string shotPath;
    std::string shotProject;
    bool shotAnimation = false;
    bool shotTileset = false;   // open the Tileset Editor (creating from the first image if none)
    int shotSliceColumns = 0;   // > 0: slice the open clip into N frames for the shot
    bool shotSliceAll = false;  // slice every animation asset in turn (overwrite repro)
    std::string shotSavePath;   // non-empty: save the project here via the real path
    int shotEntityIndex = -1;   // >= 0: select the Nth instance of the active scene
    std::string shotDropdown;   // non-empty: open this Inspector value dropdown
    std::string shotAssetMenu;  // "kind|id": open the Assets row menu on that asset
    bool lifecycleSmoke = false; // hidden, self-checking bind/detach/shutdown run
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shotPath = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-project") == 0 && i + 1 < argc)
            shotProject = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-anim") == 0) shotAnimation = true;
        else if (std::strcmp(argv[i], "--shot-tileset") == 0) shotTileset = true;
        else if (std::strcmp(argv[i], "--shot-slice") == 0 && i + 1 < argc)
            shotSliceColumns = std::atoi(argv[i + 1]);
        else if (std::strcmp(argv[i], "--shot-slice-all") == 0) shotSliceAll = true;
        else if (std::strcmp(argv[i], "--shot-save") == 0 && i + 1 < argc)
            shotSavePath = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-entity") == 0 && i + 1 < argc)
            shotEntityIndex = std::atoi(argv[i + 1]);
        else if (std::strcmp(argv[i], "--shot-dropdown") == 0 && i + 1 < argc)
            shotDropdown = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-asset-menu") == 0 && i + 1 < argc)
            shotAssetMenu = argv[i + 1];
        else if (std::strcmp(argv[i], "--lifecycle-smoke") == 0)
            lifecycleSmoke = true;
    }

    // Start empty: the editor opens a real project (File > Open) or builds one
    // from scratch (add scene/entity, import assets, Save As). No bundled demo.
    EditorCoordinator coordinator{ProjectDoc{}};

    // FLAG_WINDOW_HIGHDPI: create a framebuffer at the monitor's physical
    // resolution so RmlUi rasterises and renders at real pixels (crisp text on
    // scaled displays) instead of being upscaled from a logical-size buffer.
    unsigned int windowFlags = FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT
                             | FLAG_VSYNC_HINT | FLAG_WINDOW_HIGHDPI;
    if (lifecycleSmoke) windowFlags |= FLAG_WINDOW_HIDDEN;
    SetConfigFlags(windowFlags);
    InitWindow(1340, 840, "ArtCade Studio");
#if defined(_WIN32)
    // Dark caption matching the editor chrome: the default light title bar
    // clashed with the dark UI — even the 1px caption sliver DWM leaves under
    // the close button's hover highlight read as a stray white gap against it.
    // 20 = DWMWA_USE_IMMERSIVE_DARK_MODE (themes the caption buttons),
    // 35/36 = DWMWA_CAPTION_COLOR / DWMWA_TEXT_COLOR (Windows 11) pin the
    // exact menubar colors; COLORREF is 0x00BBGGRR.
    if (void* hwnd = GetWindowHandle()) {
        const int darkMode = 1;
        const unsigned long captionColor = 0x001B1818;   // #18181b
        const unsigned long textColor    = 0x00D8D4D4;   // #d4d4d8
        DwmSetWindowAttribute(hwnd, 20, &darkMode, sizeof(darkMode));
        DwmSetWindowAttribute(hwnd, 35, &captionColor, sizeof(captionColor));
        DwmSetWindowAttribute(hwnd, 36, &textColor, sizeof(textColor));
    }
#endif
    const std::filesystem::path resourceRoot = editorResourceRoot();
    applyWindowIcon(resourceRoot);
    if (!lifecycleSmoke) MaximizeWindow();
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    // Canvas text font (entity labels, scene chip, canvas messages): the same
    // Inter face the RmlUi chrome uses. Owned here; unloaded before CloseWindow.
    CanvasFont canvasFont = loadCanvasFont(resourceRoot);
    if (!canvasFont.loaded) {
        TraceLog(LOG_WARNING,
                 "[editor] Inter-Medium.ttf not loaded - canvas text falls back to "
                 "raylib's built-in font");
    }

    // RmlUi context + viewport are sized in physical framebuffer pixels; the dp
    // ratio scales `dp` lengths in the RCSS so the UI keeps its intended size.
    float dpi = GetWindowScaleDPI().x;
    RmlHost host;
    if (!host.initialize(GetRenderWidth(), GetRenderHeight(), dpi > 0.f ? dpi : 1.f,
                         resourceRoot, "ui/editor_shell.rml")) {
        TraceLog(LOG_ERROR, "[editor] failed to load native editor resources from %s",
                 resourceRoot.string().c_str());
        host.shutdown();
        CloseWindow();
        return 1;
    }
    Rml::ElementDocument* animationDocument =
        host.loadDocument("ui/sprite-animation/sprite_animation_editor.rml");
    if (!animationDocument) {
        TraceLog(LOG_ERROR, "[editor] failed to load sprite animation editor resources");
        host.shutdown();
        CloseWindow();
        return 1;
    }
    syncAnimationDocumentViewport(animationDocument);
    animationDocument->Hide();

    Rml::ElementDocument* tilesetDocument =
        host.loadDocument("ui/tileset/tileset_editor.rml");
    if (!tilesetDocument) {
        TraceLog(LOG_ERROR, "[editor] failed to load tileset editor resources");
        host.shutdown();
        CloseWindow();
        return 1;
    }
    syncAnimationDocumentViewport(tilesetDocument);   // same absolute full-window sync, generic
    tilesetDocument->Hide();

    int exitCode = 0;
    std::optional<EditorUi> uiOwner{std::in_place, coordinator, host.document(),
                                    animationDocument, tilesetDocument};
    EditorUi& ui = *uiOwner;
    ui.bind();
    coordinator.logInfo("ArtCade Studio ready.");
    SceneView sceneView;
    TextureCache textureCache;
    ViewportDrag drag;
    ViewportContextClick contextClick;
    std::optional<Vec2> pendingContextSpawn;

    // Project I/O is owned by the application: it holds the texture cache it must
    // clear when the document is replaced, and the platform file pickers. The UI
    // only requests these operations; it never touches files or the renderer.
    std::filesystem::path currentProjectPath;
    // The window title reflects the current project: its file name, or "Untitled"
    // before the first Save As, plus a bullet while there are unsaved changes -
    // the only always-visible dirty cue (the unsaved guard otherwise fires only
    // on close). Dirty derives from the document's revision baseline; the title
    // re-renders on path changes (New, Open, Save) and, change-guarded, from the
    // frame loop when the dirty flag flips (an O(1) revision compare, same
    // pattern as the pointer readout - not document polling).
    bool titleShowsDirty = false;
    const auto refreshWindowTitle = [&]() {
        const std::string name = currentProjectPath.empty()
            ? std::string("Untitled")
            : currentProjectPath.stem().string();
        titleShowsDirty = coordinator.document().isDirty();
        SetWindowTitle(("ArtCade Studio - " + name
                        + (titleShowsDirty ? " \xe2\x80\xa2" : "")).c_str());
    };
    const auto saveTo = [&](const std::filesystem::path& path) -> bool {
        const std::filesystem::path destination = normalizeProjectSavePath(path);
        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(), ec);
        if (ec) {
            coordinator.logError("Save failed: could not create project folder: "
                                 + ec.message());
            return false;
        }

        const std::filesystem::path previousRoot =
            currentProjectPath.empty() ? std::filesystem::path{} : currentProjectPath.parent_path();
        std::string copyError;
        if (!copyAssetsForSaveAs(previousRoot, destination.parent_path(), copyError)) {
            coordinator.logError("Save failed: " + copyError);
            return false;
        }

        const ProjectSaveResult result = saveProjectToFile(coordinator, destination);
        if (!result.ok) {
            coordinator.logError("Save failed: " + result.error.message);
            return false;
        }
        currentProjectPath = destination;
        refreshWindowTitle();
        coordinator.logInfo("Saved " + destination.string());
        return true;
    };
    // Save to the current path, or prompt for one. False when cancelled or failed.
    const auto saveCurrent = [&]() -> bool {
        if (currentProjectPath.empty()) {
            const auto picked = saveProjectFileDialog(
                suggestedProjectSavePath(coordinator.document().data()));
            return picked ? saveTo(*picked) : false;
        }
        return saveTo(currentProjectPath);
    };
    // Unsaved-changes guard for destructive actions. Returns true to proceed.
    const auto guardPasses = [&]() -> bool {
        // A focused field is authoritative even before its normal blur. Resolve
        // it first so the dirty decision can never observe stale document state.
        if (!ui.resolvePendingEdits().resolved()) return false;
        if (!coordinator.document().isDirty()) return true;
        const UnsavedChoice choice = confirmUnsavedChanges();
        const bool saveOk = (choice == UnsavedChoice::Save) ? saveCurrent() : false;
        return resolveUnsavedGuard(true, choice, saveOk) == GuardOutcome::Proceed;
    };
    ui.setProjectFileHandlers(
        [&]() {  // New: a project is born saved - pick its destination up front,
                 // so assets always have a folder to live in (no Untitled limbo).
            if (coordinator.isPlaying()) {
                coordinator.logWarning("Stop Play before creating a new project");
                return;  // no hidden auto-stop
            }
            if (!guardPasses()) return;     // dirty + Cancel / failed Save: abort
            const auto picked = saveProjectFileDialog(
                suggestedProjectSavePath(coordinator.document().data()));
            const std::optional<std::filesystem::path> destination = picked
                ? std::optional<std::filesystem::path>{normalizeProjectSavePath(*picked)}
                : std::nullopt;
            ProjectDoc fresh;
            if (destination) fresh.projectName = destination->stem().string();
            const NewProjectResult created = createNewProjectTransaction(
                coordinator, ProjectDocument{std::move(fresh)}, destination);
            if (created.cancelled) return;   // current project/path/cache stay exact
            if (!created.ok) {
                coordinator.logError("New project failed: " + created.error.message);
                return;
            }
            textureCache.clear(); // only after committed ProjectReplaced
            currentProjectPath = created.destination;
            refreshWindowTitle();
            coordinator.logInfo("New project");
        },
        [&]() {  // Open
            if (coordinator.isPlaying()) {
                coordinator.logWarning("Stop Play before opening another project");
                return;
            }
            if (!guardPasses()) return;     // dirty + Cancel / failed Save: abort
            const std::optional<std::filesystem::path> picked = openProjectFileDialog();
            if (!picked) return;  // cancelled
            const ProjectLoadResult result = loadProjectFromFile(coordinator, *picked);
            if (!result.ok) {
                coordinator.logError("Open failed: " + result.error.message);
                return;
            }
            textureCache.clear();  // explicit app path consuming ProjectReplaced
            currentProjectPath = *picked;
            refreshWindowTitle();
            coordinator.logInfo("Opened " + picked->filename().string());
        },
        [&]() {  // Save (Save As when no current path)
            saveCurrent();
        },
        [&]() {  // Save As
            const std::filesystem::path suggested = currentProjectPath.empty()
                ? suggestedProjectSavePath(coordinator.document().data())
                : currentProjectPath;
            if (const auto picked = saveProjectFileDialog(suggested))
                saveTo(*picked);
        });

    // Import is one canonical pipeline (asset_import): pick the file for the kind,
    // save the project if needed, then converge on importAsset. Returns the new
    // asset id so callers can chain (e.g. create an animation from the image).
    // Every import UI source flows through this one lambda - no duplicate path.
    const auto importAssetOfKind = [&](AssetKind kind) -> std::optional<AssetId> {
        std::optional<std::filesystem::path> picked;
        switch (kind) {
            case AssetKind::Image: picked = openImageFileDialog(); break;
            case AssetKind::Audio: picked = openAudioFileDialog(); break;
            case AssetKind::Font:  picked = openFontFileDialog();  break;
        }
        if (!picked) return std::nullopt;  // cancelled
        bool savedForImport = false;
        // Projects are born saved (New picks a destination up front), so this
        // only triggers for the startup Untitled document: assets are copied
        // next to the .artcade, so ask for the project folder and continue.
        if (currentProjectPath.empty()) {
            coordinator.logInfo(
                "Assets live inside the project folder - choose where to save the project");
            if (!saveCurrent()) {
                coordinator.logWarning("Import cancelled: the project was not saved");
                return std::nullopt;
            }
            savedForImport = true;
        }
        ImportAssetRequest request;
        request.kind = kind;
        request.sourcePath = *picked;
        const ImportAssetResult result =
            importAsset(coordinator, currentProjectPath.parent_path(), request);
        if (!result.ok) {
            coordinator.logError(result.error);
            return std::nullopt;
        }
        coordinator.logInfo("Imported " + result.assetId);
        if (savedForImport && !saveCurrent()) {
            coordinator.logWarning("Imported asset is not saved in the project file yet");
        }
        return result.assetId;
    };
    ui.setImportHandler([&](AssetKind kind) { importAssetOfKind(kind); });
    // Import an image for the Sprite Animation Editor: reuses the exact same
    // pipeline above, then hands the image id back so the editor can start a new
    // animation on it without the user leaving for the Assets panel.
    ui.setImportImageForAnimationHandler(
        [&]() { return importAssetOfKind(AssetKind::Image); });

    // Fit View / auto-fit: frame the active scene, centred, with a small padding.
    // Workspace-only (recenter pan to 0 + zoom-to-fit via intents); the viewport
    // rect is known only here. Shared by the Scene Inspector button and the
    // first-open auto-fit below.
    const auto fitActiveScene = [&]() -> bool {
        const SceneId active = coordinator.state().activeSceneId;
        const SceneDef* scene = coordinator.document().findScene(active);
        if (!scene || scene->worldSize.x <= 0.f || scene->worldSize.y <= 0.f) return false;
        const ViewportRect rect = viewportRectFromDocument(host.document());
        if (rect.width <= 0 || rect.height <= 0) return false;
        constexpr float kPad = 28.f;   // keep the scene off the panel edges
        const float availW = std::max(1.f, static_cast<float>(rect.width) - kPad * 2.f);
        const float availH = std::max(1.f, static_cast<float>(rect.height) - kPad * 2.f);
        const float fit = std::min(availW / scene->worldSize.x, availH / scene->worldSize.y);
        const EditorSceneViewState view = coordinator.sceneView(active);
        coordinator.apply(PanViewportIntent{active, {-view.pan.x, -view.pan.y}});  // centre (pan 0)
        coordinator.apply(SetViewportZoomIntent{active, fit});                     // intent clamps
        return true;
    };
    ui.setFitViewHandler([&]() { fitActiveScene(); });
    const std::function<void()> sliceAnimationHandler = [&]() {
        const SpriteAnimationEditorState& state = coordinator.state().spriteAnimationEditor;
        if (!state.openAssetId || !state.selectedClipId) {
            coordinator.logWarning("Select an animation clip before slicing");
            return;
        }

        const SpriteAnimationAssetDef* asset =
            coordinator.document().findSpriteAnimationAsset(*state.openAssetId);
        if (!asset) {
            coordinator.logError("Cannot slice: animation asset is missing");
            return;
        }
        const SpriteAnimationClipDef* clip = nullptr;
        for (const SpriteAnimationClipDef& candidate : asset->clips) {
            if (candidate.id == *state.selectedClipId) {
                clip = &candidate;
                break;
            }
        }
        if (!clip) {
            coordinator.logWarning("Select an animation clip before slicing");
            return;
        }

        const std::filesystem::path assetRoot =
            currentProjectPath.empty() ? resourceRoot : currentProjectPath.parent_path();
        SceneFrameSprite requestSprite;
        requestSprite.assetId = clip->imageId;
        requestSprite.visible = true;
        const auto requests = textureRequestsFor(coordinator.document().data(), assetRoot);
        textureCache.prepare({requestSprite}, requests);
        const TextureResource* resource = textureCache.find(clip->imageId);
        if (!resource || !resource->loaded) {
            coordinator.logError("Cannot slice: sprite sheet is not loaded");
            return;
        }

        // Frame-count driven: divide the sheet into columns x rows, deriving the
        // cell size from the sheet dimensions - the same grid the overlay draws.
        const std::optional<SpriteAnimationSliceGrid> grid =
            spriteAnimationGridFromCellCounts(
                resource->texture.width, resource->texture.height,
                state.sliceColumns, state.sliceRows,
                state.sliceMargin, state.sliceSpacing);
        if (!grid.has_value()) {
            coordinator.logError("Cannot slice: "
                                 + std::to_string(state.sliceColumns) + " x "
                                 + std::to_string(state.sliceRows)
                                 + " frames do not fit the sheet");
            return;
        }
        std::vector<SpriteAnimationFrameDef> frames = spriteAnimationFramesForGrid(
            resource->texture.width, resource->texture.height, *grid);
        if (frames.empty()) {
            coordinator.logError("Cannot slice: no frames produced");
            return;
        }

        const std::size_t frameCount = frames.size();
        const EditorOperationResult result =
            coordinator.execute(SetAnimationClipFramesCommand{
                asset->id, clip->id, std::move(frames)});
        if (result.ok) {
            coordinator.logInfo("Sliced " + clip->name + " into "
                                + std::to_string(frameCount) + " frames");
        }
    };
    ui.setAnimationSliceHandler(sliceAnimationHandler);
    // Looks up the loaded source texture for a tileset's image (preparing it
    // through the cache if needed); shared by apply and create-from-image.
    const auto loadedTilesetSource = [&](const AssetId& imageAssetId) -> const TextureResource* {
        const std::filesystem::path assetRoot =
            currentProjectPath.empty() ? resourceRoot : currentProjectPath.parent_path();
        SceneFrameSprite requestSprite;
        requestSprite.assetId = imageAssetId;
        requestSprite.visible = true;
        const auto requests = textureRequestsFor(coordinator.document().data(), assetRoot);
        textureCache.prepare({requestSprite}, requests);
        const TextureResource* resource = textureCache.find(imageAssetId);
        return (resource && resource->loaded) ? resource : nullptr;
    };
    // The one apply flow for the pending slicing, shared by the Apply button
    // and the close guard's "Apply" answer. Returns whether the pending
    // config is now committed (true also when there was nothing to change);
    // false covers every abort: no editor, missing image, unfittable tile
    // size, impact dialog cancelled, command failure.
    const std::function<bool()> tryApplyPendingTilesetSlicing = [&]() -> bool {
        const TilesetEditorState& state = coordinator.state().tilesetEditor;
        if (!state.openAssetId) {
            coordinator.logWarning("Open a tileset before applying slicing");
            return false;
        }
        const TilesetAsset* asset = coordinator.document().findTilesetAsset(*state.openAssetId);
        if (!asset) {
            coordinator.logError("Cannot slice: tileset asset is missing");
            return false;
        }
        const TextureResource* resource = loadedTilesetSource(asset->imageAssetId);
        if (!resource) {
            coordinator.logError("Cannot slice: source image is not loaded");
            return false;
        }

        // Pixel-size-first: the tiles come straight from the pending config and
        // the sheet's real dimensions - no frame-count derivation needed.
        const std::vector<TileDefinition> freshTiles = tilesForSlicing(
            resource->texture.width, resource->texture.height, state.pendingSlicing);
        if (freshTiles.empty()) {
            coordinator.logError("Cannot slice: tile size does not fit the sheet");
            return false;
        }
        // Reconcile against the asset's current tiles so a tile whose rect didn't
        // move keeps its stable id (metadata a later slice attaches survives).
        const std::vector<TileDefinition> reconciled = reconcileTiles(asset->tiles, freshTiles);
        if (sameTilesetSlicing(asset->slicing, state.pendingSlicing)
            && sameTileDefinitions(asset->tiles, reconciled)) {
            return true;   // already committed - nothing to apply
        }

        // Painted cells whose tile id disappears are cleared by the command's
        // cascade; that is destructive enough to warrant an explicit decision
        // first. The command re-derives the orphans itself - these counts only
        // feed the dialog and the log line.
        const TilesetResliceImpact impact = computeTilesetResliceImpact(
            coordinator.document(), *state.openAssetId, reconciled);
        if (impact.orphanedCells > 0
            && !confirmTilesetResliceImpact(impact.removedReferencedTiles,
                                            impact.orphanedCells, impact.affectedTilemaps)) {
            coordinator.logInfo("Slicing cancelled: " + asset->name + " keeps its "
                                + std::to_string(impact.orphanedCells) + " painted cell(s)");
            return false;
        }

        std::unordered_set<std::string> oldIds;
        for (const TileDefinition& tile : asset->tiles) oldIds.insert(tile.id);
        int kept = 0;
        for (const TileDefinition& tile : reconciled) {
            if (oldIds.count(tile.id) != 0) ++kept;
        }
        const int added   = static_cast<int>(reconciled.size()) - kept;
        const int removed = static_cast<int>(oldIds.size()) - kept;

        const EditorOperationResult result = coordinator.execute(
            ChangeTilesetSlicingCommand{*state.openAssetId, state.pendingSlicing, reconciled});
        if (!result.ok) return false;
        std::string summary = "Sliced " + asset->name + " into "
            + std::to_string(reconciled.size()) + " tiles (" + std::to_string(kept)
            + " kept, " + std::to_string(added) + " new, " + std::to_string(removed)
            + " removed";
        if (impact.orphanedCells > 0) {
            summary += "; cleared " + std::to_string(impact.orphanedCells)
                + " painted cell(s) in " + std::to_string(impact.affectedTilemaps)
                + " tilemap(s)";
        }
        coordinator.logInfo(summary + ")");
        return true;
    };
    ui.setTilesetApplySlicingHandler([&]() { tryApplyPendingTilesetSlicing(); });
    // Close guard: an unapplied pending slicing is a pending edit - it must
    // be resolved (Apply / Discard / Cancel), never dropped silently. Same
    // pure decision as the project-level unsaved guard. Named so the Close
    // button and the Esc key run the identical flow.
    const std::function<void()> closeTilesetEditorGuarded = [&]() {
        const TilesetEditorState& state = coordinator.state().tilesetEditor;
        if (!state.openAssetId) return;
        const TilesetAsset* asset = coordinator.document().findTilesetAsset(*state.openAssetId);
        const bool dirty = asset && !sameTilesetSlicing(asset->slicing, state.pendingSlicing);
        const UnsavedChoice choice =
            dirty ? confirmTilesetUnappliedChanges() : UnsavedChoice::Discard;
        const bool applied =
            (dirty && choice == UnsavedChoice::Save) ? tryApplyPendingTilesetSlicing() : false;
        if (resolveUnsavedGuard(dirty, choice, applied) == GuardOutcome::Proceed) {
            coordinator.apply(CloseTilesetEditorIntent{});
        }
    };
    ui.setTilesetCloseHandler(closeTilesetEditorGuarded);
    // Arrow-key selection on the pending grid: same positional tile ids the
    // canvas click produces; a missing/stale selection starts at tile one.
    const auto moveTilesetSelection = [&](int dx, int dy) {
        const TilesetEditorState& state = coordinator.state().tilesetEditor;
        if (!state.openAssetId) return;
        const TilesetAsset* asset = coordinator.document().findTilesetAsset(*state.openAssetId);
        if (!asset) return;
        const TextureResource* resource = loadedTilesetSource(asset->imageAssetId);
        if (!resource) return;
        const TilesetSliceResult grid = computeTilesetSlicing(
            resource->texture.width, resource->texture.height, state.pendingSlicing);
        if (grid.tileCount <= 0) return;
        const std::vector<TileDefinition> tiles = tilesForSlicing(
            resource->texture.width, resource->texture.height, state.pendingSlicing);
        int current = -1;
        if (state.selectedTileId) {
            for (std::size_t i = 0; i < tiles.size(); ++i) {
                if (tiles[i].id == *state.selectedTileId) {
                    current = static_cast<int>(i);
                    break;
                }
            }
        }
        if (current < 0) {
            coordinator.apply(SelectTilesetTileIntent{tiles.front().id});
            return;
        }
        if (const std::optional<int> next =
                adjacentTileIndex(grid.columns, grid.rows, current, dx, dy)) {
            coordinator.apply(
                SelectTilesetTileIntent{tiles[static_cast<std::size_t>(*next)].id});
        }
    };
    // Pixel-size projection for the editor's inline slicing feedback; read-only,
    // the cache stays the only owner of the loaded texture.
    ui.setTilesetImageSizeProvider(
        [&](const AssetId& imageAssetId) -> std::optional<std::pair<int, int>> {
            const TextureResource* resource = loadedTilesetSource(imageAssetId);
            if (!resource) return std::nullopt;
            return std::make_pair(resource->texture.width, resource->texture.height);
        });
    // Create-from-image slices the default grid immediately (one atomic
    // command, one undo step) so a fresh tileset is usable without a first
    // Apply; an unloadable or too-small image still creates the asset, with
    // the editor open on its explicit empty state.
    ui.setCreateTilesetFromImageHandler([&](const AssetId& imageAssetId) {
        if (coordinator.isPlaying() || !coordinator.document().hasImageAsset(imageAssetId)) {
            return;
        }
        const std::string id = uniqueTilesetAssetId(coordinator.document(), imageAssetId);
        const TilesetSlicing defaultSlicing;   // 32x32, no margin/spacing
        std::vector<TileDefinition> tiles;
        if (const TextureResource* resource = loadedTilesetSource(imageAssetId)) {
            tiles = tilesForSlicing(
                resource->texture.width, resource->texture.height, defaultSlicing);
        }
        if (!coordinator.execute(
                AddTilesetAssetCommand{id, id, imageAssetId, defaultSlicing, tiles}).ok) {
            return;
        }
        coordinator.apply(OpenTilesetEditorIntent{id});
        if (tiles.empty()) {
            coordinator.logWarning("Created " + id
                + " without tiles: source image is not loaded or smaller than one 32x32 tile"
                " - adjust the slicing and Apply");
        } else {
            coordinator.logInfo("Created " + id + " with "
                + std::to_string(tiles.size()) + " tiles (32x32 default)");
        }
    });
    const auto viewportDefaultSpawn = [&]() -> std::optional<Vec2> {
        const SceneId& active = coordinator.state().activeSceneId;
        const SceneDef* scene = coordinator.document().findScene(active);
        if (!scene) return std::nullopt;
        const ViewportRect rect = viewportRectFromDocument(host.document());
        const Vec2 candidate = rect.valid()
            ? defaultSpawnPosition(rect, coordinator.sceneView(active), scene->worldSize)
            : normalizeSpawnPosition(
                  Vec2{scene->worldSize.x * 0.5f, scene->worldSize.y * 0.5f},
                  scene->worldSize);
        // Default placement only: repeated toolbar spawns cascade instead of
        // stacking on the view centre. Explicit placements (context-menu
        // "here") keep the exact point the user chose.
        return unoccupiedSpawnPosition(*scene, candidate, scene->worldSize);
    };

    ui.setEntityPlacementHandlers(
        [&]() {
            if (const auto pos = viewportDefaultSpawn()) addEntityAt(coordinator, *pos);
            else addEntity(coordinator);
        },
        [&]() {
            if (const auto pos = viewportDefaultSpawn())
                addInstanceOfSelectedTypeAt(coordinator, *pos);
            else
                addInstanceOfSelectedType(coordinator);
        },
        [&]() {
            if (pendingContextSpawn) {
                addEntityAt(coordinator, *pendingContextSpawn);
                pendingContextSpawn.reset();
            }
        },
        [&]() {
            if (pendingContextSpawn) {
                addInstanceOfSelectedTypeAt(coordinator, *pendingContextSpawn);
                pendingContextSpawn.reset();
            }
        });

    refreshWindowTitle();   // empty start project -> "Untitled"

    // Screenshot-mode project bootstrap: same canonical load path as File >
    // Open, minus the dialogs (there is no interaction to guard against).
    if (!shotPath.empty() && !shotProject.empty()) {
        const std::filesystem::path projectPath{shotProject};
        const ProjectLoadResult result = loadProjectFromFile(coordinator, projectPath);
        if (!result.ok) {
            coordinator.logError("Open failed: " + result.error.message);
        } else {
            textureCache.clear();
            currentProjectPath = projectPath;
            refreshWindowTitle();
            if (shotAnimation) {
                const auto& animations = coordinator.document().data().spriteAnimationAssets;
                if (!animations.empty()) {
                    coordinator.apply(OpenSpriteAnimationEditorIntent{animations.front().id});
                    // Exercise the slice flow headlessly through the exact button
                    // path (auto-creates a clip when the asset has none, then fills).
                    if (shotSliceColumns > 0 && !shotSliceAll) {
                        coordinator.apply(SetAnimationSliceGridIntent{shotSliceColumns, 1, 0, 0});
                        ui.handleAction("slice-animation-grid", "", "");
                    } else if (shotSliceColumns > 0 && shotSliceAll) {
                        // Reproduce the user's sequence: open + slice each animation
                        // in turn, via the real button path (open switches assets).
                        std::vector<AssetId> ids;
                        for (const auto& a : animations) ids.push_back(a.id);
                        for (const AssetId& aid : ids) {
                            coordinator.apply(OpenSpriteAnimationEditorIntent{aid});
                            coordinator.apply(SetAnimationSliceGridIntent{shotSliceColumns, 1, 0, 0});
                            ui.handleAction("slice-animation-grid", "", "");
                        }
                    }
                }
            }
            // Tileset Editor smoke: open the first tileset, or create one from
            // the first image through the real context-menu action (which
            // auto-slices and opens the editor itself), then select a tile so
            // the Selected Tile panel renders too.
            if (shotTileset) {
                const auto& tilesets = coordinator.document().data().tilesets;
                if (!tilesets.empty()) {
                    coordinator.apply(OpenTilesetEditorIntent{tilesets.front().assetId});
                } else if (!coordinator.document().data().imageAssets.empty()) {
                    ui.handleAction("create-tileset-from-image",
                                    coordinator.document().data().imageAssets.front().assetId,
                                    "");
                }
                if (const auto& openTs = coordinator.state().tilesetEditor.openAssetId) {
                    const TilesetAsset* ts = coordinator.document().findTilesetAsset(*openTs);
                    if (ts && !ts->tiles.empty()) {
                        const std::size_t pick = ts->tiles.size() > 1 ? 1 : 0;
                        coordinator.apply(SelectTilesetTileIntent{ts->tiles[pick].id});
                    }
                }
            }
            // Inspector smoke test: select the Nth instance of the active
            // scene (--shot-entity N), optionally with one of its value
            // dropdowns open (--shot-dropdown layer|sprite-source|tilemap-
            // tileset) — same Intent/action path a real click takes.
            if (shotEntityIndex >= 0) {
                const SceneDef* scene =
                    coordinator.document().findScene(coordinator.state().activeSceneId);
                if (scene && shotEntityIndex < static_cast<int>(scene->instances.size())) {
                    coordinator.apply(SelectEntityIntent{
                        scene->instances[static_cast<std::size_t>(shotEntityIndex)].id});
                    if (!shotDropdown.empty()) {
                        // Flush the selection repaint first, as a real frame
                        // would: the Inspector resets its transient dropdown
                        // state on a selection change, so toggling in the same
                        // breath would be undone by that reset.
                        ui.processFrame();
                        ui.handleAction("toggle-inspector-dropdown", shotDropdown, "");
                    }
                }
            }
            // Assets row-menu smoke test: request the menu exactly as the "⌄"
            // click would (same deferred-show path); it appears on the loop's
            // first processFrame, well before the frame-12 capture.
            if (!shotAssetMenu.empty()) {
                const std::size_t sep = shotAssetMenu.find('|');
                std::optional<AssetMenuKind> kind;
                if (sep != std::string::npos)
                    kind = parseAssetMenuKind(shotAssetMenu.substr(0, sep));
                if (kind) {
                    ui.requestAssetContextMenu(*kind, shotAssetMenu.substr(sep + 1),
                                               320, 700);
                }
            }
            // Exercise the real save path (asset copy + atomic write) so the
            // written .artcade can be inspected for the animation persistence.
            if (!shotSavePath.empty()) {
                if (saveTo(std::filesystem::path{shotSavePath}))
                    coordinator.logInfo("shot-save wrote " + shotSavePath);
                else
                    coordinator.logError("shot-save failed");
            }
        }
    }

    int   frame       = 0;
    int   lastRenderW = GetRenderWidth();
    int   lastRenderH = GetRenderHeight();
    float lastDpi     = dpi > 0.f ? dpi : 1.f;
    int   sizeStableFrames = 0;
    bool  prevTextFocus = false;   // last frame's RmlUi text focus (see Tileset keys)
    // Tile Palette Picker-sync: the tile last scrolled into view, so a
    // repeated selection (or none) does not re-trigger ScrollIntoView.
    std::optional<TileId> lastScrolledPaletteTile;
    while (true) {
        // Exit guard: a requested close (window X) is held until the unsaved
        // guard passes. On Cancel we clear GLFW's close flag and keep running.
        // Screenshot mode skips the guard (no window interaction).
        if (WindowShouldClose()) {
            if (shotPath.empty() && !guardPasses()) {
                glfwSetWindowShouldClose(glfwGetCurrentContext(), 0);
            } else {
                break;
            }
        }

        // Re-sync RmlUi on a resize *or* a DPI change (e.g. the window dragged
        // onto a monitor with different scaling): both alter the physical
        // framebuffer size and/or the dp ratio, and must stay in lockstep.
        const int   renderW = GetRenderWidth();
        const int   renderH = GetRenderHeight();
        const float curDpi  = GetWindowScaleDPI().x > 0.f ? GetWindowScaleDPI().x : 1.f;
        if (renderW != lastRenderW || renderH != lastRenderH || curDpi != lastDpi) {
            host.resize(renderW, renderH, curDpi);
            syncAnimationDocumentViewport(animationDocument);
            syncAnimationDocumentViewport(tilesetDocument);   // same full-window overlay
            lastRenderW = renderW;
            lastRenderH = renderH;
            lastDpi     = curDpi;
            sizeStableFrames = 0;   // shot mode waits out maximize/DPI resizes
        } else {
            ++sizeStableFrames;
        }
        if (IsKeyPressed(KEY_F8)) host.toggleDebugger();

        const RmlInputResult rml = pumpRmlInput(host.context());

        // Undo/redo keyboard shortcuts share the single coordinator entry points
        // with the toolbar buttons; suppressed while a text field has focus, and
        // guarded against Play by the coordinator. Ctrl+Z = undo; Ctrl+Y or
        // Ctrl+Shift+Z = redo. Ctrl+D = clone selected; Ctrl+Shift+D = create
        // instance of selected; all three share the exact free functions the
        // Hierarchy context menu already calls, so there is one entry point per
        // operation regardless of trigger. Play-mode rejection also comes from
        // the coordinator (execute()), same as every other shortcut here.
        if (!rml.textFocus
            && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
            const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (IsKeyPressed(KEY_Y) || (shift && IsKeyPressed(KEY_Z))) {
                coordinator.redo();
            } else if (IsKeyPressed(KEY_Z)) {
                coordinator.undo();
            } else if (IsKeyPressed(KEY_C)) {
                // Copy the selected Console message. A focused RmlUi text field
                // keeps its own Ctrl+C (guarded by textFocus above); with no
                // selection this is a no-op.
                ui.copySelectedConsoleMessage();
            } else if (shift && IsKeyPressed(KEY_D)) {
                addInstanceOfSelectedType(coordinator);
            } else if (IsKeyPressed(KEY_D)) {
                cloneSelectedEntity(coordinator);
            }
        }
        if (!rml.textFocus && !coordinator.isPlaying() && IsKeyPressed(KEY_F2)) {
            ui.beginActiveSceneLayerRename();
        }
        // Tilemap tool shortcuts only switch tools between strokes/rectangles -
        // mid-operation, only Escape (handled inside routeViewportTilemapPaint,
        // which needs the operation's context) may interrupt.
        if (!rml.textFocus && !coordinator.isPlaying()
            && !coordinator.state().tilemapEditor.pendingStroke
            && !coordinator.state().tilemapEditor.pendingRectangle) {
            if (IsKeyPressed(KEY_B)) coordinator.apply(SetActiveToolIntent{EditorTool::Brush});
            else if (IsKeyPressed(KEY_E)) coordinator.apply(SetActiveToolIntent{EditorTool::Eraser});
            else if (IsKeyPressed(KEY_I)) coordinator.apply(SetActiveToolIntent{EditorTool::Picker});
            else if (IsKeyPressed(KEY_R)) coordinator.apply(SetActiveToolIntent{EditorTool::Rectangle});
            else if (IsKeyPressed(KEY_F)) coordinator.apply(SetActiveToolIntent{EditorTool::Fill});
        }
        // Delete: KEY_DELETE is also forwarded into RmlUi's own text editing
        // (editor_input.cpp), but only takes effect there while a field has
        // focus; textFocus is false in exactly the complementary case, so the
        // two never fire on the same keypress.
        if (!rml.textFocus && IsKeyPressed(KEY_DELETE)) {
            deleteSelectedEntity(coordinator);
        }
        const ViewportRect rect = viewportRectFromDocument(host.document());
        const bool contextMenuHit = ui.isContextMenuHit(
            static_cast<int>(static_cast<float>(GetMouseX()) * uiPixelScaleX()),
            static_cast<int>(static_cast<float>(GetMouseY()) * uiPixelScaleY()));
        const bool animationEditorOpen =
            coordinator.state().spriteAnimationEditor.openAssetId.has_value();
        const bool tilesetEditorOpen =
            coordinator.state().tilesetEditor.openAssetId.has_value();
        const ViewportRect animationInputRect = animationEditorOpen
            ? resolveSpriteAnimationCanvasContentRect(animationDocument)
            : ViewportRect{};
        const ViewportRect tilesetInputRect = tilesetEditorOpen
            ? resolveTilesetEditorCanvasContentRect(tilesetDocument)
            : ViewportRect{};
        if (!animationEditorOpen && !tilesetEditorOpen) {
            routeViewportInput(coordinator, rect, rml, contextMenuHit);
        }
        if (coordinator.isPlaying()) {
            const float dt = GetFrameTime();
            coordinator.advanceRuntime(dt);               // authored motion (LinearMover)
            // Gameplay input is neutral while a text field has focus.
            RuntimeInputSnapshot input;
            if (!rml.textFocus) {
                input.moveLeft  = IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A);
                input.moveRight = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
                input.moveUp    = IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W);
                input.moveDown  = IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S);
                // Edge-triggered jump for the PlatformerController (Space / W / Up).
                input.jumpPressed = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W)
                                 || IsKeyPressed(KEY_UP);
            }
            coordinator.updateRuntime(input, dt);         // input-driven (TopDownController)
        } else if (!animationEditorOpen && !tilesetEditorOpen) {
            // First time this scene is active in Edit mode: frame it once. The
            // flag lives in the scene's view state, so it shares the sceneViews
            // lifecycle (cleared/pruned with the scene). Mark only after a real
            // fit, so a not-yet-laid-out viewport retries next frame.
            const SceneId& editScene = coordinator.state().activeSceneId;
            if (!editScene.empty() && !coordinator.sceneView(editScene).initialized) {
                if (fitActiveScene()) coordinator.markSceneViewInitialized(editScene);
            }
            if (!rml.textFocus && IsKeyPressed(KEY_ESCAPE)) routeGlobalEscape(coordinator);
            routeViewportPickDrag(coordinator, rect, rml, drag, contextMenuHit);
            routeViewportContextMenu(coordinator, ui, rect, rml, contextClick,
                                     pendingContextSpawn, contextMenuHit);
            routeViewportTilemapPaint(coordinator, rect, rml);
        }

        // Pointer world/cell readout (Edit mode, mouse over the viewport).
        // Presentation only; the UI writes the label just when the text changes.
        {
            ViewportPointerReadout pointerReadout;
            if (!coordinator.isPlaying() && !animationEditorOpen && !tilesetEditorOpen
                && rect.contains(GetMouseX(), GetMouseY())) {
                const SceneId& active = coordinator.state().activeSceneId;
                if (const SceneDef* scene = coordinator.document().findScene(active)) {
                    const SceneViewCamera camera = makeSceneViewCamera(
                        rect, coordinator.sceneView(active), scene->worldSize);
                    pointerReadout = makePointerReadout(
                        Vec2{static_cast<float>(GetMouseX()),
                             static_cast<float>(GetMouseY())},
                        camera, coordinator.document(), coordinator.state(), active);
                }
            }
            ui.showViewportPointerReadout(pointerReadout);
        }

        // Sprite source paths are relative to the loaded project; with no project
        // open yet (a new/Untitled project) they fall back to the executable resources.
        const std::filesystem::path assetRoot =
            currentProjectPath.empty() ? resourceRoot : currentProjectPath.parent_path();
        const auto textureRequests = coordinator.playSession()
            ? textureRequestsFor(coordinator.playSession()->assets(), assetRoot)
            : textureRequestsFor(coordinator.document().data(), assetRoot);
        if (!coordinator.isPlaying() && animationEditorOpen) {
            routeSpriteAnimationCanvasInput(
                coordinator, animationInputRect, rml, textureCache, textureRequests);
            coordinator.advanceSpriteAnimationPreview(GetFrameTime());
        }
        if (!coordinator.isPlaying() && tilesetEditorOpen) {
            routeTilesetEditorCanvasInput(
                coordinator, tilesetInputRect, rml, textureCache, textureRequests);
            // Keyboard: Esc = guarded close, Enter = apply, arrows = move the
            // pending-grid selection. prevTextFocus covers the commit frame -
            // the Enter that just committed a field must not double as Apply,
            // and the Esc that dismissed a field must not close the editor.
            if (!rml.textFocus && !prevTextFocus) {
                if (IsKeyPressed(KEY_ESCAPE)) {
                    closeTilesetEditorGuarded();
                } else if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                    tryApplyPendingTilesetSlicing();
                } else {
                    int dx = 0;
                    int dy = 0;
                    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) dx = 1;
                    else if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) dx = -1;
                    else if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) dy = 1;
                    else if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) dy = -1;
                    if (dx != 0 || dy != 0) moveTilesetSelection(dx, dy);
                }
            }
        }
        prevTextFocus = rml.textFocus;

        ui.processFrame();
        // Title dirty-cue: follows undo/redo/save as well as edits, so it can't
        // hang off any single action path. Change-guarded O(1) check per frame.
        if (coordinator.document().isDirty() != titleShowsDirty) refreshWindowTitle();
        if (!coordinator.isPlaying() && !animationEditorOpen && !tilesetEditorOpen) {
            if (const std::optional<Vec2> preview = dragPreviewPosition(coordinator, rect, drag)) {
                ui.showEntityPositionPreview(drag.entity, *preview);
            }
        }
        host.update();
        const ViewportRect animationRenderRect = animationEditorOpen
            ? resolveSpriteAnimationCanvasContentRect(animationDocument)
            : ViewportRect{};
        const ViewportRect animationPreviewRect = animationEditorOpen
            ? resolveSpriteAnimationPreviewContentRect(animationDocument)
            : ViewportRect{};
        const ViewportRect tilesetRenderRect = tilesetEditorOpen
            ? resolveTilesetEditorCanvasContentRect(tilesetDocument)
            : ViewportRect{};
        // Per-frame timeline thumbnail slots: RmlUi lays out one thumb sub-element
        // per chip; raylib paints each frame's texture region into it (drawn after
        // host.render(), like the sheet/preview canvases).
        const SpriteAnimationClipDef* timelineClip = nullptr;
        std::vector<ViewportRect> timelineThumbRects;
        if (animationEditorOpen) {
            const SpriteAnimationEditorState& animState =
                coordinator.state().spriteAnimationEditor;
            if (animState.openAssetId && animState.selectedClipId) {
                if (const SpriteAnimationAssetDef* a =
                        coordinator.document().findSpriteAnimationAsset(*animState.openAssetId)) {
                    for (const SpriteAnimationClipDef& c : a->clips) {
                        if (c.id == *animState.selectedClipId) { timelineClip = &c; break; }
                    }
                }
            }
            if (timelineClip) {
                timelineThumbRects.reserve(timelineClip->frames.size());
                for (std::size_t i = 0; i < timelineClip->frames.size(); ++i) {
                    const std::string id = "anim-frame-thumb-" + std::to_string(i);
                    timelineThumbRects.push_back(
                        elementContentRectFromDocument(animationDocument, id.c_str()));
                }
            }
        }

        // Tile Palette thumbnail slots (Inspector's Tilemap section): shown
        // whenever the selected entity has a TilemapComponent whose tileset
        // resolves to at least one sliced tile. Same per-thumb-element rect
        // query as the animation timeline above, just against the main shell
        // document instead of the animation overlay's.
        const ViewportRect tilePaletteClipRect =
            elementContentRectFromDocument(host.document(), "inspector-body");
        const TilesetAsset* tilePaletteTileset = nullptr;
        std::vector<ViewportRect> tilePaletteThumbRects;
        if (!coordinator.isPlaying()) {
            const SceneInstanceDef* selectedInst = coordinator.document().findInstanceInScene(
                coordinator.state().activeSceneId, coordinator.selection().primaryEntity);
            if (selectedInst && selectedInst->tilemap.has_value()) {
                const TilesetAsset* candidate = coordinator.document().findTilesetAsset(
                    selectedInst->tilemap->tilesetAssetId);
                if (candidate && !candidate->tiles.empty()) tilePaletteTileset = candidate;
            }
        }
        if (tilePaletteTileset) {
            tilePaletteThumbRects.reserve(tilePaletteTileset->tiles.size());
            for (std::size_t i = 0; i < tilePaletteTileset->tiles.size(); ++i) {
                const std::string id = "tile-thumb-" + std::to_string(i);
                tilePaletteThumbRects.push_back(elementContentRectFromDocument(host.document(), id.c_str()));
            }
        }
        // Picker sync: when the workspace's selected tile changes, bring its
        // palette thumbnail into view (scrolls .inspector's own overflow, no
        // nested scroll region - see panels.rcss). Diffed against the last
        // scrolled id so this fires once per change, not every frame.
        if (tilePaletteTileset
            && coordinator.state().tilemapEditor.selectedTileId != lastScrolledPaletteTile) {
            lastScrolledPaletteTile = coordinator.state().tilemapEditor.selectedTileId;
            if (lastScrolledPaletteTile) {
                for (std::size_t i = 0; i < tilePaletteTileset->tiles.size(); ++i) {
                    if (tilePaletteTileset->tiles[i].id != *lastScrolledPaletteTile) continue;
                    const std::string id = "tile-thumb-" + std::to_string(i);
                    if (Rml::Element* el = host.document()->GetElementById(id)) {
                        el->ScrollIntoView(Rml::ScrollIntoViewOptions{
                            Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
                            Rml::ScrollBehavior::Smooth});
                    }
                    break;
                }
            }
        }

        BeginDrawing();
        ClearBackground(Color{15, 16, 20, 255});
        const PlaySession* playSession = coordinator.playSession();
        const SceneId active = playSession ? playSession->sceneId()
                                           : coordinator.state().activeSceneId;
        SceneFrameSnapshot snapshot = playSession
            ? collectSceneFrameSnapshot(*playSession)
            : collectSceneFrameSnapshot(coordinator.document(), active,
                                        coordinator.selection().primaryEntity,
                                        coordinator.sceneView(active).hiddenLayerIds);
        if (!playSession && drag.active) {
            // Local drag preview: offset the dragged entity by the live delta so
            // the move is visible before the single command lands on release.
            const std::optional<Vec2> preview = dragPreviewPosition(coordinator, rect, drag);
            const Vec2 d = preview
                ? Vec2{preview->x - drag.startEntityPos.x, preview->y - drag.startEntityPos.y}
                : Vec2{};
            for (SceneFrameEntity& e : snapshot.entities)
                if (e.entityId == drag.entity) { e.bounds.x += d.x; e.bounds.y += d.y; }
            for (SceneFrameSprite& s : snapshot.sprites)
                if (s.entityId == drag.entity) { s.destination.x += d.x; s.destination.y += d.y; }
            // The collider overlay must follow the dragged entity too, otherwise it
            // lingers at the old position until the move commits on release.
            for (SceneFrameCollider& col : snapshot.colliders)
                if (col.entityId == drag.entity) { col.worldBounds.x += d.x; col.worldBounds.y += d.y; }
        }
        if (!playSession) {
            applyPendingTilemapStrokePreview(snapshot, coordinator.document(),
                                             coordinator.state().tilemapEditor);
            applyPendingTilemapRectanglePreview(snapshot, coordinator.document(),
                                                coordinator.state().tilemapEditor);
        }
        EditorSceneViewState renderView = coordinator.sceneView(active);
        if (playSession) renderView.gridVisible = false;
        const SpriteAnimationAssetDef* animationAsset = nullptr;
        const TilesetAsset* tilesetAsset = nullptr;
        // Asset editors replace the scene viewport only in Edit mode. During
        // Play the runtime scene must always render regardless of workspace UI.
        if (!playSession) {
            if (const auto& open = coordinator.state().spriteAnimationEditor.openAssetId) {
                animationAsset = coordinator.document().findSpriteAnimationAsset(*open);
            } else if (const auto& openTileset = coordinator.state().tilesetEditor.openAssetId) {
                tilesetAsset = coordinator.document().findTilesetAsset(*openTileset);
            }
        }
        if (playSession || (!animationAsset && !tilesetAsset)) {
            textureCache.prepare(snapshot.sprites, snapshot.tilemaps, textureRequests);
            const SceneGridDefinition displayGrid = viewportDisplayGrid(
                coordinator.document(), coordinator.state(), active);
            sceneView.render(snapshot, renderView, displayGrid, rect, textureCache,
                             canvasFont);
            if (!playSession) {
                drawTilemapPaintOverlay(coordinator.document(), coordinator.state().tilemapEditor,
                                        active, coordinator.selection().primaryEntity, rect,
                                        renderView, snapshot.worldSize);
            }
        }
        host.render();
        if (animationAsset) {
            renderSpriteAnimationPreview(
                *animationAsset, coordinator.state().spriteAnimationEditor,
                animationRenderRect, textureCache, textureRequests, canvasFont);
            renderSpriteAnimationClipPreview(
                *animationAsset, coordinator.state().spriteAnimationEditor,
                animationPreviewRect, textureCache, textureRequests, canvasFont);
            if (timelineClip) {
                renderSpriteAnimationTimelineThumbnails(
                    *animationAsset, *timelineClip, timelineThumbRects,
                    textureCache, textureRequests);
            }
        }
        if (tilesetAsset) {
            renderTilesetEditorCanvas(
                *tilesetAsset, coordinator.state().tilesetEditor,
                tilesetRenderRect, textureCache, textureRequests, canvasFont);
            // Selected Tile thumbnail: resolve the id the same way the panel's
            // text does (committed tiles first, else the live pending grid).
            const TilesetEditorState& tilesetState = coordinator.state().tilesetEditor;
            if (tilesetState.selectedTileId) {
                std::optional<TileDefinition> selectedTile;
                for (const TileDefinition& t : tilesetAsset->tiles) {
                    if (t.id == *tilesetState.selectedTileId) { selectedTile = t; break; }
                }
                if (!selectedTile) {
                    const TextureResource* res =
                        textureCache.find(tilesetAsset->imageAssetId);
                    if (res && res->loaded) {
                        for (const TileDefinition& t : tilesForSlicing(
                                 res->texture.width, res->texture.height,
                                 tilesetState.pendingSlicing)) {
                            if (t.id == *tilesetState.selectedTileId) {
                                selectedTile = t;
                                break;
                            }
                        }
                    }
                }
                if (selectedTile) {
                    renderTilesetSelectedTileThumb(
                        tilesetAsset->imageAssetId, *selectedTile,
                        elementContentRectFromDocument(tilesetDocument,
                                                       "tileset-selected-thumb"),
                        textureCache, textureRequests);
                }
            }
            // Committed-tiles grid: same renderer as the Inspector palette,
            // clipped to the grid's own scroll box.
            if (!tilesetAsset->tiles.empty()) {
                const ViewportRect gridClip = elementContentRectFromDocument(
                    tilesetDocument, "tileset-tiles-grid");
                if (gridClip.valid()) {
                    std::vector<ViewportRect> gridThumbRects;
                    gridThumbRects.reserve(tilesetAsset->tiles.size());
                    for (std::size_t i = 0; i < tilesetAsset->tiles.size(); ++i) {
                        const std::string id = "tileset-grid-thumb-" + std::to_string(i);
                        const ViewportRect slot =
                            elementContentRectFromDocument(tilesetDocument, id.c_str());
                        if (!slot.valid()) break;   // capped markup: no more slots
                        gridThumbRects.push_back(slot);
                    }
                    renderTilePalette(*tilesetAsset, tilesetState.selectedTileId,
                                      gridThumbRects, gridClip, textureCache,
                                      textureRequests);
                }
            }
        }
        if (tilePaletteTileset) {
            renderTilePalette(
                *tilePaletteTileset, coordinator.state().tilemapEditor.selectedTileId,
                tilePaletteThumbRects, tilePaletteClipRect, textureCache, textureRequests);
        }
        EndDrawing();

        ++frame;
        if (lifecycleSmoke && frame == 2) {
            // Real RmlUi contract smoke:
            //  * second bind must not duplicate callbacks (one toggle only),
            //  * detach with an open popup removes every callback,
            //  * double detach and a missing-document session are safe.
            ui.bind();
            ui.showViewportContextMenu(20, 20, true);
            Rml::Element* probe = host.document()
                ? host.document()->GetElementById("btn-console-info") : nullptr;
            const bool before = coordinator.uiState().consoleShowInfo;
            if (probe) probe->DispatchEvent("click", Rml::Dictionary{});
            const bool afterSingleDispatch = coordinator.uiState().consoleShowInfo;
            const bool exactlyOneCallback = probe && afterSingleDispatch != before;

            ui.detach();
            if (probe) probe->DispatchEvent("click", Rml::Dictionary{});
            const bool noCallbackAfterDetach =
                coordinator.uiState().consoleShowInfo == afterSingleDispatch;
            ui.detach();

            EditorUi missingDocumentUi(coordinator, nullptr, nullptr, nullptr);
            missingDocumentUi.bind();
            missingDocumentUi.detach();
            missingDocumentUi.detach();

            if (!exactlyOneCallback || !noCallbackAfterDetach || ui.isBound()) {
                TraceLog(LOG_ERROR, "[editor] RmlUi lifecycle smoke failed");
                exitCode = 1;
            } else {
                TraceLog(LOG_INFO, "[editor] RmlUi lifecycle smoke passed");
            }
            break;
        }
        // Capture only once the framebuffer size has settled: the maximize
        // animation and the initial DPI rescale land within the first frames,
        // and a resize processed between the draw and the capture leaves the
        // frame's content bottom-left anchored in a larger framebuffer
        // (black bands top/right).
        if (!shotPath.empty() && frame >= 12 && sizeStableFrames >= 45) {
            TakeScreenshot(shotPath.c_str());
            break;
        }
    }

    textureCache.clear();
    unloadCanvasFont(canvasFont);   // GPU atlas: released before CloseWindow
    ui.detach();
    uiOwner.reset(); // Destroy UI/controllers before host documents and context.
    host.shutdown();
    CloseWindow();
    return exitCode;
}

} // namespace ArtCade::EditorNative
