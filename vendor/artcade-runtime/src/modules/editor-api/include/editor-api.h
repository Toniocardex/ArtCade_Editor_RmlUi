#pragma once
// =============================================================================
// editor-api.h -- bidirectional bridge between the C++ WASM runtime and the
//                 external editor host.
//
// Architectural rules (Guida_Architettura_e_SmokeTest_ArtCade):
//
//  INPUT  -- C++ reads mouse/keyboard NATIVELY via emscripten/html5.h.
//            The host never listens to canvas mouse events.
//            Zero-latency: mouse moves at 1000 Hz inside C++.
//
//  STATE  -- C++ is the Single Source of Truth.
//            The host receives final coordinates only on drop, not per-frame.
//
//  Host->C++   EMSCRIPTEN_KEEPALIVE functions called via Module.ccall()
//  C++->Host   EM_ASM macros that call window.on* globals
//              (set by wasm-bridge.ts BEFORE game.js loads)
// =============================================================================

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../presentation/include/presentation_snapshot_wasm.h"
#include "../../../core/types.h"
#include "../../scene-system/include/scene-mutation-result.h"
#include "../../scene-system/include/scene-patch.h"
#include "../../scene-system/include/scene-invalidation.h"
#include "editor-transform-gizmo.h"

// Forward declarations shared by both the WASM build and the native stub.
// Keeping them outside the #ifdef avoids a class of "undeclared identifier"
// errors when the native build of editor-api.h is included from a TU that
// doesn't bring in app.h (TECHNICAL_DEBT_REVIEW §22).
namespace ArtCade {
namespace Modules {
class RuntimeEntityGateway;
class LuaHost;
class Renderer;
class DialogManager;
class SpriteAnimator;
class Audio;
class VariableManager;
}
namespace Presentation {
class EditorViewportService;
}
struct TilePaletteEntry;
struct TilesetAsset;
struct ProjectRuntimeSettings;
struct GameVariableDefinition;

/**
 * Callback invoked by editor_load_project AFTER the gateway has been
 * populated from the JSON blob. Application registers its
 * applyEditorProjectLoaded() through EditorAPI::setProjectLoadedHandler()
 * so editor-api never has to depend on app.h
 * (TECHNICAL_DEBT_REVIEW §8 — replaced static `s_application` pointer).
 */
using EditorProjectLoadedHandler = std::function<void(
    const std::vector<TilePaletteEntry>&,
    const std::vector<TilesetAsset>&,
    const std::vector<GameVariableDefinition>&,
    const ProjectRuntimeSettings&)>;

/** Same payload as EditorProjectLoadedHandler; used by editor_restore_from_project. */
using EditorPreviewRestoreHandler = EditorProjectLoadedHandler;

/** After editor_enter_play_mode repopulates the gateway (PLAY transition). */
using EditorEnterPlayHandler = EditorProjectLoadedHandler;

/** After editor_exit_play_mode — includes design-time Lua source. */
using EditorExitPlayHandler = std::function<void(
    const std::vector<TilePaletteEntry>&,
    const std::vector<TilesetAsset>&,
    const std::vector<GameVariableDefinition>&,
    const ProjectRuntimeSettings&,
    const std::string& luaSource)>;

/** Frame-boundary scene commit for picking (layer visibility) and revision export. */
struct EditorSceneFrameCommit {
    uint64_t sceneRevision = 0;
    std::unordered_map<std::string, SceneLayerSettings> layerSettings;
};

/** Applies a normalized scene patch; registered by Application. */
using SceneMutationApplyFn = std::function<Modules::SceneMutationResult(
    const SceneId&,
    const Modules::ScenePatch&)>;

/** Post-mutation callback; Application owns renderer/presentation coordination. */
using SceneMutationHandler = std::function<void(const Modules::SceneMutationResult&)>;

/** Queue declarative invalidations from non-mutation editor sync paths. */
using SceneInvalidationQueueHandler = std::function<void(Modules::SceneInvalidation)>;

using AuthoringSyncBatchHandler = std::function<void()>;

using SceneMutationBatchOpenPredicate = std::function<bool()>;

/** Return codes for editor_enter_play_mode / editor_exit_play_mode / editor_reload_script. */
enum EditorApiResult : int {
    kEditorApiOk         = 0,
    kEditorApiJsonError  = 1,
    kEditorApiLuaError   = 2,
    kEditorApiNotWired   = 3,
};
} // namespace ArtCade

