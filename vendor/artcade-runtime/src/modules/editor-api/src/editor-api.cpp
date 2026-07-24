#include "../include/editor-api.h"

// =============================================================================
// Native build: static member definitions only (all methods are no-ops in .h)
// =============================================================================
#ifndef __EMSCRIPTEN__

namespace ArtCade {
int      EditorAPI::s_mode             = 0;
uint32_t EditorAPI::s_selectedEntityId = 0u;
std::vector<uint32_t> EditorAPI::s_selectedEntityIds;
bool     EditorAPI::s_isDragging       = false;
float    EditorAPI::s_dragStartX       = 0.f;
float    EditorAPI::s_dragStartY       = 0.f;
ManipulationMode EditorAPI::s_manipulationMode = ManipulationMode::None;
ResizeDragState  EditorAPI::s_resizeDragState{};
int      EditorAPI::s_editorTool       = 0;
bool     EditorAPI::s_editorGuidesEnabled = true;
float    EditorAPI::s_editorGridSize   = 32.f;
bool     EditorAPI::s_editorSnapEnabled = false;
bool     EditorAPI::s_physicsDebugDraw  = false;
Modules::RuntimeEntityGateway* EditorAPI::s_entityGateway = nullptr;
Modules::LuaHost*              EditorAPI::s_luaHost       = nullptr;
Modules::Renderer*             EditorAPI::s_renderer      = nullptr;
Presentation::EditorViewportService* EditorAPI::s_viewport = nullptr;
uint64_t                       EditorAPI::s_pointerPresentationRevision = 0u;
uint64_t                       EditorAPI::s_sceneFrameRevision = 0u;
Modules::DialogManager*        EditorAPI::s_dialogManager = nullptr;
Modules::SpriteAnimator*       EditorAPI::s_spriteAnimator = nullptr;
Modules::Audio*                EditorAPI::s_audio = nullptr;
Modules::VariableManager*      EditorAPI::s_variables = nullptr;
EditorProjectLoadedHandler     EditorAPI::s_onProjectLoaded{};
EditorPreviewRestoreHandler    EditorAPI::s_onPreviewRestore{};
EditorEnterPlayHandler         EditorAPI::s_onEnterPlay{};
EditorExitPlayHandler          EditorAPI::s_onExitPlay{};
std::vector<std::pair<std::string, std::string>> EditorAPI::s_consoleQueue;

} // namespace ArtCade

#else // __EMSCRIPTEN__ ─────────────────────────────────────────────────────────

// =============================================================================
// WASM implementation
//
// Phase 5 split (docs/TECHNICAL_DEBT_REVIEW.md): this translation unit now
// only owns the static state, wiring, notifications and the
// EMSCRIPTEN_KEEPALIVE exports.
//
//  • editor-input-controller.cpp owns native mouse input (pickEntityAt / on{Mouse,Key}*).
//  • project-doc-parser.cpp turns the JSON blob from editor_load_project()
//    into EntityDef / SceneDef / TilesetAsset.
// =============================================================================

// editor-api intentionally does NOT include app.h. Project-loaded behaviour
// is delivered through a callback registered by Application via
// setProjectLoadedHandler(). Keeping this TU free of app.h removes a major
// circular dependency hazard and unblocks future module splits.
#include "../../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../../modules/lua-runtime/include/lua-host.h"
#include "../../../modules/renderer/include/renderer.h"
#include "../../../modules/presentation/include/presentation_bindings.h"
#include "../../../modules/presentation/include/presentation_snapshot.h"
#include "../../../modules/presentation/include/presentation_snapshot_wasm.h"
#include "../../../modules/presentation/include/presentation_mode.h"
#include "../../../modules/presentation/include/editor_viewport_navigation.h"
#include "../../../modules/presentation/include/editor_viewport_service.h"
#include "../../../modules/presentation/include/editor_zoom_policy.h"
#include "../../../modules/dialog/include/dialog-manager.h"
#include "../../../modules/dialog/include/dialog-parser.h"
#include "../../../modules/sprite-animator/include/sprite-animator.h"
#include "../../../modules/sprite-animator/include/animation-clips-registry.h"
#include "../../../modules/variable-manager/include/variable-manager.h"
#include "../../../modules/audio/include/audio.h"
#include "../../../modules/asset-system/include/asset-manifest-index.h"
#include "../../../core/types.h"
#include "../../../core/tilemap_grid.h"
#include "../../../core/scene-json.h"
#include "../../../core/project-meta-json.h"
#include "../../../core/project-current-format.h"

#include "editor-input-controller.h"
#include "editor-spritesheet-preview.h"
#include "project-doc-parser.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;

namespace {

ArtCade::Modules::AssetManifestIndex s_editorAssetManifest;

std::string resolveImageLoadKeyFromDoc(const json& doc, const std::string& raw) {
    if (raw.empty()) return {};
    if (doc.contains("assets") && doc["assets"].is_object()) {
        if (doc["assets"].contains(raw)) {
            const auto& av = doc["assets"][raw];
            if (av.is_object()) {
                const std::string p = av.value("path", std::string{});
                if (!p.empty()) return p;
            }
        }
        for (auto& [key, av] : doc["assets"].items()) {
            if (!av.is_object()) continue;
            if (av.value("id", key) == raw) {
                const std::string p = av.value("path", std::string{});
                if (!p.empty()) return p;
            }
        }
    }
    return raw;
}

void rebuildEditorAssetManifest(const json& doc) {
    s_editorAssetManifest.clear();
    if (doc.contains("imageAssets") && doc["imageAssets"].is_array()) {
        for (const auto& av : doc["imageAssets"]) {
            if (!av.is_object()) continue;
            const std::string id = av.value("assetId", av.value("id", std::string{}));
            const std::string path = av.value(
                "sourcePath", av.value("relativePath", av.value("path", std::string{})));
            if (!id.empty() && !path.empty()) s_editorAssetManifest.addImageEntry(id, path);
        }
    }
    if (doc.contains("assets") && doc["assets"].is_object()) {
        for (auto& [key, av] : doc["assets"].items()) {
            if (!av.is_object()) continue;
            const std::string id   = av.value("id", key);
            const std::string path = av.value("path", std::string{});
            if (!path.empty()) s_editorAssetManifest.addImageEntry(id, path);
        }
    }
    if (doc.contains("tilesets") && doc["tilesets"].is_object()) {
        for (auto& [key, tv] : doc["tilesets"].items()) {
            if (!tv.is_object()) continue;
            const std::string id  = tv.value("id", key);
            const std::string raw = tv.value("spriteImagePath", std::string{});
            const std::string path = resolveImageLoadKeyFromDoc(doc, raw);
            if (!path.empty()) s_editorAssetManifest.addImageEntry(id, path);
        }
    }
    if (doc.contains("audioAssets") && doc["audioAssets"].is_object()) {
        for (auto& [key, av] : doc["audioAssets"].items()) {
            if (!av.is_object()) continue;
            const std::string id   = av.value("id", key);
            const std::string path = av.value("path", std::string{});
            if (!path.empty()) s_editorAssetManifest.addAudioEntry(id, path);
        }
    }
    if (doc.contains("audioAssets") && doc["audioAssets"].is_array()) {
        for (const auto& av : doc["audioAssets"]) {
            if (!av.is_object()) continue;
            const std::string id = av.value("assetId", av.value("id", std::string{}));
            const std::string path = av.value(
                "sourcePath", av.value("relativePath", av.value("path", std::string{})));
            if (!id.empty() && !path.empty()) s_editorAssetManifest.addAudioEntry(id, path);
        }
    }
    if (doc.contains("fontAssets") && doc["fontAssets"].is_object()) {
        for (auto& [key, av] : doc["fontAssets"].items()) {
            if (!av.is_object()) continue;
            const std::string id   = av.value("id", key);
            const std::string path = av.value("path", std::string{});
            if (!path.empty()) s_editorAssetManifest.addFontEntry(id, path);
        }
    }
}

} // namespace

