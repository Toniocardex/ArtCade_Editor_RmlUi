#include "editor-native/app/editor_app.h"

#include "editor-native/app/asset_import.h"
#include "editor-native/app/confirm_dialog.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/editor_input.h"
#include "editor-native/app/file_dialog.h"
#include "editor-native/app/generated_sfx_generation_preflight.h"
#include "editor-native/app/generated_sfx_generation_service.h"
#include "editor-native/app/generated_sfx_output_transaction.h"
#include "editor-native/app/generated_sfx_status_projection.h"
#include "editor-native/app/hierarchy_actions.h"
#include "editor-native/app/input_routing.h"
#include "editor-native/app/new_project_transaction.h"
#include "editor-native/app/project_file.h"
#include "editor-native/app/project_session_controller.h"
#include "editor-native/app/rml_host.h"
#include "editor-native/app/scene_view_interaction.h"
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
#include "editor-native/commands/generated_sfx_commands.h"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/play_sound_preload.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_animation_slicing.h"
#include "editor-native/model/tilemap_stroke_preview.h"
#include "editor-native/model/tilemap_validation.h"
#include "editor-native/model/tileset_slicing.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/scene_view.h"
#include "editor-native/view/texture_cache.h"
#include "editor-native/view/texture_request_catalog.h"
#include "editor-native/view/tileset_empty_tiles.h"
#include "editor-native/app/tile_palette_input.h"
#include "editor-native/app/tile_palette_sheet_renderer.h"
#include "editor-native/view/tilemap_paint_overlay.h"

#include "artcade/sfx/raylib_preview.hpp"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

#include <raylib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
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

void collectLogicKeyPresses(RuntimeInputSnapshot& input) {
    static constexpr std::pair<LogicKey, int> kKeys[] = {
        {LogicKey::A, KEY_A}, {LogicKey::B, KEY_B}, {LogicKey::C, KEY_C},
        {LogicKey::D, KEY_D}, {LogicKey::E, KEY_E}, {LogicKey::F, KEY_F},
        {LogicKey::G, KEY_G}, {LogicKey::H, KEY_H}, {LogicKey::I, KEY_I},
        {LogicKey::J, KEY_J}, {LogicKey::K, KEY_K}, {LogicKey::L, KEY_L},
        {LogicKey::M, KEY_M}, {LogicKey::N, KEY_N}, {LogicKey::O, KEY_O},
        {LogicKey::P, KEY_P}, {LogicKey::Q, KEY_Q}, {LogicKey::R, KEY_R},
        {LogicKey::S, KEY_S}, {LogicKey::T, KEY_T}, {LogicKey::U, KEY_U},
        {LogicKey::V, KEY_V}, {LogicKey::W, KEY_W}, {LogicKey::X, KEY_X},
        {LogicKey::Y, KEY_Y}, {LogicKey::Z, KEY_Z},
        {LogicKey::Num0, KEY_ZERO}, {LogicKey::Num1, KEY_ONE},
        {LogicKey::Num2, KEY_TWO}, {LogicKey::Num3, KEY_THREE},
        {LogicKey::Num4, KEY_FOUR}, {LogicKey::Num5, KEY_FIVE},
        {LogicKey::Num6, KEY_SIX}, {LogicKey::Num7, KEY_SEVEN},
        {LogicKey::Num8, KEY_EIGHT}, {LogicKey::Num9, KEY_NINE},
        {LogicKey::ArrowLeft, KEY_LEFT}, {LogicKey::ArrowRight, KEY_RIGHT},
        {LogicKey::ArrowUp, KEY_UP}, {LogicKey::ArrowDown, KEY_DOWN},
        {LogicKey::Space, KEY_SPACE}, {LogicKey::Enter, KEY_ENTER},
    };
    for (const auto& [logicKey, raylibKey] : kKeys) {
        if (IsKeyPressed(raylibKey)) input.pressedLogicKeys.push_back(logicKey);
        if (IsKeyReleased(raylibKey)) input.releasedLogicKeys.push_back(logicKey);
        if (IsKeyDown(raylibKey)) input.heldLogicKeys.push_back(logicKey);
    }
}

