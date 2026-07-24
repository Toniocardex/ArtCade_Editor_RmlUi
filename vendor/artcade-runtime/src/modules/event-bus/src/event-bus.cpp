#include "../include/event-bus.h"
#include <algorithm>

namespace ArtCade::Modules {

bool EventBus::init() {
    subs_.clear();
    deferred_.clear();
    nextToken_ = 1;
    return true;
}

void EventBus::shutdown() {
    subs_.clear();
    deferred_.clear();
}

EventBus::SubToken EventBus::subscribe(const std::string& event, Callback cb) {
    SubToken tok = nextToken_++;
    subs_[event].push_back({ tok, std::move(cb) });
    return tok;
}

void EventBus::unsubscribe(SubToken token) {
    for (auto& [event, vec] : subs_) {
        auto it = std::remove_if(vec.begin(), vec.end(),
            [token](const Subscriber& s){ return s.token == token; });
        if (it != vec.end()) {
            vec.erase(it, vec.end());
            return;   // tokens are globally unique — stop after first match
        }
    }
}

void EventBus::emit(const std::string& event, const std::any& payload) {
    auto it = subs_.find(event);
    if (it == subs_.end()) return;

    // Copy the subscriber list before iterating: a callback may itself call
    // subscribe/unsubscribe, which would invalidate the original vector.
    std::vector<Subscriber> snap = it->second;
    for (auto& sub : snap)
        sub.cb(payload);
}

void EventBus::emitDeferred(const std::string& event, const std::any& payload) {
    deferred_.push_back({ event, payload });
}

void EventBus::flushDeferred() {
    // Swap out so new deferred events emitted during flush land in the *next* flush
    std::vector<DeferredEvent> pending;
    pending.swap(deferred_);
    for (auto& ev : pending)
        emit(ev.event, ev.payload);
}

std::size_t EventBus::subscriberCount(const std::string& event) const {
    auto it = subs_.find(event);
    return (it != subs_.end()) ? it->second.size() : 0;
}

} // namespace ArtCade::Modules