#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/html5.h>

namespace ArtCade {

class EditorAPI {
public:
    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * Call once at startup (from Application::initSubsystems).
     * Registers all native input callbacks on the given canvas CSS selector.
     */
    static void init(const char* canvasSelector = "#artcade-canvas");
    static void shutdown();

    // -------------------------------------------------------------------------
    // Engine wiring
    // Called by Application after all modules are initialised so that the
    // editor commands (editor_load_project, editor_set_transform, ...) can
    // reach the engine's internal state.
    // -------------------------------------------------------------------------
    static void wireEngine(Modules::RuntimeEntityGateway* gateway);

    /**
     * Wire the LuaHost so editor_reload_script() can push freshly compiled
     * Logic Board Lua into the running VM. Called by Application after the
     * LuaHost is initialised.
     */
    static void wireLua(Modules::LuaHost* luaHost);

    /**
     * Wire the Renderer so editor_register_image() can upload editor-loaded
     * images (e.g. tilesets not present in the VFS) into the GPU texture
     * cache. Called by Application after the Renderer is initialised.
     */
    static void wireRenderer(Modules::Renderer* renderer);

    /**
     * Wire the editor viewport owned by Application. Navigation intents target
     * this service directly; Renderer holds a non-owning pointer for sync only.
     */
    static void wireEditorViewport(Presentation::EditorViewportService* viewport);

    /**
     * Commits the immutable scene frame used for picking and revision export.
     * Called once per frame from Application after SceneFrameSnapshot is built.
     */
    static void commit_scene_frame(const EditorSceneFrameCommit& frame);
    /** Layer visibility map from the last commit_scene_frame call. */
    static const std::unordered_map<std::string, SceneLayerSettings>&
    committed_scene_layer_settings();

    /** Wire DialogManager so editor_load_dialogs() can register preview graphs. */
    static void wireDialog(Modules::DialogManager* dialogManager);

    /** Wire SpriteAnimator so project load can register animation clips. */
    static void wireSpriteAnimator(Modules::SpriteAnimator* spriteAnimator);

    static void wireAudio(Modules::Audio* audio);
    static void wireVariables(Modules::VariableManager* variables);

    /**
     * Register the callback invoked after editor_load_project finishes
     * populating the gateway. Replaces the previous direct
     * `s_application->applyEditorProjectLoaded(...)` coupling.
     * Pass an empty handler (`{}`) to unregister.
     */
    static void setProjectLoadedHandler(EditorProjectLoadedHandler handler);

    /**
     * Register the callback invoked after editor_restore_from_project finishes
     * repopulating the gateway. Resets gameplay modules without clearing Lua.
     */
    static void setPreviewRestoreHandler(EditorPreviewRestoreHandler handler);

    /** After editor_enter_play_mode JSON is applied to the gateway. */
    static void setEnterPlayHandler(EditorEnterPlayHandler handler);

    /** After editor_exit_play_mode JSON is applied (Lua applied in handler). */
    static void setExitPlayHandler(EditorExitPlayHandler handler);

    /**
     * Register scene mutation apply + post-mutation handlers.
     * EditorAPI stays an adapter; Application is the composition root.
     */
    static void setSceneMutationBridge(
        SceneMutationApplyFn apply,
        SceneMutationHandler onResult);

    static void setSceneInvalidationQueueHandler(SceneInvalidationQueueHandler handler);

    static void setAuthoringSyncBatchHandlers(
        AuthoringSyncBatchHandler onBegin,
        AuthoringSyncBatchHandler onEnd);

    /** When true, scene mutation callbacks defer until end_authoring_sync_batch(). */
    static void setSceneMutationBatchOpenPredicate(SceneMutationBatchOpenPredicate predicate);

    // -------------------------------------------------------------------------
    // C++ -> editor-host notifications (Smoke Test 3)
    // -------------------------------------------------------------------------

    /** User clicked an entity in the viewport -> host updates Hierarchy + Inspector. */
    static void notifyEntitySelected(uint32_t entityId);