// HiDPI bridge: RmlUi runs in physical framebuffer pixels (GetRenderWidth),
// while raylib's drawing and mouse stay in logical pixels (GetScreenWidth) —
// raylib applies the DPI scale itself via screenScale / SetMouseScale. The
// factor is 1.0 on a 100% display, so this is a no-op there.
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
    bool shotLogic = false;     // open the Logic workspace for shell/layout smoke checks
    bool shotScript = false;    // open the Script workspace for shell/layout smoke checks
    bool shotSfx = false;       // create and open the native Generated SFX editor
    int shotSliceColumns = 0;   // > 0: slice the open clip into N frames for the shot
    bool shotSliceAll = false;  // slice every animation asset in turn (overwrite repro)
    std::string shotSavePath;   // non-empty: save the project here via the real path
    int shotEntityIndex = -1;   // >= 0: select the Nth instance of the active scene
    std::string shotDropdown;   // non-empty: open this Inspector value dropdown
    std::string shotSection;    // non-empty: toggle this Inspector section (e.g. "tilemap")
    std::string shotStamp;      // "c0,r0,c1,r1": select that palette region as a stamp
    float shotPaletteZoom = 0.f; // > 0: apply this palette zoom to the shot tileset
    std::string shotAssetMenu;  // "kind|id": open the Assets row menu on that asset
    int shotWidth = 0, shotHeight = 0;  // >0: force a viewport size for responsive UI shots
    bool lifecycleSmoke = false; // hidden, self-checking bind/detach/shutdown run
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc) shotPath = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-project") == 0 && i + 1 < argc)
            shotProject = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-anim") == 0) shotAnimation = true;
        else if (std::strcmp(argv[i], "--shot-tileset") == 0) shotTileset = true;
        else if (std::strcmp(argv[i], "--shot-logic") == 0) shotLogic = true;
        else if (std::strcmp(argv[i], "--shot-script") == 0) shotScript = true;
        else if (std::strcmp(argv[i], "--shot-sfx") == 0) shotSfx = true;
        else if (std::strcmp(argv[i], "--shot-slice") == 0 && i + 1 < argc)
            shotSliceColumns = std::atoi(argv[i + 1]);
        else if (std::strcmp(argv[i], "--shot-slice-all") == 0) shotSliceAll = true;
        else if (std::strcmp(argv[i], "--shot-save") == 0 && i + 1 < argc)
            shotSavePath = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-entity") == 0 && i + 1 < argc)
            shotEntityIndex = std::atoi(argv[i + 1]);
        else if (std::strcmp(argv[i], "--shot-dropdown") == 0 && i + 1 < argc)
            shotDropdown = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-section") == 0 && i + 1 < argc)
            shotSection = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-stamp") == 0 && i + 1 < argc)
            shotStamp = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-palette-zoom") == 0 && i + 1 < argc)
            shotPaletteZoom = static_cast<float>(std::atof(argv[i + 1]));
        else if (std::strcmp(argv[i], "--shot-asset-menu") == 0 && i + 1 < argc)
            shotAssetMenu = argv[i + 1];
        else if (std::strcmp(argv[i], "--shot-size") == 0 && i + 1 < argc) {
            if (const char* x = std::strchr(argv[i + 1], 'x')) {
                shotWidth = std::atoi(argv[i + 1]);
                shotHeight = std::atoi(x + 1);
            }
        }
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
    // Screenshot-mode viewport override: lets responsive UI checks reproduce a
    // narrow display (for example 1366x768) on any development monitor.
    if (shotWidth > 0 && shotHeight > 0) SetWindowSize(shotWidth, shotHeight);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);

    // RmlUi context + viewport are sized in physical framebuffer pixels; the dp
    // ratio scales `dp` lengths in the RCSS so the UI keeps its intended size.
    // GetRenderWidth/Height are sampled here, right after MaximizeWindow(): any
    // work between the two (asset loads, etc.) risks widening the window's own
    // maximize-animation race (the OS resize can still be in flight) so that
    // this sample lands on the PRE-maximize size, sizing RmlUi's entire layout
    // - hit-testing included - for a window smaller than what's on screen. Keep
    // this call immediately adjacent to MaximizeWindow(); load fonts/textures
    // after it, never between.
    const float dpi = GetWindowScaleDPI().x;
    // Keep the values actually applied to RmlUi. MaximizeWindow is asynchronous
    // on Windows, so sampling fresh values later as the resize baseline can miss
    // a size transition that happened after CreateContext but before frame 0.
    const int hostRenderW = GetRenderWidth();
    const int hostRenderH = GetRenderHeight();
    const float hostDpi = dpi > 0.f ? dpi : 1.f;
    RmlHost host;
    if (!host.initialize(hostRenderW, hostRenderH, hostDpi,
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
    syncEditorOverlayViewport(animationDocument);
    animationDocument->Hide();

    Rml::ElementDocument* tilesetDocument =
        host.loadDocument("ui/tileset/tileset_editor.rml");
    if (!tilesetDocument) {
        TraceLog(LOG_ERROR, "[editor] failed to load tileset editor resources");
        host.shutdown();
        CloseWindow();
        return 1;
    }
    syncEditorOverlayViewport(tilesetDocument);   // same absolute full-window sync, generic
    tilesetDocument->Hide();

    // Canvas text font (entity labels, scene chip, canvas messages): the same
    // Inter face the RmlUi chrome uses. Owned here; unloaded before CloseWindow.
    // Loaded only now, after RmlUi's context/documents are already sized and
    // built - see the comment above host.initialize() for why nothing may sit
    // between MaximizeWindow() and that sizing.
    CanvasFont canvasFont = loadCanvasFont(resourceRoot);
    if (!canvasFont.loaded) {
        TraceLog(LOG_WARNING,
                 "[editor] Inter-Medium.ttf not loaded - canvas text falls back to "
                 "raylib's built-in font");
    }

    int exitCode = 0;
    std::optional<EditorUi> uiOwner{std::in_place, coordinator, host.document(),
                                    animationDocument, tilesetDocument};
    EditorUi& ui = *uiOwner;
    ui.bind();
    coordinator.logInfo("ArtCade Studio ready.");
    SceneView sceneView;
    TextureCache textureCache;
    TextureRequestCatalog textureRequestCatalog;
    // Derived alpha-scan of the palette's tileset (signature-keyed, so a
    // re-slice, image swap or project replacement recomputes on next query).
    TilesetEmptyTileCache tilesetEmptyTiles;
    ViewportDrag drag;
    ViewportContextClick contextClick;
    std::optional<Vec2> pendingContextSpawn;

    ProjectSessionController projectSession{coordinator, ui, textureCache};
    projectSession.bindUi();

    artcade::sfx::RaylibPreview sfxPreview;
    bool ownsAudioDevice = false;
    // Prefer a process-lifetime device: Init once after the window is up so
    // neither Start Play nor the first Play Sound pays InitAudioDevice cost.
    if (!IsAudioDeviceReady()) {
        InitAudioDevice();
        ownsAudioDevice = IsAudioDeviceReady();
    }
    auto sfxOutputRepository =
        std::make_shared<FilesystemGeneratedSfxOutputRepository>();
    GeneratedSfxGenerationService sfxGeneration{sfxOutputRepository};
    projectSession.setProjectRelocationAvailabilityQuery(
        [&] { return !sfxGeneration.busy(); });
    GeneratedSfxOutputTransaction sfxOutputTransaction{
        coordinator, sfxOutputRepository};
    bool sfxBatchWasActive = false;
    // Play Sound's Raylib-side cache: loaded once per referenced audio asset at
    // Start Play, reused for every firing this session. Keyed by AssetId,
    // matching RuntimeAudioCommand/PlayAssetCatalogSnapshot::audioAssets.
    // Play-scoped only — cleared on Stop and rebuilt on every Start (Generated
    // SFX may have been regenerated between sessions).
    std::unordered_map<AssetId, Sound> playSoundCache;
    std::unordered_set<std::string> playAudioDiagnostics;
    const auto logPlayAudioErrorOnce = [&](const AssetId& id,
                                            const std::string& reason) {
        const std::string key = id + "\n" + reason;
        if (playAudioDiagnostics.insert(key).second)
            coordinator.logError("Play Sound '" + id + "': " + reason);
    };
    const auto clearPlaySoundCache = [&] {
        for (auto& [unused, sound] : playSoundCache) {
            (void)unused;
            UnloadSound(sound);
        }
        playSoundCache.clear();
        playAudioDiagnostics.clear();
    };
    const auto preparePlaySoundCache = [&]() -> bool {
        clearPlaySoundCache();

        if (!IsAudioDeviceReady()) {
            InitAudioDevice();
            ownsAudioDevice = IsAudioDeviceReady();
        }
        if (!IsAudioDeviceReady()) {
            coordinator.logError(
                "Cannot start Play: audio device is unavailable");
            return false;
        }

        const PlaySession* session = coordinator.playSession();
        if (!session) {
            coordinator.logError(
                "Cannot prepare audio without an active PlaySession");
            return false;
        }

        const std::filesystem::path assetRoot =
            projectSession.assetRoot(resourceRoot);
        std::vector<PlaySoundPreloadEntry> plan;
        std::string planError;
        if (!planPlaySoundPreload(session->assets(), assetRoot, &plan, &planError)) {
            coordinator.logError(planError.empty()
                ? "Cannot start Play: audio preload planning failed"
                : planError);
            clearPlaySoundCache();
            return false;
        }

        const double preloadStart = GetTime();
        for (const PlaySoundPreloadEntry& entry : plan) {
            const double loadStart = GetTime();
            Sound sound = LoadSound(entry.resolvedPath.string().c_str());
            const double loadMs = (GetTime() - loadStart) * 1000.0;
            if (!IsSoundReady(sound)) {
                coordinator.logError(
                    "Cannot start Play: audio file could not be decoded: "
                    + entry.sourcePath);
                clearPlaySoundCache();
                return false;
            }
            const auto [it, inserted] =
                playSoundCache.emplace(entry.assetId, sound);
            (void)it;
            if (!inserted) {
                UnloadSound(sound);
                coordinator.logError(
                    "Cannot start Play: duplicate audio asset in snapshot: "
                    + entry.assetId);
                clearPlaySoundCache();
                return false;
            }
            TraceLog(LOG_INFO,
                     "[audio preload] asset=%s load=%.2fms",
                     entry.assetId.c_str(), loadMs);
        }
        TraceLog(LOG_INFO,
                 "[audio preload] %zu assets loaded in %.2f ms",
                 playSoundCache.size(),
                 (GetTime() - preloadStart) * 1000.0);
        return true;
    };

    // One preflight authority for UI capability and click-time revalidation.
    // Unknown ids are prospective Create-from-Current identities; existing ids
    // use their persisted link to distinguish Generate from Regenerate.
    const auto sfxGenerationPreflight = [&](const std::string& id) {
        const artcade::sfx::GeneratedSfxDef* definition =
            coordinator.document().findGeneratedSfx(id);
        artcade::sfx::GeneratedSfxDef prospective;
        if (!definition) {
            prospective.id = id;
            definition = &prospective;
        }
        const std::filesystem::path projectPath = projectSession.currentProjectPath();
        return preflightGeneratedSfxGeneration(
            coordinator.document(), *definition,
            projectPath.empty() ? std::filesystem::path{} : projectPath.parent_path(),
            *sfxOutputRepository);
    };

    const auto requestSfxGeneration = [&](const std::string& id) {
        const auto result = sfxGeneration.requestGenerate(
            coordinator.document(), projectSession.projectSessionId(),
            projectSession.currentProjectPath(), GenerateGeneratedSfxIntent{id},
            coordinator.isPlaying());
        if (result.accepted) coordinator.logInfo(result.message);
        else coordinator.logError(result.message);
        return result;
    };

    ui.setGeneratedSfxHandlers(
        [&](const std::string& id) {
            const auto result = sfxGeneration.requestPreview(
                coordinator.document(), projectSession.projectSessionId(),
                PreviewGeneratedSfxIntent{id},
                coordinator.isPlaying());
            if (result.accepted) coordinator.logInfo(result.message);
            else coordinator.logWarning(result.message);
        },
        [&]() { sfxPreview.stop(); },
        [&](const std::string& id) {
            (void)requestSfxGeneration(id);
        });
    ui.setGeneratedSfxDiagnosticHandler([&](const std::string& id) {
        sfxGeneration.dismissDiagnostic(id);
    });
    ui.setGeneratedSfxCreateFromCurrentHandler(
        [&](const std::string& sourceId, const std::string& newId,
            const std::string& name) {
            const GeneratedSfxGenerationPreflight preflight =
                sfxGenerationPreflight(newId);
            if (!preflight.allowed())
                return EditorOperationResult::failure(preflight.error);
            const auto duplicated = coordinator.apply(
                DuplicateGeneratedSfxIntent{sourceId, newId, name});
            if (!duplicated.ok) return duplicated;
            coordinator.logInfo(
                "Created Generated SFX \"" + name
                + "\"; generation failure remains recoverable through Retry Generate");
            (void)requestSfxGeneration(newId);
            return duplicated;
        });
    ui.setGeneratedSfxDeleteHandler(
        [&](const RemoveGeneratedSfxIntent& intent) {
            const std::filesystem::path projectPath =
                projectSession.currentProjectPath();
            const auto removed = sfxOutputTransaction.remove(
                intent, projectPath.empty() ? std::filesystem::path{}
                                            : projectPath.parent_path());
            if (removed.ok) {
                sfxPreview.stop();
                sfxGeneration.cancelAll("Generated SFX deleted");
                ui.setSfxBatchState(sfxGeneration.batchState());
                coordinator.logInfo(
                    "Deleted Generated SFX, linked audio, project references and WAV");
            }
            return removed;
        });
    ui.setSfxBatchHandlers(
        [&] {
            const auto result = sfxGeneration.requestRegenerateAllStale(
                coordinator.document(), projectSession.projectSessionId(),
                projectSession.currentProjectPath(), RegenerateAllStaleSfxIntent{},
                coordinator.isPlaying());
            if (result.accepted) coordinator.logInfo(result.message);
            else if (result.message == "No stale Generated SFX to regenerate")
                coordinator.logInfo(result.message);
            else coordinator.logError(result.message);
            ui.setSfxBatchState(sfxGeneration.batchState());
        },
        [&] {
            if (!sfxGeneration.batchState().active) return;
            sfxGeneration.cancelBatch();
            ui.setSfxBatchState(sfxGeneration.batchState());
            coordinator.logInfo("SFX queue will stop after the current item");
        },
        [&] {
            sfxGeneration.dismissBatchSummary();
            ui.setSfxBatchState(sfxGeneration.batchState());
        });
    ui.setProjectSavedQuery([&] { return !projectSession.currentProjectPath().empty(); });
    ui.setGeneratedSfxGenerationAvailabilityQuery([&](const std::string& id) {
        const GeneratedSfxGenerationPreflight preflight = sfxGenerationPreflight(id);
        return EditorUi::GeneratedSfxGenerationAvailability{
            preflight.allowed(), preflight.error};
    });
    ui.setGeneratedSfxStatusQuery([&](const std::string& id) {
        const auto* definition = coordinator.document().findGeneratedSfx(id);
        if (!definition) return GeneratedSfxStatusProjection{
            GeneratedSfxObservedStatus::MissingOutput,
            "Generated SFX no longer exists"};
        const std::filesystem::path projectPath = projectSession.currentProjectPath();
        return projectGeneratedSfxStatus(
            coordinator.document(), *definition,
            projectPath.empty() ? std::filesystem::path{} : projectPath.parent_path(),
            *sfxOutputRepository, sfxGeneration.snapshot());
    });

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
            projectSession.assetRoot(resourceRoot);
        const AssetId sheetId = asset->sourceImageAssetId;
        SceneFrameSprite requestSprite;
        requestSprite.assetId = sheetId;
        requestSprite.visible = true;
        const auto& requests = textureRequestCatalog.forDocument(
            coordinator.document(), assetRoot);
        textureCache.prepare({requestSprite}, requests);
        const TextureResource* resource = textureCache.find(sheetId);
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
        std::vector<SpriteFrameDef> frames = spriteAnimationFramesForGrid(
            resource->texture.width, resource->texture.height, *grid);
        if (frames.empty()) {
            coordinator.logError("Cannot slice: no frames produced");
            return;
        }

        std::vector<SpriteFrameId> frameIds;
        frameIds.reserve(frames.size());
        for (const SpriteFrameDef& frame : frames) frameIds.push_back(frame.id);
        const std::size_t frameCount = frames.size();
        const AssetId assetId = asset->id;
        const std::string clipId = clip->id;
        const std::string clipName = clip->name;
        if (!coordinator.execute(ReplaceAnimationFramesCommand{
                assetId, std::move(frames)}).ok) {
            return;
        }
        const EditorOperationResult result =
            coordinator.execute(SetAnimationClipFrameIdsCommand{
                assetId, clipId, std::move(frameIds)});
        if (result.ok) {
            coordinator.logInfo("Sliced " + clipName + " into "
                                + std::to_string(frameCount) + " frames");
        }
    };
    ui.setAnimationSliceHandler(sliceAnimationHandler);
    // Looks up the loaded source texture for a tileset's image (preparing it
    // through the cache if needed); shared by apply and create-from-image.
    const auto loadedTilesetSource = [&](const AssetId& imageAssetId) -> const TextureResource* {
        const std::filesystem::path assetRoot =
            projectSession.assetRoot(resourceRoot);
        SceneFrameSprite requestSprite;
        requestSprite.assetId = imageAssetId;
        requestSprite.visible = true;
        const auto& requests = textureRequestCatalog.forDocument(
            coordinator.document(), assetRoot);
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

    projectSession.refreshWindowTitle();   // empty start project -> "Untitled"

    // Screenshot-mode project bootstrap: same canonical load path as File >
    // Open, minus the dialogs (there is no interaction to guard against).
    if (!shotPath.empty() && !shotProject.empty()) {
        const std::filesystem::path projectPath{shotProject};
        const ProjectLoadResult result = loadProjectFromFile(coordinator, projectPath);
        if (!result.ok) {
            coordinator.logError("Open failed: " + result.error.message);
        } else {
            textureCache.clear();
            projectSession.setCurrentProjectPath(projectPath);
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
                    if (!shotSection.empty()) {
                        // Expand a collapsed-by-default section (e.g. the
                        // Tilemap section's tile palette) via the exact click
                        // path; same flush-first discipline as shotDropdown.
                        ui.processFrame();
                        ui.handleAction("toggle-inspector-section", shotSection, "");
                    }
                    // Multi-tile stamp smoke test (--shot-stamp c0,r0,c1,r1):
                    // the marquee's verifiable seam. The raylib drag itself
                    // cannot be synthesized headlessly, so this exercises its
                    // exact result path - region -> buildTileStampFromRegion
                    // (with the real empty mask) -> SelectPaintStampIntent.
                    if (!shotStamp.empty()) {
                        int c0 = 0;
                        int r0 = 0;
                        int c1 = 0;
                        int r1 = 0;
                        const SceneInstanceDef* inst =
                            coordinator.document().findInstanceInScene(
                                coordinator.state().activeSceneId,
                                coordinator.selection().primaryEntity);
                        const TilesetAsset* ts = (inst && inst->tilemap.has_value())
                            ? coordinator.document().findTilesetAsset(
                                  inst->tilemap->tilesetAssetId)
                            : nullptr;
                        const TextureResource* sheet = ts
                            ? loadedTilesetSource(ts->imageAssetId) : nullptr;
                        if (sheet && std::sscanf(shotStamp.c_str(), "%d,%d,%d,%d",
                                                 &c0, &r0, &c1, &r1) == 4) {
                            const std::optional<TilesetGridGeometry> geometry =
                                computeTilesetGridGeometry(ts->slicing,
                                                           sheet->texture.width,
                                                           sheet->texture.height);
                            const auto& requests = textureRequestCatalog.forDocument(
                                coordinator.document(), projectSession.assetRoot(resourceRoot));
                            const auto request = requests.find(ts->imageAssetId);
                            const TilesetEmptyMaskView mask = tilesetEmptyTiles.maskFor(
                                *ts, request != requests.end()
                                    ? request->second.resolvedSourcePath
                                    : std::filesystem::path{});
                            if (geometry) {
                                if (const std::optional<TilemapTileStamp> stamp =
                                        buildTileStampFromRegion(
                                            *ts, *geometry, c0, r0, c1, r1,
                                            mask.status == TilesetEmptyMaskStatus::Ready
                                                ? mask.flags : nullptr)) {
                                    coordinator.apply(SelectPaintStampIntent{*stamp});
                                }
                            }
                        }
                        if (ts && shotPaletteZoom > 0.f) {
                            coordinator.apply(
                                SetTilePaletteZoomIntent{ts->assetId, shotPaletteZoom});
                        }
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
                if (projectSession.saveTo(std::filesystem::path{shotSavePath}))
                    coordinator.logInfo("shot-save wrote " + shotSavePath);
                else
                    coordinator.logError("shot-save failed");
            }
        }
    }
    if (!shotPath.empty() && shotLogic) {
        ui.handleAction("open-logic-workspace", "", "");
    }
    if (!shotPath.empty() && shotScript) {
        ui.handleAction("open-script-workspace", "", "");
        if (!coordinator.document().data().scriptAssets.empty()) {
            projectSession.openScript(
                coordinator.document().data().scriptAssets.front().assetId);
            ui.processFrame();
        }
    }
    if (!shotPath.empty() && shotSfx) {
        ui.handleAction("create-generated-sfx", "coin", "");
        ui.processFrame();
    }

    int   frame       = 0;
    int   lastRenderW = hostRenderW;
    int   lastRenderH = hostRenderH;
    float lastDpi     = hostDpi;
    int   sizeStableFrames = 0;
    bool  prevTextFocus = false;   // last frame's RmlUi text focus (see Tileset keys)
    CenterWorkspaceMode previousWorkspaceMode = coordinator.state().centerWorkspaceMode;
    bool gameplayInputFocused = false;
    bool previousPlaying = false;
    // Tile Palette sheet view: the app-side gesture state (marquee, dblclick
    // timing, pan-into-view diff) and last frame's visible palette rect - the
    // wheel-suppression bridge for Ctrl+zoom, computed before this frame's
    // layout exists. An invalid rect always disables the suppression, so the
    // Inspector never loses plain scrolling to a stale box.
    TilePaletteInputState paletteInput;
    ViewportRect prevPaletteClipRect;
    while (true) {
        // Exit guard: a requested close (window X) is held until the unsaved
        // guard passes. On Cancel we clear GLFW's close flag and keep running.
        // Screenshot mode skips the guard (no window interaction).
        if (WindowShouldClose()) {
            if (shotPath.empty() && !projectSession.resolveUnsavedChanges()) {
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
            syncEditorOverlayViewport(animationDocument);
            syncEditorOverlayViewport(tilesetDocument);   // same full-window overlay
            lastRenderW = renderW;
            lastRenderH = renderH;
            lastDpi     = curDpi;
            sizeStableFrames = 0;   // shot mode waits out maximize/DPI resizes
        } else {
            ++sizeStableFrames;
        }
        if (IsKeyPressed(KEY_F8)) host.toggleDebugger();

        RmlInputSuppression inputSuppression;
        inputSuppression.mouseWheel =
            (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
            && prevPaletteClipRect.valid()
            && prevPaletteClipRect.contains(GetMouseX(), GetMouseY());
        const RmlInputResult rml = pumpRmlInput(host.context(), inputSuppression);
        const bool nonSceneWorkspace = coordinator.state().centerWorkspaceMode
            != CenterWorkspaceMode::Scene;
        if (coordinator.state().centerWorkspaceMode != previousWorkspaceMode) {
            // Local presentation gestures belong to the surface that started
            // them. Never let a hidden Scene drag/context click complete after
            // switching to Logic (or vice versa).
            drag = ViewportDrag{};
            contextClick = ViewportContextClick{};
            pendingContextSpawn.reset();
            ui.hideContextMenus();
            previousWorkspaceMode = coordinator.state().centerWorkspaceMode;
        }

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
            } else if (!nonSceneWorkspace && shift && IsKeyPressed(KEY_D)) {
                addInstanceOfSelectedType(coordinator);
            } else if (!nonSceneWorkspace && IsKeyPressed(KEY_D)) {
                cloneSelectedEntity(coordinator);
            }
        }
        if (!nonSceneWorkspace && !rml.textFocus && !coordinator.isPlaying() && IsKeyPressed(KEY_F2)) {
            ui.beginActiveSceneLayerRename();
        }
        // Tilemap tool shortcuts only switch tools between strokes/rectangles -
        // mid-operation, only Escape (handled inside routeViewportTilemapPaint,
        // which needs the operation's context) may interrupt.
        if (!nonSceneWorkspace && !rml.textFocus && !coordinator.isPlaying()
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
        if (!nonSceneWorkspace && !rml.textFocus && IsKeyPressed(KEY_DELETE)) {
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
        bool playing = coordinator.isPlaying();
        const bool popupOpen = ui.hasOpenContextMenu();

        // Keyboard ownership is a durable Scene View state, not a side effect
        // of the cursor's current position. Play started from Scene acquires it;
        // any explicit UI/text/window/popup transition releases it.
        // A fresh Play always re-resolves audio from disk (same "re-validate at
        // every Play start" convention materialize() already applies to every
        // other asset kind) — otherwise a Generated SFX regenerated between two
        // Play sessions would keep playing back whatever was cached from the
        // first one.
        // The cache is Play-scoped even though EditorApp owns the Raylib
        // handles. Preload is part of Start Play preparation: simulation must
        // not run until every referenced Sound is ready (or Play is aborted).
        if (!playing && previousPlaying) clearPlaySoundCache();
        if (playing && !previousPlaying) {
            if (!preparePlaySoundCache()) {
                const auto stopped = coordinator.stopPlaying();
                if (!stopped.ok) {
                    coordinator.logError(
                        "Audio preload failed and Stop Play also failed: "
                        + stopped.error);
                }
                playing = false;
            } else {
                gameplayInputFocused = !nonSceneWorkspace;
            }
        }
        if (!playing || nonSceneWorkspace || rml.textFocus || !IsWindowFocused() || popupOpen
            || animationEditorOpen || tilesetEditorOpen)
            gameplayInputFocused = false;
        if (playing && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            gameplayInputFocused = !nonSceneWorkspace && !rml.textFocus && !popupOpen
                && !contextMenuHit && rect.contains(GetMouseX(), GetMouseY());
        }
        previousPlaying = playing;
        const ViewportRect animationInputRect = animationEditorOpen
            ? resolveSpriteAnimationCanvasContentRect(animationDocument)
            : ViewportRect{};
        const ViewportRect tilesetInputRect = tilesetEditorOpen
            ? resolveTilesetEditorCanvasContentRect(tilesetDocument)
            : ViewportRect{};
        if (!animationEditorOpen && !tilesetEditorOpen && !nonSceneWorkspace) {
            routeViewportInput(coordinator, rect, rml, contextMenuHit);
        }
        if (playing) {
            const float dt = GetFrameTime();
            coordinator.advanceRuntime(dt);               // authored motion (LinearMover)
            // Simulation continues in every workspace. Keyboard input belongs
            // to the Scene View's persistent gameplay focus; pointer hover is
            // deliberately irrelevant here.
            RuntimeInputSnapshot input;
            if (shouldForwardGameplayKeyboardInput({
                    !nonSceneWorkspace, gameplayInputFocused, IsWindowFocused(),
                    rml.textFocus, popupOpen})) {
                input.moveLeft  = IsKeyDown(KEY_LEFT)  || IsKeyDown(KEY_A);
                input.moveRight = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
                input.moveUp    = IsKeyDown(KEY_UP)    || IsKeyDown(KEY_W);
                input.moveDown  = IsKeyDown(KEY_DOWN)  || IsKeyDown(KEY_S);
                // Edge-triggered jump for the PlatformerController (Space / W / Up).
                input.jumpPressed = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W)
                                 || IsKeyPressed(KEY_UP);
                collectLogicKeyPresses(input);
            }
            coordinator.updateRuntime(input, dt);         // input-driven (TopDownController)

            // Play Sound hot path: PlaySession only queues (Raylib-free). After
            // Start Play preload, drain is cache lookup + volume + PlaySound —
            // never InitAudioDevice / filesystem / LoadSound.
            const std::vector<RuntimeAudioCommand> audioCommands = coordinator.drainAudioCommands();
            for (const RuntimeAudioCommand& command : audioCommands) {
                const auto cacheIt = playSoundCache.find(command.audioAssetId);
                if (cacheIt == playSoundCache.end()) {
                    logPlayAudioErrorOnce(command.audioAssetId,
                        "audio asset was not prepared at Play start");
                    continue;
                }
                SetSoundVolume(cacheIt->second, command.volume);
                PlaySound(cacheIt->second);
            }
        } else if (!animationEditorOpen && !tilesetEditorOpen && !nonSceneWorkspace) {
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
                && !nonSceneWorkspace
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
            projectSession.assetRoot(resourceRoot);
        const auto& textureRequests = coordinator.playSession()
            ? textureRequestCatalog.forPlay(coordinator.playSession()->assets(), assetRoot)
            : textureRequestCatalog.forDocument(coordinator.document(), assetRoot);
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

        // -- Tile Palette (Inspector sheet view) -----------------------------
        // One resolution per frame, shared by input (here, against the last
        // completed layout - the editor canvases' own convention) and by
        // painting (which re-reads the rects after host.update()). The
        // renderer receives texture and empty-mask already resolved; it never
        // touches the cache or the document itself.
        const auto resolveTilePaletteRects = [&]() -> std::pair<ViewportRect, ViewportRect> {
            const ViewportRect hole =
                elementContentRectFromDocument(host.document(), "tile-palette");
            const ViewportRect body =
                elementContentRectFromDocument(host.document(), "inspector-body");
            if (!hole.valid() || !body.valid()) return {ViewportRect{}, ViewportRect{}};
            ViewportRect clip;
            clip.x = std::max(body.x, hole.x);
            clip.y = std::max(body.y, hole.y);
            clip.width  = std::min(body.x + body.width,  hole.x + hole.width)  - clip.x;
            clip.height = std::min(body.y + body.height, hole.y + hole.height) - clip.y;
            if (!clip.valid()) return {ViewportRect{}, ViewportRect{}};
            return {hole, clip};
        };
        const TilesetAsset* tilePaletteTileset = nullptr;
        const TextureResource* tilePaletteTexture = nullptr;
        TilesetEmptyMaskView tilePaletteMask;
        if (!coordinator.isPlaying() && !animationEditorOpen && !tilesetEditorOpen
            && !nonSceneWorkspace) {
            const SceneInstanceDef* selectedInst = coordinator.document().findInstanceInScene(
                coordinator.state().activeSceneId, coordinator.selection().primaryEntity);
            if (selectedInst && selectedInst->tilemap.has_value()) {
                const TilesetAsset* candidate = coordinator.document().findTilesetAsset(
                    selectedInst->tilemap->tilesetAssetId);
                if (candidate && !candidate->tiles.empty()) tilePaletteTileset = candidate;
            }
        }
        prevPaletteClipRect = ViewportRect{};
        if (tilePaletteTileset) {
            SceneFrameSprite paletteRequestSprite;
            paletteRequestSprite.assetId = tilePaletteTileset->imageAssetId;
            paletteRequestSprite.visible = true;
            textureCache.prepare({paletteRequestSprite}, textureRequests);
            tilePaletteTexture = textureCache.find(tilePaletteTileset->imageAssetId);
            const auto paletteRequest = textureRequests.find(tilePaletteTileset->imageAssetId);
            tilePaletteMask = tilesetEmptyTiles.maskFor(
                *tilePaletteTileset,
                paletteRequest != textureRequests.end()
                    ? paletteRequest->second.resolvedSourcePath
                    : std::filesystem::path{});
            const auto [inputHole, inputClip] = resolveTilePaletteRects();
            routeTilePaletteInput(coordinator, *tilePaletteTileset, inputHole, inputClip,
                                  rml, tilePaletteMask, tilePaletteTexture, paletteInput);
            prevPaletteClipRect = inputClip;
        } else {
            // Not routable this frame (Play, other workspace, an asset editor
            // open, no tilemap selection): the marquee never survives, and an
            // invalid rect disables the Ctrl+wheel claim.
            cancelTilePaletteGesture(paletteInput);
        }
        prevTextFocus = rml.textFocus;

        // The service owns worker/token/batch state and returns only completions
        // validated against session, revision, asset and recipe. Raylib and the
        // persistent Command remain on this owner thread.
        if (auto completed = sfxGeneration.poll(
                coordinator.document(), projectSession.projectSessionId(),
                projectSession.currentProjectPath(), coordinator.isPlaying())) {
            if (completed->kind == GeneratedSfxServiceEventKind::Failed) {
                coordinator.logError("Generated SFX failed: " + completed->message);
                ui.notifyGeneratedSfxStatusChanged();
            } else if (completed->kind == GeneratedSfxServiceEventKind::Discarded) {
                coordinator.logWarning(
                    "Discarded Generated SFX result: " + completed->message);
            } else if (completed->kind == GeneratedSfxServiceEventKind::PreviewReady) {
                if (!IsAudioDeviceReady()) {
                    InitAudioDevice();
                    ownsAudioDevice = IsAudioDeviceReady();
                }
                const auto loaded = sfxPreview.load(completed->audio);
                if (!loaded.ok()) coordinator.logError(loaded.error().message);
                else {
                    sfxPreview.play();
                    const auto* current = coordinator.document().findGeneratedSfx(
                        completed->stamp.assetId);
                    coordinator.logInfo("Playing SFX preview: "
                        + (current ? current->name : completed->stamp.assetId));
                }
            } else {
                const auto registered = sfxOutputTransaction.commit(
                    completed->stamp.assetId, completed->stamp.recipe,
                    completed->output, completed->regenerating,
                    completed->stamp.token);
                const SfxBatchItemStatus resolution = registered.ok
                    ? SfxBatchItemStatus::Succeeded : SfxBatchItemStatus::Failed;
                const std::string resolutionMessage = registered.ok
                    ? "Ready" : (registered.error.empty()
                        ? "Output transaction failed" : registered.error);
                if (registered.ok) {
                    coordinator.logInfo(
                        (completed->regenerating ? "Regenerated WAV: "
                                                 : "Generated WAV: ")
                        + completed->output.relativePath);
                    ui.notifyGeneratedSfxOutputReady(completed->stamp.assetId);
                } else {
                    coordinator.logError(
                        "Could not commit generated audio asset: "
                        + resolutionMessage);
                }
                sfxGeneration.resolveGeneration(
                    completed->stamp.token, resolution, resolutionMessage,
                    registered.failure == GeneratedSfxOutputCommitFailure::Collision
                        ? GeneratedSfxGenerationFailureKind::Collision
                        : GeneratedSfxGenerationFailureKind::Failed);
                if (!registered.ok) ui.notifyGeneratedSfxStatusChanged();
            }
        }

        ui.setSfxBatchState(sfxGeneration.batchState());
        if (sfxBatchWasActive && !sfxGeneration.batchState().active) {
            const auto& summary = sfxGeneration.batchState();
            coordinator.logInfo(
                "SFX batch complete: " + std::to_string(summary.succeeded)
                + " succeeded, " + std::to_string(summary.skipped) + " skipped, "
                + std::to_string(summary.failed) + " failed, "
                + std::to_string(summary.cancelled) + " cancelled");
        }
        sfxBatchWasActive = sfxGeneration.batchState().active;

        ui.processFrame();
        ui.syncGeneratedSfxPreviewPlaying(sfxPreview.playing());
        // Title dirty-cue: follows undo/redo/save as well as edits, so it can't
        // hang off any single action path. Change-guarded O(1) check per frame.
        projectSession.refreshWindowTitleIfNeeded();
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
                timelineThumbRects.reserve(timelineClip->frameIds.size());
                for (std::size_t i = 0; i < timelineClip->frameIds.size(); ++i) {
                    const std::string id = "anim-frame-thumb-" + std::to_string(i);
                    timelineThumbRects.push_back(
                        elementContentRectFromDocument(animationDocument, id.c_str()));
                }
            }
        }

        // Tile Palette sheet rects for painting, re-read after host.update()
        // so raylib scissors to this frame's real layout. Tileset, texture
        // and empty-mask were resolved once in the input phase above; picker
        // sync is the input router's pan-into-view, not a DOM scroll.
        const auto [tilePaletteHoleRect, tilePaletteClipRect] = resolveTilePaletteRects();

        BeginDrawing();
        ClearBackground(Color{15, 16, 20, 255});
        const PlaySession* playSession = coordinator.playSession();
        const SpriteAnimationAssetDef* animationAsset = nullptr;
        const TilesetAsset* tilesetAsset = nullptr;
        // Logic owns an opaque RmlUi surface. Do not collect a graphical scene
        // snapshot, prepare textures, render overlays, or paint an asset editor
        // behind/over it. PlaySession ticking above remains independent.
        if (!nonSceneWorkspace) {
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
                                            coordinator.effectiveTilemapTool(),
                                            active, coordinator.selection().primaryEntity, rect,
                                            renderView, snapshot.worldSize);
                }
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
            renderTilePaletteSheet(*tilePaletteTileset, coordinator.state().tilemapEditor,
                                   tilePaletteHoleRect, tilePaletteClipRect,
                                   tilePaletteTexture, tilePaletteMask, paletteInput.marquee);
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

            static constexpr const char* requiredIds[] = {
                "status-bar", "status-health", "status-coords", "status-context",
                "status-project", "hierarchy-pane", "assets-pane",
                "assets-search-slot", "assets-filter-input",
                "inspector-section-project-toggle", "inspector-project-name",
            };
            bool requiredElementsPresent = host.document() != nullptr;
            for (const char* id : requiredIds) {
                requiredElementsPresent = requiredElementsPresent
                    && host.document()->GetElementById(id) != nullptr;
            }

            Rml::Element* projectToggle = host.document()
                ? host.document()->GetElementById("inspector-section-project-toggle") : nullptr;
            if (projectToggle) projectToggle->DispatchEvent("click", Rml::Dictionary{});
            const bool collapsedOnce = host.document()
                && host.document()->GetElementById("inspector-project-name") == nullptr;
            projectToggle = host.document()
                ? host.document()->GetElementById("inspector-section-project-toggle") : nullptr;
            if (projectToggle) projectToggle->DispatchEvent("click", Rml::Dictionary{});
            const bool expandedAgain = host.document()
                && host.document()->GetElementById("inspector-project-name") != nullptr;

            Rml::Element* assetFilter = host.document()
                ? host.document()->GetElementById("assets-filter-input") : nullptr;
            auto* assetFilterControl =
                rmlui_dynamic_cast<Rml::ElementFormControl*>(assetFilter);
            if (assetFilterControl) assetFilterControl->SetValue("lifecycle-image");
            if (assetFilter) assetFilter->DispatchEvent("change", Rml::Dictionary{});
            const bool changeCallback =
                coordinator.uiState().assetFilter == "lifecycle-image";

            // Regression smoke for the Generated SFX modal lifetime. Dispatch
            // the real RmlUi click from markup that closes on success; its
            // rebuild must wait for processFrame so the event target survives.
            ui.setProjectSavedQuery([] { return true; });
            ui.setGeneratedSfxCreateFromCurrentHandler(
                [&](const std::string& sourceId, const std::string& newId,
                    const std::string& name) {
                    return coordinator.apply(DuplicateGeneratedSfxIntent{
                        sourceId, newId, name});
                });
            ui.handleAction("create-generated-sfx", "coin", "");
            const std::size_t sfxBeforeCreateFromCurrent =
                coordinator.document().data().generatedSfx.size();
            ui.setGeneratedSfxGenerationAvailabilityQuery([](const std::string&) {
                return EditorUi::GeneratedSfxGenerationAvailability{
                    false, "Audio file already exists"};
            });
            ui.handleAction("open-sfx-create-from-current", "", "");
            Rml::Element* createFromCurrent = host.document()
                ? host.document()->GetElementById("btn-sfx-create-from-current-confirm")
                : nullptr;
            const bool hadBlockedCreateButton = createFromCurrent != nullptr;
            if (createFromCurrent)
                createFromCurrent->DispatchEvent("click", Rml::Dictionary{});
            ui.processFrame();
            const bool sfxCreationBlocked = hadBlockedCreateButton
                && coordinator.document().data().generatedSfx.size()
                    == sfxBeforeCreateFromCurrent;

            ui.setGeneratedSfxGenerationAvailabilityQuery([](const std::string&) {
                return EditorUi::GeneratedSfxGenerationAvailability{true, {}};
            });
            createFromCurrent = host.document()
                ? host.document()->GetElementById("btn-sfx-create-from-current-confirm")
                : nullptr;
            Rml::Element* createNameInput = host.document()
                ? host.document()->GetElementById("sfx-create-from-current-name")
                : nullptr;
            if (createNameInput) {
                createNameInput->Focus(true);
                createNameInput->Blur();
            }
            // Reproduce a real press/release spanning frames. Blur must not
            // rebuild and invalidate the button that is about to receive click.
            ui.processFrame();
            const bool sfxTargetSurvivedBlurFrame = createFromCurrent
                && host.document()
                && host.document()->GetElementById(
                    "btn-sfx-create-from-current-confirm") == createFromCurrent;
            const bool hadCreateFromCurrentButton = createFromCurrent != nullptr;
            if (createFromCurrent)
                createFromCurrent->DispatchEvent("click", Rml::Dictionary{});
            ui.processFrame();
            const bool sfxCreateFromCurrentDispatch = sfxCreationBlocked
                && hadCreateFromCurrentButton
                && sfxTargetSurvivedBlurFrame
                && coordinator.document().data().generatedSfx.size()
                    == sfxBeforeCreateFromCurrent + 1;

            // Deleting the last browser item clears only the selection. The
            // audio workspace is independent and remains open until Close.
            std::vector<std::string> lifecycleSfxIds;
            for (const auto& definition :
                 coordinator.document().data().generatedSfx) {
                lifecycleSfxIds.push_back(definition.id);
            }
            bool sfxDeleteClicksDispatched = !lifecycleSfxIds.empty();
            for (const std::string& id : lifecycleSfxIds) {
                Rml::Element* remove = host.document()
                    ? host.document()->GetElementById("sfx-delete-" + id)
                    : nullptr;
                sfxDeleteClicksDispatched = sfxDeleteClicksDispatched
                    && remove != nullptr;
                if (remove) remove->DispatchEvent("click", Rml::Dictionary{});
                ui.processFrame();
            }
            Rml::Element* sfxWorkspace = host.document()
                ? host.document()->GetElementById("sfx-editor") : nullptr;
            Rml::Element* sfxEmptyBody = host.document()
                ? host.document()->GetElementById("sfx-editor-body") : nullptr;
            const bool sfxDeleteKeepsWorkspaceOpen = sfxDeleteClicksDispatched
                && coordinator.document().data().generatedSfx.empty()
                && sfxWorkspace && !sfxWorkspace->IsClassSet("hidden")
                && sfxEmptyBody
                && sfxEmptyBody->GetInnerRML().find("No Generated SFX selected")
                    != std::string::npos;

            ui.detach();
            if (probe) probe->DispatchEvent("click", Rml::Dictionary{});
            const bool noCallbackAfterDetach =
                coordinator.uiState().consoleShowInfo == afterSingleDispatch;
            if (assetFilterControl) assetFilterControl->SetValue("after-detach");
            if (assetFilter) assetFilter->DispatchEvent("change", Rml::Dictionary{});
            const bool noChangeCallbackAfterDetach =
                coordinator.uiState().assetFilter == "lifecycle-image";
            ui.detach();

            EditorUi missingDocumentUi(coordinator, nullptr, nullptr, nullptr);
            missingDocumentUi.bind();
            missingDocumentUi.detach();
            missingDocumentUi.detach();

            if (!requiredElementsPresent || !collapsedOnce || !expandedAgain
                || !exactlyOneCallback || !changeCallback || !sfxCreateFromCurrentDispatch
                || !sfxDeleteKeepsWorkspaceOpen
                || !noCallbackAfterDetach || !noChangeCallbackAfterDetach || ui.isBound()) {
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

    sfxGeneration.shutdown();
    ui.setSfxBatchState(sfxGeneration.batchState());
    sfxPreview.unload();
    clearPlaySoundCache();
    if (ownsAudioDevice && IsAudioDeviceReady()) CloseAudioDevice();
    textureCache.clear();
    unloadCanvasFont(canvasFont);   // GPU atlas: released before CloseWindow
    ui.detach();
    uiOwner.reset(); // Destroy UI/controllers before host documents and context.
    host.shutdown();
    CloseWindow();
    return exitCode;
}

} // namespace ArtCade::EditorNative