namespace ArtCade {

// ── Static state ──────────────────────────────────────────────────────────────
int      EditorAPI::s_mode             = 0;
uint32_t EditorAPI::s_selectedEntityId = 0u;
std::vector<uint32_t> EditorAPI::s_selectedEntityIds;
bool     EditorAPI::s_isDragging       = false;
float    EditorAPI::s_dragStartX       = 0.f;
float    EditorAPI::s_dragStartY       = 0.f;
ManipulationMode EditorAPI::s_manipulationMode = ManipulationMode::None;
ResizeDragState  EditorAPI::s_resizeDragState{};
int      EditorAPI::s_editorTool       = 0;
bool     EditorAPI::s_editorGuidesEnabled = true;
float    EditorAPI::s_editorGridSize   = 32.f;
bool     EditorAPI::s_editorSnapEnabled = false;
bool     EditorAPI::s_physicsDebugDraw  = false;

Modules::RuntimeEntityGateway* EditorAPI::s_entityGateway = nullptr;
Modules::LuaHost*              EditorAPI::s_luaHost       = nullptr;
Modules::Renderer*             EditorAPI::s_renderer      = nullptr;
Presentation::EditorViewportService* EditorAPI::s_viewport = nullptr;
uint64_t                       EditorAPI::s_pointerPresentationRevision = 0u;
uint64_t                       EditorAPI::s_sceneFrameRevision = 0u;
Modules::DialogManager*        EditorAPI::s_dialogManager = nullptr;
Modules::SpriteAnimator*       EditorAPI::s_spriteAnimator = nullptr;
Modules::Audio*                EditorAPI::s_audio = nullptr;
Modules::VariableManager*      EditorAPI::s_variables = nullptr;
EditorProjectLoadedHandler     EditorAPI::s_onProjectLoaded{};
EditorPreviewRestoreHandler    EditorAPI::s_onPreviewRestore{};
EditorEnterPlayHandler         EditorAPI::s_onEnterPlay{};
EditorExitPlayHandler          EditorAPI::s_onExitPlay{};
SceneMutationApplyFn           EditorAPI::s_applySceneMutation{};
SceneMutationHandler           EditorAPI::s_onSceneMutation{};
SceneInvalidationQueueHandler  EditorAPI::s_queueSceneInvalidations{};
AuthoringSyncBatchHandler      EditorAPI::s_onAuthoringSyncBatchBegin{};
AuthoringSyncBatchHandler      EditorAPI::s_onAuthoringSyncBatchEnd{};
SceneMutationBatchOpenPredicate EditorAPI::s_isSceneMutationBatchOpen{};
std::vector<std::pair<std::string, std::string>> EditorAPI::s_consoleQueue;

namespace {
std::string g_wasm_boot_failure_owned;
const char* g_wasm_boot_failure_step = nullptr;
/** -2 until Application::run records 0 (ok) or 1 (init failed). */
int g_wasm_main_exit_code = -2;
} // namespace

Presentation::PresentationMode s_playPresentationMode =
    Presentation::PresentationMode::PlayEmbedded;

Presentation::PresentationMode parse_play_presentation_mode(int mode) {
    using Presentation::PresentationMode;
    if (mode < static_cast<int>(PresentationMode::PlayEmbedded)
        || mode > static_cast<int>(PresentationMode::PlayFullscreen)) {
        return PresentationMode::PlayEmbedded;
    }
    return static_cast<PresentationMode>(mode);
}

namespace {
float s_runtimeProfile[6] = {};
} // namespace

void EditorAPI::publishRuntimeProfile(const float fps,
                                      const float luaMs,
                                      const float physicsMs,
                                      const float renderMs,
                                      const uint32_t entityCount,
                                      const uint32_t physicsBodies) {
    s_runtimeProfile[0] = fps;
    s_runtimeProfile[1] = luaMs;
    s_runtimeProfile[2] = physicsMs;
    s_runtimeProfile[3] = renderMs;
    s_runtimeProfile[4] = static_cast<float>(entityCount);
    s_runtimeProfile[5] = static_cast<float>(physicsBodies);
}

const float* EditorAPI::runtimeProfileBuffer() {
    return s_runtimeProfile;
}

// ── Engine wiring ─────────────────────────────────────────────────────────────
void EditorAPI::wireEngine(Modules::RuntimeEntityGateway* gateway) {
    s_entityGateway = gateway;
    if (gateway) {
        gateway->setSpawnLogCallback([](const std::string& msg) {
            EditorAPI::queueConsoleLine(msg.c_str(), "info");
        });
    }
    notifyConsoleLine("[EditorAPI] Engine wired to RuntimeEntityGateway.", "info");
}

void EditorAPI::wireLua(Modules::LuaHost* luaHost) {
    s_luaHost = luaHost;
    notifyConsoleLine("[EditorAPI] Engine wired to LuaHost (hot-reload ready).", "info");
}

void EditorAPI::wireRenderer(Modules::Renderer* renderer) {
    s_renderer = renderer;
    if (renderer) {
        renderer->setTextureKeyResolver([](const std::string& ref) {
            return s_editorAssetManifest.resolveImageKey(ref);
        });
        renderer->setFontKeyResolver([](const std::string& ref) {
            return s_editorAssetManifest.resolveFontKey(ref);
        });
    }
    notifyConsoleLine("[EditorAPI] Engine wired to Renderer (image upload ready).", "info");
}

void EditorAPI::wireEditorViewport(Presentation::EditorViewportService* viewport) {
    s_viewport = viewport;
}

namespace {
std::unordered_map<std::string, SceneLayerSettings> s_committedSceneLayerSettings;
} // namespace

void EditorAPI::commit_scene_frame(const EditorSceneFrameCommit& frame) {
    s_sceneFrameRevision = frame.sceneRevision;
    s_committedSceneLayerSettings = frame.layerSettings;
}

const std::unordered_map<std::string, SceneLayerSettings>&
EditorAPI::committed_scene_layer_settings() {
    return s_committedSceneLayerSettings;
}

void EditorAPI::wireSpriteAnimator(Modules::SpriteAnimator* spriteAnimator) {
    s_spriteAnimator = spriteAnimator;
}

void EditorAPI::wireAudio(Modules::Audio* audio) {
    s_audio = audio;
    if (audio) {
        audio->setAssetPathResolver([](const std::string& ref) {
            return s_editorAssetManifest.resolveAudioKey(ref);
        });
    }
}

void EditorAPI::wireVariables(Modules::VariableManager* variables) {
    s_variables = variables;
}

void EditorAPI::wireDialog(Modules::DialogManager* dialogManager) {
    s_dialogManager = dialogManager;
    notifyConsoleLine("[EditorAPI] Engine wired to DialogManager.", "info");
}

void EditorAPI::setProjectLoadedHandler(EditorProjectLoadedHandler handler) {
    s_onProjectLoaded = std::move(handler);
}

void EditorAPI::setPreviewRestoreHandler(EditorPreviewRestoreHandler handler) {
    s_onPreviewRestore = std::move(handler);
}

void EditorAPI::setEnterPlayHandler(EditorEnterPlayHandler handler) {
    s_onEnterPlay = std::move(handler);
}

void EditorAPI::setExitPlayHandler(EditorExitPlayHandler handler) {
    s_onExitPlay = std::move(handler);
}

void EditorAPI::setSceneMutationBridge(
    SceneMutationApplyFn apply, SceneMutationHandler onResult) {
    s_applySceneMutation = std::move(apply);
    s_onSceneMutation = std::move(onResult);
}

void EditorAPI::setSceneInvalidationQueueHandler(
    SceneInvalidationQueueHandler handler) {
    s_queueSceneInvalidations = std::move(handler);
}

void EditorAPI::setAuthoringSyncBatchHandlers(
    AuthoringSyncBatchHandler onBegin, AuthoringSyncBatchHandler onEnd) {
    s_onAuthoringSyncBatchBegin = std::move(onBegin);
    s_onAuthoringSyncBatchEnd = std::move(onEnd);
}

void EditorAPI::setSceneMutationBatchOpenPredicate(
    SceneMutationBatchOpenPredicate predicate) {
    s_isSceneMutationBatchOpen = std::move(predicate);
}

// ── Init / Shutdown ───────────────────────────────────────────────────────────
void EditorAPI::init(const char* canvasSelector) {
    EditorInputController::initCanvas(canvasSelector);
    // Queue — onConsoleLine may not be wired yet during C++ main(); flushed on first frame.
    queueConsoleLine("[EditorAPI] Bridge initialised -- native input active.", "info");
}

void EditorAPI::shutdown() {
    EditorInputController::shutdownCanvas();
}

// ── C++ -> editor-host notifications ──────────────────────────────────────────
// EM_ASM calls window.on* globals that wasm-bridge.ts sets BEFORE game.js loads.

void EditorAPI::notifyEntitySelected(uint32_t entityId) {
    EM_ASM({
        if (typeof window.onEntitySelected === 'function')
            window.onEntitySelected($0);
    }, static_cast<int>(entityId));
}

void EditorAPI::notifyEntityDuplicateRequested(uint32_t entityId, float x, float y) {
    EM_ASM({
        if (typeof window.onEntityDuplicateRequested === 'function')
            window.onEntityDuplicateRequested($0, $1, $2);
    }, static_cast<int>(entityId), x, y);
}

void EditorAPI::notifyTransformChanged(uint32_t entityId,
    float x, float y, float rotation, float scaleX, float scaleY)
{
    EM_ASM({
        if (typeof window.onEntityTransformChanged === 'function')
            window.onEntityTransformChanged($0, $1, $2, $3, $4, $5);
    }, static_cast<int>(entityId), x, y, rotation, scaleX, scaleY);
}

void EditorAPI::notifyTransformPreview(uint32_t entityId,
    float x, float y, float rotation, float scaleX, float scaleY)
{
    EM_ASM({
        if (typeof window.onEntityTransformPreview === 'function')
            window.onEntityTransformPreview($0, $1, $2, $3, $4, $5);
    }, static_cast<int>(entityId), x, y, rotation, scaleX, scaleY);
}

void EditorAPI::notifyConsoleLine(const char* message, const char* level) {
    EM_ASM({
        var msg = UTF8ToString($0);
        var lvl = UTF8ToString($1);
        if (typeof window.onConsoleLine === 'function')
            window.onConsoleLine(msg, lvl);
    }, message, level);
}

void EditorAPI::notifyRuntimeProfile(const float fps,
                                    const float luaMs,
                                    const float physicsMs,
                                    const float renderMs,
                                    const uint32_t entityCount,
                                    const uint32_t physicsBodies) {
    EM_ASM({
        if (typeof window.onRuntimeProfile === 'function')
            window.onRuntimeProfile($0, $1, $2, $3, $4, $5);
    }, fps, luaMs, physicsMs, renderMs,
       static_cast<int>(entityCount), static_cast<int>(physicsBodies));
}

void EditorAPI::notifyCursorWorld(float x, float y) {
    static int lastX = 0x7fffffff;
    static int lastY = 0x7fffffff;
    const int ix = static_cast<int>(std::lround(x));
    const int iy = static_cast<int>(std::lround(y));
    if (ix == lastX && iy == lastY) return;
    lastX = ix;
    lastY = iy;
    EM_ASM({
        if (typeof window.onEditorCursorWorld === 'function')
            window.onEditorCursorWorld($0, $1);
    }, ix, iy);
}

void EditorAPI::queueConsoleLine(const char* message, const char* level) {
    s_consoleQueue.emplace_back(message ? message : "", level ? level : "info");
}

void EditorAPI::flushConsoleLines() {
    for (const auto& [message, level] : s_consoleQueue)
        notifyConsoleLine(message.c_str(), level.c_str());
    s_consoleQueue.clear();
}

void EditorAPI::clearEngineWiring() {
    s_entityGateway = nullptr;
    s_luaHost = nullptr;
    s_renderer = nullptr;
    s_viewport = nullptr;
    s_dialogManager = nullptr;
    s_spriteAnimator = nullptr;
    s_audio = nullptr;
    s_variables = nullptr;
}

bool EditorAPI::isEngineWired() {
    return s_entityGateway != nullptr;
}

void EditorAPI::recordBootFailure(const char* step) {
    if (!step || !*step) step = "unknown";
    g_wasm_boot_failure_owned = step;
    g_wasm_boot_failure_step = g_wasm_boot_failure_owned.c_str();
    char buf[160];
    std::snprintf(buf, sizeof(buf), "[App] init failed: %s", step);
    queueConsoleLine(buf, "error");
}

const char* EditorAPI::bootFailureStep() {
    return g_wasm_boot_failure_step ? g_wasm_boot_failure_step : "";
}

void EditorAPI::recordWasmMainExitCode(int code) {
    g_wasm_main_exit_code = code;
}

int EditorAPI::wasmMainExitCode() {
    return g_wasm_main_exit_code;
}

} // namespace ArtCade

