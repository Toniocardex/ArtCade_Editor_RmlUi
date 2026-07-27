#include "../include/logic-runtime.h"

#include "lua-host.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ArtCade::Logic {
namespace {

thread_local int64_t g_instructionBudget = 0;

void instructionHook(lua_State* state, lua_Debug*) {
    g_instructionBudget -= 1000;
    if (g_instructionBudget <= 0) luaL_error(state, "Logic callback instruction budget exceeded");
}

std::string callbackError(const std::string& ruleId, EntityId owner, const std::string& error) {
    std::ostringstream out;
    out << "Logic rule " << ruleId << " on entity " << owner << " disabled: " << error;
    return out.str();
}

// Capabilities this LogicRuntime build knows how to execute. A program
// compiled by a newer logic-core (declaring a feature this runtime predates)
// must be rejected up front rather than silently calling an undefined Lua
// method at dispatch time.
const std::unordered_set<std::string>& supportedFeatures() {
    static const std::unordered_set<std::string> value{
        "event.start",
        "event.on_update",
        "event.every_seconds",
        "input.key_pressed",
        "input.key_released",
        "input.key_held",
        "input.key_down",
        "entity.visibility",
        "entity.transform",
        "entity.spawn",
        "sprite.facing",
        "physics.set_velocity",
        "platformer.grounded",
        "platformer.falling",
        "platformer.motion_state",
        "platformer.move",
        "platformer.jump",
        "topdown.move",
        "collision.enter",
        "collision.exit",
        "collision.other_type",
        "collision.destroy_other",
        "entity.destroy",
        "animation.play_clip",
        "animation.stop",
        "animation.set_playback_speed",
        "animation.on_started",
        "animation.on_finished",
        "audio.play_sound",
        "scene.restart",
        "scene.go_to",
        "flow.wait",
        "state.set_number",
        "state.add_number",
        "state.toggle_boolean",
        "state.compare_number",
        "state.compare_boolean",
        "state.compare_string",
        "logic.execution.once_per_activation",
    };
    return value;
}

} // namespace

struct LogicRuntime::Impl {
    enum class EventKind {
        Start,
        Update,
        EverySeconds,
        KeyPressed,
        KeyReleased,
        KeyHeld,
        CollisionEnter,
        CollisionExit,
        AnimationStarted,
        AnimationFinished,
    };

    struct ContextProxy;

    struct RuleExecutionState {
        bool activationLatched = false;
    };

    struct Scope {
        ScopeToken token = 0;
        ObjectTypeId objectTypeId;
        EntityId owner = INVALID_ENTITY;
        bool active = true;
        // Lua closures retain `context`; keep the referenced C++ object at a
        // stable heap address for the complete scope lifetime.
        std::unique_ptr<ContextProxy> context;
        // Rising-edge gate state for OncePerActivation (per rule, per instance).
        std::unordered_map<LogicRuleId, RuleExecutionState> ruleStates;
        // ADR-0028: deterministic board-instance RNG (never math.random).
        uint32_t rngState = 1u;
    };

    struct Subscription {
        uint64_t token = 0;
        ScopeToken scope = 0;
        EntityId owner = INVALID_ENTITY;
        LogicRuleId ruleId;
        EventKind kind = EventKind::Start;
        LogicKey key = LogicKey::Space;
        float intervalSeconds = 0.f;
        float accumulated = 0.f;
        sol::protected_function callback;
        bool active = true;
    };

    struct DelayedCallback {
        uint64_t token = 0;
        ScopeToken scope = 0;
        EntityId owner = INVALID_ENTITY;
        float remaining = 0.f;
        sol::protected_function callback;
        bool active = true;
    };

    struct Factory {
        LogicBoardId boardId;
        sol::protected_function function;
    };

    struct SelfProxy {
        Impl* impl = nullptr;
        EntityId owner = INVALID_ENTITY;

