#pragma once

#include "../../../core/module.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <optional>

namespace ArtCade::Modules {

/**
 * GameStateManager — string-keyed finite state machine.
 *
 * States are just string names (e.g. "menu", "playing", "paused", "gameover").
 * Transitions can be guarded; on-enter / on-exit / on-update callbacks
 * are registered per state.  History stack enables "back" navigation.
 *
 * EventBus integration is optional: when an EventBus* is supplied via
 * setEventBus(), state changes emit "state.entered" and "state.exited"
 * events with the state name as a std::string payload.
 */
class EventBus;   // forward — no header pulled in

class GameStateManager final : public IModule {
public:
    GameStateManager() = default;

    bool init()     override;
    void shutdown() override;

    using StateCallback  = std::function<void()>;
    using UpdateCallback = std::function<void(float dt)>;
    using Guard          = std::function<bool()>;  // returns true if transition allowed

    // ------------------------------------------------------------------ registration

    // Define a state (idempotent — calling again adds/replaces callbacks)
    void defineState(const std::string& name,
                     StateCallback    onEnter = {},
                     StateCallback    onExit  = {},
                     UpdateCallback   onUpdate = {});

    // Add a guarded transition from → to (guard optional → always allowed)
    void addTransition(const std::string& from, const std::string& to,
                       Guard guard = {});

    // ------------------------------------------------------------------ control

    // Move to a new state; returns false if the transition is not allowed
    bool goTo(const std::string& state);

    // Push current state onto history stack, then go to `state`
    bool push(const std::string& state);

    // Pop the history stack (returns to previous state); no-op if stack empty
    bool pop();

    // Force a transition ignoring any guards (useful for editor / debug)
    void forceGoTo(const std::string& state);

    // Call once per frame to run the current state's onUpdate callback
    void update(float dt);

    // ------------------------------------------------------------------ query

    const std::string& currentState() const;
    bool               isInState(const std::string& state) const;
    std::size_t        historyDepth() const;

    // ------------------------------------------------------------------ optional integration

    void setEventBus(EventBus* bus);

private:
    struct State {
        StateCallback  onEnter;
        StateCallback  onExit;
        UpdateCallback onUpdate;
    };

    std::unordered_map<std::string, State> states_;

    // Adjacency: from → list of (to, guard)
    std::unordered_map<std::string,
        std::vector<std::pair<std::string, Guard>>> transitions_;

    std::string              current_;
    std::vector<std::string> history_;

    EventBus* eventBus_ = nullptr;

    bool transitionTo(const std::string& target, bool ignoreGuards);
    bool canTransition(const std::string& from, const std::string& to) const;
};

} // namespace ArtCade::Modules