namespace {

// Tool ids accepted by editor_set_tool(); kept in this TU to validate
// incoming values without exposing the enum from the input controller.
constexpr int kEditorToolSelect = 0;
constexpr int kEditorToolPan    = 1;

void editor_sync_viewport_pending() {
    auto* vp = ArtCade::EditorAPI::s_viewport;
    auto* r = ArtCade::EditorAPI::s_renderer;
    auto* gw = ArtCade::EditorAPI::s_entityGateway;
    if (!vp || !r) return;
    const ArtCade::SceneDef* scene = gw ? gw->activeScene() : nullptr;
    vp->sync_from_scene(
        scene,
        r->gatherSimulationPresentationInputs(),
        r->windowWidth(),
        r->windowHeight());
    vp->refresh_pending_snapshot();
}

void editor_nav_prepare() {
    auto* vp = ArtCade::EditorAPI::s_viewport;
    auto* r = ArtCade::EditorAPI::s_renderer;
    if (!vp || !r) return;
    vp->navigation_prepare(r->windowWidth(), r->windowHeight());
}

void editor_nav_commit() {
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp) return;
    vp->navigation_commit();
}

void editor_enter_scene_edit_if_needed() {
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp || vp->scene_edit_active()) return;
    vp->host().enter_scene_edit();
    vp->set_presentation_mode(ArtCade::Presentation::PresentationMode::SceneEdit);
}

} // namespace