        void setVisible(bool value) {
            if (!impl || !impl->host.setVisible(owner, value))
                throw sol::error("set_visible failed for owner");
        }
        bool isVisible() {
            return impl && impl->host.isVisible(owner);
        }
        void setFlipX(bool flipX) {
            if (!impl || !impl->host.setSpriteFlipX(owner, flipX))
                throw sol::error("set_flip_x failed for owner");
        }
        void setPosition(float x, float y) {
            if (!impl || !impl->host.setPosition(owner, Vec2{x, y}))
                throw sol::error("set_position failed for owner");
        }
        float getPositionX() {
            const auto position = impl ? impl->host.getPosition(owner) : std::nullopt;
            if (!position) throw sol::error("get_position_x failed for owner");
            return position->x;
        }
        float getPositionY() {
            const auto position = impl ? impl->host.getPosition(owner) : std::nullopt;
            if (!position) throw sol::error("get_position_y failed for owner");
            return position->y;
        }
        void translate(float x, float y) {
            if (!impl || !impl->host.translate(owner, Vec2{x, y}))
                throw sol::error("translate failed for owner");
        }
        void setRotation(float radians) {
            if (!impl || !impl->host.setRotation(owner, radians))
                throw sol::error("set_rotation failed for owner");
        }
        void rotateBy(float deltaRadians) {
            if (!impl || !impl->host.rotateBy(owner, deltaRadians))
                throw sol::error("rotate_by failed for owner");
        }
        void setScale(float x, float y) {
            if (!impl || !impl->host.setScale(owner, Vec2{x, y}))
                throw sol::error("set_scale failed for owner");
        }
        void setVelocity(float x, float y) {
            if (!impl || !impl->host.setVelocity(owner, Vec2{x, y}))
                throw sol::error("set_velocity failed for owner");
        }
        void spawn(const std::string& objectTypeId, float x, float y) {
            if (!impl || impl->host.spawnObjectType(owner, objectTypeId, x, y) == INVALID_ENTITY)
                throw sol::error("spawn failed for owner");
        }
        bool isGrounded() {
            return impl && impl->host.isGrounded(owner);
        }
        bool isFalling() {
            return impl && impl->host.isFalling(owner);
        }
        std::string platformerState() {
            if (!impl) return "Stopped";
            switch (impl->host.platformerState(owner)) {
            case PlatformerState::Moving:  return "Moving";
            case PlatformerState::Jumping: return "Jumping";
            case PlatformerState::Falling: return "Falling";
            case PlatformerState::Stopped:
            default: return "Stopped";
            }
        }
        bool isPlatformerMoving() {
            return impl && impl->host.isPlatformerMoving(owner);
        }
        void platformerMove(float axis) {
            if (!impl || !impl->host.requestPlatformerMove(owner, axis))
                throw sol::error("platformer_move failed for owner");
        }
        void topDownMove(float x, float y) {
            if (!impl || !impl->host.requestTopDownMove(owner, Vec2{x, y}))
                throw sol::error("topdown_move failed for owner");
        }
        void platformerJump() {
            if (!impl || !impl->host.requestPlatformerJump(owner))
                throw sol::error("platformer_jump failed for owner");
        }
        void destroySelf() {
            if (!impl || !impl->host.requestDestroy(owner))
                throw sol::error("destroy_self failed for owner");
        }
        void playAnimationClip(const std::string& animationAssetId, const std::string& clipId) {
            if (!impl || !impl->host.playAnimationClip(owner, animationAssetId, clipId))
                throw sol::error("play_animation_clip failed for owner");
        }
        void stopAnimation() {
            if (!impl || !impl->host.stopAnimation(owner))
                throw sol::error("stop_animation failed for owner");
        }
        void setAnimationPlaybackSpeed(float speed) {
            if (!impl || !impl->host.setAnimationPlaybackSpeed(owner, speed))
                throw sol::error("set_animation_playback_speed failed for owner");
        }
        void playSound(const std::string& audioAssetId, float volume) {
            if (!impl || !impl->host.playSound(owner, audioAssetId, volume))
                throw sol::error("play_sound failed for owner");
        }
    };

    struct ContextProxy {
        Impl* impl = nullptr;
        ScopeToken scope = 0;
        EntityId owner = INVALID_ENTITY;
        SelfProxy self;

