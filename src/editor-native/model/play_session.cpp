#include "editor-native/model/play_session.h"

#include "editor-native/model/path_confinement.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/project_io.h"
#include "editor-native/model/tilemap_render_view.h"

#include "app/src/gameplay_session.h"
#include "app/render/scene_frame_snapshot.h"
#include "core/engine-context.h"
#include "core/project-current-format.h"
#include "logic-core.h"
#include "modules/asset-system/include/asset-loader.h"
#include "modules/audio/include/audio.h"
#include "modules/input/include/input.h"
#include "script-core.h"
#include "script-runtime.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ArtCade::EditorNative {

namespace {

// RU-04 (docs/PLAY_RUNTIME_UNIFICATION_ROADMAP.md §11): a fresh, unique
// scratch directory per materialize() call - never reused, always removed
// before returning. Only ever holds project.json (see materialize()'s
// comment on why asset bytes never need to be copied here), so this can be
// created/torn down synchronously within a single Play-start.
std::filesystem::path makeScratchDir() {
    static std::atomic<unsigned> counter{0};
    const unsigned long pid =
#ifdef _WIN32
        static_cast<unsigned long>(::GetCurrentProcessId());
#else
        static_cast<unsigned long>(::getpid());
#endif
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::temp_directory_path(ec);
    if (ec) dir = std::filesystem::current_path();
    dir /= "artcade-play-" + std::to_string(pid) + "-"
         + std::to_string(counter.fetch_add(1, std::memory_order_relaxed));
    std::filesystem::create_directories(dir, ec);
    return dir;
}

struct ScratchDirGuard {
    std::filesystem::path dir;
    ~ScratchDirGuard() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

} // namespace

// RU-02c host-port adapter (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md 4.3),
// same shape as runtime-cpp's own app_modules.h::AudioServiceAdapter - no
// gameplay logic, only forwards Audio::update() so Logic Board audio.*
// actions can play through the real Modules::Audio this session owns.
class PlaySession::AudioServiceAdapter final : public IGameplayAudioService {
public:
    explicit AudioServiceAdapter(Modules::Audio& audio) : audio_(audio) {}
    void update() override { audio_.update(); }

private:
    Modules::Audio& audio_;
};

PlaySession::PlaySession() = default;

PlaySession::~PlaySession() { shutdownRuntime(); }

PlaySession::PlaySession(PlaySession&& other) noexcept
    : scene_(std::move(other.scene_)),
      renderEntityOrder_(std::move(other.renderEntityOrder_)),
      tilemaps_(std::move(other.tilemaps_)),
      ctx_(std::move(other.ctx_)),
      runtime_(std::move(other.runtime_)),
      audio_(std::move(other.audio_)),
      input_(std::move(other.input_)),
      audioAdapter_(std::move(other.audioAdapter_)) {}

PlaySession& PlaySession::operator=(PlaySession&& other) noexcept {
    if (this == &other) return *this;
    shutdownRuntime();
    scene_ = std::move(other.scene_);
    renderEntityOrder_ = std::move(other.renderEntityOrder_);
    tilemaps_ = std::move(other.tilemaps_);
    ctx_ = std::move(other.ctx_);
    runtime_ = std::move(other.runtime_);
    audio_ = std::move(other.audio_);
    input_ = std::move(other.input_);
    audioAdapter_ = std::move(other.audioAdapter_);
    return *this;
}

void PlaySession::shutdownRuntime() {
    if (runtime_) {
        // Same relative order as Application::shutdownModules()
        // (app_bootstrap.cpp) for the gameplay-owned graph.
        runtime_->shutdownLogicModules();
        runtime_->shutdownScriptRuntime();
        runtime_->shutdownScriptingModules();
        runtime_->shutdownGraph();
        runtime_->shutdownPhysics();
        runtime_->shutdownUtilities();
        runtime_.reset();
    }
    audioAdapter_.reset();
    if (audio_) { audio_->shutdown(); audio_.reset(); }
    if (input_) { input_->shutdown(); input_.reset(); }
}

void PlaySession::setAssetRoot(const std::filesystem::path& root) {
    if (!audio_) return;
    audio_->setAssetPathResolver([root](const std::string& ref) -> std::string {
        const PathConfinementResult resolved =
            resolvePathInsideRoot(root, std::filesystem::u8path(ref));
        return resolved.ok ? resolved.value.string() : ref;
    });
}

std::optional<PlaySession> PlaySession::materialize(
    const ProjectDocument& document,
    const SceneId& sceneId,
    const std::vector<Scripts::ScriptProgram>& scripts,
    std::string* error) {
    // Compile every Logic Board before touching the runtime. A blocking
    // diagnostic rejects Play atomically and leaves authoring untouched -
    // same contract the old hand-written materialize() had.
    const Logic::LogicCompileResult logic = Logic::compileProjectLogic(document.data());
    if (!logic.ok()) {
        if (error) {
            *error = "Cannot start Play: Logic Board validation failed";
            if (!logic.diagnostics.empty()) {
                *error += " [" + logic.diagnostics.front().code + "] "
                       + logic.diagnostics.front().message;
            }
        }
        return std::nullopt;
    }

    // Editor-only concern (game.exe loads pre-compiled bytecode from disk;
    // the editor Play button hands over the in-memory/unsaved script buffer
    // instead) - validate the snapshot is complete and each program compiles,
    // same checks the old materialize() ran, now feeding
    // GameplaySession::setScriptCatalog() instead of a private ScriptRuntime.
    std::unordered_map<AssetId, Scripts::ScriptProgram> scriptPrograms;
    if (!scripts.empty()) {
        Scripts::ScriptRuntime validator;
        for (const Scripts::ScriptProgram& program : scripts) {
            if (!scriptPrograms.emplace(program.assetId, program).second) {
                if (error) *error = "Cannot start Play: duplicate Script Program snapshot";
                return std::nullopt;
            }
            std::string scriptError;
            if (!validator.validateProgram(program, &scriptError)) {
                if (error) {
                    *error = "Cannot start Play: " + program.sourcePath + ": " + scriptError;
                }
                return std::nullopt;
            }
        }
    }
    const std::vector<AssetId> linkedScriptAssets = document.referencedScriptAssetIds(true);
    if (scriptPrograms.size() != linkedScriptAssets.size()) {
        if (error) *error = "Cannot start Play: Script Program snapshot is incomplete";
        return std::nullopt;
    }
    for (const AssetId& linked : linkedScriptAssets) {
        if (scriptPrograms.count(linked) == 0) {
            if (error) *error = "Cannot start Play: no saved snapshot for Script Asset " + linked;
            return std::nullopt;
        }
    }

    const SceneDef* scene = document.findScene(sceneId);
    if (!scene) {
        if (error) *error = "Cannot start Play: scene does not exist";
        return std::nullopt;
    }

    std::unordered_map<ObjectTypeId, std::vector<ScriptAttachmentDef>> scriptAttachments;
    for (const auto& [typeId, objectType] : document.data().objectTypes) {
        if (!objectType.scripts) continue;
        scriptAttachments.emplace(typeId, objectType.scripts->attachments);
    }
    for (const auto& [typeId, attachments] : scriptAttachments) {
        (void)typeId;
        for (const ScriptAttachmentDef& attachment : attachments) {
            if (!attachment.enabled) continue;
            if (scriptPrograms.count(attachment.scriptAssetId) == 0) {
                if (error) {
                    *error = "Cannot start Play: no saved snapshot for Script Asset "
                           + attachment.scriptAssetId;
                }
                return std::nullopt;
            }
        }
    }

    PlaySession session;
    session.scene_.sourceSceneId = scene->id;
    session.scene_.name = scene->name;
    session.scene_.worldSize = scene->worldSize;
    session.scene_.backgroundColor = scene->backgroundColor;

    // RU-04 (docs/PLAY_RUNTIME_UNIFICATION_ROADMAP.md §11): Play now parses
    // the project through the exact same canonical loader the exported game
    // uses (Modules::AssetLoader::loadDirectory), not just the same read_*
    // functions RU-01 already shares - this also exercises AssetLoader's own
    // orchestration (validate_current_project_json, manifest indexing)
    // end to end, which a direct ProjectDocument::deserialize() call never
    // touches. Round-trips through a throwaway scratch directory holding only
    // project.json: AssetLoader::parseProjectJson never reads asset bytes
    // eagerly (images/audio are resolved lazily, later, against the real
    // project root via PlaySession::setAssetRoot() - untouched by this), so
    // no asset files need to be copied here. generatedSfx does not survive
    // this round-trip (no ProjectJson::read_generated_sfx exists - the
    // exported game never rigenerates SFX, only plays the baked
    // AudioAssetDef the Generate step already produced), which is the same
    // gap the exported game already has today, not one introduced here.
    const SerializeResult serialized = ProjectSerializer::serialize(document);
    if (!serialized.ok) {
        if (error) *error = "Cannot start Play: failed to serialize project for load";
        return std::nullopt;
    }
    // Fail fast with a specific diagnostic before touching disk: the same
    // validator AssetLoader::parseProjectJson calls internally, just without
    // a generic "rejected" message hiding which field tripped it.
    {
        std::string validationError;
        nlohmann::json parsed;
        bool parseOk = true;
        try { parsed = nlohmann::json::parse(serialized.value); }
        catch (...) { parseOk = false; }
        if (!parseOk || !ProjectJson::validate_current_project_json(parsed, validationError)) {
            if (error) {
                *error = "Cannot start Play: canonical loader rejected the project"
                       + (validationError.empty() ? std::string{} : " [" + validationError + "]");
            }
            return std::nullopt;
        }
    }
    const std::filesystem::path scratchDir = makeScratchDir();
    ScratchDirGuard scratchDirGuard{scratchDir};
    {
        std::ofstream projectFile(scratchDir / "project.json", std::ios::binary);
        if (!projectFile) {
            if (error) *error = "Cannot start Play: failed to write temp project package";
            return std::nullopt;
        }
        projectFile << serialized.value;
        if (!projectFile) {
            if (error) *error = "Cannot start Play: failed to write temp project package";
            return std::nullopt;
        }
    }

    ProjectDoc doc;
    Modules::AssetLoader loader;
    loader.init();
    if (!loader.loadDirectory(scratchDir.string(), doc)) {
        if (error) *error = "Cannot start Play: canonical loader rejected the project";
        return std::nullopt;
    }

    // World::init(doc) activates whatever doc.activeSceneId names - Play
    // Current Scene requests a scene that may differ from the project's own
    // start scene, so materialize a copy with that one override (same "one
    // materialization, immutable" rule the plan requires - this copy is never
    // written back).
    doc.activeSceneId = sceneId;

    // Composition root, mirroring Application::initUtilities()/
    // initSubsystems() (app_bootstrap.cpp) step for step - the same
    // GameplaySession game.exe/WASM use, not a parallel one.
    session.runtime_ = std::make_unique<GameplaySession>();
    std::string bootError;
    const auto bootStep = [&](const char* step, bool ok) {
        if (!ok && bootError.empty()) {
            bootError = std::string("Cannot start Play: failed to initialize ") + step;
        }
        return ok;
    };
    if (!session.runtime_->initializeUtilities(bootStep)) {
        if (error) *error = bootError;
        return std::nullopt;
    }

    // Heap-allocated, owned by the returned PlaySession (see play_session.h):
    // GameAPI/LuaHost/etc. bind this EngineContext by reference and read it
    // for the whole Play session, so it must outlive this function - a
    // materialize()-local would leave them holding a dangling reference to
    // freed stack memory the instant this function returns.
    session.ctx_ = std::make_unique<EngineContext>();
    EngineContext& ctx = *session.ctx_;
    // No presentation Renderer: the editor's own Scene View draws from the
    // frame snapshot (scene_frame_snapshot.cpp), never through GameplaySession/
    // World directly - World already tolerates a null renderer (RU-02a).
    if (!session.runtime_->initialize(
            PhysicsMode::Auto, ctx, /*presentationRenderer=*/nullptr, bootStep,
            [](const Modules::SceneTransitionResult&) {})) {
        if (error) *error = bootError;
        return std::nullopt;
    }

    session.audio_ = std::make_unique<Modules::Audio>();
    // Audio device availability is host environment, not a Play precondition:
    // a missing/unavailable device degrades to silent Play Sound rather than
    // blocking Play (see setAssetRoot()'s doc comment - the same philosophy).
    session.audio_->init();
    session.audio_->setRuntimeAssetCatalog(doc.audioAssets);

    session.input_ = std::make_unique<Modules::Input>();
    if (!bootStep("input", session.input_->init())) {
        if (error) *error = bootError;
        return std::nullopt;
    }

    if (!session.runtime_->initializeGameplayModules(
            ctx, *session.audio_, *session.input_, bootStep)) {
        if (error) *error = bootError;
        return std::nullopt;
    }

    session.audioAdapter_ = std::make_unique<AudioServiceAdapter>(*session.audio_);
    session.runtime_->wireHostPorts(session.audioAdapter_.get(), nullptr, nullptr);

    if (!session.runtime_->loadLogicPrograms(logic.programs, &bootError)) {
        if (error) *error = "Cannot start Play: " + bootError;
        return std::nullopt;
    }
    session.runtime_->setScriptCatalog(std::move(scriptPrograms), std::move(scriptAttachments));
    session.runtime_->loadWorldProject(doc);
    if (!session.runtime_->installLogicScopesForActiveScene()) {
        if (error) *error = "Cannot start Play: failed to install Logic Board scopes";
        return std::nullopt;
    }
    if (!session.runtime_->installScriptScopesForActiveScene()) {
        if (error) *error = "Cannot start Play: failed to install Script scopes";
        return std::nullopt;
    }

    // ADR-0001: compile entity-owned tilemaps once into PlaySession. Missing
    // tileset/image or unresolvable TileIds skip that instance (Play still
    // starts — same lenient gate RU-03 tests lock in); painted valid maps are
    // projected into SceneFrameSnapshot each frame without rereading the
    // authoring document.
    for (const SceneInstanceDef* inst : document.instancesInRenderOrder(sceneId)) {
        session.renderEntityOrder_.push_back(inst->id);
        if (!inst->tilemap.has_value()) continue;
        const TilesetAsset* tileset =
            document.findTilesetAsset(inst->tilemap->tilesetAssetId);
        if (!tileset || !document.findImageAsset(tileset->imageAssetId)) continue;
        const std::optional<std::vector<TilemapResolvedCell>> resolved =
            resolveTilemapCellsStrict(*inst->tilemap, *tileset);
        if (!resolved) continue;
        PlayTilemap playTm;
        playTm.entityId = inst->id;
        playTm.imageAssetId = tileset->imageAssetId;
        playTm.cellSize = inst->tilemap->cellSize;
        playTm.authoredOrigin = inst->transform.position;
        playTm.visible = inst->visible;
        playTm.cells.reserve(resolved->size());
        for (const TilemapResolvedCell& cell : *resolved) {
            playTm.cells.push_back(PlayTilemapCell{
                cell.cellX, cell.cellY,
                cell.source.x, cell.source.y, cell.source.width, cell.source.height});
        }
        session.tilemaps_.push_back(std::move(playTm));
    }

    return session;
}

std::optional<PlaySession> PlaySession::startProject(const ProjectDocument& document,
                                                     std::string* error) {
    return materialize(document, document.startSceneId(), {}, error);
}

std::optional<PlaySession> PlaySession::startProject(
    const ProjectDocument& document,
    const std::vector<Scripts::ScriptProgram>& scripts,
    std::string* error) {
    return materialize(document, document.startSceneId(), scripts, error);
}

std::optional<PlaySession> PlaySession::startActiveScene(const ProjectDocument& document,
                                                        const SceneId& sceneId,
                                                        std::string* error) {
    return materialize(document, sceneId, {}, error);
}

std::optional<PlaySession> PlaySession::startActiveScene(
    const ProjectDocument& document,
    const SceneId& sceneId,
    const std::vector<Scripts::ScriptProgram>& scripts,
    std::string* error) {
    return materialize(document, sceneId, scripts, error);
}

void PlaySession::tick(const RuntimeInputSnapshot& input, float dt) {
    if (!runtime_ || !input_) return;
    input_->poll();
    GameplayInputFrame frame;
    frame.pressed = input.pressedLogicKeys;
    frame.released = input.releasedLogicKeys;
    frame.held = input.heldLogicKeys;
    runtime_->dispatchInput(frame);
    if (std::isfinite(dt) && dt > 0.f) {
        runtime_->tickFixedStep(dt);
    }
}

std::vector<Scripts::ScriptRuntimeDiagnostic> PlaySession::drainScriptDiagnostics() {
    return runtime_ ? runtime_->drainScriptDiagnostics()
                     : std::vector<Scripts::ScriptRuntimeDiagnostic>{};
}

std::vector<RenderableEntitySnapshot> PlaySession::renderables() const {
    if (!runtime_) return {};
    auto out = runtime_->buildFrameSnapshot(::ArtCade::SceneFrameSnapshot{}).renderables;
    return out;
}

} // namespace ArtCade::EditorNative
