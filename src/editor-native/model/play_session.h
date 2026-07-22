#pragma once

// RU-03 (docs/PLAY_RUNTIME_UNIFICATION_ROADMAP.md §10, RU02_GAMEPLAY_SESSION_
// REFACTOR.md §7/§14 D-01): PlaySession is now a thin facade over the real
// GameplaySession (runtime-cpp/src/app/src/gameplay_session.h) - the same
// simulation game.exe/WASM use - instead of its own hand-written runtime
// (RuntimeEntity/RuntimeScene ECS, kinematic movement, AABB collision
// resolution, a private Logic/Script host). Public API kept intentionally
// close to the old shape so editor_coordinator.h/scene_frame_snapshot.h don't
// need to change: startProject/startActiveScene, sceneId(), scene() (now a
// small PlaySceneInfo instead of the full RuntimeScene - no external caller
// ever read individual entities through it, only aggregate metadata),
// tick() (replaces advance()+update() - both were driven by the same
// GetFrameTime() call anyway), drainScriptDiagnostics(), and the render
// hand-off buildFrame() for scene_frame_snapshot.cpp.
//
// Removed (verified zero external callers before deleting): entities(),
// findEntity(), drainAudioCommands(), drainAnimationEvents(), scriptRuntime(),
// assets()/PlayAssetCatalogSnapshot (only ever fed the now-removed Play Sound
// preload cache - see play_sound_preload.h's removal). Play Sound now plays
// through the real Modules::Audio this class owns, exactly like game.exe.
//
// Known gap closed for *editor Play Scene View only* (ADR-0001 Slice 8):
// entity-owned TilemapComponent cells are materialized once into PlaySession
// (PlayTilemap) and projected into SceneFrameSnapshot.tilemaps each frame.
// game.exe / shared World still only render the legacy scene-level grid —
// that remains cross-repo debt, not reintroduced here as a second World path.
//
// runtime_/audio_/input_ are owned via unique_ptr (not by value): none of
// GameplaySession/Modules::Audio/Modules::Input were designed to be moved
// (GameplaySession has a user-declared destructor, which suppresses the
// implicit move constructor; Application itself only ever owns them via
// unique_ptr too - app_modules.h). PlaySession still needs to be movable
// (EditorCoordinator stores it in a std::optional it emplaces/reassigns), so
// owning them behind a pointer sidesteps the question entirely instead of
// adding move support those classes were never meant to need.

#include "core/types.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade {
class GameplaySession;
struct RenderableEntitySnapshot;
struct GameplayInputFrame;
struct EngineContext;
namespace Modules {
class Audio;
class Input;
}
namespace Scripts {
class ScriptRuntime;
struct ScriptProgram;
struct ScriptRuntimeDiagnostic;
}
namespace Logic {
struct LogicProgram;
}
} // namespace ArtCade

namespace ArtCade::EditorNative {

class ProjectDocument;

// Per-frame gameplay input, built by the application from the platform and
// fed to the session. PlaySession stays free of Raylib/RmlUi.
struct RuntimeInputSnapshot {
    // Edge-triggered Logic Board keys in deterministic registry order - the
    // only input GameplaySession consumes (GameplayInputFrame), same as
    // game.exe. Movement is authored via Logic Board/Script (input.key_held
    // -> platformer.move or similar), never hardcoded in the host.
    std::vector<LogicKey> pressedLogicKeys;
    std::vector<LogicKey> releasedLogicKeys;
    std::vector<LogicKey> heldLogicKeys;
};

// Scene metadata needed by scene_view_interaction.cpp/editor_ui.cpp/
// scene_frame_snapshot.cpp - replaces RuntimeScene (which also carried the
// entity list; nothing outside PlaySession ever read entities through it).
struct PlaySceneInfo {
    SceneId sourceSceneId;
    std::string name;
    Vec2 worldSize;
    Vec4 backgroundColor;
};

// One populated tilemap cell in *local* cell coordinates — never world-space.
// World destinations are recomputed each Play frame from the owning entity's
// current transform (renderables when present, else authored origin).
struct PlayTilemapCell {
    int cellX = 0;
    int cellY = 0;
    float sourceX = 0.f;
    float sourceY = 0.f;
    float sourceW = 0.f;
    float sourceH = 0.f;
};

// Independent copy of a TilemapComponent compiled once at Start Play
// (ADR-0001). No pointer back to ProjectDocument.
struct PlayTilemap {
    EntityId entityId = INVALID_ENTITY;
    AssetId  imageAssetId;
    Vec2     cellSize{32.f, 32.f};
    Vec2     authoredOrigin{};   // fallback when the entity is not in renderables
    bool     visible = true;     // authored root visibility fallback
    std::vector<PlayTilemapCell> cells;
};

// Runtime side of Play/Stop. Built once from ProjectDocument at Start Play
// (materialize), then draw/tick read this session and never the authoring
// document.
class PlaySession {
public:
    ~PlaySession();
    PlaySession(PlaySession&& other) noexcept;
    PlaySession& operator=(PlaySession&& other) noexcept;
    PlaySession(const PlaySession&) = delete;
    PlaySession& operator=(const PlaySession&) = delete;