        void onStart(const std::string& ruleId, sol::protected_function callback) {
            impl->addSubscription(scope, owner, ruleId, EventKind::Start,
                                  LogicKey::Space, std::move(callback));
        }
        void onUpdate(const std::string& ruleId, sol::protected_function callback) {
            impl->addSubscription(scope, owner, ruleId, EventKind::Update,
                                  LogicKey::Space, std::move(callback));
        }
        void onEverySeconds(const std::string& ruleId, double seconds,
                            sol::protected_function callback) {
            const float interval = seconds > 0.0 ? static_cast<float>(seconds) : 0.f;
            impl->addSubscription(scope, owner, ruleId, EventKind::EverySeconds,
                                  LogicKey::Space, std::move(callback), interval);
        }
        void onAnimationStarted(const std::string& ruleId, sol::protected_function callback) {
            impl->addSubscription(scope, owner, ruleId, EventKind::AnimationStarted,
                                  LogicKey::Space, std::move(callback));
        }
        void onAnimationFinished(const std::string& ruleId, sol::protected_function callback) {
            impl->addSubscription(scope, owner, ruleId, EventKind::AnimationFinished,
                                  LogicKey::Space, std::move(callback));
        }
        void onKeyPressed(const std::string& ruleId, const std::string& keyName,
                          sol::protected_function callback) {
            const std::optional<LogicKey> key = logicKeyFromName(keyName);
            if (!key) throw sol::error("Unsupported Logic key: " + keyName);
            impl->addSubscription(scope, owner, ruleId, EventKind::KeyPressed,
                                  *key, std::move(callback));
        }
        void onKeyReleased(const std::string& ruleId, const std::string& keyName,
                           sol::protected_function callback) {
            const std::optional<LogicKey> key = logicKeyFromName(keyName);
            if (!key) throw sol::error("Unsupported Logic key: " + keyName);
            impl->addSubscription(scope, owner, ruleId, EventKind::KeyReleased, *key, std::move(callback));
        }
        void onKeyHeld(const std::string& ruleId, const std::string& keyName,
                       sol::protected_function callback) {
            const std::optional<LogicKey> key = logicKeyFromName(keyName);
            if (!key) throw sol::error("Unsupported Logic key: " + keyName);
            impl->addSubscription(scope, owner, ruleId, EventKind::KeyHeld, *key, std::move(callback));
        }
        void onCollisionEnter(const std::string& ruleId, sol::protected_function callback) {
            impl->addSubscription(scope, owner, ruleId, EventKind::CollisionEnter,
                                  LogicKey::Space, std::move(callback));
        }
        void onCollisionExit(const std::string& ruleId, sol::protected_function callback) {
            impl->addSubscription(scope, owner, ruleId, EventKind::CollisionExit,
                                  LogicKey::Space, std::move(callback));
        }
        void wait(double seconds, sol::protected_function callback) {
            if (!impl) throw sol::error("Logic context is inactive");
            if (!callback.valid()) throw sol::error("Logic wait callback is invalid");
            Scope* activeScope = impl->findScope(scope);
            if (!activeScope || !activeScope->active) throw sol::error("Logic scope is inactive");
            const float remaining = seconds > 0.0 ? static_cast<float>(seconds) : 0.f;
            impl->delayedCallbacks.push_back(DelayedCallback{
                impl->nextDelayed++, scope, owner, remaining, std::move(callback), true});
        }
        void stateSetNumber(const std::string& key, double value) {
            if (!impl || !impl->host.setStateNumber(key, value))
                throw sol::error("state_set_number failed");
        }
        void stateAddNumber(const std::string& key, double delta) {
            if (!impl || !impl->host.addStateNumber(key, delta))
                throw sol::error("state_add_number failed");
        }
        void stateToggleBoolean(const std::string& key) {
            if (!impl || !impl->host.toggleStateBoolean(key))
                throw sol::error("state_toggle_boolean failed");
        }
        bool stateCompareNumber(const std::string& key, const std::string& op, double value) {
            if (!impl) return false;
            const auto current = impl->host.getStateNumber(key);
            if (!current) return false;
            if (op == "==") return *current == value;
            if (op == "!=") return *current != value;
            if (op == "<") return *current < value;
            if (op == "<=") return *current <= value;
            if (op == ">") return *current > value;
            if (op == ">=") return *current >= value;
            throw sol::error("Unsupported state_compare_number operator: " + op);
        }
        bool stateCompareBoolean(const std::string& key, bool expected) {
            if (!impl) return false;
            const auto current = impl->host.getStateBoolean(key);
            if (!current) return false;
            return *current == expected;
        }
        bool stateCompareString(const std::string& key, const std::string& op,
                                const std::string& value) {
            if (!impl) return false;
            const auto current = impl->host.getStateString(key);
            if (!current) return false;
            if (op == "==") return *current == value;
            if (op == "!=") return *current != value;
            throw sol::error("Unsupported state_compare_string operator: " + op);
        }
        void sceneRestart() {
            if (!impl || !impl->host.requestSceneRestart())
                throw sol::error("scene_restart failed");
        }
        void sceneGoTo(const std::string& sceneId) {
            if (!impl || !impl->host.requestSceneGoTo(sceneId))
                throw sol::error("scene_go_to failed for scene: " + sceneId);
        }
        bool isKeyDown(const std::string& keyName) {
            const std::optional<LogicKey> key = logicKeyFromName(keyName);
            if (!key) throw sol::error("Unsupported Logic key: " + keyName);
            return impl && impl->host.isKeyDown(*key);
        }
        bool otherIsObjectType(EntityId other, const std::string& objectTypeId) {
            return impl && other != INVALID_ENTITY && !objectTypeId.empty()
                && impl->host.isObjectType(other, objectTypeId);
        }
        void destroyOther(EntityId other) {
            if (!impl || other == INVALID_ENTITY || !impl->host.requestDestroy(other))
                throw sol::error("destroy_other failed");
        }
        float sceneWorldWidth() {
            const auto size = impl ? impl->host.getSceneWorldSize() : std::nullopt;
            if (!size) throw sol::error("scene_world_width unavailable");
            return size->x;
        }
        float sceneWorldHeight() {
            const auto size = impl ? impl->host.getSceneWorldSize() : std::nullopt;
            if (!size) throw sol::error("scene_world_height unavailable");
            return size->y;
        }
        double getGlobalNumber(const std::string& key) {
            if (!impl) throw sol::error("get_global_number failed");
            const auto value = impl->host.getStateNumber(key);
            if (!value) throw sol::error("get_global_number failed for key: " + key);
            return *value;
        }
        double getLocalNumber(const std::string& key) {
            if (!impl) throw sol::error("get_local_number failed");
            const auto value = impl->host.getLocalNumber(owner, key);
            if (!value) throw sol::error("get_local_number failed for key: " + key);
            return *value;
        }
        double randomRange(double minimum, double maximum) {
            if (!impl) throw sol::error("random_range failed");
            Scope* active = impl->findScope(scope);
            if (!active || !active->active) throw sol::error("random_range scope inactive");
            if (!(minimum <= maximum) || !std::isfinite(minimum) || !std::isfinite(maximum))
                return std::numeric_limits<double>::quiet_NaN();
            if (minimum == maximum) return minimum;
            // xorshift32
            uint32_t& s = active->rngState;
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            if (s == 0) s = 1;
            const double unit = (s & 0x00FFFFFFu) / static_cast<double>(0x01000000u);
            return minimum + (maximum - minimum) * unit;
        }
        /**
         * Rising-edge execution gate for the complete WHEN expression.
         * Pulse triggers treat every event as a fresh activation; Level triggers
         * latch while WHEN stays true and rearm when it becomes false.
         */
        bool shouldExecute(const std::string& ruleId, const std::string& modeName,
                           const std::string& activationKindName, bool whenActive) {
            if (!impl) return false;
            const auto mode = logicExecutionModeFromString(modeName);
            const auto activationKind =
                logicTriggerActivationKindFromString(activationKindName);
            if (!mode || !activationKind) {
                throw sol::error("Unsupported Logic execution gate arguments");
            }
            if (*mode == LogicExecutionMode::EveryOccurrence) return whenActive;
            if (*activationKind == LogicTriggerActivationKind::Pulse) return whenActive;
            Scope* scope = impl->findScope(this->scope);
            if (!scope || !scope->active) return false;
            RuleExecutionState& state = scope->ruleStates[ruleId];
            if (!whenActive) {
                state.activationLatched = false;
                return false;
            }
            if (state.activationLatched) return false;
            state.activationLatched = true;
            return true;
        }
    };