// =============================================================================
// Editor-host -> C++ exported commands
// =============================================================================

extern "C" {

EMSCRIPTEN_KEEPALIVE void editor_set_play_presentation(int mode) {
    ArtCade::s_playPresentationMode = ArtCade::parse_play_presentation_mode(mode);
    if (ArtCade::EditorAPI::s_mode != 1)
        return;
    if (auto* vp = ArtCade::EditorAPI::s_viewport)
        vp->set_presentation_mode(ArtCade::s_playPresentationMode);
}

EMSCRIPTEN_KEEPALIVE int editor_is_engine_wired(void) {
    return ArtCade::EditorAPI::isEngineWired() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* editor_get_boot_failure(void) {
    return ArtCade::EditorAPI::bootFailureStep();
}

EMSCRIPTEN_KEEPALIVE int editor_get_main_exit_code(void) {
    return ArtCade::EditorAPI::wasmMainExitCode();
}

EMSCRIPTEN_KEEPALIVE void editor_set_mode(int mode) {
    ArtCade::EditorAPI::s_mode = mode;
    if (auto* vp = ArtCade::EditorAPI::s_viewport) {
        vp->set_presentation_mode(mode == 0
            ? ArtCade::Presentation::PresentationMode::SceneEdit
            : ArtCade::s_playPresentationMode);
    }
    if (auto* r = ArtCade::EditorAPI::s_renderer) {
        // Play uses the game-view compositor; edit draws directly to the surface.
        r->setGameViewCompositorEnabled(mode != 0);
    }
    if (auto* gw = ArtCade::EditorAPI::s_entityGateway) {
        if (mode == 1) gw->applyDesignVisibilityForPlay();
        else           gw->restoreDesignVisibilityForEdit();
    }
    ArtCade::EditorAPI::notifyConsoleLine(
        mode == 0 ? "[EditorAPI] Mode: EDIT" : "[EditorAPI] Mode: PLAY", "info");
}

EMSCRIPTEN_KEEPALIVE void editor_select_entity(uint32_t entityId) {
    ArtCade::EditorAPI::s_selectedEntityId = entityId;
    ArtCade::EditorAPI::s_selectedEntityIds =
        entityId != 0u ? std::vector<uint32_t>{ entityId } : std::vector<uint32_t>{};
}

EMSCRIPTEN_KEEPALIVE void editor_select_entities(const char* csv) {
    std::vector<uint32_t> ids;
    uint32_t value = 0u;
    bool reading = false;
    for (const char* p = csv; p && *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            value = value * 10u + static_cast<uint32_t>(*p - '0');
            reading = true;
            continue;
        }
        if (reading && value != 0u)
            ids.push_back(value);
        value = 0u;
        reading = false;
    }
    if (reading && value != 0u)
        ids.push_back(value);
    ArtCade::EditorAPI::s_selectedEntityIds = ids;
    ArtCade::EditorAPI::s_selectedEntityId = ids.empty() ? 0u : ids.back();
}

// Direct single-cell write — no texture eviction, no echo.
// When @p layerName is set, updates that layer grid and recomposites merged tilemap.

// Full per-layer tilemap resync — no texture eviction, no full project reload.
// JSON: { "layerIds": [...], "tilemapLayers": { layerId: {tileSize,cols,rows,data,...} },
//         "mergedData": [...] }

// Full tilemap data resync — no texture eviction, no full project reload.
// Accepts a JSON array of integers matching the active scene's tilemap size.
// Used by the JS sync path when only tilemap.data changed (paint or undo).

EMSCRIPTEN_KEEPALIVE void editor_set_tool(int toolId) {
    if (toolId < kEditorToolSelect || toolId > kEditorToolPan)
        toolId = kEditorToolSelect;
    ArtCade::EditorAPI::s_editorTool = toolId;
}

EMSCRIPTEN_KEEPALIVE void editor_set_guides_enabled(int enabled) {
    ArtCade::EditorAPI::s_editorGuidesEnabled = (enabled != 0);
}

EMSCRIPTEN_KEEPALIVE void editor_set_grid_size(float tileSize) {
    ArtCade::EditorAPI::s_editorGridSize = tileSize >= 4.f ? tileSize : 32.f;
}

EMSCRIPTEN_KEEPALIVE void editor_set_snap_to_grid(int enabled) {
    ArtCade::EditorAPI::s_editorSnapEnabled = (enabled != 0);
}

EMSCRIPTEN_KEEPALIVE void editor_set_edit_camera(
    float targetX, float targetY, float zoom, int vpW, int vpH) {
    if (ArtCade::EditorAPI::s_mode != 0) return; // edit mode only
    auto* vp = ArtCade::EditorAPI::s_viewport;
    auto* r = ArtCade::EditorAPI::s_renderer;
    if (!vp || !r) return;
    // Resize the framebuffer to the visible canvas (device px) only when it
    // actually changed — comparing against the live size avoids redundant
    // SetWindowSize calls (which restripe the canvas CSS) on every pan/zoom.
    if (vpW > 0 && vpH > 0
        && (static_cast<uint32_t>(vpW) != r->windowWidth()
            || static_cast<uint32_t>(vpH) != r->windowHeight())) {
        r->setWindowSize(static_cast<uint32_t>(vpW),
                         static_cast<uint32_t>(vpH), "ArtCade V2");
    }
    vp->set_editor_camera(
        static_cast<double>(targetX),
        static_cast<double>(targetY),
        static_cast<double>(zoom));
    editor_sync_viewport_pending();
}

EMSCRIPTEN_KEEPALIVE void editor_resize_surface(
    float cssW, float cssH, float devicePixelRatio) {
    if (ArtCade::EditorAPI::s_mode != 0) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    auto* r = ArtCade::EditorAPI::s_renderer;
    if (!vp || !r) return;
    const float safeDpr = devicePixelRatio > 0.f ? devicePixelRatio : 1.f;
    const uint32_t fbW = static_cast<uint32_t>(
        std::max(1., std::round(static_cast<double>(cssW) * static_cast<double>(safeDpr))));
    const uint32_t fbH = static_cast<uint32_t>(
        std::max(1., std::round(static_cast<double>(cssH) * static_cast<double>(safeDpr))));
    if (fbW != r->windowWidth() || fbH != r->windowHeight()) {
        r->setWindowSize(fbW, fbH, "ArtCade V2");
    }
    vp->resize_editor_surface(
        static_cast<double>(cssW),
        static_cast<double>(cssH),
        static_cast<double>(safeDpr),
        fbW,
        fbH);
    editor_sync_viewport_pending();
    // Commit presentation immediately so picking/overlays match the new FB size
    // without waiting for the next render-frame begin_frame().
    vp->begin_frame();
}

EMSCRIPTEN_KEEPALIVE void editor_begin_pan(float cssX, float cssY) {
    if (ArtCade::EditorAPI::s_mode != 0) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp || !vp->scene_edit_active()) return;
    editor_nav_prepare();
    const auto surface = ArtCade::Presentation::editor_viewport_css_to_surface(
        cssX, cssY, vp->host().editorSurfaceDpr);
    vp->controller().begin_pan(surface);
    editor_nav_commit();
    editor_sync_viewport_pending();
}

