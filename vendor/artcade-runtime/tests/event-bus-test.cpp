// event-bus-test.cpp
// Compile from runtime-cpp/tests/:
//   g++ -std=c++17 -I../src \
//       event-bus-test.cpp \
//       ../src/modules/event-bus/src/event-bus.cpp \
//       -o event_bus_test && ./event_bus_test

#include <cassert>
#include <cstdio>
#include <string>

#include "../src/modules/event-bus/include/event-bus.h"

using EB = ArtCade::Modules::EventBus;

// ------------------------------------------------------------------ tests

static void test_init_shutdown() {
    EB bus;
    assert(bus.init());
    assert(bus.subscriberCount("anything") == 0);
    bus.shutdown();
    assert(bus.subscriberCount("anything") == 0);
    std::puts("  [ok] init / shutdown");
}

static void test_subscribe_emit() {
    EB bus; bus.init();
    int hits = 0;
    bus.subscribe("onHit", [&](const std::any&){ ++hits; });
    bus.emit("onHit");
    assert(hits == 1);
    bus.emit("onHit");
    assert(hits == 2);
    std::puts("  [ok] subscribe + emit");
}

static void test_payload_forwarded() {
    EB bus; bus.init();
    int received = -1;
    bus.subscribe("score", [&](const std::any& p){
        received = std::any_cast<int>(p);
    });
    bus.emit("score", 42);
    assert(received == 42);
    std::puts("  [ok] payload forwarded to subscriber");
}

static void test_multiple_subscribers() {
    EB bus; bus.init();
    int a = 0, b = 0;
    bus.subscribe("ev", [&](const std::any&){ ++a; });
    bus.subscribe("ev", [&](const std::any&){ ++b; });
    bus.emit("ev");
    assert(a == 1 && b == 1);
    std::puts("  [ok] multiple subscribers on same event");
}

static void test_unsubscribe_by_token() {
    EB bus; bus.init();
    int hits = 0;
    auto tok = bus.subscribe("ev", [&](const std::any&){ ++hits; });
    bus.emit("ev");
    assert(hits == 1);
    bus.unsubscribe(tok);
    bus.emit("ev");
    assert(hits == 1);   // no second hit
    std::puts("  [ok] unsubscribe by token");
}

static void test_emit_unknown_event_is_safe() {
    EB bus; bus.init();
    bus.emit("nonexistent", std::string("hello"));   // must not crash
    std::puts("  [ok] emit unknown event is safe");
}

static void test_deferred_emit() {
    EB bus; bus.init();
    int hits = 0;
    bus.subscribe("deferred", [&](const std::any&){ ++hits; });
    bus.emitDeferred("deferred");
    assert(hits == 0);         // not yet
    bus.flushDeferred();
    assert(hits == 1);         // now
    std::puts("  [ok] emitDeferred fires on flushDeferred");
}

static void test_deferred_within_flush_lands_next_frame() {
    EB bus; bus.init();
    int hits = 0;
    bus.subscribe("chain", [&](const std::any&){
        ++hits;
        bus.emitDeferred("chain");   // re-queue during flush
    });
    bus.emitDeferred("chain");
    bus.flushDeferred();    // fires once, re-queues once
    assert(hits == 1);
    bus.flushDeferred();    // fires the re-queued one
    assert(hits == 2);
    std::puts("  [ok] deferred re-queue during flush lands in next flush");
}

static void test_subscribe_during_emit_safe() {
    EB bus; bus.init();
    int hits = 0;
    bus.subscribe("ev", [&](const std::any&){
        ++hits;
        if (hits == 1)
            bus.subscribe("ev", [&](const std::any&){ ++hits; });
    });
    bus.emit("ev");   // snapshot taken — new sub NOT called this round
    assert(hits == 1);
    bus.emit("ev");   // now two subs active
    assert(hits == 3);
    std::puts("  [ok] subscribe during emit is safe (snapshot semantics)");
}

// ------------------------------------------------------------------ main

int main() {
    std::puts("=== EventBus check ===");
    test_init_shutdown();
    test_subscribe_emit();
    test_payload_forwarded();
    test_multiple_subscribers();
    test_unsubscribe_by_token();
    test_emit_unknown_event_is_safe();
    test_deferred_emit();
    test_deferred_within_flush_lands_next_frame();
    test_subscribe_during_emit_safe();
    std::puts("=== all 9 tests passed ===");
    return 0;
}