    Impl(ILogicRuntimeHost& runtimeHost, LogicRuntimeLimits runtimeLimits)
        : host(runtimeHost), limits(runtimeLimits),
          lua(ArtCade::Modules::LuaHostOptions{
              ArtCade::Modules::LuaSandboxProfile::LogicBoardStrict,
              runtimeLimits.maxMemoryBytes}) {}

    ILogicRuntimeHost& host;
    LogicRuntimeLimits limits;
    ArtCade::Modules::LuaHost lua;
    std::unordered_map<ObjectTypeId, Factory> factories;
    std::vector<Scope> scopes;
    std::vector<Subscription> subscriptions;
    std::vector<DelayedCallback> delayedCallbacks;
    std::vector<std::string> diagnosticLog;
    /** ADR-0028: once-per-key rate limit for non-finite expression diagnostics. */
    std::unordered_set<std::string> expressionDiagnosticKeys;
    ScopeToken nextScope = 1;
    uint64_t nextSubscription = 1;
    uint64_t nextDelayed = 1;
    uint32_t dispatchDepth = 0;
    uint32_t eventsThisFrame = 0;
    bool eventBudgetLogged = false;
    bool initialized = false;
    bool enabled = true;
    bool apiVersionAccepted = false;
    bool programsRequireTick = false;

    Scope* findScope(ScopeToken token) {
        const auto it = std::find_if(scopes.begin(), scopes.end(),
            [&](const Scope& scope) { return scope.token == token; });
        return it == scopes.end() ? nullptr : &*it;
    }

    Subscription* findSubscription(uint64_t token) {
        const auto it = std::find_if(subscriptions.begin(), subscriptions.end(),
            [&](const Subscription& sub) { return sub.token == token; });
        return it == subscriptions.end() ? nullptr : &*it;
    }

    DelayedCallback* findDelayed(uint64_t token) {
        const auto it = std::find_if(delayedCallbacks.begin(), delayedCallbacks.end(),
            [&](const DelayedCallback& cb) { return cb.token == token; });
        return it == delayedCallbacks.end() ? nullptr : &*it;
    }

    void addSubscription(ScopeToken scopeToken, EntityId owner,
                         const std::string& ruleId, EventKind kind, LogicKey key,
                         sol::protected_function callback, float intervalSeconds = 0.f) {
        Scope* scope = findScope(scopeToken);
        if (!scope || !scope->active) throw sol::error("Logic scope is inactive");
        if (!callback.valid()) throw sol::error("Logic callback is invalid");
        const uint32_t inScope = static_cast<uint32_t>(std::count_if(
            subscriptions.begin(), subscriptions.end(), [&](const Subscription& sub) {
                return sub.active && sub.scope == scopeToken;
            }));
        if (inScope >= limits.maxSubscriptionsPerScope)
            throw sol::error("Logic scope subscription limit exceeded");
        if (subscriptions.size() >= limits.maxSubscriptions)
            throw sol::error("Logic session subscription limit exceeded");
        subscriptions.push_back(Subscription{
            nextSubscription++, scopeToken, owner, ruleId, kind, key,
            intervalSeconds, 0.f, std::move(callback), true});
    }