EMSCRIPTEN_KEEPALIVE void editor_update_pan(float cssX, float cssY) {
    if (ArtCade::EditorAPI::s_mode != 0) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp || !vp->scene_edit_active()) return;
    const auto surface = ArtCade::Presentation::editor_viewport_css_to_surface(
        cssX, cssY, vp->host().editorSurfaceDpr);
    vp->controller().update_pan(surface);
    editor_nav_commit();
    editor_sync_viewport_pending();
}

EMSCRIPTEN_KEEPALIVE void editor_end_pan() {
    if (ArtCade::EditorAPI::s_mode != 0) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp) return;
    vp->controller().end_pan();
}

EMSCRIPTEN_KEEPALIVE void editor_zoom_at(
    float cssX, float cssY, float zoomFactor) {
    if (ArtCade::EditorAPI::s_mode != 0) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp || !vp->scene_edit_active() || !(zoomFactor > 0.f)) return;
    editor_nav_prepare();
    const auto surface = ArtCade::Presentation::editor_viewport_css_to_surface(
        cssX, cssY, vp->host().editorSurfaceDpr);
    vp->controller().zoom_at(surface, static_cast<double>(zoomFactor));
    editor_nav_commit();
    editor_sync_viewport_pending();
}

EMSCRIPTEN_KEEPALIVE void editor_frame_world_bounds(
    float minX, float minY, float maxX, float maxY) {
    if (ArtCade::EditorAPI::s_mode != 0) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp) return;
    editor_enter_scene_edit_if_needed();
    editor_nav_prepare();
    vp->controller().frame_world_bounds(
        static_cast<double>(minX),
        static_cast<double>(minY),
        static_cast<double>(maxX),
        static_cast<double>(maxY));
    editor_nav_commit();
    editor_sync_viewport_pending();
}

EMSCRIPTEN_KEEPALIVE void editor_frame_selection(
    float posX, float posY, float scaleX, float scaleY) {
    if (ArtCade::EditorAPI::s_mode != 0) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp) return;
    editor_enter_scene_edit_if_needed();
    editor_nav_prepare();
    vp->controller().frame_selection_at(
        static_cast<double>(posX),
        static_cast<double>(posY),
        static_cast<double>(scaleX),
        static_cast<double>(scaleY),
        ArtCade::Presentation::kEditorCanvasPaddingPx);
    editor_nav_commit();
    editor_sync_viewport_pending();
}

EMSCRIPTEN_KEEPALIVE void editor_get_editor_view(
    float* outX, float* outY, float* outZoom) {
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp) {
        if (outX) *outX = 0.f;
        if (outY) *outY = 0.f;
        if (outZoom) *outZoom = 1.f;
        return;
    }
    const auto& camera = vp->host().editorCamera;
    if (outX)
        *outX = static_cast<float>(camera.positionX);
    if (outY)
        *outY = static_cast<float>(camera.positionY);
    if (outZoom) {
        const double zoom = camera.zoom > 0. ? camera.zoom : 1.;
        *outZoom = static_cast<float>(zoom);
    }
}

EMSCRIPTEN_KEEPALIVE void editor_set_editor_view(
    float targetX, float targetY, float zoomDevicePx) {
    if (ArtCade::EditorAPI::s_mode != 0) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp) return;
    vp->set_editor_camera(
        static_cast<double>(targetX),
        static_cast<double>(targetY),
        static_cast<double>(zoomDevicePx));
    if (auto* r = ArtCade::EditorAPI::s_renderer) {
        vp->navigation_prepare(r->windowWidth(), r->windowHeight());
        vp->navigation_commit();
    }
    editor_sync_viewport_pending();
}

EMSCRIPTEN_KEEPALIVE double editor_get_presentation_revision() {
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp) return 0.;
    return static_cast<double>(vp->presentation_revision());
}

EMSCRIPTEN_KEEPALIVE double editor_get_scene_revision() {
    return static_cast<double>(ArtCade::EditorAPI::s_sceneFrameRevision);
}

EMSCRIPTEN_KEEPALIVE void editor_set_pointer_presentation_revision(double revision) {
    ArtCade::EditorAPI::s_pointerPresentationRevision =
        revision > 0. ? static_cast<uint64_t>(revision) : 0u;
}

EMSCRIPTEN_KEEPALIVE const ArtCade::Presentation::PresentationSnapshotWasm*
editor_get_presentation_snapshot() {
    static ArtCade::Presentation::PresentationSnapshotWasm abi{};
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp) {
        abi = {};
        return &abi;
    }
    abi = ArtCade::Presentation::snapshot_to_wasm(vp->committed_snapshot());
    return &abi;
}

EMSCRIPTEN_KEEPALIVE void editor_surface_to_world(
    float surfaceX, float surfaceY, float* outWorldX, float* outWorldY) {
    if (!outWorldX || !outWorldY) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    if (!vp) {
        *outWorldX = surfaceX;
        *outWorldY = surfaceY;
        return;
    }
    const ArtCade::Presentation::WorldPoint world =
        ArtCade::Presentation::PresentationBindings::surface_to_world(
            vp->committed_snapshot(),
            ArtCade::Presentation::SurfacePoint{ surfaceX, surfaceY });
    *outWorldX = static_cast<float>(world.x);
    *outWorldY = static_cast<float>(world.y);
}

EMSCRIPTEN_KEEPALIVE void editor_surface_to_world_at_revision(
    float surfaceX,
    float surfaceY,
    double revision,
    float* outWorldX,
    float* outWorldY) {
    if (!outWorldX || !outWorldY) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    auto* r = ArtCade::EditorAPI::s_renderer;
    if (!vp || !r) {
        *outWorldX = surfaceX;
        *outWorldY = surfaceY;
        return;
    }
    const uint64_t committedRevision =
        revision > 0. ? static_cast<uint64_t>(revision) : 0u;
    const ArtCade::Presentation::WorldPoint world =
        ArtCade::Presentation::PresentationBindings::surface_to_world(
            vp->controller().presentation(),
            committedRevision,
            ArtCade::Presentation::SurfacePoint{ surfaceX, surfaceY });
    *outWorldX = static_cast<float>(world.x);
    *outWorldY = static_cast<float>(world.y);
}

EMSCRIPTEN_KEEPALIVE void editor_sync_play_surface(
    float cssW, float cssH, float devicePixelRatio) {
    if (ArtCade::EditorAPI::s_mode != 1) return;
    auto* vp = ArtCade::EditorAPI::s_viewport;
    auto* r = ArtCade::EditorAPI::s_renderer;
    if (!vp || !r || cssW <= 0.f || cssH <= 0.f) return;
    r->resizePlayFramebuffer(cssW, cssH, devicePixelRatio);
    vp->sync_play_surface(
        static_cast<double>(cssW),
        static_cast<double>(cssH),
        static_cast<double>(devicePixelRatio > 0.f ? devicePixelRatio : 1.f),
        r->windowWidth(),
        r->windowHeight());
}