    /**
     * User Ctrl/Cmd-clicked an entity in the viewport.
     * @param entityId  source scene-instance id
     * @param x         snapped world-space clone centre
     * @param y         snapped world-space clone centre
     */
    static void notifyEntityDuplicateRequested(uint32_t entityId, float x, float y);

    /**
     * Canvas manipulation finished (move or resize) -> host commits once.
     * Called on mouse-UP, NOT every mouse-move (Single Source of Truth rule).
     */
    static void notifyTransformChanged(uint32_t entityId,
        float x, float y, float rotation,
        float scaleX, float scaleY);

    /**
     * Live canvas drag preview for Inspector display only.
     * Called on mouse-move when the quantized transform changes.
     */
    static void notifyTransformPreview(uint32_t entityId,
        float x, float y, float rotation,
        float scaleX, float scaleY);

    /** Engine / Lua debug.log() -> host Console panel. */
    static void notifyConsoleLine(const char* message, const char* level = "info");
    static void notifyRuntimeProfile(float fps,
                                     float luaMs,
                                     float physicsMs,
                                     float renderMs,
                                     uint32_t entityCount,
                                     uint32_t physicsBodies);
    static void queueConsoleLine(const char* message, const char* level = "info");
    static void flushConsoleLines();
    /** Null EditorAPI module pointers after Application::shutdownModules(). */
    static void clearEngineWiring();
    /** @return true when RuntimeEntityGateway is wired for editor_load_project. */
    static bool isEngineWired();
    /** Records which WASM boot step failed (shown on the editor splash). */
    static void recordBootFailure(const char* step);
    /** @return empty when boot succeeded; otherwise the failed init step name. */
    static const char* bootFailureStep();
    /** Stores main() return code for the editor splash (-1 = not run yet). */
    static void recordWasmMainExitCode(int code);
    /** @return last WASM main() exit code recorded by Application::run(). */
    static int wasmMainExitCode();

    /** Editor viewport: mouse world position for the status bar (editor mode only). */
    static void notifyCursorWorld(float x, float y);

    /** Spritesheet Studio preview queue (processed on the engine main loop). */
    static void queueSpritesheetPreview(
        const char* texturePath,
        const char* clipName,
        float dtSeconds,
        int canvasW,
        int canvasH);
    static void resetSpritesheetPreview();
    static void processSpritesheetPreviewQueue();

    // Written by extern "C" exports -- public so they can be set directly
    static int      s_mode;
    static uint32_t s_selectedEntityId;
    static std::vector<uint32_t> s_selectedEntityIds;
    static bool     s_isDragging;
    static float    s_dragStartX, s_dragStartY;
    static ManipulationMode s_manipulationMode;
    static ResizeDragState  s_resizeDragState;
    static int      s_editorTool;      // 0 select, 1 pan
    static bool     s_editorGuidesEnabled;
    static float    s_editorGridSize;
    /** Scene-settings "Snap while editing" — live magnetism during canvas drag. */
    static bool     s_editorSnapEnabled;
    /** World Settings → draw collider outlines in play mode (from project JSON). */
    static bool     s_physicsDebugDraw;

    /** Last frame timings for editor status bar (WASM); see publishRuntimeProfile(). */
    static void publishRuntimeProfile(float fps,
                                      float luaMs,
                                      float physicsMs,
                                      float renderMs,
                                      uint32_t entityCount,
                                      uint32_t physicsBodies);
    static const float* runtimeProfileBuffer();

    // Engine pointers wired in wireEngine() / wireLua() / wireDialog()
    static Modules::RuntimeEntityGateway* s_entityGateway;
    static Modules::LuaHost*              s_luaHost;
    static Modules::Renderer*             s_renderer;
    static Presentation::EditorViewportService* s_viewport;
    /** Revision captured by the browser at pointer-event time (0 = use committed). */
    static uint64_t                       s_pointerPresentationRevision;
    /** Last SceneFrameSnapshot::sceneRevision (frame-boundary geometry authority). */
    static uint64_t                       s_sceneFrameRevision;
    static Modules::DialogManager*        s_dialogManager;
    static Modules::SpriteAnimator*        s_spriteAnimator;
    static Modules::Audio*                 s_audio;
    static Modules::VariableManager*       s_variables;
    static EditorProjectLoadedHandler     s_onProjectLoaded;
    static EditorPreviewRestoreHandler    s_onPreviewRestore;
    static EditorEnterPlayHandler         s_onEnterPlay;
    static EditorExitPlayHandler          s_onExitPlay;
    static SceneMutationApplyFn           s_applySceneMutation;
    static SceneMutationHandler           s_onSceneMutation;
    static SceneInvalidationQueueHandler  s_queueSceneInvalidations;
    static AuthoringSyncBatchHandler      s_onAuthoringSyncBatchBegin;
    static AuthoringSyncBatchHandler      s_onAuthoringSyncBatchEnd;
    static SceneMutationBatchOpenPredicate s_isSceneMutationBatchOpen;
    static std::vector<std::pair<std::string, std::string>> s_consoleQueue;