    /**
     * By value, deliberately: the argument is normally an element of
     * `subscriptions` / `delayedCallbacks`, and a callback in flight can grow
     * those vectors reentrantly. Spawn Object is the real path - the action
     * calls the host, which installs a Logic scope for the new entity, whose
     * program registers handlers, which push_back here. A reallocation would
     * then destroy the sol::protected_function currently being called through.
     * The copy is a second Lua registry reference, so the handle in flight
     * stays owned by this frame no matter what the callback does to the
     * containers.
     */
    bool callProtected(sol::protected_function callback, EntityId owner,
                       const std::string& ruleId,
                       std::optional<EntityId> other = std::nullopt) {
        if (!enabled) return true;
        if (dispatchDepth >= limits.maxEventDepth) {
            diagnosticLog.push_back(callbackError(ruleId, owner, "event depth limit exceeded"));
            return false;
        }
        ++dispatchDepth;
        lua_State* state = callback.lua_state();
        g_instructionBudget = limits.maxInstructionsPerCallback;
        lua_sethook(state, instructionHook, LUA_MASKCOUNT, 1000);
        sol::protected_function_result result = other ? callback(*other) : callback();
        lua_sethook(state, nullptr, 0, 0);
        --dispatchDepth;
        if (!result.valid()) {
            sol::error err = result;
            diagnosticLog.push_back(callbackError(ruleId, owner, err.what()));
            if (lua.memoryLimitExceeded()) {
                enabled = false;
                diagnosticLog.push_back("Logic Runtime disabled: memory limit exceeded");
            }
            return false;
        }
        return true;
    }

    bool callSubscription(uint64_t token, std::optional<EntityId> other = std::nullopt) {
        Subscription* sub = findSubscription(token);
        if (!sub || !sub->active || !enabled) return true;
        const LogicRuleId ruleId = sub->ruleId;
        const EntityId owner = sub->owner;
        const bool ok = callProtected(sub->callback, owner, ruleId, other);
        if (!ok) {
            sub = findSubscription(token);
            if (sub) sub->active = false;
        }
        return ok;
    }

    bool callDelayed(uint64_t token) {
        DelayedCallback* cb = findDelayed(token);
        if (!cb || !cb->active || !enabled) return true;
        const EntityId owner = cb->owner;
        const bool ok = callProtected(cb->callback, owner, "wait");
        cb = findDelayed(token);
        if (cb) cb->active = false;
        return ok;
    }

    void dispatch(EventKind kind, LogicKey key, EntityId owner = INVALID_ENTITY,
                  std::optional<EntityId> other = std::nullopt,
                  bool allowNested = false) {
        if (!enabled || (!allowNested && dispatchDepth != 0)) return;
        const bool matchOwner = kind == EventKind::CollisionEnter
            || kind == EventKind::CollisionExit
            || kind == EventKind::AnimationStarted
            || kind == EventKind::AnimationFinished
            || (kind == EventKind::Start && owner != INVALID_ENTITY);
        const bool matchKey = kind == EventKind::KeyPressed
            || kind == EventKind::KeyReleased
            || kind == EventKind::KeyHeld;
        std::vector<uint64_t> snapshot;
        snapshot.reserve(subscriptions.size());
        for (const Subscription& sub : subscriptions) {
            const Scope* scope = nullptr;
            const auto it = std::find_if(scopes.begin(), scopes.end(),
                [&](const Scope& value) { return value.token == sub.scope; });
            if (it != scopes.end()) scope = &*it;
            if (!sub.active || !scope || !scope->active || sub.kind != kind) continue;
            if (matchOwner && sub.owner != owner) continue;
            if (matchKey && sub.key != key) continue;
            snapshot.push_back(sub.token);
        }
        const uint32_t available = eventsThisFrame >= limits.maxEventsPerDispatch
            ? 0 : limits.maxEventsPerDispatch - eventsThisFrame;
        if (snapshot.size() > available) {
            snapshot.resize(available);
            if (!eventBudgetLogged) {
                diagnosticLog.push_back("Logic dispatch truncated at the per-frame event budget");
                eventBudgetLogged = true;
            }
        }
        eventsThisFrame += static_cast<uint32_t>(snapshot.size());
        for (uint64_t token : snapshot) callSubscription(token, other);
    }

    void tickEverySeconds(float dt) {
        if (!enabled || dispatchDepth != 0) return;
        std::vector<uint64_t> due;
        for (Subscription& sub : subscriptions) {
            if (!sub.active || sub.kind != EventKind::EverySeconds) continue;
            const Scope* scope = findScope(sub.scope);
            if (!scope || !scope->active) continue;
            if (sub.intervalSeconds <= 0.f) {
                due.push_back(sub.token);
                continue;
            }
            sub.accumulated += dt;
            while (sub.accumulated >= sub.intervalSeconds) {
                sub.accumulated -= sub.intervalSeconds;
                due.push_back(sub.token);
            }
        }
        const uint32_t available = eventsThisFrame >= limits.maxEventsPerDispatch
            ? 0 : limits.maxEventsPerDispatch - eventsThisFrame;
        if (due.size() > available) {
            due.resize(available);
            if (!eventBudgetLogged) {
                diagnosticLog.push_back("Logic dispatch truncated at the per-frame event budget");
                eventBudgetLogged = true;
            }
        }
        eventsThisFrame += static_cast<uint32_t>(due.size());
        for (uint64_t token : due) callSubscription(token);
    }