EMSCRIPTEN_KEEPALIVE int editor_register_image(
    const char* path, const uint8_t* bytes, int len, const char* ext) {
    if (!path || !*path || !bytes || len <= 0) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] editor_register_image: invalid arguments.", "warn");
        return 0;
    }
    auto* r = ArtCade::EditorAPI::s_renderer;
    if (!r) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] editor_register_image: Renderer not wired yet.", "warn");
        return 0;
    }
    const std::string fileExt = (ext && *ext) ? ext : ".png";
    const bool ok = r->registerImageFromMemory(
        path, reinterpret_cast<const unsigned char*>(bytes), len, fileExt);
    if (ok) {
        std::string msg = "[EditorAPI] Tileset image uploaded: ";
        msg += path;
        ArtCade::EditorAPI::notifyConsoleLine(msg.c_str(), "info");
        return 1;
    }
    std::string msg = "[EditorAPI] Failed to decode image: ";
    msg += path;
    ArtCade::EditorAPI::notifyConsoleLine(msg.c_str(), "error");
    return 0;
}

EMSCRIPTEN_KEEPALIVE void editor_register_audio(
    const char* path, const uint8_t* bytes, int len, const char* ext) {
    if (!path || !*path || !bytes || len <= 0) return;
    auto* a = ArtCade::EditorAPI::s_audio;
    if (!a) return;
    const std::string fileExt = (ext && *ext) ? ext : ".ogg";
    (void)a->registerSoundFromMemory(
        path, reinterpret_cast<const unsigned char*>(bytes), len, fileExt);
}

EMSCRIPTEN_KEEPALIVE void editor_invalidate_asset(
    const char* assetKey, const char* type) {
    if (!assetKey || !*assetKey || !type || !*type) return;
    const std::string key(assetKey);
    const std::string kind(type);
    if (kind == "image") {
        if (auto* r = ArtCade::EditorAPI::s_renderer)
            r->invalidateImageAsset(key);
    } else if (kind == "audio") {
        if (auto* a = ArtCade::EditorAPI::s_audio)
            a->invalidateSound(key);
    } else if (kind == "font") {
        if (auto* r = ArtCade::EditorAPI::s_renderer)
            r->invalidateFontAsset(key);
    }
}

EMSCRIPTEN_KEEPALIVE void editor_register_font(
    const char* path, const uint8_t* bytes, int len, const char* ext, int baseSize) {
    if (!path || !*path || !bytes || len <= 0) return;
    auto* r = ArtCade::EditorAPI::s_renderer;
    if (!r) return;
    const std::string fileExt = (ext && *ext) ? ext : ".ttf";
    const int size = baseSize > 0 ? baseSize : 32;
    (void)r->registerFontFromMemory(
        path, reinterpret_cast<const unsigned char*>(bytes), len, fileExt, size);
}

EMSCRIPTEN_KEEPALIVE void editor_deselect() {
    ArtCade::EditorAPI::s_selectedEntityId = 0u;
    ArtCade::EditorAPI::s_selectedEntityIds.clear();
}

EMSCRIPTEN_KEEPALIVE int editor_reregister_animation_clips(const char* json_utf8) {
    auto* anim = ArtCade::EditorAPI::s_spriteAnimator;
    if (!anim) return -1;
    if (!json_utf8 || !*json_utf8) {
        anim->clearClips();
        return 0;
    }
    try {
        const json doc = json::parse(json_utf8);
        auto imageAssets = ArtCade::ProjectDocParser::parseImageAssets(doc);
        auto animationAssets = ArtCade::ProjectDocParser::parseSpriteAnimationAssets(doc);
        registerAnimationClipsFromAssets(*anim, imageAssets);
        appendAnimationClipsFromAssets(*anim, animationAssets);
        return 0;
    } catch (const std::exception&) {
        return -2;
    }
}

EMSCRIPTEN_KEEPALIVE void editor_preview_spritesheet_reset() {
    ArtCade::EditorAPI::resetSpritesheetPreview();
}

EMSCRIPTEN_KEEPALIVE int editor_preview_spritesheet_submit(
    const char* texturePath,
    const char* clipName,
    float dtSeconds,
    int canvasW,
    int canvasH) {
    if (!texturePath || !*texturePath || !clipName || !*clipName) return -2;
    ArtCade::EditorAPI::queueSpritesheetPreview(texturePath, clipName, dtSeconds, canvasW, canvasH);
    return 0;
}

namespace {

enum class ProjectLoadKind { HotSync, PreviewRestore, EnterPlay, ExitPlay };

/** Load dialog JSON; returns false if DialogManager missing. */
bool loadDialogsFromJson(const char* json_utf8) {
    auto* dm = ArtCade::EditorAPI::s_dialogManager;
    if (!dm) return false;
    if (!json_utf8) {
        (void)dm->loadDialogGraphsJson("[]");
        return true;
    }
    return dm->loadDialogGraphsJson(json_utf8);
}

int loadLuaFromUtf8(const char* lua_utf8) {
    if (!lua_utf8 || !*lua_utf8) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] empty Lua source.", "warn");
        return ArtCade::kEditorApiLuaError;
    }
    auto* host = ArtCade::EditorAPI::s_luaHost;
    if (!host) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] LuaHost not wired yet.", "warn");
        return ArtCade::kEditorApiNotWired;
    }
    if (host->loadLuaSource(lua_utf8)) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] Logic Board hot-reloaded.", "info");
        return ArtCade::kEditorApiOk;
    }
    std::string msg =
        "[EditorAPI] Hot-reload failed: " + host->lastError();
    ArtCade::EditorAPI::notifyConsoleLine(msg.c_str(), "error");
    return ArtCade::kEditorApiLuaError;
}

