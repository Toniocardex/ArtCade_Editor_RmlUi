#include "editor-native/app/editor_app.h"

#include "editor-native/app/asset_import.h"
#include "editor-native/app/confirm_dialog.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/editor_input.h"
#include "editor-native/app/file_dialog.h"
#include "editor-native/app/hierarchy_actions.h"
#include "editor-native/app/input_routing.h"
#include "editor-native/app/project_file.h"
#include "editor-native/app/rml_host.h"
#include "editor-native/app/sprite_animation_canvas_input.h"
#include "editor-native/app/sprite_animation_preview_renderer.h"
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
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_animation_slicing.h"
#include "editor-native/model/tilemap_stroke_preview.h"
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
#include <utility>
#include <vector>

// raylib (5.0) has no public way to cancel a requested window close, so we reset
// GLFW's flag directly to keep the app open when the user picks Cancel in the
// unsaved-changes guard. GetWindowHandle() returns the GLFWwindow*.
extern "C" void glfwSetWindowShouldClose(void* window, int value);

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
        worldPosition, makeSceneGridDefinition(coordinator.sceneView(sceneId)));
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
            const EntityId picked = pickEntityAt(frame, world);
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
    const std::filesystem::path path(sourcePath);
    if (path.is_absolute()) return path.lexically_normal();
    return std::filesystem::absolute(resourceRoot / path).lexically_normal();
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

    const std::filesystem::path source = previousRoot / "assets";
    std::error_code ec;
    if (!std::filesystem::exists(source, ec)) return true;
    if (ec) {
        error = "Could not inspect existing assets folder: " + ec.message();
        return false;
    }

    std::filesystem::create_directories(nextRoot, ec);
    if (ec) {
        error = "Could not create project folder: " + ec.message();
        return false;
    }

    const std::filesystem::path destination = nextRoot / "assets";
    std::filesystem::copy(source, destination,
                          std::filesystem::copy_options::recursive
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
    int shotSliceColumns = 0;   // > 0: slice the open clip into N frames for the shot
    bool shotSliceAll = false;  // slice every animation asset in turn (overwrite repro)
    std::string shotSavePath;   // non-empty: save the project here via the real path
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shotPath = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-project") == 0 && i + 1 < argc)
            shotProject = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-anim") == 0) shotAnimation = true;
        else if (std::strcmp(argv[i], "--shot-slice") == 0 && i + 1 < argc)
            shotSliceColumns = std::atoi(argv[i + 1]);
        else if (std::strcmp(argv[i], "--shot-slice-all") == 0) shotSliceAll = true;
        else if (std::strcmp(argv[i], "--shot-save") == 0 && i + 1 < argc)
            shotSavePath = argv[i + 1];
    }

    // Start empty: the editor opens a real project (File > Open) or builds one
    // from scratch (add scene/entity, import assets, Save As). No bundled demo.
    EditorCoordinator coordinator{ProjectDoc{}};

    // FLAG_WINDOW_HIGHDPI: create a framebuffer at the monitor's physical
    // resolution so RmlUi rasterises and renders at real pixels (crisp text on
    // scaled displays) instead of being upscaled from a logical-size buffer.
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT |
                   FLAG_WINDOW_HIGHDPI);
    InitWindow(1340, 840, "ArtCade Studio");
    const std::filesystem::path resourceRoot = editorResourceRoot();
    applyWindowIcon(resourceRoot);
    MaximizeWindow();
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

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

    EditorUi ui(coordinator, host.document(), animationDocument, tilesetDocument);
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
    // before the first Save As. Kept truthful wherever the path changes (New,
    // Open, Save), so "Untitled" means "no destination on disk yet", which is
    // distinct from dirty (content differs from the last baseline).
    const auto refreshWindowTitle = [&]() {
        const std::string name = currentProjectPath.empty()
            ? std::string("Untitled")
            : currentProjectPath.stem().string();
        SetWindowTitle(("ArtCade Studio - " + name).c_str());
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
            if (!picked) return;            // cancelled: the current project stays
            ProjectDoc fresh;
            fresh.projectName = normalizeProjectSavePath(*picked).stem().string();
            coordinator.replaceProject(ProjectDocument{std::move(fresh)});  // empty valid project
            textureCache.clear();           // explicit app path consuming ProjectReplaced
            currentProjectPath.clear();     // saveTo sets the path only on success
            refreshWindowTitle();
            if (saveTo(*picked)) coordinator.logInfo("New project");
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
    const std::function<void()> applyTilesetSlicingHandler = [&]() {
        const TilesetEditorState& state = coordinator.state().tilesetEditor;
        if (!state.openAssetId) {
            coordinator.logWarning("Open a tileset before applying slicing");
            return;
        }
        const TilesetAsset* asset = coordinator.document().findTilesetAsset(*state.openAssetId);
        if (!asset) {
            coordinator.logError("Cannot slice: tileset asset is missing");
            return;
        }

        const std::filesystem::path assetRoot =
            currentProjectPath.empty() ? resourceRoot : currentProjectPath.parent_path();
        SceneFrameSprite requestSprite;
        requestSprite.assetId = asset->imageAssetId;
        requestSprite.visible = true;
        const auto requests = textureRequestsFor(coordinator.document().data(), assetRoot);
        textureCache.prepare({requestSprite}, requests);
        const TextureResource* resource = textureCache.find(asset->imageAssetId);
        if (!resource || !resource->loaded) {
            coordinator.logError("Cannot slice: source image is not loaded");
            return;
        }

        // Pixel-size-first: the tiles come straight from the pending config and
        // the sheet's real dimensions - no frame-count derivation needed.
        const std::vector<TileDefinition> freshTiles = tilesForSlicing(
            resource->texture.width, resource->texture.height, state.pendingSlicing);
        if (freshTiles.empty()) {
            coordinator.logError("Cannot slice: tile size does not fit the sheet");
            return;
        }
        // Reconcile against the asset's current tiles so a tile whose rect didn't
        // move keeps its stable id (metadata a later slice attaches survives).
        const std::vector<TileDefinition> reconciled = reconcileTiles(asset->tiles, freshTiles);

        const EditorOperationResult result = coordinator.execute(
            ChangeTilesetSlicingCommand{*state.openAssetId, state.pendingSlicing, reconciled});
        if (result.ok) {
            coordinator.logInfo("Sliced " + asset->name + " into "
                                + std::to_string(reconciled.size()) + " tiles");
        }
    };
    ui.setTilesetApplySlicingHandler(applyTilesetSlicingHandler);
    const auto viewportDefaultSpawn = [&]() -> std::optional<Vec2> {
        const SceneId& active = coordinator.state().activeSceneId;
        const SceneDef* scene = coordinator.document().findScene(active);
        if (!scene) return std::nullopt;
        const ViewportRect rect = viewportRectFromDocument(host.document());
        if (!rect.valid()) {
            return normalizeSpawnPosition(
                Vec2{scene->worldSize.x * 0.5f, scene->worldSize.y * 0.5f},
                scene->worldSize);
        }
        return defaultSpawnPosition(rect, coordinator.sceneView(active), scene->worldSize);
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
    while (true) {
        // Exit guard: a requested close (window X) is held until the unsaved
        // guard passes. On Cancel we clear GLFW's close flag and keep running.
        // Screenshot mode skips the guard (no window interaction).
        if (WindowShouldClose()) {
            if (shotPath.empty() && !guardPasses()) {
                glfwSetWindowShouldClose(GetWindowHandle(), 0);
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
            lastRenderW = renderW;
            lastRenderH = renderH;
            lastDpi     = curDpi;
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
            routeViewportPickDrag(coordinator, rect, rml, drag, contextMenuHit);
            routeViewportContextMenu(coordinator, ui, rect, rml, contextClick,
                                     pendingContextSpawn, contextMenuHit);
            routeViewportTilemapPaint(coordinator, rect, rml);
        }

        // Pointer world-position readout (Edit mode, mouse over the viewport).
        // Presentation only; the UI writes the label just when the text changes.
        {
            std::optional<Vec2> pointerWorld;
            if (!coordinator.isPlaying() && !animationEditorOpen && !tilesetEditorOpen
                && rect.contains(GetMouseX(), GetMouseY())) {
                const SceneId& active = coordinator.state().activeSceneId;
                if (const SceneDef* scene = coordinator.document().findScene(active)) {
                    const SceneViewCamera camera = makeSceneViewCamera(
                        rect, coordinator.sceneView(active), scene->worldSize);
                    pointerWorld = screenToWorld(
                        camera, Vec2{static_cast<float>(GetMouseX()),
                                     static_cast<float>(GetMouseY())});
                }
            }
            ui.showPointerWorldPosition(pointerWorld);
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
        }

        ui.processFrame();
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
        if (const auto& open = coordinator.state().spriteAnimationEditor.openAssetId) {
            animationAsset = coordinator.document().findSpriteAnimationAsset(*open);
        } else if (const auto& openTileset = coordinator.state().tilesetEditor.openAssetId) {
            tilesetAsset = coordinator.document().findTilesetAsset(*openTileset);
        } else {
            textureCache.prepare(snapshot.sprites, snapshot.tilemaps, textureRequests);
            sceneView.render(snapshot, renderView, rect, textureCache);
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
                animationRenderRect, textureCache, textureRequests);
            renderSpriteAnimationClipPreview(
                *animationAsset, coordinator.state().spriteAnimationEditor,
                animationPreviewRect, textureCache, textureRequests);
            if (timelineClip) {
                renderSpriteAnimationTimelineThumbnails(
                    *animationAsset, *timelineClip, timelineThumbRects,
                    textureCache, textureRequests);
            }
        }
        if (tilesetAsset) {
            renderTilesetEditorCanvas(
                *tilesetAsset, coordinator.state().tilesetEditor,
                tilesetRenderRect, textureCache, textureRequests);
        }
        EndDrawing();

        if (!shotPath.empty() && ++frame == 12) {
            TakeScreenshot(shotPath.c_str());
            break;
        }
    }

    textureCache.clear();
    host.shutdown();
    CloseWindow();
    return 0;
}

} // namespace ArtCade::EditorNative