    void tickDelayed(float dt) {
        if (!enabled || dispatchDepth != 0) return;
        std::vector<uint64_t> due;
        for (DelayedCallback& cb : delayedCallbacks) {
            if (!cb.active) continue;
            const Scope* scope = findScope(cb.scope);
            if (!scope || !scope->active) {
                cb.active = false;
                continue;
            }
            cb.remaining -= dt;
            if (cb.remaining <= 0.f) due.push_back(cb.token);
        }
        const uint32_t available = eventsThisFrame >= limits.maxEventsPerDispatch
            ? 0 : limits.maxEventsPerDispatch - eventsThisFrame;
        if (due.size() > available) {
            due.resize(available);
            if (!eventBudgetLogged) {
                diagnosticLog.push_back("Logic dispatch truncated at the per-frame event budget");
                eventBudgetLogged = true;
            }
        }
        eventsThisFrame += static_cast<uint32_t>(due.size());
        for (uint64_t token : due) callDelayed(token);
    }

    bool hasActiveTickWork() const {
        for (const Subscription& sub : subscriptions) {
            if (!sub.active) continue;
            if (sub.kind == EventKind::Update || sub.kind == EventKind::EverySeconds)
                return true;
        }
        for (const DelayedCallback& cb : delayedCallbacks) {
            if (cb.active) return true;
        }
        return false;
    }
};

LogicRuntime::LogicRuntime(ILogicRuntimeHost& host, LogicRuntimeLimits limits)
    : impl_(std::make_unique<Impl>(host, limits)) {}

LogicRuntime::~LogicRuntime() { shutdown(); }

bool LogicRuntime::initialize(std::string* error) {
    if (impl_->initialized) return true;
    impl_->lua.registerBindings([impl = impl_.get()](sol::state& lua) {
        lua.new_usertype<Impl::SelfProxy>(
            "LogicSelf", sol::no_constructor,
            "set_visible", &Impl::SelfProxy::setVisible,
            "is_visible", &Impl::SelfProxy::isVisible,
            "set_flip_x", &Impl::SelfProxy::setFlipX,
            "set_position", &Impl::SelfProxy::setPosition,
            "get_position_x", &Impl::SelfProxy::getPositionX,
            "get_position_y", &Impl::SelfProxy::getPositionY,
            "translate", &Impl::SelfProxy::translate,
            "set_rotation", &Impl::SelfProxy::setRotation,
            "rotate_by", &Impl::SelfProxy::rotateBy,
            "set_scale", &Impl::SelfProxy::setScale,
            "set_velocity", &Impl::SelfProxy::setVelocity,
            "spawn", &Impl::SelfProxy::spawn,
            "is_grounded", &Impl::SelfProxy::isGrounded,
            "is_falling", &Impl::SelfProxy::isFalling,
            "platformer_state", &Impl::SelfProxy::platformerState,
            "is_platformer_moving", &Impl::SelfProxy::isPlatformerMoving,
            "platformer_move", &Impl::SelfProxy::platformerMove,
            "topdown_move", &Impl::SelfProxy::topDownMove,
            "platformer_jump", &Impl::SelfProxy::platformerJump,
            "destroy_self", &Impl::SelfProxy::destroySelf,
            "play_animation_clip", &Impl::SelfProxy::playAnimationClip,
            "stop_animation", &Impl::SelfProxy::stopAnimation,
            "set_animation_playback_speed", &Impl::SelfProxy::setAnimationPlaybackSpeed,
            "play_sound", &Impl::SelfProxy::playSound);
        lua.new_usertype<Impl::ContextProxy>(
            "LogicContext", sol::no_constructor,
            "self", &Impl::ContextProxy::self,
            "on_start", &Impl::ContextProxy::onStart,
            "on_update", &Impl::ContextProxy::onUpdate,
            "on_every_seconds", &Impl::ContextProxy::onEverySeconds,
            "on_animation_started", &Impl::ContextProxy::onAnimationStarted,
            "on_animation_finished", &Impl::ContextProxy::onAnimationFinished,
            "on_key_pressed", &Impl::ContextProxy::onKeyPressed,
            "on_key_released", &Impl::ContextProxy::onKeyReleased,
            "on_key_held", &Impl::ContextProxy::onKeyHeld,
            "on_collision_enter", &Impl::ContextProxy::onCollisionEnter,
            "on_collision_exit", &Impl::ContextProxy::onCollisionExit,
            "wait", &Impl::ContextProxy::wait,
            "state_set_number", &Impl::ContextProxy::stateSetNumber,
            "state_add_number", &Impl::ContextProxy::stateAddNumber,
            "state_toggle_boolean", &Impl::ContextProxy::stateToggleBoolean,
            "state_compare_number", &Impl::ContextProxy::stateCompareNumber,
            "state_compare_boolean", &Impl::ContextProxy::stateCompareBoolean,
            "state_compare_string", &Impl::ContextProxy::stateCompareString,
            "scene_restart", &Impl::ContextProxy::sceneRestart,
            "scene_go_to", &Impl::ContextProxy::sceneGoTo,
            "is_key_down", &Impl::ContextProxy::isKeyDown,
            "other_is_object_type", &Impl::ContextProxy::otherIsObjectType,
            "destroy_other", &Impl::ContextProxy::destroyOther,
            "scene_world_width", &Impl::ContextProxy::sceneWorldWidth,
            "scene_world_height", &Impl::ContextProxy::sceneWorldHeight,
            "get_global_number", &Impl::ContextProxy::getGlobalNumber,
            "get_local_number", &Impl::ContextProxy::getLocalNumber,
            "should_execute", &Impl::ContextProxy::shouldExecute);

        sol::table logic = lua.create_named_table("logic");
        sol::table number = lua.create_table();
        number.set_function("is_finite", [](double value) {
            return std::isfinite(value);
        });
        number.set_function("divide", [](double left, double right) {
            if (!std::isfinite(left) || !std::isfinite(right)
                || std::abs(right) <= kExpressionDivisionEpsilon)
                return std::numeric_limits<double>::quiet_NaN();
            return left / right;
        });
        number.set_function("clamp", [](double value, double minimum, double maximum) {
            if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum)
                || minimum > maximum)
                return std::numeric_limits<double>::quiet_NaN();
            return std::min(maximum, std::max(minimum, value));
        });
        number.set_function("lerp", [](double from, double to, double amount) {
            if (!std::isfinite(from) || !std::isfinite(to) || !std::isfinite(amount))
                return std::numeric_limits<double>::quiet_NaN();
            return from + (to - from) * amount;
        });
        logic["number"] = number;
        sol::table random = lua.create_table();
        random.set_function("range", [](Impl::ContextProxy& context, double minimum,
                                        double maximum) {
            return context.randomRange(minimum, maximum);
        });
        logic["random"] = random;
        sol::table diagnostics = lua.create_table();
        diagnostics.set_function("expression_once", [impl](const std::string& key) {
            if (key.empty()) return;
            if (!impl->expressionDiagnosticKeys.insert(key).second) return;
            impl->diagnosticLog.push_back(
                "Logic expression produced a non-finite number (" + key + ")");
        });
        logic["diagnostics"] = diagnostics;
        logic.set_function("require_api_version", [impl](uint32_t version) {
            if (version != kLogicApiVersion)
                throw sol::error("Unsupported Logic API version");
            impl->apiVersionAccepted = true;
        });
        logic.set_function("define_board",
            [impl](const std::string& boardId, const std::string& objectTypeId,
                   sol::protected_function factory) {
                if (!impl->apiVersionAccepted)
                    throw sol::error("Logic API version was not declared");
                if (boardId.empty() || objectTypeId.empty() || !factory.valid())
                    throw sol::error("Invalid Logic Board program definition");
                if (!impl->factories.emplace(objectTypeId,
                        Impl::Factory{boardId, std::move(factory)}).second)
                    throw sol::error("Duplicate Logic Board program for Object Type");
            });
    });
    if (!impl_->lua.init()) {
        if (error) *error = "Cannot initialize the Logic Board Lua VM";
        return false;
    }
    impl_->initialized = true;
    return true;
}

