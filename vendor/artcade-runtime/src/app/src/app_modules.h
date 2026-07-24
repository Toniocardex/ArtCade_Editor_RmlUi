#pragma once

#include "../include/app.h"

#include "../../modules/asset-system/include/asset-loader.h"
#include "../../modules/audio/include/audio.h"
#include "../../modules/camera-manager/include/camera-manager.h"
#include "../../modules/dialog/include/dialog-manager.h"
#include "../../modules/event-bus/include/event-bus.h"
#include "../../modules/game-state/include/game-state-manager.h"
#include "../../modules/input/include/input.h"
#include "../../modules/presentation/include/editor_viewport_service.h"
#include "../../modules/renderer/include/renderer.h"
#include "../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../modules/save-load/include/save-load-manager.h"
#include "../../modules/scene-system/include/scene-manager.h"
#include "../../modules/sprite-animator/include/sprite-animator.h"
#include "../../modules/texture-manager/include/texture-manager.h"
#include "../../modules/time/include/time-manager.h"
#include "../../modules/tween-manager/include/tween-manager.h"
#include "../../modules/variable-manager/include/variable-manager.h"

#include "gameplay_session.h"

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace ArtCade {

// RU-02c host-port adapters (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md 4.3):
// thin, stateless-beyond-the-reference translations from GameplaySession's
// narrow ports to the concrete modules Application still owns. No gameplay
// logic lives here - each adapter only forwards.
class AudioServiceAdapter final : public IGameplayAudioService {
public:
    explicit AudioServiceAdapter(Modules::Audio& audio) : audio_(audio) {}
    void update() override { audio_.update(); }

private:
    Modules::Audio& audio_;
};

class DialogGateAdapter final : public IGameplayDialogGate {
public:
    explicit DialogGateAdapter(Modules::DialogManager& dialog) : dialog_(dialog) {}
    bool blocksGameplay() const override { return dialog_.isBlocking(); }
    void tick(float dt) override { dialog_.tick(dt); }

private:
    Modules::DialogManager& dialog_;
};

class ProfilerSinkAdapter final : public IRuntimeProfilerSink {
public:
    explicit ProfilerSinkAdapter(RuntimeProfiler& profiler) : profiler_(profiler) {}
    void addGameplayMs(double ms) override { profiler_.addGameplayMs(ms); }
    void addPhysicsMs(double ms) override { profiler_.addPhysicsMs(ms); }
    void addLuaMs(double ms) override { profiler_.addLuaMs(ms); }
    void addLuaEvents(std::uint32_t count) override { profiler_.addLuaEvents(count); }
    void setLuaTickEnabled(bool enabled) override { profiler_.setLuaTickEnabled(enabled); }

private:
    RuntimeProfiler& profiler_;
};

// RU-02e-2: RuntimeLogicHostAdapter now lives in gameplay_session.h/.cpp -
// GameplaySession owns the instance directly (docs/RU02_GAMEPLAY_SESSION_
// REFACTOR.md, editor repo, RU-02e).

/** Internal ownership table shared only by Application implementation units. */
struct Application::Modules {
    std::unique_ptr<ArtCade::Presentation::EditorViewportService> editorViewport;
    std::unique_ptr<ArtCade::Modules::Renderer> renderer;
    std::unique_ptr<ArtCade::Modules::Input> input;
    std::unique_ptr<ArtCade::Modules::Audio> audio;
    // D-20 (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo, debt
    // register, "Accesso mutabile agli interni della sessione"): Physics/
    // SceneMutationService/World/RuntimeLogicHostAdapter/LogicRuntime/
    // GameAPI/LuaHost/ScriptRuntime - and the Logic/Script scope bookkeeping
    // that used to live here (logicScopes/logicObjectTypes/scriptPrograms/
    // scriptAttachments) - no longer have Application-level aliases at all.
    // GameplaySession dropped the physics()/sceneMutation()/logicHost()/
    // logicRuntime()/gameAPI()/luaHost()/world() accessors that used to hand
    // out live mutable references to them; Application now calls narrow,
    // intent-named GameplaySession methods instead (setGravity/
    // loadLogicPrograms/loadLuaSource/installLogicScopesForActiveScene/etc.),
    // and the scope bookkeeping moved into GameplaySession alongside them
    // (simulation state living across ticks, not host bookkeeping - same
    // rule that already moved activeGameplayCollisionPairs there in RU-02c).
    // SceneManager/RuntimeEntityGateway/SpriteAnimator keep their aliases
    // below - RU-02g already approved render passes reading them live
    // (editor-overlay/authoring-adjacent data, not mutable simulation
    // control), so removing those would just break approved architecture.
    ArtCade::Modules::SceneManager* sceneManager = nullptr;
    ArtCade::Modules::RuntimeEntityGateway* entityGateway = nullptr;
    std::unique_ptr<ArtCade::Modules::AssetLoader> assetLoader;

    // RU-02f: the remaining utility modules are owned by gameplaySession too
    // now (Application::initUtilities() constructs the session and calls
    // GameplaySession::initializeUtilities() instead of building these
    // directly) - same non-owning-alias pattern as every module above.
    ArtCade::Modules::TimeManager* timeManager = nullptr;
    ArtCade::Modules::EventBus* eventBus = nullptr;
    ArtCade::Modules::VariableManager* variableManager = nullptr;
    ArtCade::Modules::GameStateManager* gameStateManager = nullptr;
    std::unique_ptr<ArtCade::Modules::TextureManager> textureManager;
    ArtCade::Modules::SpriteAnimator* spriteAnimator = nullptr;
    ArtCade::Modules::CameraManager* cameraManager = nullptr;
    ArtCade::Modules::TweenManager* tweenManager = nullptr;
    ArtCade::Modules::SaveLoadManager* saveLoadManager = nullptr;
    std::unique_ptr<ArtCade::Modules::DialogManager> dialogManager;

    // RU-02f: the three remaining host-port adapters GameplaySession consumes
    // via wireHostPorts() (GameplayRuntimeRefs, T-01, is eliminated).
    std::unique_ptr<AudioServiceAdapter> audioAdapter;
    std::unique_ptr<DialogGateAdapter> dialogAdapter;
    std::unique_ptr<ProfilerSinkAdapter> profilerAdapter;
    std::unique_ptr<GameplaySession> gameplaySession;
};

} // namespace ArtCade
