#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/logic_board_commands.h"
#include "editor-native/model/project_io.h"
#include "logic-core.h"

#include <iostream>
#include <string>

using namespace ArtCade;
using namespace ArtCade::EditorNative;

static int passed = 0;
static int failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

static ProjectDoc makeProjectData() {
    ProjectDoc doc;
    doc.formatVersion = 3;
    doc.projectName = "Logic Test";
    EntityDef hero;
    hero.name = "Hero";
    hero.className = "Hero";
    doc.objectTypes.emplace("Hero", hero);

    SceneDef scene;
    scene.id = "scene-1";
    scene.name = "Scene 1";
    scene.worldSize = {512.f, 320.f};
    scene.defaultLayerId = "layer-1";
    scene.layers.push_back(SceneLayerDef{"layer-1", "Layer 1"});
    SceneInstanceDef instance;
    instance.id = 1;
    instance.objectTypeId = "Hero";
    instance.instanceName = "Hero 1";
    instance.layerId = "layer-1";
    instance.transform.position = {5.f, 6.f};
    scene.instances.push_back(instance);
    scene.entityIds.push_back(1);
    doc.scenes.emplace(scene.id, scene);
    doc.activeSceneId = scene.id;
    return doc;
}

static void testCommandsAndPersistence() {
    EditorCoordinator coordinator{makeProjectData()};
    const uint64_t revision = coordinator.document().revision();
    auto result = coordinator.execute(CreateLogicBoardCommand{"Hero"});
    CHECK(result.ok);
    CHECK(has(result.invalidation, EditorInvalidation::LogicBoard));
    CHECK(coordinator.document().revision() == revision + 1);
    CHECK(coordinator.document().isDirty());

    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
    CHECK(!coordinator.execute(RemoveLogicActionCommand{"Hero", rule.id, 0}).ok);

    const auto beforeUndo = Logic::logicBoardToJson(
        *coordinator.document().data().objectTypes.at("Hero").logicBoard);
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules.empty());
    CHECK(coordinator.redo().ok);
    CHECK(Logic::logicBoardToJson(
        *coordinator.document().data().objectTypes.at("Hero").logicBoard) == beforeUndo);

    const auto serialized = ProjectSerializer::serialize(coordinator.document());
    CHECK(serialized.ok);
    CHECK(serialized.value.find("\"formatVersion\": 3") != std::string::npos);
    const auto loaded = ProjectSerializer::deserialize(serialized.value);
    CHECK(loaded.ok);
    CHECK(loaded.value.data().objectTypes.at("Hero").logicBoard.has_value());

    std::string v2 = serialized.value;
    const std::string boardField = "      \"logicBoard\": ";
    const std::size_t boardAt = v2.find(boardField);
    if (boardAt != std::string::npos) {
        const std::size_t objectStart = v2.find('{', boardAt + boardField.size());
        int depth = 0;
        std::size_t end = objectStart;
        for (; end < v2.size(); ++end) {
            if (v2[end] == '{') ++depth;
            else if (v2[end] == '}' && --depth == 0) { ++end; break; }
        }
        if (end < v2.size() && v2[end] == ',') ++end;
        while (end < v2.size() && (v2[end] == '\r' || v2[end] == '\n')) ++end;
        v2.erase(boardAt, end - boardAt);
    }
    const std::size_t version = v2.find("\"formatVersion\": 3");
    if (version != std::string::npos) v2.replace(version, 18, "\"formatVersion\": 2");
    auto migratedRaw = ProjectSerializer::deserialize(v2);
    CHECK(migratedRaw.ok);
    auto migrated = ProjectMigration::migrate(std::move(migratedRaw.value));
    CHECK(migrated.ok);
    CHECK(migrated.value.data().formatVersion == 3);
    CHECK(!migrated.value.data().objectTypes.at("Hero").logicBoard.has_value());

    std::string malformed = serialized.value;
    const std::size_t trigger = malformed.find("event.on_start");
    if (trigger != std::string::npos) malformed.replace(trigger, 14, "unknown.event!");
    CHECK(!ProjectSerializer::deserialize(malformed).ok);
}

static void testPlayRuntimeIsolation() {
    EditorCoordinator coordinator{makeProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& initial = *coordinator.document().data().objectTypes.at("Hero").logicBoard;

    LogicRuleDef start = Logic::makeDefaultRule(nextLogicRuleId(initial));
    std::get<bool>(start.actions[0].properties[1].value) = false;
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", start, 0}).ok);

    const LogicBoardDef& withStart = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    LogicRuleDef key = Logic::makeDefaultRule(nextLogicRuleId(withStart));
    key.trigger = {Logic::kKeyPressed, {{"key", LogicKey::Space}}};
    key.actions[0] = {Logic::kSetPosition,
        {{"target", LogicEntityReference{}}, {"position", Vec2{40.f, 50.f}}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", key, 1}).ok);

    const uint64_t revision = coordinator.document().revision();
    CHECK(coordinator.playCurrentScene().ok);
    CHECK(coordinator.playSession() != nullptr);
    CHECK(!coordinator.playSession()->findEntity(1)->visible);
    CHECK(coordinator.document().revision() == revision);
    CHECK(coordinator.document().findInstanceInScene("scene-1", 1)->transform.position.x == 5.f);

    RuntimeInputSnapshot input;
    input.pressedLogicKeys.push_back(LogicKey::Space);
    coordinator.updateRuntime(input, 1.f / 60.f);
    CHECK(coordinator.playSession()->findEntity(1)->transform.position.x == 40.f);
    CHECK(coordinator.playSession()->findEntity(1)->transform.position.y == 50.f);
    CHECK(coordinator.document().findInstanceInScene("scene-1", 1)->transform.position.x == 5.f);
    CHECK(coordinator.stopPlaying().ok);
    CHECK(coordinator.playSession() == nullptr);
    CHECK(coordinator.document().findInstanceInScene("scene-1", 1)->transform.position.x == 5.f);
}

static void testInvalidPlayIsAtomic() {
    ProjectDoc data = makeProjectData();
    LogicBoardDef board;
    board.id = "logic:Hero";
    LogicRuleDef rule = Logic::makeDefaultRule("rule-1");
    rule.trigger.typeId = "unknown.trigger";
    board.rules.push_back(rule);
    data.objectTypes.at("Hero").logicBoard = board;
    EditorCoordinator coordinator{std::move(data)};
    const uint64_t revision = coordinator.document().revision();
    CHECK(!coordinator.playCurrentScene().ok);
    CHECK(!coordinator.isPlaying());
    CHECK(coordinator.document().revision() == revision);
}

int main() {
    testCommandsAndPersistence();
    testPlayRuntimeIsolation();
    testInvalidPlayIsAtomic();
    std::cout << "logic-board-editor-test: " << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