    // Native input callbacks -- bypass the JS thread entirely (Smoke Test 2)
    // Public because editor-input-controller.cpp registers them with the
    // emscripten event API; nobody else should be calling them directly.
    static EM_BOOL onMouseMove (int, const EmscriptenMouseEvent*,    void*);
    static EM_BOOL onMouseDown (int, const EmscriptenMouseEvent*,    void*);
    static EM_BOOL onMouseUp   (int, const EmscriptenMouseEvent*,    void*);
    static EM_BOOL onKeyDown   (int, const EmscriptenKeyboardEvent*, void*);
    static EM_BOOL onKeyUp     (int, const EmscriptenKeyboardEvent*, void*);
};

} // namespace ArtCade

// =============================================================================
// Editor-host -> C++ exported commands (called via Module.ccall()).
// =============================================================================
extern "C" {

/** 0 = editor mode (gizmos, grid)  |  1 = play / game mode */
EMSCRIPTEN_KEEPALIVE void editor_set_mode(int mode);

/** @return 1 when RuntimeEntityGateway is wired (editor_load_project is callable). */
EMSCRIPTEN_KEEPALIVE int editor_is_engine_wired(void);
/** @return UTF-8 init failure step, or "" when boot succeeded. */
EMSCRIPTEN_KEEPALIVE const char* editor_get_boot_failure(void);
/** @return main() exit code (-1 before run, 0 ok, non-zero init failure). */
EMSCRIPTEN_KEEPALIVE int editor_get_main_exit_code(void);

/** Programmatically select an entity from the Hierarchy panel. */
EMSCRIPTEN_KEEPALIVE void editor_select_entity(uint32_t entityId);

/** Clear the viewport selection. */
EMSCRIPTEN_KEEPALIVE void editor_deselect();

/**
 * Hot-reload project data from the editor host.
 * json_utf8: null-terminated UTF-8 JSON string (marshalled by wasm-bridge.ts).
 * Parses with nlohmann/json and calls SceneManager::registerScenes().
 * @return EditorApiResult (0 = ok).
 */
EMSCRIPTEN_KEEPALIVE int editor_load_project(const char* json_utf8);

/**
 * Preview STOP: reload ProjectDoc; Lua is not reset here — use
 * editor_exit_play_mode for atomic STOP or editor_reload_script after restore.
 */
EMSCRIPTEN_KEEPALIVE void editor_restore_from_project(const char* json_utf8);

/**
 * Atomic PLAY: parse project JSON, sync world for play, load dialogs + Lua, mode=1.
 * @return EditorApiResult (0 = ok).
 */
EMSCRIPTEN_KEEPALIVE int editor_enter_play_mode(
    const char* project_json,
    const char* lua_utf8,
    const char* dialogs_json);

/**
 * Atomic STOP: mode=0, restore design project, load design-time Lua.
 * @return EditorApiResult (0 = ok).
 */
EMSCRIPTEN_KEEPALIVE int editor_exit_play_mode(
    const char* project_json,
    const char* lua_utf8);

/**
 * Push a transform change from the Inspector into the C++ scene.
 * Routes the update through RuntimeEntityGateway::setTransform().
 */
EMSCRIPTEN_KEEPALIVE void editor_set_transform(
    uint32_t entityId,
    float x,      float y,
    float rotation,
    float scaleX, float scaleY);

/**
 * Incrementally update one entity from a JSON EntityDef blob (Inspector edits).
 * Does not clear the registry, unload textures, or reset Lua.
 */
EMSCRIPTEN_KEEPALIVE void editor_update_entity(
    uint32_t entityId, const char* json_utf8);

/**
 * Patch scene viewport/world/background for one scene without a full reload.
 * json_utf8: SceneDef subset (id, worldSize, viewportSize, backgroundColor).
 */
EMSCRIPTEN_KEEPALIVE void editor_set_scene_settings(
    const char* sceneId, const char* json_utf8);

/**
 * Coalesce incremental authoring sync (scene + entities + tilemap) into one
 * sceneRevision bump and one invalidation pass at end_authoring_sync_batch().
 */
EMSCRIPTEN_KEEPALIVE void editor_begin_authoring_sync_batch();
EMSCRIPTEN_KEEPALIVE void editor_end_authoring_sync_batch();

/**
 * Hot-reload game logic from the Logic Board editor.
 * lua_utf8: null-terminated UTF-8 Lua SOURCE (compiled by the editor's
 * compileLogicBoard()). Executed via LuaHost::loadLuaSource(), which
 * redefines the global tick(). On error the previous script stays active
 * and the message is pushed to the editor console.
 */
/** @return EditorApiResult (0 = ok). */
EMSCRIPTEN_KEEPALIVE int editor_reload_script(const char* lua_utf8);

/**
 * Hot-reload dialog graphs from the editor library (JSON array of graph objects).
 * Used in preview when dialogs/ is not on disk yet.
 */
EMSCRIPTEN_KEEPALIVE void editor_load_dialogs(const char* json_utf8);

/** Returns pointer to 6 floats: fps, luaMs, physicsMs, renderMs, entityCount, physicsBodies. */
EMSCRIPTEN_KEEPALIVE const float* editor_get_runtime_profile();
EMSCRIPTEN_KEEPALIVE const char* editor_get_variables_json(uint32_t entityId);


/** Editor viewport tool: 0 select, 1 pan. */
EMSCRIPTEN_KEEPALIVE void editor_set_tool(int toolId);

/** Toggle runtime-side editor guides (world bounds / viewport / grid). */
EMSCRIPTEN_KEEPALIVE void editor_set_guides_enabled(int enabled);

/** Editor-only guide/snap grid size in world pixels. Does not affect tilemap. */
EMSCRIPTEN_KEEPALIVE void editor_set_grid_size(float tileSize);

/**
 * Editor preview camera (screen-resolution rendering). The editor owns pan/zoom
 * via scroll + zoom factor and drives the runtime camera each frame:
 *   targetX/Y : world point shown at the canvas top-left corner.
 *   zoom      : device px per world unit (editorZoom × devicePixelRatio).
 *   vpW/vpH   : framebuffer size in DEVICE px (the visible canvas). The runtime
 *               resizes the framebuffer only when this changes, so the world is
 *               drawn at native resolution (crisp 1px grid at any zoom).
 */
EMSCRIPTEN_KEEPALIVE void editor_set_edit_camera(
    float targetX, float targetY, float zoom, int vpW, int vpH);

/** Phase 6: fixed-surface editor viewport — resize only (camera via ViewController). */
EMSCRIPTEN_KEEPALIVE void editor_resize_surface(
    float cssW, float cssH, float devicePixelRatio);

EMSCRIPTEN_KEEPALIVE void editor_begin_pan(float cssX, float cssY);
EMSCRIPTEN_KEEPALIVE void editor_update_pan(float cssX, float cssY);
EMSCRIPTEN_KEEPALIVE void editor_end_pan();

/** Cursor-anchored zoom; @p zoomFactor multiplies device-px-per-world zoom. */
EMSCRIPTEN_KEEPALIVE void editor_zoom_at(float cssX, float cssY, float zoomFactor);

EMSCRIPTEN_KEEPALIVE void editor_frame_world_bounds(
    float minX, float minY, float maxX, float maxY);

/** Frames the selected entity (F shortcut) via ViewController. */
EMSCRIPTEN_KEEPALIVE void editor_frame_selection(
    float posX, float posY, float scaleX, float scaleY);

/** Reads editor camera top-left world position and device-px-per-world zoom. */
EMSCRIPTEN_KEEPALIVE void editor_get_editor_view(
    float* outX, float* outY, float* outZoom);

EMSCRIPTEN_KEEPALIVE void editor_set_editor_view(
    float targetX, float targetY, float zoomDevicePx);

/** Committed presentation revision for the current frame (picking / overlay sync). */
EMSCRIPTEN_KEEPALIVE double editor_get_presentation_revision();
/** Committed scene revision from SceneFrameSnapshot (geometry authority for picking). */
EMSCRIPTEN_KEEPALIVE double editor_get_scene_revision();
EMSCRIPTEN_KEEPALIVE void editor_set_pointer_presentation_revision(double revision);

/** Committed presentation snapshot (flat ABI; static storage — Phase 5). */
EMSCRIPTEN_KEEPALIVE const ArtCade::Presentation::PresentationSnapshotWasm*
editor_get_presentation_snapshot();

/**
 * Maps framebuffer surface pixels to world using the committed presentation snapshot.
 * @param outWorldX/outWorldY must be non-null.
 */
EMSCRIPTEN_KEEPALIVE void editor_surface_to_world(
    float surfaceX, float surfaceY, float* outWorldX, float* outWorldY);

EMSCRIPTEN_KEEPALIVE void editor_surface_to_world_at_revision(
    float surfaceX,
    float surfaceY,
    double revision,
    float* outWorldX,
    float* outWorldY);

/**
 * Sync play-mode surface: CSS host size × DPR → framebuffer and compositor snapshot.
 * @param cssW/cssH host area in CSS pixels (not logical viewport size)
 */
EMSCRIPTEN_KEEPALIVE void editor_sync_play_surface(
    float cssW, float cssH, float devicePixelRatio);

/** Select play presentation variant (PlayEmbedded / PlayExternal / PlayFullscreen). */
EMSCRIPTEN_KEEPALIVE void editor_set_play_presentation(int mode);

/** Editor placement snap (magnetic drag + host commit); not gameplay grid.snapToGrid. */
EMSCRIPTEN_KEEPALIVE void editor_set_snap_to_grid(int enabled);

/**
 * Phase F3: upload an editor-loaded image (e.g. a tileset spritesheet not
 * present in the WASM VFS) into the renderer's texture cache under `path`
 * (must match TilesetAsset.spriteImagePath). `bytes` is the raw encoded
 * image file (PNG/JPG/...); `ext` is the file-type hint, e.g. ".png".
 */
EMSCRIPTEN_KEEPALIVE int editor_register_image(
    const char* path, const uint8_t* bytes, int len, const char* ext);

EMSCRIPTEN_KEEPALIVE void editor_register_audio(
    const char* path, const uint8_t* bytes, int len, const char* ext);

EMSCRIPTEN_KEEPALIVE void editor_register_font(
    const char* path, const uint8_t* bytes, int len, const char* ext, int baseSize);

EMSCRIPTEN_KEEPALIVE void editor_invalidate_asset(const char* assetKey, const char* type);

/**
 * Rebuild SpriteAnimator clips from a partial project JSON blob containing
 * `assets` (same shape as editor_load_project). Used when Spritesheet Studio
 * edits clips without a full project hot-sync.
 * @return 0 on success, negative on failure.
 */
EMSCRIPTEN_KEEPALIVE int editor_reregister_animation_clips(const char* json_utf8);

/** Stop the isolated Spritesheet Studio preview animator instance. */
EMSCRIPTEN_KEEPALIVE void editor_preview_spritesheet_reset();

/**
 * Queue one Spritesheet Studio preview frame (processed on the engine main loop).
 * @return 0 when queued, negative on invalid arguments.
 */
EMSCRIPTEN_KEEPALIVE int editor_preview_spritesheet_submit(
    const char* texturePath,
    const char* clipName,
    float dtSeconds,
    int canvasW,
    int canvasH);

} // extern "C"

