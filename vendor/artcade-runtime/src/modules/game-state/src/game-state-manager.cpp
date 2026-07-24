#include "../include/game-state-manager.h"
#include "../../event-bus/include/event-bus.h"
#include <algorithm>

namespace ArtCade::Modules {

bool GameStateManager::init() {
    states_.clear();
    transitions_.clear();
    current_.clear();
    history_.clear();
    return true;
}

void GameStateManager::shutdown() {
    if (!current_.empty()) {
        auto it = states_.find(current_);
        if (it != states_.end() && it->second.onExit)
            it->second.onExit();
    }
    states_.clear();
    transitions_.clear();
    current_.clear();
    history_.clear();
}

// ------------------------------------------------------------------ registration

void GameStateManager::defineState(const std::string& name,
                                   StateCallback  onEnter,
                                   StateCallback  onExit,
                                   UpdateCallback onUpdate) {
    states_[name] = { std::move(onEnter), std::move(onExit), std::move(onUpdate) };
}

void GameStateManager::addTransition(const std::string& from,
                                     const std::string& to,
                                     Guard guard) {
    transitions_[from].push_back({ to, std::move(guard) });
}

// ------------------------------------------------------------------ control

bool GameStateManager::canTransition(const std::string& from,
                                     const std::string& to) const {
    // If no transitions defined for `from`, block by default (explicit opt-in model)
    auto it = transitions_.find(from);
    if (it == transitions_.end()) return false;

    for (const auto& [dest, guard] : it->second) {
        if (dest == to)
            return !guard || guard();   // no guard = always allowed
    }
    return false;
}

bool GameStateManager::transitionTo(const std::string& target, bool ignoreGuards) {
    if (!ignoreGuards && !current_.empty()) {
        if (!canTransition(current_, target)) return false;
    }

    // Exit current
    if (!current_.empty()) {
        auto it = states_.find(current_);
        if (it != states_.end() && it->second.onExit) {
            it->second.onExit();
        }
        if (eventBus_)
            eventBus_->emit("state.exited", current_);
    }

    std::string prev = current_;
    current_ = target;

    // Enter new
    auto it = states_.find(current_);
    if (it != states_.end() && it->second.onEnter)
        it->second.onEnter();

    if (eventBus_)
        eventBus_->emit("state.entered", current_);

    return true;
}

bool GameStateManager::goTo(const std::string& state) {
    return transitionTo(state, false);
}

bool GameStateManager::push(const std::string& state) {
    if (!current_.empty())
        history_.push_back(current_);
    return transitionTo(state, true);
}

bool GameStateManager::pop() {
    if (history_.empty()) return false;
    std::string prev = history_.back();
    history_.pop_back();
    return transitionTo(prev, true);
}

void GameStateManager::forceGoTo(const std::string& state) {
    transitionTo(state, true);
}

void GameStateManager::update(float dt) {
    if (current_.empty()) return;
    auto it = states_.find(current_);
    if (it != states_.end() && it->second.onUpdate)
        it->second.onUpdate(dt);
}

// ------------------------------------------------------------------ query

const std::string& GameStateManager::currentState() const {
    return current_;
}

bool GameStateManager::isInState(const std::string& state) const {
    return current_ == state;
}

std::size_t GameStateManager::historyDepth() const {
    return history_.size();
}

void GameStateManager::setEventBus(EventBus* bus) {
    eventBus_ = bus;
}

} // namespace ArtCade::Modules
