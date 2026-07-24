#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

#include "../src/modules/save-load/include/save-load-manager.h"

namespace fs = std::filesystem;
using SL = ArtCade::Modules::SaveLoadManager;
using VM = ArtCade::Modules::VariableManager;

static const std::string kTestDir = "test_saves/";

static void cleanup() {
    std::error_code error;
    fs::remove_all(kTestDir, error);
    fs::remove("escape.sav", error);
}

static SL::Snapshot makeSnapshot(double score = 42.0) {
    SL::Snapshot snapshot;
    snapshot.globals = {
        {"score", score}, {"dead", false}, {"name", std::string("hero")},
    };
    snapshot.entities[7] = {
        {"durability", 75.0}, {"enabled", true},
    };
    return snapshot;
}

static void test_roundtrip() {
    SL saves;
    saves.init();
    saves.setSaveDirectory(kTestDir);
    assert(saves.save("slot1", makeSnapshot()));
    const auto loaded = saves.load("slot1");
    assert(loaded);
    assert(std::get<double>(loaded->globals.at("score")) == 42.0);
    assert(std::get<std::string>(loaded->globals.at("name")) == "hero");
    assert(std::get<double>(loaded->entities.at(7).at("durability")) == 75.0);
    cleanup();
}

static void test_slot_operations() {
    SL saves;
    saves.init();
    saves.setSaveDirectory(kTestDir);
    assert(!saves.hasSave("missing"));
    assert(!saves.load("missing"));
    assert(saves.save("alpha", makeSnapshot()));
    assert(saves.save("beta", makeSnapshot()));
    const auto slots = saves.listSlots();
    assert(slots.size() == 2 && slots[0] == "alpha" && slots[1] == "beta");
    saves.deleteSave("alpha");
    assert(!saves.hasSave("alpha"));
    cleanup();
}

static void test_overwrite_and_path_validation() {
    SL saves;
    saves.init();
    saves.setSaveDirectory(kTestDir);
    assert(saves.save("main", makeSnapshot(10.0)));
    assert(saves.save("main", makeSnapshot(99.0)));
    const auto loaded = saves.load("main");
    assert(loaded && std::get<double>(loaded->globals.at("score")) == 99.0);
    assert(!saves.save("../escape", makeSnapshot()));
    assert(!saves.save("nested/slot", makeSnapshot()));
    assert(!saves.save("C:\\escape", makeSnapshot()));
    cleanup();
}

int main() {
    std::puts("=== SaveLoadManager check ===");
    cleanup();
    test_roundtrip();
    test_slot_operations();
    test_overwrite_and_path_validation();
    std::puts("=== all save/load tests passed ===");
    return 0;
}
