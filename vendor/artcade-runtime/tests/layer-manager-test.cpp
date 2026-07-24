// layer-manager-test.cpp
// Compile from runtime-cpp/tests/:
//   g++ -std=c++17 -I../src \
//       layer-manager-test.cpp \
//       ../src/modules/layer-manager/src/layer-manager.cpp \
//       -o layer_manager_test && ./layer_manager_test

#include <cassert>
#include <cstdio>
#include <string>

#include "../src/modules/layer-manager/include/layer-manager.h"

using LM = ArtCade::Modules::LayerManager;

static void test_init_shutdown() {
    LM lm; lm.init();
    assert(lm.layerCount() == 0);
    lm.shutdown();
    std::puts("  [ok] init / shutdown");
}

static void test_define_has_layer() {
    LM lm; lm.init();
    lm.defineLayer("bg", 0);
    lm.defineLayer("fg", 10);
    assert(lm.hasLayer("bg"));
    assert(lm.hasLayer("fg"));
    assert(!lm.hasLayer("hud"));
    assert(lm.layerCount() == 2);
    std::puts("  [ok] defineLayer / hasLayer");
}

static void test_sorted_layers_back_to_front() {
    LM lm; lm.init();
    lm.defineLayer("hud", 20);
    lm.defineLayer("bg",  0);
    lm.defineLayer("mid", 10);
    auto sorted = lm.sortedLayers();
    assert(sorted[0] == "bg" && sorted[1] == "mid" && sorted[2] == "hud");
    std::puts("  [ok] sortedLayers returns back-to-front order");
}

static void test_visibility() {
    LM lm; lm.init();
    lm.defineLayer("ui", 5, true);
    assert(lm.isVisible("ui"));
    lm.setVisible("ui", false);
    assert(!lm.isVisible("ui"));
    std::puts("  [ok] setVisible / isVisible");
}

static void test_opacity_immediate() {
    LM lm; lm.init();
    lm.defineLayer("fx", 3, true, 1.f);
    lm.setOpacity("fx", 0.5f);
    assert(lm.opacity("fx") > 0.49f && lm.opacity("fx") < 0.51f);
    std::puts("  [ok] setOpacity immediate");
}

static void test_opacity_tween() {
    LM lm; lm.init();
    lm.defineLayer("fx", 3, true, 0.f);
    lm.setOpacity("fx", 1.f, 1.f);   // tween 0 → 1 over 1 s
    lm.update(0.5f);
    float o = lm.opacity("fx");
    assert(o > 0.4f && o < 0.6f);    // ~0.5
    lm.update(0.6f);
    assert(lm.opacity("fx") == 1.f);
    std::puts("  [ok] opacity tween");
}

static void test_assign_entity() {
    LM lm; lm.init();
    lm.defineLayer("bg", 0);
    lm.assignEntity(1u, "bg");
    assert(lm.layerOf(1u) == "bg");
    auto& ents = lm.entitiesInLayer("bg");
    assert(ents.size() == 1 && ents[0] == 1u);
    std::puts("  [ok] assignEntity");
}

static void test_reassign_entity_moves_it() {
    LM lm; lm.init();
    lm.defineLayer("bg",  0);
    lm.defineLayer("mid", 5);
    lm.assignEntity(1u, "bg");
    lm.assignEntity(1u, "mid");   // move to mid
    assert(lm.layerOf(1u) == "mid");
    assert(lm.entitiesInLayer("bg").empty());
    assert(!lm.entitiesInLayer("mid").empty());
    std::puts("  [ok] reassign entity moves it between layers");
}

static void test_unassign_entity() {
    LM lm; lm.init();
    lm.defineLayer("bg", 0);
    lm.assignEntity(1u, "bg");
    lm.unassignEntity(1u);
    assert(lm.layerOf(1u).empty());
    assert(lm.entitiesInLayer("bg").empty());
    std::puts("  [ok] unassignEntity");
}

static void test_remove_layer_unassigns_entities() {
    LM lm; lm.init();
    lm.defineLayer("bg", 0);
    lm.assignEntity(1u, "bg");
    lm.assignEntity(2u, "bg");
    lm.removeLayer("bg");
    assert(!lm.hasLayer("bg"));
    assert(lm.layerOf(1u).empty());
    assert(lm.layerOf(2u).empty());
    std::puts("  [ok] removeLayer unassigns all entities");
}

static void test_z_order_change() {
    LM lm; lm.init();
    lm.defineLayer("a", 5);
    lm.setZOrder("a", 100);
    assert(lm.zOrder("a") == 100);
    std::puts("  [ok] setZOrder");
}

int main() {
    std::puts("=== LayerManager check ===");
    test_init_shutdown();
    test_define_has_layer();
    test_sorted_layers_back_to_front();
    test_visibility();
    test_opacity_immediate();
    test_opacity_tween();
    test_assign_entity();
    test_reassign_entity_moves_it();
    test_unassign_entity();
    test_remove_layer_unassigns_entities();
    test_z_order_change();
    std::puts("=== all 11 tests passed ===");
    return 0;
}
