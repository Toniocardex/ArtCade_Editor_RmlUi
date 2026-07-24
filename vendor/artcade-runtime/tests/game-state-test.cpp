// game-state-test.cpp
// Compile from runtime-cpp/tests/:
//   g++ -std=c++17 -I../src \
//       game-state-test.cpp \
//       ../src/modules/game-state/src/game-state-manager.cpp \
//       ../src/modules/event-bus/src/event-bus.cpp \
//       -o game_state_test && ./game_state_test

#include <cassert>
#include <cstdio>
#include <string>

#include "../src/modules/game-state/include/game-state-manager.h"
#include "../src/modules/event-bus/include/event-bus.h"

using GSM = ArtCade::Modules::GameStateManager;

static void test_init_empty() {
    GSM gsm; gsm.init();
    assert(gsm.currentState().empty());
    assert(!gsm.isInState("menu"));
    gsm.shutdown();
    std::puts("  [ok] init / shutdown empty");
}

static void test_force_go_to() {
    GSM gsm; gsm.init();
    gsm.defineState("menu");
    gsm.forceGoTo("menu");
    assert(gsm.isInState("menu"));
    std::puts("  [ok] forceGoTo without transitions");
}

static void test_enter_exit_callbacks() {
    GSM gsm; gsm.init();
    int enters = 0, exits = 0;
    gsm.defineState("a",
        [&]{ ++enters; },
        [&]{ ++exits; });
    gsm.defineState("b");

    gsm.forceGoTo("a");
    assert(enters == 1 && exits == 0);

    gsm.forceGoTo("b");
    assert(enters == 1 && exits == 1);
    std::puts("  [ok] enter / exit callbacks");
}

static void test_transition_allowed() {
    GSM gsm; gsm.init();
    gsm.defineState("menu");
    gsm.defineState("playing");
    gsm.addTransition("menu", "playing");

    gsm.forceGoTo("menu");
    bool ok = gsm.goTo("playing");
    assert(ok && gsm.isInState("playing"));
    std::puts("  [ok] allowed transition succeeds");
}

static void test_transition_blocked() {
    GSM gsm; gsm.init();
    gsm.defineState("playing");
    gsm.defineState("menu");
    // No transition playing → menu defined

    gsm.forceGoTo("playing");
    bool ok = gsm.goTo("menu");
    assert(!ok && gsm.isInState("playing"));
    std::puts("  [ok] undefined transition blocked");
}

static void test_guarded_transition() {
    GSM gsm; gsm.init();
    gsm.defineState("a");
    gsm.defineState("b");

    bool allow = false;
    gsm.addTransition("a", "b", [&]{ return allow; });
    gsm.forceGoTo("a");

    assert(!gsm.goTo("b"));
    allow = true;
    assert(gsm.goTo("b"));
    std::puts("  [ok] guarded transition");
}

static void test_push_pop() {
    GSM gsm; gsm.init();
    gsm.defineState("playing");
    gsm.defineState("paused");

    gsm.forceGoTo("playing");
    assert(gsm.historyDepth() == 0);

    gsm.push("paused");
    assert(gsm.isInState("paused") && gsm.historyDepth() == 1);

    gsm.pop();
    assert(gsm.isInState("playing") && gsm.historyDepth() == 0);
    std::puts("  [ok] push / pop history");
}

static void test_pop_on_empty_stack_is_safe() {
    GSM gsm; gsm.init();
    bool ok = gsm.pop();   // must not crash
    assert(!ok);
    std::puts("  [ok] pop on empty stack is safe");
}

static void test_update_callback() {
    GSM gsm; gsm.init();
    float received = -1.f;
    gsm.defineState("playing", {}, {},
        [&](float dt){ received = dt; });
    gsm.forceGoTo("playing");
    gsm.update(0.016f);
    assert(received > 0.015f);
    std::puts("  [ok] update callback receives dt");
}

static void test_event_bus_integration() {
    ArtCade::Modules::EventBus bus; bus.init();
    GSM gsm; gsm.init();
    gsm.setEventBus(&bus);
    gsm.defineState("a");
    gsm.defineState("b");

    std::string enteredState, exitedState;
    bus.subscribe("state.entered", [&](const std::any& p){
        enteredState = std::any_cast<std::string>(p);
    });
    bus.subscribe("state.exited", [&](const std::any& p){
        exitedState = std::any_cast<std::string>(p);
    });

    gsm.forceGoTo("a");
    assert(enteredState == "a");

    gsm.forceGoTo("b");
    assert(exitedState == "a" && enteredState == "b");
    std::puts("  [ok] EventBus state.entered / state.exited events");
}

int main() {
    std::puts("=== GameStateManager check ===");
    test_init_empty();
    test_force_go_to();
    test_enter_exit_callbacks();
    test_transition_allowed();
    test_transition_blocked();
    test_guarded_transition();
    test_push_pop();
    test_pop_on_empty_stack_is_safe();
    test_update_callback();
    test_event_bus_integration();
    std::puts("=== all 10 tests passed ===");
    return 0;
}