/** @return EditorApiResult; sets console message on failure. */
ArtCade::EditorApiResult loadProjectFromJson(const char* json_utf8, ProjectLoadKind kind,
                         const std::string* exitPlayLua = nullptr) {
    const char* apiName = "editor_load_project";
    switch (kind) {
    case ProjectLoadKind::HotSync:       apiName = "editor_load_project"; break;
    case ProjectLoadKind::PreviewRestore: apiName = "editor_restore_from_project"; break;
    case ProjectLoadKind::EnterPlay:     apiName = "editor_enter_play_mode"; break;
    case ProjectLoadKind::ExitPlay:      apiName = "editor_exit_play_mode"; break;
    }

    if (!json_utf8 || !*json_utf8) {
        std::string msg = std::string("[EditorAPI] ") + apiName + ": empty JSON.";
        ArtCade::EditorAPI::notifyConsoleLine(msg.c_str(), "warn");
        return ArtCade::kEditorApiJsonError;
    }
    auto* gateway = ArtCade::EditorAPI::s_entityGateway;
    if (!gateway) {
        std::string msg = std::string("[EditorAPI] ") + apiName + ": engine not wired yet.";
        ArtCade::EditorAPI::notifyConsoleLine(msg.c_str(), "warn");
        return ArtCade::kEditorApiNotWired;
    }

    namespace Parser = ArtCade::ProjectDocParser;

    try {
        const json doc = json::parse(json_utf8);
        std::string validationError;
        if (!ArtCade::ProjectJson::validate_current_project_json(doc, validationError)) {
            const std::string message = std::string("[EditorAPI] ") + apiName
                + ": " + validationError;
            ArtCade::EditorAPI::notifyConsoleLine(message.c_str(), "warn");
            return ArtCade::kEditorApiJsonError;
        }
        rebuildEditorAssetManifest(doc);

        auto objectTypes = Parser::parseObjectTypes(doc);
        auto entityDefs  = Parser::parseEntities(doc);
        auto sceneDefs   = Parser::parseScenes(doc);
        auto tilesets    = Parser::parseTilesets(doc);
        auto tilePalette = Parser::parseTilePalette(doc);
        auto imageAssets = Parser::parseImageAssets(doc);
        auto animationAssets = Parser::parseSpriteAnimationAssets(doc);
        auto audioAssets = Parser::parseAudioAssets(doc);

        Parser::materializeV2Project(entityDefs, sceneDefs, objectTypes);

        if (ArtCade::EditorAPI::s_spriteAnimator) {
            registerAnimationClipsFromAssets(
                *ArtCade::EditorAPI::s_spriteAnimator, imageAssets);
            appendAnimationClipsFromAssets(
                *ArtCade::EditorAPI::s_spriteAnimator, animationAssets);
        }
        if (ArtCade::EditorAPI::s_audio)
            ArtCade::EditorAPI::s_audio->setRuntimeAssetCatalog(audioAssets);

        const std::string activeId = doc.at("activeSceneId").get<std::string>();

        const std::unordered_map<std::string, ArtCade::EntityDef>* typesPtr =
            objectTypes.empty() ? nullptr : &objectTypes;
        gateway->replaceProject(sceneDefs, entityDefs, activeId, typesPtr);
        gateway->setTilesets(tilesets);

        gateway->setCollisionProjectData(
            Parser::parsePhysicsLayers(doc),
            Parser::parseCollisionProfiles(doc),
            Parser::parseSpritePathToAssetId(doc));

        std::vector<ArtCade::SceneLayerDef> sceneLayers;
        auto scene_layers_it = sceneDefs.find(activeId);
        if (scene_layers_it != sceneDefs.end()) {
            sceneLayers = scene_layers_it->second.layers;
        }
        gateway->setSceneLayers(std::move(sceneLayers));

        const ArtCade::ProjectRuntimeSettings runtimeSettings =
            Parser::parseRuntimeSettings(doc);
        const auto globalVariables = Parser::parseGlobalVariables(doc);

        if (kind == ProjectLoadKind::HotSync) {
            if (ArtCade::EditorAPI::s_onProjectLoaded)
                ArtCade::EditorAPI::s_onProjectLoaded(
                    tilePalette, tilesets, globalVariables, runtimeSettings);
        } else if (kind == ProjectLoadKind::PreviewRestore) {
            if (ArtCade::EditorAPI::s_onPreviewRestore)
                ArtCade::EditorAPI::s_onPreviewRestore(
                    tilePalette, tilesets, globalVariables, runtimeSettings);
        } else if (kind == ProjectLoadKind::EnterPlay) {
            if (ArtCade::EditorAPI::s_onEnterPlay)
                ArtCade::EditorAPI::s_onEnterPlay(
                    tilePalette, tilesets, globalVariables, runtimeSettings);
        } else if (kind == ProjectLoadKind::ExitPlay) {
            const std::string lua = exitPlayLua ? *exitPlayLua : std::string{};
            if (ArtCade::EditorAPI::s_onExitPlay)
                ArtCade::EditorAPI::s_onExitPlay(
                    tilePalette, tilesets, globalVariables, runtimeSettings, lua);
        }

        char buf[128];
        if (kind == ProjectLoadKind::HotSync) {
            std::snprintf(buf, sizeof(buf),
                "[EditorAPI] Project loaded: %zu entities, %zu scenes.",
                entityDefs.size(), sceneDefs.size());
        } else if (kind == ProjectLoadKind::EnterPlay) {
            std::snprintf(buf, sizeof(buf),
                "[EditorAPI] Enter play: %zu entities, %zu scenes.",
                entityDefs.size(), sceneDefs.size());
        } else if (kind == ProjectLoadKind::ExitPlay) {
            std::snprintf(buf, sizeof(buf),
                "[EditorAPI] Exit play: %zu entities, %zu scenes.",
                entityDefs.size(), sceneDefs.size());
        } else {
            std::snprintf(buf, sizeof(buf),
                "[EditorAPI] Preview restored: %zu entities, %zu scenes.",
                entityDefs.size(), sceneDefs.size());
        }
        ArtCade::EditorAPI::notifyConsoleLine(buf, "info");
        return ArtCade::kEditorApiOk;

    } catch (const std::exception& ex) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "[EditorAPI] JSON parse error: %s", ex.what());
        ArtCade::EditorAPI::notifyConsoleLine(buf, "error");
        return ArtCade::kEditorApiJsonError;
    }
}

} // namespace

EMSCRIPTEN_KEEPALIVE int editor_load_project(const char* json_utf8) {
    return static_cast<int>(
        loadProjectFromJson(json_utf8, ProjectLoadKind::HotSync));
}

EMSCRIPTEN_KEEPALIVE void editor_restore_from_project(const char* json_utf8) {
    (void)loadProjectFromJson(json_utf8, ProjectLoadKind::PreviewRestore);
}

EMSCRIPTEN_KEEPALIVE int editor_enter_play_mode(
    const char* project_json,
    const char* lua_utf8,
    const char* dialogs_json)
{
    if (!ArtCade::EditorAPI::s_entityGateway || !ArtCade::EditorAPI::s_luaHost)
        return ArtCade::kEditorApiNotWired;

    const int loadResult = static_cast<int>(
        loadProjectFromJson(project_json, ProjectLoadKind::EnterPlay));
    if (loadResult != ArtCade::kEditorApiOk)
        return loadResult;

    if (!loadDialogsFromJson(dialogs_json))
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] editor_enter_play_mode: DialogManager not wired.", "warn");

    const int luaResult = loadLuaFromUtf8(lua_utf8);
    if (luaResult != ArtCade::kEditorApiOk)
        return luaResult;

    ArtCade::EditorAPI::s_mode = 1;
    if (auto* vp = ArtCade::EditorAPI::s_viewport)
        vp->set_presentation_mode(ArtCade::s_playPresentationMode);
    if (auto* r = ArtCade::EditorAPI::s_renderer)
        r->setGameViewCompositorEnabled(true);
    if (auto* gw = ArtCade::EditorAPI::s_entityGateway)
        gw->applyDesignVisibilityForPlay();
    ArtCade::EditorAPI::notifyConsoleLine("[EditorAPI] Mode: PLAY", "info");
    return ArtCade::kEditorApiOk;
}