bool LogicRuntime::loadPrograms(const std::vector<LogicProgram>& programs, std::string* error) {
    if (!initialize(error)) return false;
    if (!impl_->factories.empty()) {
        if (error) *error = "Logic programs were already loaded";
        return false;
    }
    impl_->programsRequireTick = false;
    for (const LogicProgram& program : programs) {
        for (const std::string& feature : program.requiredFeatures) {
            if (!supportedFeatures().count(feature)) {
                if (error) *error = "Logic program requires unsupported feature: " + feature;
                impl_->enabled = false;
                return false;
            }
        }
        impl_->programsRequireTick = impl_->programsRequireTick || program.requiresTick;
        impl_->apiVersionAccepted = false;
        if (!impl_->lua.loadLuaSource(program.source)) {
            if (error) *error = impl_->lua.lastError();
            impl_->enabled = false;
            return false;
        }
        const auto it = impl_->factories.find(program.objectTypeId);
        if (it == impl_->factories.end() || it->second.boardId != program.boardId) {
            if (error) *error = "Generated Logic program metadata does not match its board";
            impl_->enabled = false;
            return false;
        }
    }
    return true;
}

std::optional<ScopeToken> LogicRuntime::install(const ObjectTypeId& objectTypeId,
                                                EntityId owner, std::string* error) {
    if (!impl_->enabled || owner == INVALID_ENTITY) {
        if (error) *error = "Cannot install an inactive Logic Board scope";
        return std::nullopt;
    }
    const auto factory = impl_->factories.find(objectTypeId);
    if (factory == impl_->factories.end()) return std::nullopt;
    if (impl_->scopes.size() >= impl_->limits.maxScopes) {
        if (error) *error = "Logic scope limit exceeded";
        return std::nullopt;
    }

    const ScopeToken token = impl_->nextScope++;
    uint32_t seed = 0xA5A5A5A5u
        ^ static_cast<uint32_t>(owner) * 0x9E3779B9u
        ^ static_cast<uint32_t>(token) * 0x85EBCA6Bu;
    if (seed == 0) seed = 1u;
    impl_->scopes.push_back(Impl::Scope{token, objectTypeId, owner, true, nullptr, {}, seed});
    Impl::Scope* scope = impl_->findScope(token);
    scope->context = std::make_unique<Impl::ContextProxy>(
        Impl::ContextProxy{impl_.get(), token, owner, {impl_.get(), owner}});
    lua_State* state = factory->second.function.lua_state();
    g_instructionBudget = impl_->limits.maxInstructionsPerCallback;
    lua_sethook(state, instructionHook, LUA_MASKCOUNT, 1000);
    sol::protected_function_result result = factory->second.function(*scope->context);
    lua_sethook(state, nullptr, 0, 0);
    if (!result.valid()) {
        sol::error err = result;
        cancelScope(token);
        if (error) *error = err.what();
        return std::nullopt;
    }
    return token;
}

