#pragma once

#include "../../../core/module.h"
#include "../../../core/engine-context.h"
#include "../../sprite-animator/include/sprite-animator.h"
#include <cstdint>
#include <vector>

// Forward-declare sol::state — avoids pulling sol2 into every includer
namespace sol { class state; }

namespace ArtCade::Modules {

/**
 * GameAPI — Lua binding layer.
 *
 * This is the single entry point for registering ALL game functions into the
 * Lua VM.  Actual binding logic is split by domain into separate .cpp files:
 *
 *   entity-api.cpp   — entity.* / object.* / pool.*
 *   physics-api.cpp  — collision.* / physics.*
 *   input-api.cpp    — input.*
 *   audio-api.cpp    — audio.*
 *   text-api.cpp     — text.*
 *   state-api.cpp    — state.*
 *   debug-api.cpp    — debug.*
 *   save-api.cpp     — save.*
 *   event-api.cpp    — event.*  (pure-Lua EventBus)
 *   time-api.cpp     — time.*   (pure-Lua timer system)
 *   camera-api.cpp   — camera.* (via Renderer)
 *   animation-api.cpp  — animation.onFinished / animation.pollFinished
 *   lifecycle-api.cpp — lifecycle.pollDestroyed
 *   grid-api.cpp       — grid.* (snap, offset, space check)
 *   component-api.cpp  — linearMover.* / magnet.* / horde.* / autoDestroy.*
 *                        / platformer.isGrounded
 *
 * game-api.cpp calls each sub-binder in sequence — it stays < 50 lines.
 */
class GameAPI final : public IModule {
public:
    explicit GameAPI(const EngineContext& ctx);

    bool init() override;
    void shutdown() override;

    // Register every category into the provided Lua state
    void registerAll(sol::state& lua);

    /** Drain entity lifecycle events from the gateway and route them to
     *  Lua handlers registered via lifecycle.onSpawn / lifecycle.onDestroy.
     *  Called once per fixed step from the main loop, after
     *  flushEntityQueues so spawn → destroy in the same frame are visible
     *  in arrival order. No-op when the Lua state has not been bound yet. */
    uint32_t dispatchLifecycleEvents();
    uint32_t dispatchInputEvents();
    /** Polls animator buffers then dispatches to Lua animation handlers. */
    uint32_t dispatchAnimationEvents();
    /**
     * Dispatches already-drained animation events to Lua (no poll).
     * Refreshes the animator watched-kinds mask from registered handlers.
     */
    uint32_t dispatchAnimationEvents(
        const std::vector<SpriteAnimator::FinishEvent>& finished,
        const std::vector<SpriteAnimator::AnimEvent>& events);

private:
    const EngineContext& ctx_;
    sol::state*          luaState_ = nullptr; // cached in registerAll()
    std::vector<uint32_t> eventBridgeTokens_;

    void bindEntityAPI (sol::state& lua);
    void bindPhysicsAPI(sol::state& lua);
    void bindInputAPI  (sol::state& lua);
    void bindIntentAPI (sol::state& lua);
    void bindAudioAPI  (sol::state& lua);
    void bindTextAPI   (sol::state& lua);
    void bindStateAPI  (sol::state& lua);
    void bindDebugAPI  (sol::state& lua);
    void bindSaveAPI   (sol::state& lua);
    void bindEventAPI  (sol::state& lua);
    void bindTimeAPI   (sol::state& lua);
    void bindCameraAPI (sol::state& lua);
    void bindAnimationAPI  (sol::state& lua);
    void bindLifecycleAPI  (sol::state& lua);
    void bindGridAPI       (sol::state& lua);
    void bindShaderAPI     (sol::state& lua);
    void bindComponentAPI  (sol::state& lua);
    void bindDialogAPI     (sol::state& lua);
};

} // namespace ArtCade::Modules