// =============================================================================
// Native-build stubs (non-WASM platforms -- compiles but does nothing)
// =============================================================================
#else // !__EMSCRIPTEN__

namespace ArtCade {

struct EditorAPI {
    static void init(const char* = nullptr) {}
    static void shutdown() {}
    static void wireEngine(Modules::RuntimeEntityGateway*) {}
    static void wireLua(Modules::LuaHost*) {}
    static void wireRenderer(Modules::Renderer*) {}
    static void wireEditorViewport(Presentation::EditorViewportService*) {}
    static void commit_scene_frame(const EditorSceneFrameCommit&) {}
    static const std::unordered_map<std::string, SceneLayerSettings>&
    committed_scene_layer_settings() {
        static const std::unordered_map<std::string, SceneLayerSettings> empty;
        return empty;
    }
    static void wireDialog(Modules::DialogManager*) {}
    static void wireSpriteAnimator(Modules::SpriteAnimator*) {}
    static void wireAudio(Modules::Audio*) {}
    static void wireVariables(Modules::VariableManager*) {}
    static void setProjectLoadedHandler(EditorProjectLoadedHandler) {}
    static void setPreviewRestoreHandler(EditorPreviewRestoreHandler) {}
    static void setEnterPlayHandler(EditorEnterPlayHandler) {}
    static void setExitPlayHandler(EditorExitPlayHandler) {}
    static void setSceneMutationBridge(SceneMutationApplyFn, SceneMutationHandler) {}
    static void setSceneInvalidationQueueHandler(SceneInvalidationQueueHandler) {}
    static void setAuthoringSyncBatchHandlers(
        AuthoringSyncBatchHandler, AuthoringSyncBatchHandler) {}
    static void setSceneMutationBatchOpenPredicate(SceneMutationBatchOpenPredicate) {}
    static void notifyEntitySelected(uint32_t) {}
    static void notifyEntityDuplicateRequested(uint32_t, float, float) {}
    static void notifyTransformChanged(uint32_t, float, float, float, float, float) {}
    static void notifyTransformPreview(uint32_t, float, float, float, float, float) {}
    static void notifyConsoleLine(const char*, const char* = nullptr) {}
    static void notifyRuntimeProfile(float, float, float, float, uint32_t, uint32_t) {}
    static void queueConsoleLine(const char*, const char* = nullptr) {}
    static void flushConsoleLines() {}
    static void clearEngineWiring() {}
    static bool isEngineWired() { return false; }
    static void recordBootFailure(const char*) {}
    static const char* bootFailureStep() { return ""; }
    static void recordWasmMainExitCode(int) {}
    static int wasmMainExitCode() { return -1; }
    static void notifyCursorWorld(float, float) {}
    static void queueSpritesheetPreview(const char*, const char*, float, int, int) {}
    static void resetSpritesheetPreview() {}
    static void processSpritesheetPreviewQueue() {}
    static int      s_mode;
    static uint32_t s_selectedEntityId;
    static std::vector<uint32_t> s_selectedEntityIds;
    static bool     s_isDragging;
    static float    s_dragStartX, s_dragStartY;
    static ManipulationMode s_manipulationMode;
    static ResizeDragState  s_resizeDragState;
    static int      s_editorTool;
    static bool     s_editorGuidesEnabled;
    static float    s_editorGridSize;
    static bool     s_editorSnapEnabled;
    static bool     s_physicsDebugDraw;
    static void publishRuntimeProfile(float, float, float, float, uint32_t, uint32_t) {}
    static const float* runtimeProfileBuffer() {
        static float zeros[6] = {};
        return zeros;
    }
    static Modules::RuntimeEntityGateway* s_entityGateway;
    static Modules::LuaHost*              s_luaHost;
    static Modules::Renderer*             s_renderer;
    static Presentation::EditorViewportService* s_viewport;
    /** Revision captured by the browser at pointer-event time (0 = use committed). */
    static uint64_t                       s_pointerPresentationRevision;
    /** Last SceneFrameSnapshot::sceneRevision (frame-boundary geometry authority). */
    static uint64_t                       s_sceneFrameRevision;
    static Modules::DialogManager*        s_dialogManager;
    static Modules::SpriteAnimator*        s_spriteAnimator;
    static Modules::Audio*                 s_audio;
    static Modules::VariableManager*       s_variables;
    static EditorProjectLoadedHandler     s_onProjectLoaded;
    static EditorPreviewRestoreHandler    s_onPreviewRestore;
    static EditorEnterPlayHandler         s_onEnterPlay;
    static EditorExitPlayHandler          s_onExitPlay;
    static std::vector<std::pair<std::string, std::string>> s_consoleQueue;
};
} // namespace ArtCade

#endif // __EMSCRIPTEN__