EMSCRIPTEN_KEEPALIVE int editor_exit_play_mode(
    const char* project_json,
    const char* lua_utf8)
{
    if (!ArtCade::EditorAPI::s_entityGateway)
        return ArtCade::kEditorApiNotWired;

    const std::string luaCopy = lua_utf8 ? lua_utf8 : std::string{};
    const int loadResult = static_cast<int>(
        loadProjectFromJson(project_json, ProjectLoadKind::ExitPlay, &luaCopy));
    if (loadResult != ArtCade::kEditorApiOk)
        return loadResult;

    ArtCade::EditorAPI::s_mode = 0;
    if (auto* vp = ArtCade::EditorAPI::s_viewport) {
        vp->set_presentation_mode(
            ArtCade::Presentation::PresentationMode::SceneEdit);
    }
    if (auto* r = ArtCade::EditorAPI::s_renderer)
        r->setGameViewCompositorEnabled(false);
    if (auto* gw = ArtCade::EditorAPI::s_entityGateway)
        gw->restoreDesignVisibilityForEdit();

    ArtCade::EditorAPI::notifyConsoleLine("[EditorAPI] Mode: EDIT", "info");
    return ArtCade::kEditorApiOk;
}

EMSCRIPTEN_KEEPALIVE void editor_set_transform(
    uint32_t entityId,
    float x,      float y,
    float rotation,
    float scaleX, float scaleY)
{
    auto* gateway = ArtCade::EditorAPI::s_entityGateway;
    if (!gateway) return;
    if (!gateway->setTransform(entityId, {x, y}, rotation, {scaleX, scaleY})) return;
    ArtCade::EditorAPI::notifyTransformChanged(entityId, x, y, rotation, scaleX, scaleY);
}

EMSCRIPTEN_KEEPALIVE void editor_update_entity(
    uint32_t entityId, const char* json_utf8)
{
    if (!json_utf8 || !*json_utf8) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] editor_update_entity: empty JSON.", "warn");
        return;
    }
    auto* gateway = ArtCade::EditorAPI::s_entityGateway;
    if (!gateway) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] editor_update_entity: engine not wired yet.", "warn");
        return;
    }

    try {
        const json doc = json::parse(json_utf8);
        namespace Parser = ArtCade::ProjectDocParser;
        auto def = Parser::parseEntityDef(doc, entityId);
        def.id = entityId;

        if (!gateway->updateEntity(entityId, def)) {
            char buf[96];
            std::snprintf(buf, sizeof(buf),
                "[EditorAPI] editor_update_entity: unknown entity #%u.", entityId);
            ArtCade::EditorAPI::notifyConsoleLine(buf, "warn");
            return;
        }
    } catch (const std::exception& ex) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "[EditorAPI] editor_update_entity parse error: %s", ex.what());
        ArtCade::EditorAPI::notifyConsoleLine(buf, "error");
    }
}

EMSCRIPTEN_KEEPALIVE void editor_begin_authoring_sync_batch() {
    if (ArtCade::EditorAPI::s_onAuthoringSyncBatchBegin)
        ArtCade::EditorAPI::s_onAuthoringSyncBatchBegin();
}

EMSCRIPTEN_KEEPALIVE void editor_end_authoring_sync_batch() {
    if (ArtCade::EditorAPI::s_onAuthoringSyncBatchEnd)
        ArtCade::EditorAPI::s_onAuthoringSyncBatchEnd();
}

EMSCRIPTEN_KEEPALIVE void editor_set_scene_settings(
    const char* sceneId, const char* json_utf8)
{
    if (!sceneId || !*sceneId || !json_utf8 || !*json_utf8) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] editor_set_scene_settings: invalid arguments.", "warn");
        return;
    }
    if (!ArtCade::EditorAPI::s_applySceneMutation) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] editor_set_scene_settings: mutation bridge not wired.", "warn");
        return;
    }

    try {
        const json doc = json::parse(json_utf8);
        namespace Parser = ArtCade::ProjectDocParser;
        const auto parsed = Parser::parseSceneDef(doc, sceneId);
        const auto patch = ArtCade::Modules::ScenePatch::from_projection(parsed);

        const auto result = ArtCade::EditorAPI::s_applySceneMutation(
            sceneId,
            patch);

        if (result.error == ArtCade::Modules::SceneMutationError::SceneNotFound) {
            std::string msg = "[EditorAPI] editor_set_scene_settings: unknown scene ";
            msg += sceneId;
            ArtCade::EditorAPI::notifyConsoleLine(msg.c_str(), "warn");
            return;
        }
        if (result.error == ArtCade::Modules::SceneMutationError::InvalidPatch) {
            ArtCade::EditorAPI::notifyConsoleLine(
                "[EditorAPI] editor_set_scene_settings: invalid patch.", "warn");
            return;
        }

        const bool deferMutation =
            ArtCade::EditorAPI::s_isSceneMutationBatchOpen
            && ArtCade::EditorAPI::s_isSceneMutationBatchOpen();
        if (result.changed && ArtCade::EditorAPI::s_onSceneMutation && !deferMutation)
            ArtCade::EditorAPI::s_onSceneMutation(result);

        std::string msg = "[EditorAPI] Scene settings patched: ";
        msg += sceneId;
        ArtCade::EditorAPI::notifyConsoleLine(msg.c_str(), "info");
    } catch (const std::exception& ex) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "[EditorAPI] editor_set_scene_settings parse error: %s", ex.what());
        ArtCade::EditorAPI::notifyConsoleLine(buf, "error");
    }
}

EMSCRIPTEN_KEEPALIVE int editor_reload_script(const char* lua_utf8) {
    return loadLuaFromUtf8(lua_utf8);
}

EMSCRIPTEN_KEEPALIVE const float* editor_get_runtime_profile() {
    return ArtCade::EditorAPI::runtimeProfileBuffer();
}

EMSCRIPTEN_KEEPALIVE const char* editor_get_variables_json(uint32_t entityId) {
    static std::string payload;
    nlohmann::json root = {
        {"globals", nlohmann::json::object()},
        {"locals", nlohmann::json::object()},
    };
    if (auto* variables = ArtCade::EditorAPI::s_variables) {
        const auto encode = [](const ArtCade::GameVariableValue& value) {
            return std::visit([](const auto& item) -> nlohmann::json { return item; }, value);
        };
        for (const auto& [key, value] : variables->takeSnapshot())
            root["globals"][key] = encode(value);
        if (entityId != ArtCade::INVALID_ENTITY) {
            for (const auto& [key, value] : variables->takeEntitySnapshot(entityId))
                root["locals"][key] = encode(value);
        }
    }
    payload = root.dump();
    return payload.c_str();
}

EMSCRIPTEN_KEEPALIVE void editor_load_dialogs(const char* json_utf8) {
    auto* dm = ArtCade::EditorAPI::s_dialogManager;
    if (!dm) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] editor_load_dialogs: DialogManager not wired yet.", "warn");
        return;
    }
    if (!json_utf8) {
        (void)dm->loadDialogGraphsJson("[]");
        return;
    }
    if (dm->loadDialogGraphsJson(json_utf8)) {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] Dialog library synced to runtime.", "info");
    } else {
        ArtCade::EditorAPI::notifyConsoleLine(
            "[EditorAPI] Dialog library sync had parse errors — see stderr.", "warn");
    }
}

} // extern "C"

#endif // __EMSCRIPTEN__
