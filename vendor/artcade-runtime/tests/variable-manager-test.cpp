// variable-manager-test.cpp — failure-first VariableManager contract (2A)

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>

#include "../src/modules/variable-manager/include/variable-manager.h"

using VM = ArtCade::Modules::VariableManager;
using ArtCade::Modules::VariableMutationStatus;

static ArtCade::GameVariableDefinition numberDef(const char* key, double value) {
    return {key, ArtCade::GameVariableDefinition::Type::Number, value, {}};
}
static ArtCade::GameVariableDefinition boolDef(const char* key, bool value) {
    return {key, ArtCade::GameVariableDefinition::Type::Boolean, value, {}};
}
static ArtCade::GameVariableDefinition stringDef(const char* key, const char* value) {
    return {key, ArtCade::GameVariableDefinition::Type::String, std::string(value), {}};
}
static void configure(VM& vm) {
    vm.configureGlobals({numberDef("score", 0), numberDef("speed", 0),
        numberDef("lives", 0), numberDef("hp", 0), numberDef("x", 0),
        boolDef("dead", false), boolDef("muted", false),
        stringDef("name", ""), stringDef("level", "")});
}

static void test_init_shutdown() {
    VM vm; vm.init(); configure(vm);
    assert(vm.exists("score"));
    vm.shutdown();
    std::puts("  [ok] init / shutdown");
}

static void test_set_get() {
    VM vm; vm.init(); configure(vm);
    assert(vm.setGlobal("score", 100.0).changed());
    assert(vm.getInt("score") == 100);
    assert(vm.setGlobal("speed", 3.14).changed());
    assert(vm.getFloat("speed") > 3.13f && vm.getFloat("speed") < 3.15f);
    assert(vm.setGlobal("dead", true).changed());
    assert(vm.getBool("dead") == true);
    assert(vm.setGlobal("name", std::string("hero")).changed());
    assert(vm.getString("name") == "hero");
    std::puts("  [ok] setGlobal / get typed");
}

static void test_missing_does_not_create() {
    VM vm; vm.init(); configure(vm);
    const auto result = vm.setGlobal("scroe", 10.0);
    assert(result.status == VariableMutationStatus::MissingVariable);
    assert(!vm.exists("scroe"));
    assert(vm.getInt("score") == 0);
    std::puts("  [ok] set missing → MissingVariable, no create");
}

static void test_type_mismatch() {
    VM vm; vm.init(); configure(vm);
    assert(vm.setGlobal("score", true).status == VariableMutationStatus::TypeMismatch);
    assert(vm.getInt("score") == 0);
    assert(vm.addNumber("muted", 1.0).status == VariableMutationStatus::TypeMismatch);
    assert(vm.getBool("muted") == false);
    assert(vm.toggleBoolean("score").status == VariableMutationStatus::TypeMismatch);
    assert(vm.getInt("score") == 0);
    std::puts("  [ok] type mismatch leaves value unchanged");
}

static void test_unchanged_no_observer() {
    VM vm; vm.init(); configure(vm);
    assert(vm.setGlobal("score", 10.0).changed());
    int calls = 0;
    vm.observe("score", [&](const std::string&, const VM::Value&) { ++calls; });
    const auto same = vm.setGlobal("score", 10.0);
    assert(same.status == VariableMutationStatus::Unchanged);
    assert(calls == 0);
    assert(vm.setGlobal("score", 11.0).changed());
    assert(calls == 1);
    std::puts("  [ok] Unchanged → no observer");
}

static void test_non_finite() {
    VM vm; vm.init(); configure(vm);
    assert(vm.setGlobal("score", std::numeric_limits<double>::quiet_NaN()).status
           == VariableMutationStatus::NonFiniteValue);
    assert(vm.setGlobal("score", std::numeric_limits<double>::infinity()).status
           == VariableMutationStatus::NonFiniteValue);
    assert(vm.addNumber("score", std::numeric_limits<double>::infinity()).status
           == VariableMutationStatus::NonFiniteValue);
    assert(vm.getInt("score") == 0);
    std::puts("  [ok] non-finite rejected");
}

static void test_add_number() {
    VM vm; vm.init(); configure(vm);
    assert(vm.setGlobal("lives", 3.0).changed());
    const auto r = vm.addNumber("lives", -1.0);
    assert(r.changed());
    assert(std::get<double>(r.after) == 2.0);
    assert(vm.getInt("lives") == 2);
    std::puts("  [ok] addNumber");
}