bool LogicRuntime::cancelScope(ScopeToken token) {
    Impl::Scope* scope = impl_->findScope(token);
    if (!scope || !scope->active) return false;
    scope->active = false;
    for (Impl::Subscription& sub : impl_->subscriptions)
        if (sub.scope == token) sub.active = false;
    for (Impl::DelayedCallback& cb : impl_->delayedCallbacks)
        if (cb.scope == token) cb.active = false;
    return true;
}

void LogicRuntime::beginFrame() {
    if (!impl_) return;
    impl_->eventsThisFrame = 0;
    impl_->eventBudgetLogged = false;
}

void LogicRuntime::dispatchStart() { impl_->dispatch(Impl::EventKind::Start, LogicKey::Space); }
void LogicRuntime::dispatchStartForOwner(EntityId owner) {
    if (owner == INVALID_ENTITY) return;
    // Nested OK: spawn from a Logic action must still run On Start for the
    // new entity without re-firing every other Start subscription.
    impl_->dispatch(Impl::EventKind::Start, LogicKey::Space, owner, std::nullopt, true);
}
void LogicRuntime::dispatchKeyPressed(LogicKey key) {
    impl_->dispatch(Impl::EventKind::KeyPressed, key);
}
void LogicRuntime::dispatchKeyReleased(LogicKey key) {
    impl_->dispatch(Impl::EventKind::KeyReleased, key);
}
void LogicRuntime::dispatchKeyHeld(LogicKey key) {
    impl_->dispatch(Impl::EventKind::KeyHeld, key);
}
void LogicRuntime::dispatchCollisionEnter(EntityId owner, EntityId other) {
    impl_->dispatch(Impl::EventKind::CollisionEnter, LogicKey::Space, owner, other);
}
void LogicRuntime::dispatchCollisionExit(EntityId owner, EntityId other) {
    impl_->dispatch(Impl::EventKind::CollisionExit, LogicKey::Space, owner, other);
}
void LogicRuntime::dispatchTick(float dt) {
    if (!impl_ || !impl_->enabled) return;
    if (!(dt > 0.f)) dt = 0.f;
    impl_->dispatch(Impl::EventKind::Update, LogicKey::Space);
    impl_->tickEverySeconds(dt);
    impl_->tickDelayed(dt);
}
void LogicRuntime::dispatchAnimationStarted(EntityId owner) {
    if (owner == INVALID_ENTITY) return;
    impl_->dispatch(Impl::EventKind::AnimationStarted, LogicKey::Space, owner);
}
void LogicRuntime::dispatchAnimationFinished(EntityId owner) {
    if (owner == INVALID_ENTITY) return;
    impl_->dispatch(Impl::EventKind::AnimationFinished, LogicKey::Space, owner);
}

void LogicRuntime::shutdown() noexcept {
    if (!impl_ || !impl_->initialized) return;
    for (Impl::Scope& scope : impl_->scopes) scope.active = false;
    impl_->subscriptions.clear();
    impl_->delayedCallbacks.clear();
    impl_->factories.clear();
    impl_->scopes.clear();
    impl_->diagnosticLog.clear();
    impl_->expressionDiagnosticKeys.clear();
    impl_->lua.shutdown();
    impl_->enabled = false;
    impl_->initialized = false;
    impl_->programsRequireTick = false;
}

bool LogicRuntime::isEnabled() const { return impl_ && impl_->enabled; }
bool LogicRuntime::requiresTick() const {
    if (!impl_ || !impl_->enabled) return false;
    return impl_->programsRequireTick || impl_->hasActiveTickWork();
}
const std::vector<std::string>& LogicRuntime::diagnostics() const {
    return impl_->diagnosticLog;
}

std::vector<std::string> LogicRuntime::drainDiagnostics() {
    if (!impl_) return {};
    std::vector<std::string> out = std::move(impl_->diagnosticLog);
    impl_->diagnosticLog.clear();
    return out;
}

} // namespace ArtCade::Logic