    static std::optional<PlaySession> startProject(const ProjectDocument& document,
                                                   std::string* error = nullptr);
    static std::optional<PlaySession> startProject(
        const ProjectDocument& document,
        const std::vector<Scripts::ScriptProgram>& scripts,
        std::string* error = nullptr);

    static std::optional<PlaySession> startActiveScene(const ProjectDocument& document,
                                                       const SceneId& sceneId,
                                                       std::string* error = nullptr);
    static std::optional<PlaySession> startActiveScene(
        const ProjectDocument& document,
        const SceneId& sceneId,
        const std::vector<Scripts::ScriptProgram>& scripts,
        std::string* error = nullptr);

    const SceneId& sceneId() const { return scene_.sourceSceneId; }
    const PlaySceneInfo& scene() const { return scene_; }

    // Layer back-to-front entity ids captured at materialize (render-only).
    const std::vector<EntityId>& renderEntityOrder() const { return renderEntityOrder_; }
    // Entity-owned tilemaps materialized once; empty cells = invisible in Play.
    const std::vector<PlayTilemap>& tilemaps() const { return tilemaps_; }

    // Wires Play Sound (Logic Board audio.playSound / Script ctx:playSound)
    // to resolve relative asset sourcePaths against the real project
    // directory - EditorCoordinator calls this once right after a successful
    // start, before the first tick(), using the same confinement helper
    // (resolvePathInsideRoot) the old Play Sound preload cache used. Optional:
    // if never called (e.g. every existing headless test that only checks
    // materialize() success), Play Sound resolves nothing and silently no-ops
    // instead of crashing - audio hardware/asset-root availability degrades
    // gracefully rather than blocking Play.
    void setAssetRoot(const std::filesystem::path& root);

    // Replaces advance(dt)+update(input,dt): both used to run once per frame
    // driven by the same GetFrameTime() anyway (no fixed-step accumulator
    // existed before this either - not a behavior regression to keep it
    // that way here). Builds a GameplayInputFrame from the Logic keys and
    // drives the same dispatchInput()+tickFixedStep() sequence game.exe uses.
    void tick(const RuntimeInputSnapshot& input, float dt);

    std::vector<Scripts::ScriptRuntimeDiagnostic> drainScriptDiagnostics();

    // Render hand-off for scene_frame_snapshot.cpp's Play overload: resolves
    // gameplay-visible entities (transform/sprite/visibility) via the same
    // GameplaySession::buildFrameSnapshot() the shared renderer uses. const:
    // GameplaySession's own buildFrameSnapshot() is const (reads, never
    // mutates), so this stays callable through EditorCoordinator::
    // playSession()'s const pointer. Defined out-of-line (gameplay_session.h/
    // scene_frame_snapshot.h stay out of this header - only forward-declared -
    // so editor translation units including play_session.h don't pull in the
    // whole runtime composition root transitively).
    // Returns just the renderables, not the runtime's whole SceneFrameSnapshot:
    // ArtCade::SceneFrameSnapshot shares its bare name with
    // ArtCade::EditorNative::SceneFrameSnapshot, and forward-declaring it here
    // would make it visible (via `using namespace ArtCade`) to every test file
    // that also has `using namespace ArtCade::EditorNative` - an unqualified
    // ambiguity fully-qualifying this declaration cannot fix, since the
    // ambiguity comes from the forward declaration existing at all, not from
    // how it is referenced. RenderableEntitySnapshot has no such collision.
    std::vector<RenderableEntitySnapshot> renderables() const;
    /** Runtime camera state; presentation reads it but never writes it. */
    Vec2 cameraCenter() const;

private:
    PlaySession();
    static std::optional<PlaySession> materialize(const ProjectDocument& document,
                                                  const SceneId& sceneId,
                                                  const std::vector<Scripts::ScriptProgram>& scripts,
                                                  std::string* error);
    void shutdownRuntime();

    PlaySceneInfo scene_;
    std::vector<EntityId> renderEntityOrder_;
    std::vector<PlayTilemap> tilemaps_;
    // Heap-allocated (not a materialize()-local): GameplaySession's
    // sub-modules (GameAPI, LuaHost, ...) bind ctx by reference and read it
    // for the whole Play session, so its storage must outlive materialize()
    // and live exactly as long as runtime_ does. Declared before runtime_ so
    // it's destroyed *after* runtime_ (members unwind in reverse declaration
    // order) - runtime_'s own shutdown may still touch ctx-bound state.
    std::unique_ptr<EngineContext> ctx_;
    std::unique_ptr<GameplaySession> runtime_;
    std::unique_ptr<Modules::Audio> audio_;
    std::unique_ptr<Modules::Input> input_;
    class AudioServiceAdapter;
    std::unique_ptr<AudioServiceAdapter> audioAdapter_;
};

} // namespace ArtCade::EditorNative