static void test_toggle_boolean() {
    VM vm; vm.init(); configure(vm);
    assert(vm.toggleBoolean("muted").changed());
    assert(vm.getBool("muted") == true);
    assert(vm.toggleBoolean("muted").changed());
    assert(vm.getBool("muted") == false);
    std::puts("  [ok] toggleBoolean");
}

static void test_try_get_number() {
    VM vm; vm.init(); configure(vm);
    assert(vm.tryGetNumber("missing") == std::nullopt);
    assert(vm.tryGetNumber("muted") == std::nullopt);
    assert(vm.setGlobal("score", 7.0).changed());
    assert(vm.tryGetNumber("score").value() == 7.0);
    std::puts("  [ok] tryGetNumber nullopt on missing/wrong type");
}

static void test_remove_and_clear() {
    VM vm; vm.init(); configure(vm);
    assert(vm.setGlobal("score", 1.0).changed());
    assert(vm.setGlobal("lives", 2.0).changed());
    vm.remove("score");
    assert(!vm.exists("score") && vm.exists("lives"));
    vm.clear();
    assert(!vm.exists("lives"));
    std::puts("  [ok] remove + clear");
}

static void test_observer_key_scoped() {
    VM vm; vm.init(); configure(vm);
    int hits = 0;
    vm.observe("score", [&](const std::string&, const VM::Value&) { ++hits; });
    (void)vm.setGlobal("lives", 1.0);
    assert(hits == 0);
    (void)vm.setGlobal("score", 1.0);
    assert(hits == 1);
    std::puts("  [ok] observer is key-scoped");
}

static void test_stop_observing() {
    VM vm; vm.init(); configure(vm);
    int hits = 0;
    auto tok = vm.observe("score", [&](const std::string&, const VM::Value&) { ++hits; });
    (void)vm.setGlobal("score", 1.0);
    vm.stopObserving(tok);
    (void)vm.setGlobal("score", 2.0);
    assert(hits == 1);
    std::puts("  [ok] stopObserving");
}

static void test_snapshot_restore() {
    VM vm; vm.init(); configure(vm);
    (void)vm.setGlobal("score", 10.0);
    (void)vm.setGlobal("level", std::string("A"));
    auto snap = vm.takeSnapshot();
    (void)vm.setGlobal("score", 999.0);
    vm.restoreSnapshot(snap);
    assert(vm.getInt("score") == 10);
    assert(vm.getString("level") == "A");
    std::puts("  [ok] takeSnapshot / restoreSnapshot");
}

static void test_local_variables_and_persistent_snapshot() {
    VM vm; vm.init(); configure(vm);
    vm.createEntity(7, {numberDef("durability", 100), boolDef("enabled", true)}, {{"durability", 75.0}});
    vm.createEntity(8, {numberDef("durability", 100)});
    assert(vm.getEntity(7, "durability") == VM::Value{75.0});
    assert(vm.addEntity(7, "durability", -5).value() == 70.0);
    assert(!vm.setEntity(7, "missing", 1.0));

    const auto snapshot = vm.takeGameSnapshot({7});
    assert(snapshot.entities.count(7) == 1);
    assert(snapshot.entities.count(8) == 0);
    vm.setEntity(7, "durability", 1.0);
    assert(vm.restoreGameSnapshot(snapshot, {7}));
    assert(vm.getEntity(7, "durability") == VM::Value{70.0});
    std::puts("  [ok] local variables + persistent game snapshot");
}

static void test_restore_rejects_extra_keys() {
    VM vm; vm.init(); configure(vm);
    (void)vm.setGlobal("score", 42.0);
    auto snapshot = vm.takeGameSnapshot({});
    snapshot.globals["logicScore"] = 99.0;

    VM restored; restored.init(); configure(restored);
    assert(!restored.restoreGameSnapshot(snapshot, {}));
    assert(!restored.exists("logicScore"));
    assert(restored.getInt("score") == 0);
    std::puts("  [ok] restore rejects extra Number keys");
}

int main() {
    std::puts("=== VariableManager check ===");
    test_init_shutdown();
    test_set_get();
    test_missing_does_not_create();
    test_type_mismatch();
    test_unchanged_no_observer();
    test_non_finite();
    test_add_number();
    test_toggle_boolean();
    test_try_get_number();
    test_remove_and_clear();
    test_observer_key_scoped();
    test_stop_observing();
    test_snapshot_restore();
    test_local_variables_and_persistent_snapshot();
    test_restore_rejects_extra_keys();
    std::puts("=== all VariableManager tests passed ===");
    return 0;
}
