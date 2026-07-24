#pragma once

#include "../../../core/module.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <any>
#include <cstdint>

namespace ArtCade::Modules {

/**
 * EventBus — type-erased publish/subscribe bus.
 *
 * Subscribers register with a string event name and receive an std::any
 * payload.  They get back a numeric token they can pass to unsubscribe().
 *
 * All callbacks are invoked synchronously inside emit().
 * Deferred emit (next-frame) is supported via emitDeferred() + flushDeferred().
 *
 * Intentionally has NO dependency on any other module.
 */
class EventBus final : public IModule {
public:
    EventBus() = default;

    bool init()     override;
    void shutdown() override;

    using Callback  = std::function<void(const std::any& payload)>;
    using SubToken  = uint32_t;

    // Subscribe to an event; returns an opaque token for unsubscribe
    SubToken subscribe(const std::string& event, Callback cb);

    // Unsubscribe by token
    void unsubscribe(SubToken token);

    // Fire synchronously — all subscribers called before emit() returns
    void emit(const std::string& event, const std::any& payload = {});

    // Queue an event to be fired on the next flushDeferred() call
    void emitDeferred(const std::string& event, const std::any& payload = {});

    // Call from the game loop once per frame (after tick, before render)
    void flushDeferred();

    // Number of live subscribers for an event (useful in tests)
    std::size_t subscriberCount(const std::string& event) const;

private:
    struct Subscriber {
        SubToken token;
        Callback cb;
    };

    struct DeferredEvent {
        std::string event;
        std::any    payload;
    };

    std::unordered_map<std::string, std::vector<Subscriber>> subs_;
    std::vector<DeferredEvent>                                deferred_;

    SubToken nextToken_ = 1;
};

} // namespace ArtCade::Modules
