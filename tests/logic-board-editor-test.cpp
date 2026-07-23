#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/project_file.h"
#include "editor-native/commands/audio_asset_commands.h"
#include "editor-native/commands/global_variable_commands.h"
#include "editor-native/commands/logic_board_commands.h"
#include "editor-native/commands/top_down_controller_commands.h"
#include "editor-native/model/logic_component_references.h"
#include "editor-native/model/project_io.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/ui/logic_board_editor_controller.h"
#include "app/render/scene_frame_snapshot.h"
#include "logic-core.h"

#include <cmath>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

using namespace ArtCade;
using namespace ArtCade::EditorNative;

static int passed = 0;
static int failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

// RU-03 (D-01): PlaySession no longer exposes findEntity()/RuntimeEntity
// (per-entity gameplay/physics internals) - only the render hand-off,
// renderables(), which needs a SpriteComponent (see makeProjectData()'s
// spriteRenderer) to enumerate an entity at all.
// renderables() returns by value — never return & into that temporary.
static std::optional<ArtCade::RenderableEntitySnapshot> findRenderable(
    const PlaySession& session, EntityId id) {
    for (const auto& entity : session.renderables())
        if (entity.id == id) return entity;
    return std::nullopt;
}

// Same resolution scene_frame_snapshot.cpp's Play branch uses: the resolved
// per-frame sprite asset id, falling back to the type-owned static one.
static AssetId resolvedSpriteAssetId(const ArtCade::RenderableEntitySnapshot& entity) {
    return entity.spriteFrame.assetId.empty() ? entity.sprite.spriteAssetId : entity.spriteFrame.assetId;
}

static ProjectDoc makeProjectData() {
    ProjectDoc doc;
    doc.formatVersion = 4;
    doc.projectName = "Logic Test";
    EntityDef hero;
    hero.name = "Hero";
    hero.className = "Hero";
    // RU-03: a SpriteRendererComponent (even with no image asset) is what
    // makes an entity appear in GameplaySession's renderables() - needed so
    // Play tests below can observe position/visibility through PlaySession's
    // render hand-off, the only per-entity introspection the facade exposes.
    hero.spriteRenderer = SpriteRendererComponent{{}, true};
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
    CHECK(coordinator.execute(RemoveLogicActionCommand{"Hero", rule.id, 0}).ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
              .actions.empty());
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
              .actions.size()
          == 1);

    const auto beforeUndo = Logic::logicBoardToJson(
        *coordinator.document().data().objectTypes.at("Hero").logicBoard);
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules.empty());
    CHECK(coordinator.redo().ok);
    CHECK(Logic::logicBoardToJson(
        *coordinator.document().data().objectTypes.at("Hero").logicBoard) == beforeUndo);

    const auto serialized = ProjectSerializer::serialize(coordinator.document());
    CHECK(serialized.ok);
    CHECK(serialized.value.find("\"formatVersion\": 10") != std::string::npos);
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
    const std::string currentVersion = "\"formatVersion\": 10";
    const std::size_t version = v2.find(currentVersion);
    if (version != std::string::npos) {
        v2.replace(version, currentVersion.size(), "\"formatVersion\": 2");
    }
    auto migratedRaw = ProjectSerializer::deserialize(v2);
    CHECK(migratedRaw.ok);
    auto migrated = ProjectMigration::migrate(std::move(migratedRaw.value));
    CHECK(migrated.ok);
    CHECK(migrated.value.data().formatVersion == 10);
    CHECK(!migrated.value.data().objectTypes.at("Hero").logicBoard.has_value());

    std::string malformed = serialized.value;
    const std::size_t trigger = malformed.find("event.on_start");
    if (trigger != std::string::npos) malformed.replace(trigger, 14, "unknown.event!");
    // ADR-0013: unknown block types are structurally loadable (repairable).
    const auto loadedUnknown = ProjectSerializer::deserialize(malformed);
    CHECK(loadedUnknown.ok);
    CHECK(loadedUnknown.value.data().objectTypes.at("Hero").logicBoard.has_value());
    CHECK(loadedUnknown.value.data().objectTypes.at("Hero").logicBoard->rules[0]
              .trigger.typeId
          == "unknown.event!");
}

static void testDuplicateLogicRule() {
    ProjectDoc project = makeProjectData();
    LogicBoardDef board;
    board.id = "logic:Hero";
    board.sections.push_back(LogicSectionDef{"movement", "Movement"});
    project.objectTypes.at("Hero").logicBoard = std::move(board);
    EditorCoordinator coordinator{std::move(project)};

    LogicRuleDef source = Logic::makeDefaultRule("rule-1");
    source.name = "Move";
    source.enabled = false;
    source.executionMode = LogicExecutionMode::OncePerActivation;
    source.sectionId = "movement";
    source.trigger = {Logic::kKeyPressed, {{"key", LogicKey::D}}};
    LogicConditionClause condition;
    condition.joinBefore = LogicConditionJoin::And;
    condition.negated = true;
    condition.block = Logic::makeDefaultBlock(Logic::kIsVisible, Logic::BlockKind::Condition);
    source.conditions.push_back(std::move(condition));
    source.actions[0] = {Logic::kSetVisible,
        {{"target", LogicEntityReference{}}, {"visible", false}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", source, 0}).ok);

    const uint64_t revisionBefore = coordinator.document().revision();
    const std::size_t undoBefore = coordinator.undoSize();
    CHECK(coordinator.apply(DuplicateLogicRuleIntent{"Hero", "rule-1"}).ok);
    const auto& copiedBoard = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(copiedBoard.rules.size() == 2);
    const LogicRuleDef& clone = copiedBoard.rules[1];
    CHECK(clone.id == "rule-2");
    CHECK(clone.name == "Move Copy");
    CHECK(clone.enabled == source.enabled);
    CHECK(clone.executionMode == source.executionMode);
    CHECK(clone.sectionId == source.sectionId);
    CHECK(clone.trigger.typeId == source.trigger.typeId);
    CHECK(std::get<LogicKey>(Logic::findProperty(clone.trigger, "key")->value) == LogicKey::D);
    CHECK(clone.conditions.size() == 1);
    CHECK(clone.conditions[0].joinBefore == LogicConditionJoin::And);
    CHECK(clone.conditions[0].negated);
    CHECK(clone.actions.size() == source.actions.size());
    CHECK(std::get<bool>(Logic::findProperty(clone.actions[0], "visible")->value) == false);
    CHECK(coordinator.document().revision() == revisionBefore + 1);
    CHECK(coordinator.undoSize() == undoBefore + 1);

    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules.size() == 1);
    CHECK(coordinator.redo().ok);
    const auto& redoneBoard = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(redoneBoard.rules.size() == 2);
    CHECK(redoneBoard.rules[1].id == "rule-2");
    CHECK(redoneBoard.rules[1].name == "Move Copy");

    // The two values are independent after cloning; no mutable authoring data
    // is shared between cards.
    CHECK(coordinator.execute(SetLogicRuleEnabledCommand{"Hero", "rule-2", true}).ok);
    const auto& independentlyEdited = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(!independentlyEdited.rules[0].enabled);
    CHECK(independentlyEdited.rules[1].enabled);

    const uint64_t revisionBeforeFailure = coordinator.document().revision();
    const std::size_t undoBeforeFailure = coordinator.undoSize();
    LogicRuleDef invalidClone = independentlyEdited.rules[0];
    invalidClone.id = "rule-2";
    CHECK(!coordinator.execute(DuplicateLogicRuleCommand{
        "Hero", "rule-1", std::move(invalidClone), 1}).ok);
    CHECK(!coordinator.apply(DuplicateLogicRuleIntent{"Hero", "missing"}).ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules.size() == 2);
    CHECK(coordinator.document().revision() == revisionBeforeFailure);
    CHECK(coordinator.undoSize() == undoBeforeFailure);

    const auto serialized = ProjectSerializer::serialize(coordinator.document());
    CHECK(serialized.ok);
    const auto loaded = ProjectSerializer::deserialize(serialized.value);
    CHECK(loaded.ok);
    const auto& loadedRules = loaded.value.data().objectTypes.at("Hero").logicBoard->rules;
    CHECK(loadedRules.size() == 2);
    CHECK(loadedRules[0].id == "rule-1" && loadedRules[1].id == "rule-2");
}

static void testGlobalVariableCommands() {
    ProjectDoc project = makeProjectData();
    project.globalVariables = {
        {"score", GameVariableDefinition::Type::Number, 1.0, "Player score"},
        {"doorOpen", GameVariableDefinition::Type::Boolean, false, "Door state"},
    };
    EditorCoordinator coordinator{std::move(project)};

    GameVariableDefinition health{
        "health", GameVariableDefinition::Type::Number, 100.0, "Player health"};
    CHECK(coordinator.execute(AddGlobalVariableCommand{health}).ok);
    CHECK(coordinator.document().data().globalVariables.size() == 3);
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().globalVariables.size() == 2);
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().data().globalVariables[2].key == "health");

    const uint64_t beforeInvalidAdd = coordinator.document().revision();
    CHECK(!coordinator.execute(AddGlobalVariableCommand{
        {"1bad", GameVariableDefinition::Type::Number, 0.0, {}}}).ok);
    CHECK(!coordinator.execute(AddGlobalVariableCommand{health}).ok);
    CHECK(coordinator.document().revision() == beforeInvalidAdd);

    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    rule.actions[0] =
        Logic::makeDefaultBlock(Logic::kStateAdd, Logic::BlockKind::Action);
    Logic::applyDeterministicVariableDefault(coordinator.document().data(), rule.actions[0]);
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
    CHECK(coordinator.execute(AddLogicConditionCommand{
        "Hero", rule.id,
        Logic::makeDefaultBlock(Logic::kStateCompare, Logic::BlockKind::Condition), 0}).ok);
    CHECK(coordinator.execute(SetLogicPropertyCommand{
        "Hero", rule.id, LogicPropertyTarget::Action, 0,
        "key", LogicVariableReference{"score"}}).ok);
    CHECK(coordinator.execute(SetLogicPropertyCommand{
        "Hero", rule.id, LogicPropertyTarget::Condition, 0,
        "key", LogicVariableReference{"score"}}).ok);
    CHECK(countGlobalVariableReferences(coordinator.document(), "score") == 2);

    const uint64_t beforeBlockedRemove = coordinator.document().revision();
    CHECK(!coordinator.execute(RemoveGlobalVariableCommand{"score"}).ok);
    CHECK(coordinator.document().revision() == beforeBlockedRemove);

    CHECK(coordinator.execute(RenameGlobalVariableCommand{"score", "points"}).ok);
    CHECK(countGlobalVariableReferences(coordinator.document(), "score") == 0);
    CHECK(countGlobalVariableReferences(coordinator.document(), "points") == 2);
    const LogicBoardDef& renamed =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(std::get<LogicVariableReference>(
        Logic::findProperty(renamed.rules[0].actions[0], "key")->value).id == "points");
    CHECK(std::get<LogicVariableReference>(
        Logic::findProperty(renamed.rules[0].conditions[0].block, "key")->value).id == "points");
    CHECK(coordinator.undo().ok);
    CHECK(countGlobalVariableReferences(coordinator.document(), "score") == 2);
    CHECK(coordinator.redo().ok);
    CHECK(countGlobalVariableReferences(coordinator.document(), "points") == 2);

    const uint64_t beforeIncompatibleType = coordinator.document().revision();
    CHECK(!coordinator.execute(SetGlobalVariableTypeCommand{
        "points", GameVariableDefinition::Type::Boolean}).ok);
    CHECK(!coordinator.execute(SetGlobalVariableInitialValueCommand{"points", false}).ok);
    CHECK(coordinator.document().revision() == beforeIncompatibleType);

    CHECK(coordinator.execute(
        SetGlobalVariableInitialValueCommand{"points", 42.5}).ok);
    CHECK(std::get<double>(coordinator.document().data().globalVariables[0].initialValue) == 42.5);
    CHECK(coordinator.undo().ok);
    CHECK(std::get<double>(coordinator.document().data().globalVariables[0].initialValue) == 1.0);
    CHECK(coordinator.redo().ok);
    CHECK(std::get<double>(coordinator.document().data().globalVariables[0].initialValue) == 42.5);

    CHECK(coordinator.execute(
        SetGlobalVariableDescriptionCommand{"points", "Points earned"}).ok);
    CHECK(coordinator.document().data().globalVariables[0].description == "Points earned");
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().globalVariables[0].description == "Player score");
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().data().globalVariables[0].description == "Points earned");

    CHECK(coordinator.execute(SetGlobalVariableTypeCommand{
        "doorOpen", GameVariableDefinition::Type::String}).ok);
    CHECK(std::get<std::string>(
        coordinator.document().data().globalVariables[1].initialValue).empty());
    CHECK(coordinator.undo().ok);
    CHECK(std::get<bool>(
        coordinator.document().data().globalVariables[1].initialValue) == false);
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().data().globalVariables[1].type
          == GameVariableDefinition::Type::String);
    CHECK(coordinator.undo().ok);

    ProjectDoc invalid = coordinator.document().data();
    // Type/value mismatch is structurally invalid for globals (not Logic semantics).
    invalid.globalVariables[0].type = GameVariableDefinition::Type::Boolean;
    invalid.globalVariables[0].initialValue = 42.5;
    CHECK(!ProjectValidator::validate(ProjectDocument{std::move(invalid)}).ok);

    CHECK(coordinator.execute(ChangeLogicActionTypeCommand{
        "Hero", rule.id, 0, Logic::kSetVisible}).ok);
    CHECK(coordinator.execute(RemoveLogicConditionCommand{"Hero", rule.id, 0}).ok);
    CHECK(countGlobalVariableReferences(coordinator.document(), "points") == 0);
    CHECK(coordinator.execute(RemoveGlobalVariableCommand{"points"}).ok);
    CHECK(coordinator.document().data().globalVariables[0].key == "doorOpen");
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().globalVariables[0].key == "points");
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().data().globalVariables[0].key == "doorOpen");
}

static void testConditionCommands() {
    ProjectDoc project = makeProjectData();
    project.objectTypes.at("Hero").platformerController = PlatformerControllerComponent{};
    EditorCoordinator coordinator{std::move(project)};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);

    CHECK(coordinator.execute(AddLogicConditionCommand{
        "Hero", rule.id, Logic::makeDefaultCondition(), 0}).ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
        .conditions.size() == 1);

    for (std::size_t i = 1; i < Logic::kMaxConditionsPerRule; ++i)
        CHECK(coordinator.execute(AddLogicConditionCommand{
            "Hero", rule.id, Logic::makeDefaultCondition(), i}).ok);
    CHECK(!coordinator.execute(AddLogicConditionCommand{
        "Hero", rule.id, Logic::makeDefaultCondition(), Logic::kMaxConditionsPerRule}).ok);

    CHECK(coordinator.execute(SetLogicPropertyCommand{
        "Hero", rule.id, LogicPropertyTarget::Condition, 0, "expected", false}).ok);
    CHECK(std::get<bool>(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].conditions[0].block.properties[0].value) == false);

    const auto beforeMove = Logic::logicBoardToJson(
        *coordinator.document().data().objectTypes.at("Hero").logicBoard);
    CHECK(coordinator.execute(MoveLogicConditionCommand{"Hero", rule.id, 0, 2}).ok);
    CHECK(coordinator.undo().ok);
    CHECK(Logic::logicBoardToJson(
        *coordinator.document().data().objectTypes.at("Hero").logicBoard) == beforeMove);
    CHECK(coordinator.redo().ok);

    const std::size_t countBeforeRemove = coordinator.document().data().objectTypes.at("Hero")
        .logicBoard->rules[0].conditions.size();
    CHECK(coordinator.execute(RemoveLogicConditionCommand{"Hero", rule.id, 0}).ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
        .conditions.size() == countBeforeRemove - 1);
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
        .conditions.size() == countBeforeRemove);
}

static void testConditionControllerAndGenericProperties() {
    ProjectDoc project = makeProjectData();
    project.objectTypes.at("Hero").platformerController = PlatformerControllerComponent{};
    project.globalVariables = {
        {"score", GameVariableDefinition::Type::Number, 0.0, {}},
        {"doorOpen", GameVariableDefinition::Type::Boolean, false, {}},
    };
    EditorCoordinator coordinator{std::move(project)};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
    CHECK(coordinator.apply(OpenLogicBoardIntent{"Hero"}).ok);
    LogicBoardEditorController controller{coordinator, nullptr};

    CHECK(controller.handleAction(
        "change-logic-trigger", rule.id, Logic::kEverySeconds, {}));
    CHECK(controller.handleAction(
        "commit-logic-property", rule.id + "|t|0|seconds", "2.5", {}));
    const LogicBoardDef* board =
        &*coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(std::get<double>(
        Logic::findProperty(board->rules[0].trigger, "seconds")->value) == 2.5);

    CHECK(controller.handleAction(
        "add-logic-condition-type", rule.id, Logic::kStateCompare, {}));
    CHECK(controller.handleAction(
        "pick-logic-property", rule.id + "|c|0|key", "score", {}));
    CHECK(controller.handleAction(
        "pick-logic-property", rule.id + "|c|0|op", ">=", {}));
    CHECK(controller.handleAction(
        "commit-logic-property", rule.id + "|c|0|value", "10", {}));
    board = &*coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(board->rules[0].conditions.size() == 1);
    CHECK(std::get<LogicVariableReference>(Logic::findProperty(
        board->rules[0].conditions[0].block, "key")->value).id == "score");
    CHECK(std::get<LogicStringValue>(Logic::findProperty(
        board->rules[0].conditions[0].block, "op")->value).value == ">=");
    CHECK(std::get<double>(Logic::findProperty(
        board->rules[0].conditions[0].block, "value")->value) == 10.0);

    CHECK(controller.handleAction(
        "add-logic-condition-type", rule.id, Logic::kKeyDown, {}));
    CHECK(controller.handleAction(
        "pick-logic-property", rule.id + "|c|1|key", "A", {}));
    CHECK(controller.handleAction(
        "set-logic-condition-join", rule.id + "|1", "or", {}));
    CHECK(controller.handleAction(
        "toggle-logic-condition-negated", rule.id + "|1", {}, {}));
    board = &*coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(board->rules[0].conditions[1].joinBefore == LogicConditionJoin::Or);
    CHECK(board->rules[0].conditions[1].negated);
    CHECK(std::get<LogicKey>(Logic::findProperty(
        board->rules[0].conditions[1].block, "key")->value) == LogicKey::A);
    CHECK(coordinator.undo().ok);
    CHECK(!coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].conditions[1].negated);
    CHECK(coordinator.redo().ok);

    CHECK(controller.handleAction(
        "move-logic-condition-up", rule.id + "|1", {}, {}));
    board = &*coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(board->rules[0].conditions[0].block.typeId == Logic::kKeyDown);
    CHECK(board->rules[0].conditions[0].joinBefore == LogicConditionJoin::And);
    CHECK(board->rules[0].conditions[0].negated);
    CHECK(controller.handleAction(
        "move-logic-condition-down", rule.id + "|0", {}, {}));
    CHECK(controller.handleAction(
        "set-logic-condition-join", rule.id + "|1", "or", {}));
    CHECK(controller.handleAction(
        "change-logic-condition", rule.id + "|1", Logic::kIsVisible, {}));
    board = &*coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(board->rules[0].conditions[1].block.typeId == Logic::kIsVisible);
    CHECK(board->rules[0].conditions[1].joinBefore == LogicConditionJoin::Or);
    CHECK(board->rules[0].conditions[1].negated);

    CHECK(controller.handleAction(
        "change-logic-action", rule.id + "|0", Logic::kStateSet, {}));
    CHECK(controller.handleAction(
        "pick-logic-property", rule.id + "|a|0|key", "score", {}));
    CHECK(controller.handleAction(
        "commit-logic-property", rule.id + "|a|0|value", "42", {}));
    board = &*coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(std::get<double>(
        Logic::findProperty(board->rules[0].actions[0], "value")->value) == 42.0);

    CHECK(controller.handleAction(
        "change-logic-action", rule.id + "|0", Logic::kStateAdd, {}));
    CHECK(controller.handleAction(
        "commit-logic-property", rule.id + "|a|0|amount", "3", {}));
    CHECK(controller.handleAction(
        "change-logic-action", rule.id + "|0", Logic::kStateSubtract, {}));
    CHECK(controller.handleAction(
        "commit-logic-property", rule.id + "|a|0|amount", "2", {}));
    CHECK(controller.handleAction(
        "change-logic-action", rule.id + "|0", Logic::kStateToggle, {}));
    CHECK(controller.handleAction(
        "pick-logic-property", rule.id + "|a|0|key", "doorOpen", {}));
    board = &*coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(std::get<LogicVariableReference>(
        Logic::findProperty(board->rules[0].actions[0], "key")->value).id == "doorOpen");

    CHECK(controller.handleAction(
        "change-logic-action", rule.id + "|0", Logic::kSetVelocity, {}));
    CHECK(controller.handleAction(
        "commit-logic-property-component", rule.id + "|a|0|velocity|x", "7.5", {}));
    CHECK(controller.handleAction(
        "commit-logic-property-component", rule.id + "|a|0|velocity|y", "-4", {}));
    board = &*coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const Vec2 velocity = std::get<Vec2>(
        Logic::findProperty(board->rules[0].actions[0], "velocity")->value);
    CHECK(velocity.x == 7.5f);
    CHECK(velocity.y == -4.f);

    CHECK(controller.handleAction(
        "remove-logic-condition", rule.id + "|0", {}, {}));
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].conditions.size() == 1);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].conditions[0].joinBefore == LogicConditionJoin::And);
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].conditions.size() == 2);
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].conditions.size() == 1);
}

static void testConditionCompatibility() {
    EditorCoordinator coordinator{makeProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
    // Is Grounded requires Platformer — StructuralCommit still allows the edit
    // (ADR-0013); AuthoringDiagnostics reports LB_INCOMPATIBLE_BLOCK.
    const auto result = coordinator.execute(AddLogicConditionCommand{
        "Hero", rule.id, Logic::makeDefaultCondition(), 0});
    CHECK(result.ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
        .conditions.size() == 1);
    const auto authoring = Logic::validateBoard(
        "Hero", *coordinator.document().data().objectTypes.at("Hero").logicBoard,
        coordinator.document().findObjectType("Hero"), &coordinator.document().data(),
        Logic::LogicValidationPurpose::AuthoringDiagnostics);
    CHECK(std::any_of(authoring.begin(), authoring.end(), [](const Logic::LogicDiagnostic& d) {
        return d.code == "LB_INCOMPATIBLE_BLOCK";
    }));
}

static ProjectDoc makePlatformerProjectData() {
    ProjectDoc doc;
    doc.formatVersion = 4;
    doc.projectName = "Platformer Logic Test";
    doc.activeSceneId = "s";

    EntityDef hero;
    hero.name = "Hero";
    hero.className = "Hero";
    PlatformerControllerComponent pc;
    pc.maxSpeed = 180.f; pc.jumpForce = 420.f; pc.customGravity = 1200.f;
    hero.platformerController = pc;
    hero.boxCollider2D = BoxCollider2DComponent{{0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Solid};
    hero.spriteRenderer = SpriteRendererComponent{{}, true};
    doc.objectTypes.emplace("Hero", hero);

    EntityDef floor;
    floor.name = "Floor";
    floor.className = "Floor";
    floor.boxCollider2D = BoxCollider2DComponent{{0.f, 0.f}, {200.f, 32.f}, true, BoxColliderMode::Solid};
    doc.objectTypes.emplace("Floor", floor);

    SceneDef scene;
    scene.id = "s";
    scene.name = "Scene";
    scene.worldSize = {4000.f, 4000.f};
    scene.defaultLayerId = "layer-1";
    scene.layers.push_back(SceneLayerDef{"layer-1", "Layer 1"});

    SceneInstanceDef heroInstance;
    heroInstance.id = 1;
    heroInstance.objectTypeId = "Hero";
    heroInstance.instanceName = "Hero 1";
    heroInstance.layerId = "layer-1";
    heroInstance.transform.position = {0.f, 0.f};
    scene.instances.push_back(heroInstance);
    scene.entityIds.push_back(1);

    SceneInstanceDef floorInstance;
    floorInstance.id = 2;
    floorInstance.objectTypeId = "Floor";
    floorInstance.instanceName = "Floor 1";
    floorInstance.layerId = "layer-1";
    floorInstance.transform.position = {0.f, 100.f};
    scene.instances.push_back(floorInstance);
    scene.entityIds.push_back(2);

    doc.scenes.emplace(scene.id, scene);
    return doc;
}

static ProjectDoc makeAnimationLogicProjectData() {
    ProjectDoc doc = makeProjectData();
    ImageAssetDef heroImage;
    heroImage.assetId = "img-hero";
    heroImage.sourcePath = "sprites/hero.ppm";
    doc.imageAssets.push_back(heroImage);
    ImageAssetDef altImage;
    altImage.assetId = "img-alt";
    altImage.sourcePath = "sprites/alt.ppm";
    doc.imageAssets.push_back(altImage);

    SpriteAnimationAssetDef heroAnim;
    heroAnim.id = "hero.anim";
    heroAnim.name = "Hero Anim";
    heroAnim.sourceImageAssetId = "img-hero";
    heroAnim.frames.push_back(SpriteFrameDef{"f0", 0, 0, 32, 32});
    SpriteAnimationClipDef idle;
    idle.id = "idle";
    idle.name = "Idle";
    idle.frameIds = {"f0"};
    heroAnim.clips.push_back(idle);
    doc.spriteAnimationAssets.push_back(heroAnim);

    SpriteAnimationAssetDef altAnim;
    altAnim.id = "alt.anim";
    altAnim.name = "Alt Anim";
    altAnim.sourceImageAssetId = "img-alt";
    altAnim.frames.push_back(SpriteFrameDef{"f0", 64, 0, 32, 32});
    SpriteAnimationClipDef run;
    run.id = "run";
    run.name = "Run";
    run.framesPerSecond = 12.f;
    run.frameIds = {"f0"};
    altAnim.clips.push_back(run);
    doc.spriteAnimationAssets.push_back(altAnim);

    EntityDef& hero = doc.objectTypes.at("Hero");
    hero.spriteRenderer = SpriteRendererComponent{{}, true};
    hero.spriteAnimator = SpriteAnimatorComponent{"hero.anim", "idle", true, 1.f};
    return doc;
}

static ProjectDoc makeAudioLogicProjectData() {
    ProjectDoc doc = makeProjectData();
    AudioAssetDef jump;
    jump.assetId = "jump.wav";
    jump.name = "Jump";
    jump.sourcePath = "audio/jump.wav";
    jump.loadMode = AudioLoadMode::StaticSound;
    doc.audioAssets.push_back(jump);
    AudioAssetDef theme;
    theme.assetId = "theme.ogg";
    theme.name = "Theme";
    theme.sourcePath = "audio/theme.ogg";
    theme.loadMode = AudioLoadMode::Stream;
    doc.audioAssets.push_back(theme);
    return doc;
}

// Floor at y=100 -> top at 84 -> the player settles at y=68 (mirrors the
// contact math already proven in editor-core-test.cpp's platformer suite).
static void testConditionGatesRuntimeDispatch() {
    EditorCoordinator coordinator{makePlatformerProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& initial = *coordinator.document().data().objectTypes.at("Hero").logicBoard;

    LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(initial));
    rule.trigger = {Logic::kKeyPressed, {{"key", LogicKey::Space}}};
    rule.actions[0] = {Logic::kSetVisible, {{"target", LogicEntityReference{}}, {"visible", false}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
    CHECK(coordinator.execute(AddLogicConditionCommand{
        "Hero", rule.id, Logic::makeDefaultCondition(), 0}).ok);

    CHECK(coordinator.playCurrentScene().ok);
    CHECK(coordinator.playSession() != nullptr);

    RuntimeInputSnapshot jump;
    jump.pressedLogicKeys.push_back(LogicKey::Space);

    // Airborne at Start Play: the condition blocks the action.
    coordinator.tickRuntime(jump, 1.f / 60.f);
    CHECK(findRenderable(*coordinator.playSession(), 1)->visibleInGame);

    RuntimeInputSnapshot none;
    for (int i = 0; i < 600; ++i) coordinator.tickRuntime(none, 1.f / 60.f);

    // Grounded now: the condition passes and the action fires.
    coordinator.tickRuntime(jump, 1.f / 60.f);
    CHECK(!findRenderable(*coordinator.playSession(), 1)->visibleInGame);
    CHECK(coordinator.stopPlaying().ok);
}

static void testIsGroundedEventRunsOnTick() {
    EditorCoordinator coordinator{makePlatformerProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& initial = *coordinator.document().data().objectTypes.at("Hero").logicBoard;

    LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(initial));
    rule.trigger = Logic::makeDefaultEventBlock(Logic::kIsGrounded);
    rule.actions[0] = {Logic::kSetVisible,
        {{"target", LogicEntityReference{}}, {"visible", false}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);

    CHECK(coordinator.playCurrentScene().ok);
    CHECK(coordinator.playSession() != nullptr);
    CHECK(findRenderable(*coordinator.playSession(), 1)->visibleInGame);

    RuntimeInputSnapshot none;
    for (int i = 0; i < 600; ++i) coordinator.tickRuntime(none, 1.f / 60.f);
    // Once grounded, the predicate event must fire via dispatchTick.
    CHECK(!findRenderable(*coordinator.playSession(), 1)->visibleInGame);
    CHECK(coordinator.stopPlaying().ok);
}

static void testIsFallingEventTrueWhileDescendingFalseWhenGroundedOrRising() {
    EditorCoordinator coordinator{makePlatformerProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& initial = *coordinator.document().data().objectTypes.at("Hero").logicBoard;

    // While falling → hide. A paired Is Grounded → show restores visibility on
    // land so rising can be asserted without mutating PlaySession entities.
    // Logic dispatchTick runs *before* platformer physics each frame.
    LogicRuleDef fallRule = Logic::makeDefaultRule(nextLogicRuleId(initial));
    fallRule.trigger = Logic::makeDefaultEventBlock(Logic::kIsFalling);
    fallRule.actions[0] = {Logic::kSetVisible,
        {{"target", LogicEntityReference{}}, {"visible", false}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", fallRule, 0}).ok);

    const LogicBoardDef& withFall =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    LogicRuleDef landRule = Logic::makeDefaultRule(nextLogicRuleId(withFall));
    landRule.trigger = Logic::makeDefaultEventBlock(Logic::kIsGrounded);
    landRule.actions[0] = {Logic::kSetVisible,
        {{"target", LogicEntityReference{}}, {"visible", true}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", landRule, 1}).ok);

    // RU-03: jump is now always Logic Board/Script authored (no hardcoded
    // host input) - add the key-press -> platformer.jump rule the exported
    // game would also need.
    const LogicBoardDef& withLand =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    LogicRuleDef jumpRule = Logic::makeDefaultRule(nextLogicRuleId(withLand));
    jumpRule.trigger = {Logic::kKeyPressed, {{"key", LogicKey::Space}}};
    jumpRule.actions[0] = Logic::makeDefaultBlock(Logic::kJump, Logic::BlockKind::Action);
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", jumpRule, 2}).ok);

    CHECK(coordinator.playCurrentScene().ok);
    CHECK(coordinator.playSession() != nullptr);
    // RU-03 (D-01): PlaySession no longer exposes platformerController
    // internals (grounded/verticalVelocity) - only visibleInGame, via the
    // render hand-off (findRenderable), which the Is Falling/Is Grounded
    // rules above still drive observably.
    CHECK(findRenderable(*coordinator.playSession(), 1)->visibleInGame);

    RuntimeInputSnapshot none;
    // Frame 1: tick still sees vy=0 → not falling; physics then applies gravity.
    coordinator.tickRuntime(none, 1.f / 60.f);
    CHECK(findRenderable(*coordinator.playSession(), 1)->visibleInGame);

    // Frame 2: tick sees descending → Is Falling fires → hide.
    coordinator.tickRuntime(none, 1.f / 60.f);
    CHECK(!findRenderable(*coordinator.playSession(), 1)->visibleInGame);

    // Settle: Is Grounded shows again; while grounded Is Falling must stay false.
    for (int i = 0; i < 600; ++i) coordinator.tickRuntime(none, 1.f / 60.f);
    CHECK(findRenderable(*coordinator.playSession(), 1)->visibleInGame);

    // Jump: while rising, Is Falling must stay false → remain visible.
    RuntimeInputSnapshot jump;
    jump.pressedLogicKeys.push_back(LogicKey::Space);
    coordinator.tickRuntime(jump, 1.f / 60.f);
    CHECK(findRenderable(*coordinator.playSession(), 1)->visibleInGame);

    // Later in the jump arc, once descending again, Is Falling hides again.
    bool hidWhileDescending = false;
    for (int i = 0; i < 180; ++i) {
        coordinator.tickRuntime(none, 1.f / 60.f);
        const auto hero = findRenderable(*coordinator.playSession(), 1);
        if (!hero) break;
        if (!hero->visibleInGame) {
            hidWhileDescending = true;
            break;
        }
    }
    CHECK(hidWhileDescending);
    CHECK(coordinator.stopPlaying().ok);
}

static void testIsVisibleEventAndMoveBy() {
    EditorCoordinator coordinator{makeProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& initial = *coordinator.document().data().objectTypes.at("Hero").logicBoard;

    LogicRuleDef visibleRule = Logic::makeDefaultRule(nextLogicRuleId(initial));
    visibleRule.trigger = Logic::makeDefaultEventBlock(Logic::kIsVisible);
    visibleRule.actions[0] = {Logic::kTranslateBy, {{"offset", Vec2{10.f, 20.f}}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", visibleRule, 0}).ok);

    const LogicBoardDef& authored =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    CHECK(authored.rules[0].trigger.typeId == Logic::kIsVisible);
    CHECK(authored.rules[0].actions[0].typeId == Logic::kTranslateBy);
    CHECK(coordinator.execute(SetLogicPropertyCommand{
        "Hero", authored.rules[0].id, LogicPropertyTarget::Action, 0,
        "offset", Vec2{12.f, -3.f}}).ok);
    const LogicBoardDef& updated =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicPropertyDef* offset =
        Logic::findProperty(updated.rules[0].actions[0], "offset");
    CHECK(offset != nullptr);
    const auto* offsetValue = std::get_if<Vec2>(&offset->value);
    CHECK(offsetValue != nullptr);
    CHECK(offsetValue->x == 12.f);
    CHECK(offsetValue->y == -3.f);

    CHECK(coordinator.playCurrentScene().ok);
    CHECK(coordinator.playSession() != nullptr);
    const float startX = findRenderable(*coordinator.playSession(), 1)->transform.position.x;
    const float startY = findRenderable(*coordinator.playSession(), 1)->transform.position.y;
    CHECK(findRenderable(*coordinator.playSession(), 1)->visibleInGame);

    RuntimeInputSnapshot none;
    coordinator.tickRuntime(none, 1.f / 60.f);
    CHECK(findRenderable(*coordinator.playSession(), 1)->transform.position.x == startX + 12.f);
    CHECK(findRenderable(*coordinator.playSession(), 1)->transform.position.y == startY - 3.f);
    CHECK(coordinator.stopPlaying().ok);
}

static void testTopDownMovementViaLogicInput() {
    auto makeCoordinator = [](bool fourDirections) {
        ProjectDoc document = makeProjectData();
        TopDownControllerComponent controller;
        controller.maxSpeed = 260.f;
        controller.acceleration = 1600.f;
        controller.friction = 2200.f;
        controller.fourDirections = fourDirections;
        document.objectTypes.at("Hero").topDownController = controller;
        return EditorCoordinator{std::move(document)};
    };
    auto addMoveRule = [](EditorCoordinator& coordinator, LogicKey key,
                          const std::string& direction) {
        const LogicBoardDef& board =
            *coordinator.document().data().objectTypes.at("Hero").logicBoard;
        LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(board));
        rule.trigger = {Logic::kKeyHeld, {{"key", key}}};
        rule.actions[0] = {Logic::kTopDownMove,
                           {{"direction", LogicStringValue{direction}}}};
        return coordinator.execute(AddLogicRuleCommand{"Hero", std::move(rule),
                                                        board.rules.size()}).ok;
    };

    {
        EditorCoordinator coordinator = makeCoordinator(false);
        CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
        CHECK(addMoveRule(coordinator, LogicKey::ArrowRight, "Right"));
        CHECK(addMoveRule(coordinator, LogicKey::ArrowUp, "Up"));
        const auto compiled = Logic::compileProjectLogic(coordinator.document().data());
        CHECK(compiled.ok());
        CHECK(compiled.programs.front().source.find("topdown_move(1, 0)")
              != std::string::npos);

        const uint64_t revision = coordinator.document().revision();
        CHECK(coordinator.playCurrentScene().ok);
        const auto initial = findRenderable(*coordinator.playSession(), 1);
        CHECK(initial.has_value());
        const Vec2 start = initial->transform.position;

        RuntimeInputSnapshot right;
        right.heldLogicKeys.push_back(LogicKey::ArrowRight);
        coordinator.tickRuntime(right, 0.1f);
        const Vec2 afterRight = findRenderable(*coordinator.playSession(), 1)->transform.position;
        CHECK(afterRight.x > start.x);
        CHECK(std::fabs(afterRight.y - start.y) < 0.001f);

        RuntimeInputSnapshot diagonal;
        diagonal.heldLogicKeys = {LogicKey::ArrowRight, LogicKey::ArrowUp};
        coordinator.tickRuntime(diagonal, 0.1f);
        const Vec2 afterDiagonal =
            findRenderable(*coordinator.playSession(), 1)->transform.position;
        CHECK(afterDiagonal.x > afterRight.x);
        CHECK(afterDiagonal.y < afterRight.y);

        RuntimeInputSnapshot none;
        for (int i = 0; i < 10; ++i) coordinator.tickRuntime(none, 1.f / 60.f);
        // Coasting after key release is runtime-dependent; assert Play did not mutate authoring.
        CHECK(coordinator.document().revision() == revision);
        CHECK(coordinator.stopPlaying().ok);
    }

    {
        EditorCoordinator coordinator = makeCoordinator(false);
        CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
        CHECK(addMoveRule(coordinator, LogicKey::ArrowLeft, "Left"));
        CHECK(addMoveRule(coordinator, LogicKey::ArrowRight, "Right"));
        CHECK(coordinator.playCurrentScene().ok);
        const Vec2 start = findRenderable(*coordinator.playSession(), 1)->transform.position;
        RuntimeInputSnapshot opposing;
        opposing.heldLogicKeys = {LogicKey::ArrowLeft, LogicKey::ArrowRight};
        coordinator.tickRuntime(opposing, 0.1f);
        const Vec2 after = findRenderable(*coordinator.playSession(), 1)->transform.position;
        CHECK(std::fabs(after.x - start.x) < 0.001f);
        CHECK(std::fabs(after.y - start.y) < 0.001f);
        CHECK(coordinator.stopPlaying().ok);
    }

    {
        EditorCoordinator coordinator = makeCoordinator(true);
        CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
        CHECK(addMoveRule(coordinator, LogicKey::ArrowRight, "Right"));
        CHECK(addMoveRule(coordinator, LogicKey::ArrowUp, "Up"));
        CHECK(coordinator.playCurrentScene().ok);
        const Vec2 start = findRenderable(*coordinator.playSession(), 1)->transform.position;
        RuntimeInputSnapshot diagonal;
        diagonal.heldLogicKeys = {LogicKey::ArrowRight, LogicKey::ArrowUp};
        coordinator.tickRuntime(diagonal, 0.1f);
        const Vec2 after = findRenderable(*coordinator.playSession(), 1)->transform.position;
        CHECK(after.x > start.x);
        CHECK(std::fabs(after.y - start.y) < 0.001f);
        CHECK(coordinator.stopPlaying().ok);
    }

    ProjectDoc withoutController = makeProjectData();
    const Logic::LogicBlockDescriptor* move = Logic::findDescriptor(Logic::kTopDownMove);
    CHECK(move != nullptr);
    CHECK(!Logic::blockAvailability(withoutController.objectTypes.at("Hero"), *move).compatible);
    CHECK(move->properties.size() == 1);
    CHECK(move->properties[0].valueKind == Logic::LogicValueKind::String);
    CHECK(move->properties[0].semantic == Logic::LogicPropertySemantic::TopDownDirection);
    const std::vector<std::string> directionOptions{"Left", "Right", "Up", "Down"};
    CHECK(move->properties[0].options == directionOptions);

    const Logic::LogicBlockDescriptor* facing = Logic::findDescriptor(Logic::kSpriteSetFacing);
    CHECK(facing != nullptr);
    CHECK(facing->displayName == std::string("Flip Horizontal"));
    CHECK(facing->properties.size() == 1);
    CHECK(facing->properties[0].semantic == Logic::LogicPropertySemantic::SpriteFacing);
    const std::vector<std::string> facingOptions{"Left", "Right"};
    CHECK(facing->properties[0].options == facingOptions);

    ProjectDoc invalidDirection = makeProjectData();
    invalidDirection.objectTypes.at("Hero").topDownController = TopDownControllerComponent{};
    LogicBoardDef invalidBoard;
    invalidBoard.id = "logic:Hero";
    LogicRuleDef invalidRule = Logic::makeDefaultRule("rule-1");
    invalidRule.trigger = {Logic::kKeyHeld, {{"key", LogicKey::ArrowLeft}}};
    invalidRule.actions[0] = {Logic::kTopDownMove,
                              {{"direction", LogicStringValue{"Diagonal"}}}};
    invalidBoard.rules.push_back(std::move(invalidRule));
    invalidDirection.objectTypes.at("Hero").logicBoard = std::move(invalidBoard);
    CHECK(!Logic::compileProjectLogic(invalidDirection).ok());

    ProjectDoc invalidFacing = makeProjectData();
    LogicBoardDef facingBoard;
    facingBoard.id = "logic:Hero";
    LogicRuleDef facingRule = Logic::makeDefaultRule("rule-1");
    facingRule.trigger = {Logic::kKeyPressed, {{"key", LogicKey::ArrowLeft}}};
    facingRule.actions[0] = {Logic::kSpriteSetFacing,
                             {{"facing", LogicStringValue{"Up"}}}};
    facingBoard.rules.push_back(std::move(facingRule));
    invalidFacing.objectTypes.at("Hero").logicBoard = std::move(facingBoard);
    CHECK(!Logic::compileProjectLogic(invalidFacing).ok());

    ProjectDoc leftFacing = makeProjectData();
    LogicBoardDef leftBoard;
    leftBoard.id = "logic:Hero";
    LogicRuleDef leftRule = Logic::makeDefaultRule("rule-1");
    leftRule.trigger = {Logic::kOnStart, {}};
    leftRule.actions[0] = {Logic::kSpriteSetFacing,
                           {{"facing", LogicStringValue{"Left"}}}};
    leftBoard.rules.push_back(std::move(leftRule));
    leftFacing.objectTypes.at("Hero").logicBoard = std::move(leftBoard);
    const auto leftCompiled = Logic::compileProjectLogic(leftFacing);
    CHECK(leftCompiled.ok());
    CHECK(leftCompiled.programs.front().source.find("set_flip_x(true)")
          != std::string::npos);

    const Logic::LogicBlockDescriptor* moveH = Logic::findDescriptor(Logic::kMoveHorizontal);
    CHECK(moveH != nullptr);
    CHECK(moveH->properties.size() == 1);
    CHECK(moveH->properties[0].key == "direction");
    CHECK(moveH->properties[0].semantic == Logic::LogicPropertySemantic::PlatformerDirection);
    const std::vector<std::string> moveOptions{"Left", "Right"};
    CHECK(moveH->properties[0].options == moveOptions);

    ProjectDoc platformerMove = makeProjectData();
    platformerMove.objectTypes.at("Hero").platformerController = PlatformerControllerComponent{};
    LogicBoardDef moveBoard;
    moveBoard.id = "logic:Hero";
    LogicRuleDef moveRule = Logic::makeDefaultRule("rule-1");
    moveRule.trigger = {Logic::kKeyHeld, {{"key", LogicKey::A}}};
    moveRule.actions[0] = {Logic::kMoveHorizontal,
                           {{"direction", LogicStringValue{"Left"}}}};
    moveBoard.rules.push_back(std::move(moveRule));
    platformerMove.objectTypes.at("Hero").logicBoard = std::move(moveBoard);
    const auto moveCompiled = Logic::compileProjectLogic(platformerMove);
    CHECK(moveCompiled.ok());
    CHECK(moveCompiled.programs.front().source.find("platformer_move(-1)")
          != std::string::npos);

    // While Key Held + Move Horizontal must stop when the key is released
    // (frame-local intent, same contract as Top Down Move).
    {
        EditorCoordinator coordinator{makePlatformerProjectData()};
        CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
        const LogicBoardDef& board =
            *coordinator.document().data().objectTypes.at("Hero").logicBoard;
        LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(board));
        rule.trigger = {Logic::kKeyHeld, {{"key", LogicKey::D}}};
        rule.actions[0] = {Logic::kMoveHorizontal,
                           {{"direction", LogicStringValue{"Right"}}}};
        CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", std::move(rule), 0}).ok);
        CHECK(coordinator.playCurrentScene().ok);

        RuntimeInputSnapshot held;
        held.heldLogicKeys.push_back(LogicKey::D);
        const float startX = findRenderable(*coordinator.playSession(), 1)->transform.position.x;
        coordinator.tickRuntime(held, 0.1f);
        const float afterHeld =
            findRenderable(*coordinator.playSession(), 1)->transform.position.x;
        CHECK(afterHeld > startX);

        RuntimeInputSnapshot none;
        coordinator.tickRuntime(none, 0.1f);
        const float afterRelease =
            findRenderable(*coordinator.playSession(), 1)->transform.position.x;
        CHECK(std::fabs(afterRelease - afterHeld) < 0.001f);
        CHECK(coordinator.stopPlaying().ok);
    }
}

static void testFlipHorizontalProjectsToSceneView() {
    ProjectDoc project = makeProjectData();
    ImageAssetDef image;
    image.assetId = "img-hero";
    image.sourcePath = "sprites/hero.ppm";
    project.imageAssets.push_back(image);
    project.objectTypes.at("Hero").spriteRenderer =
        SpriteRendererComponent{"img-hero", true};
    EditorCoordinator coordinator{std::move(project)};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& board =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(board));
    rule.trigger = {Logic::kOnStart, {}};
    rule.actions[0] = {Logic::kSpriteSetFacing,
                       {{"facing", LogicStringValue{"Left"}}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", std::move(rule), 0}).ok);

    CHECK(coordinator.playCurrentScene().ok);
    const auto hero = findRenderable(*coordinator.playSession(), 1);
    CHECK(hero.has_value());
    CHECK(hero->sprite.flipX);
    CHECK(!hero->sprite.flipY);

    const EditorNative::SceneFrameSnapshot snap =
        collectSceneFrameSnapshot(*coordinator.playSession());
    const EditorNative::SceneFrameSprite* projected = nullptr;
    for (const EditorNative::SceneFrameSprite& sprite : snap.sprites) {
        if (sprite.entityId == 1) {
            projected = &sprite;
            break;
        }
    }
    CHECK(projected != nullptr);
    if (projected) {
        CHECK(projected->flipX);
        CHECK(!projected->flipY);
    }
    CHECK(coordinator.stopPlaying().ok);
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
    CHECK(!findRenderable(*coordinator.playSession(), 1)->visibleInGame);
    CHECK(coordinator.document().revision() == revision);
    CHECK(coordinator.document().findInstanceInScene("scene-1", 1)->transform.position.x == 5.f);

    RuntimeInputSnapshot input;
    input.pressedLogicKeys.push_back(LogicKey::Space);
    coordinator.tickRuntime(input, 1.f / 60.f);
    CHECK(findRenderable(*coordinator.playSession(), 1)->transform.position.x == 40.f);
    CHECK(findRenderable(*coordinator.playSession(), 1)->transform.position.y == 50.f);
    CHECK(coordinator.document().findInstanceInScene("scene-1", 1)->transform.position.x == 5.f);
    CHECK(coordinator.stopPlaying().ok);
    CHECK(coordinator.playSession() == nullptr);
    CHECK(coordinator.document().findInstanceInScene("scene-1", 1)->transform.position.x == 5.f);
}

static void testCollisionEventOtherAndDeferredDestroy() {
    ProjectDoc data = makeProjectData();
    data.objectTypes.at("Hero").boxCollider2D =
        BoxCollider2DComponent{{0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    EntityDef pickup;
    pickup.name = "Pickup";
    pickup.className = "Pickup";
    pickup.boxCollider2D = BoxCollider2DComponent{
        {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    // RU-03: a SpriteRendererComponent makes destruction observable through
    // findRenderable() below - otherwise "absent from renderables()" would be
    // trivially true even without correct destroy behavior (no sprite -> no
    // component -> never enumerated in the first place).
    pickup.spriteRenderer = SpriteRendererComponent{{}, true};
    data.objectTypes.emplace("Pickup", pickup);
    EntityDef sensor;
    sensor.name = "Sensor";
    sensor.className = "Sensor";
    sensor.boxCollider2D = BoxCollider2DComponent{
        {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    // RU-03: LinearMover moves every tick without needing Logic Board/Script
    // authoring for input (unlike TopDownController, which has no Logic
    // Board action - movement.setIntent is Script-only) - simplest way to
    // separate the colliding pair and exercise CollisionExit below.
    sensor.linearMover = LinearMoverComponent{1.f, 0.f, 600.f};
    data.objectTypes.emplace("Sensor", sensor);
    SceneInstanceDef pickupInstance;
    pickupInstance.id = 2;
    pickupInstance.objectTypeId = "Pickup";
    pickupInstance.instanceName = "Pickup 1";
    pickupInstance.layerId = "layer-1";
    pickupInstance.transform.position = {5.f, 6.f}; // overlaps Hero from the first runtime frame
    data.scenes.at("scene-1").instances.push_back(pickupInstance);
    data.scenes.at("scene-1").entityIds.push_back(2);
    SceneInstanceDef sensorInstance;
    sensorInstance.id = 3;
    sensorInstance.objectTypeId = "Sensor";
    sensorInstance.instanceName = "Sensor 1";
    sensorInstance.layerId = "layer-1";
    sensorInstance.transform.position = {5.f, 6.f};
    data.scenes.at("scene-1").instances.push_back(sensorInstance);
    data.scenes.at("scene-1").entityIds.push_back(3);

    EditorCoordinator coordinator{std::move(data)};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Pickup"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Pickup").logicBoard;
    LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    rule.trigger = Logic::makeDefaultBlock(Logic::kCollisionEnter, Logic::BlockKind::Trigger);
    rule.actions[0] = Logic::makeDefaultBlock(Logic::kDestroySelf, Logic::BlockKind::Action);
    LogicBlockDef other = Logic::makeDefaultBlock(Logic::kOtherIsObjectType, Logic::BlockKind::Condition);
    other.properties[0].value = LogicStringValue{"Hero"};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Pickup", rule, 0}).ok);
    CHECK(coordinator.execute(AddLogicConditionCommand{"Pickup", rule.id, other, 0}).ok);

    // EventOther is not valid outside collision triggers. StructuralCommit still
    // accepts the edit (ADR-0013); AuthoringDiagnostics reports the semantic error.
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& heroEmpty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef heroRule = Logic::makeDefaultRule(nextLogicRuleId(heroEmpty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", heroRule, 0}).ok);
    CHECK(coordinator.execute(AddLogicConditionCommand{"Hero", heroRule.id, other, 0}).ok);
    const auto heroAuthoring = Logic::validateBoard(
        "Hero", *coordinator.document().data().objectTypes.at("Hero").logicBoard,
        coordinator.document().findObjectType("Hero"), &coordinator.document().data(),
        Logic::LogicValidationPurpose::AuthoringDiagnostics);
    CHECK(Logic::hasLogicErrors(heroAuthoring));
    CHECK(coordinator.execute(RemoveLogicConditionCommand{"Hero", heroRule.id, 0}).ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
              .conditions.empty());

    const LogicBoardDef& heroBoard = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    LogicRuleDef exitRule = Logic::makeDefaultRule(nextLogicRuleId(heroBoard));
    exitRule.trigger = Logic::makeDefaultBlock(Logic::kCollisionExit, Logic::BlockKind::Trigger);
    exitRule.actions[0] = {Logic::kSetVisible,
                           {{"target", LogicEntityReference{}}, {"visible", false}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", exitRule, 1}).ok);

    // ADR-0014: BoxCollider2D materialises into CollisionBody on the shared
    // spawn path — enter destroys Pickup; exit hides Hero once Sensor separates.
    CHECK(coordinator.playCurrentScene().ok);
    RuntimeInputSnapshot none;
    coordinator.tickRuntime(none, 1.f / 60.f);
    // Destroy Self queues removal after collision dispatch; the next fixed-step
    // flush applies it (same deferred contract as game.exe).
    coordinator.tickRuntime(none, 1.f / 60.f);
    CHECK(!findRenderable(*coordinator.playSession(), 2).has_value());
    CHECK(coordinator.document().findInstanceInScene("scene-1", 2) != nullptr);
    // The exit subscription fires once for the pair transition and is not
    // emitted while the pair remains absent on later frames. Sensor's
    // LinearMover separates the pair without needing input.
    coordinator.tickRuntime(none, 0.2f);
    CHECK(!findRenderable(*coordinator.playSession(), 1)->visibleInGame);
    CHECK(!findRenderable(*coordinator.playSession(), 2).has_value());
    CHECK(coordinator.stopPlaying().ok);
}

static void testAnimationActions() {
    EditorCoordinator coordinator{makeAnimationLogicProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;

    LogicRuleDef start = Logic::makeDefaultRule(nextLogicRuleId(empty));
    start.actions.clear();
    LogicBlockDef play = Logic::makeDefaultBlock(Logic::kAnimationPlayClip, Logic::BlockKind::Action);
    play.properties[0].value = LogicAssetReference{"alt.anim"};
    play.properties[1].value = LogicStringValue{"run"};
    LogicBlockDef speed = Logic::makeDefaultBlock(
        Logic::kAnimationSetPlaybackSpeed, Logic::BlockKind::Action);
    speed.properties[0].value = 2.0;
    start.actions.push_back(play);
    start.actions.push_back(speed);
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", start, 0}).ok);

    const LogicBoardDef& withStart = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    LogicRuleDef stop = Logic::makeDefaultRule(nextLogicRuleId(withStart));
    stop.trigger = {Logic::kKeyPressed, {{"key", LogicKey::Space}}};
    stop.actions[0] = Logic::makeDefaultBlock(Logic::kAnimationStop, Logic::BlockKind::Action);
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", stop, 1}).ok);

    const auto compiled = Logic::compileProjectLogic(coordinator.document().data());
    CHECK(compiled.ok());
    CHECK(!compiled.programs.empty());
    CHECK(compiled.programs.front().source.find("play_animation_clip") != std::string::npos);

    CHECK(coordinator.playCurrentScene().ok);
    // RU-03 (D-01): PlaySession no longer exposes SpriteAnimator internals
    // (currentClipId/playbackSpeed/playing) - only the resolved per-frame
    // sprite asset id, via the render hand-off.
    const auto hero = coordinator.playSession()
        ? findRenderable(*coordinator.playSession(), 1) : std::nullopt;
    CHECK(hero && resolvedSpriteAssetId(*hero) == "img-alt");

    RuntimeInputSnapshot input;
    input.pressedLogicKeys.push_back(LogicKey::Space);
    coordinator.tickRuntime(input, 1.f / 60.f);
    CHECK(coordinator.stopPlaying().ok);

    CHECK(coordinator.execute(SetLogicAnimationClipCommand{
        "Hero", start.id, 0, "hero.anim", "idle"}).ok);
    const LogicBlockDef& changed = coordinator.document().data().objectTypes.at("Hero")
        .logicBoard->rules[0].actions[0];
    CHECK(std::get<LogicAssetReference>(changed.properties[0].value).id == "hero.anim");
    CHECK(std::get<LogicStringValue>(changed.properties[1].value).value == "idle");
    CHECK(coordinator.undo().ok);
    const LogicBlockDef& undone = coordinator.document().data().objectTypes.at("Hero")
        .logicBoard->rules[0].actions[0];
    CHECK(std::get<LogicAssetReference>(undone.properties[0].value).id == "alt.anim");
    CHECK(std::get<LogicStringValue>(undone.properties[1].value).value == "run");
    CHECK(coordinator.redo().ok);
}

static void testAnimationActionValidation() {
    ProjectDoc data = makeAnimationLogicProjectData();
    LogicBoardDef board;
    board.id = "logic:Hero";
    LogicRuleDef missingAsset = Logic::makeDefaultRule("rule-1");
    missingAsset.actions[0] = Logic::makeDefaultBlock(
        Logic::kAnimationPlayClip, Logic::BlockKind::Action);
    missingAsset.actions[0].properties[0].value = LogicAssetReference{"missing.anim"};
    missingAsset.actions[0].properties[1].value = LogicStringValue{"idle"};
    board.rules.push_back(missingAsset);
    data.objectTypes.at("Hero").logicBoard = board;
    // Missing animation asset is semantic (AuthoringDiagnostics / Executable), not
    // StructuralCommit — ProjectValidator must still accept the document.
    CHECK(ProjectValidator::validate(ProjectDocument{data}).ok);
    CHECK(Logic::hasLogicErrors(Logic::validateBoard(
        "Hero", *data.objectTypes.at("Hero").logicBoard,
        &data.objectTypes.at("Hero"), &data,
        Logic::LogicValidationPurpose::AuthoringDiagnostics)));

    ProjectDoc noAnimator = makeProjectData();
    noAnimator.spriteAnimationAssets = data.spriteAnimationAssets;
    noAnimator.imageAssets = data.imageAssets;
    noAnimator.objectTypes.at("Hero").logicBoard = std::move(board);
    CHECK(ProjectValidator::validate(ProjectDocument{noAnimator}).ok);
    CHECK(Logic::hasLogicErrors(Logic::validateBoard(
        "Hero", *noAnimator.objectTypes.at("Hero").logicBoard,
        &noAnimator.objectTypes.at("Hero"), &noAnimator,
        Logic::LogicValidationPurpose::AuthoringDiagnostics)));

    ProjectDoc draft = makeAnimationLogicProjectData();
    LogicBoardDef draftBoard;
    draftBoard.id = "logic:Hero";
    LogicRuleDef draftRule = Logic::makeDefaultRule("rule-1");
    draftRule.actions[0] = Logic::makeDefaultBlock(
        Logic::kAnimationPlayClip, Logic::BlockKind::Action);
    draftBoard.rules.push_back(draftRule);
    draft.objectTypes.at("Hero").logicBoard = draftBoard;
    CHECK(ProjectValidator::validate(ProjectDocument{draft}).ok);
    CHECK(!Logic::compileProjectLogic(draft).ok());
}

static void testPlaySoundAction() {
    EditorCoordinator coordinator{makeAudioLogicProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;

    LogicRuleDef start = Logic::makeDefaultRule(nextLogicRuleId(empty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", start, 0}).ok);

    // Deterministic default: the only StaticSound asset in the project is
    // picked automatically the moment the action is added (see
    // assignDefaultAudioAsset in logic_board_commands.cpp) — never left to
    // depend on unordered_map iteration order.
    CHECK(coordinator.execute(AddLogicActionCommand{
        "Hero", start.id,
        Logic::makeDefaultBlock(Logic::kAudioPlaySound, Logic::BlockKind::Action), 0}).ok);
    const LogicBlockDef* added = &coordinator.document().data().objectTypes.at("Hero")
        .logicBoard->rules[0].actions[0];
    CHECK(added->typeId == Logic::kAudioPlaySound);
    const LogicPropertyDef* defaultAsset = Logic::findProperty(*added, "audioAssetId");
    CHECK(defaultAsset && std::get<LogicAssetReference>(defaultAsset->value).id == "jump.wav");

    // Same commands the UI's set-logic-audio-asset / commit-logic-audio-volume
    // actions dispatch (logic_board_editor_controller.cpp).
    CHECK(coordinator.execute(SetLogicPropertyCommand{
        "Hero", start.id, LogicPropertyTarget::Action, 0,
        "audioAssetId", LogicAssetReference{"jump.wav"}}).ok);
    CHECK(coordinator.execute(SetLogicPropertyCommand{
        "Hero", start.id, LogicPropertyTarget::Action, 0, "volume", 0.5}).ok);

    const auto compiled = Logic::compileProjectLogic(coordinator.document().data());
    CHECK(compiled.ok());
    CHECK(compiled.programs.front().source.find("play_sound(\"jump.wav\", 0.5)") != std::string::npos);

    // RU-03 (D-01): Play Sound now plays through the real Modules::Audio the
    // session owns (like game.exe), not a host-visible command queue -
    // PlaySession no longer exposes drainAudioCommands(). This only checks
    // Play still starts and ticks correctly with the compiled action.
    CHECK(coordinator.playCurrentScene().ok);
    CHECK(coordinator.stopPlaying().ok);

    CHECK(coordinator.execute(SetLogicPropertyCommand{
        "Hero", start.id, LogicPropertyTarget::Action, 0, "volume", 0.9}).ok);
    CHECK(coordinator.undo().ok);
    const LogicBlockDef& undone = coordinator.document().data().objectTypes.at("Hero")
        .logicBoard->rules[0].actions[0];
    CHECK(std::get<double>(Logic::findProperty(undone, "volume")->value) == 0.5);
}

static void testPlaySoundCanBeSelectedBeforeImportingAudio() {
    EditorCoordinator coordinator{makeProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef start = Logic::makeDefaultRule(nextLogicRuleId(empty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", start, 0}).ok);
    CHECK(coordinator.apply(OpenLogicBoardIntent{"Hero"}).ok);
    LogicBoardEditorController controller{coordinator, nullptr};

    // Exercise the exact RmlUi action -> Controller -> Intent -> Coordinator ->
    // Command path used by the picker, not merely the Command in isolation.
    const uint64_t revisionBefore = coordinator.document().revision();
    CHECK(controller.handleAction(
        "change-logic-action", start.id + "|0", Logic::kAudioPlaySound, {}));
    CHECK(coordinator.document().revision() == revisionBefore + 1);
    CHECK(coordinator.document().isDirty());
    const LogicBlockDef& selected = coordinator.document().data().objectTypes.at("Hero")
        .logicBoard->rules[0].actions[0];
    CHECK(selected.typeId == Logic::kAudioPlaySound);
    const LogicPropertyDef* audioAsset = Logic::findProperty(selected, "audioAssetId");
    CHECK(audioAsset && std::get<LogicAssetReference>(audioAsset->value).id.empty());
    CHECK(ProjectValidator::validate(ProjectDocument{coordinator.document().data()}).ok);
    CHECK(!Logic::compileProjectLogic(coordinator.document().data()).ok());
    CHECK(!coordinator.playCurrentScene().ok);

    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].actions[0].typeId == Logic::kSetVisible);
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].actions[0].typeId == Logic::kAudioPlaySound);

    // Other edits remain linear while the draft is incomplete: no Command
    // locally reinterprets the core diagnostic policy.
    CHECK(controller.handleAction(
        "commit-logic-audio-volume", start.id + "|0", "0.4", {}));
    CHECK(std::abs(std::get<double>(Logic::findProperty(
        coordinator.document().data().objectTypes.at("Hero").logicBoard
            ->rules[0].actions[0], "volume")->value) - 0.4) < 1e-6);

    // Drafts are real authoring states: Save/Load preserves them, while the
    // executable validator above continues to block Play.
    const std::filesystem::path draftPath =
        std::filesystem::temp_directory_path() / "artcade-logic-audio-draft.artcade-project";
    std::error_code cleanupError;
    std::filesystem::remove(draftPath, cleanupError);
    CHECK(saveProjectToFile(coordinator, draftPath).ok);
    CHECK(!coordinator.document().isDirty());
    EditorCoordinator reloaded;
    CHECK(loadProjectFromFile(reloaded, draftPath).ok);
    const LogicBlockDef& reloadedDraft = reloaded.document().data().objectTypes.at("Hero")
        .logicBoard->rules[0].actions[0];
    CHECK(reloadedDraft.typeId == Logic::kAudioPlaySound);
    CHECK(std::get<LogicAssetReference>(
        Logic::findProperty(reloadedDraft, "audioAssetId")->value).id.empty());
    CHECK(std::abs(std::get<double>(
        Logic::findProperty(reloadedDraft, "volume")->value) - 0.4) < 1e-6);

    // Complete the same loaded draft through normal authoring Commands, then
    // Save and Play successfully.
    CHECK(reloaded.execute(AddAudioAssetCommand{
        "jump.wav", "audio/jump.wav", AudioLoadMode::StaticSound}).ok);
    CHECK(reloaded.apply(OpenLogicBoardIntent{"Hero"}).ok);
    LogicBoardEditorController reloadedController{reloaded, nullptr};
    CHECK(reloadedController.handleAction(
        "set-logic-audio-asset", start.id + "|0", "jump.wav", {}));
    CHECK(Logic::compileProjectLogic(reloaded.document().data()).ok());
    CHECK(saveProjectToFile(reloaded, draftPath).ok);
    CHECK(reloaded.playCurrentScene().ok);
    CHECK(reloaded.stopPlaying().ok);
    std::filesystem::remove(draftPath, cleanupError);

    // The same policy applies to the "+ Add Action" catalog path.
    EditorCoordinator addCoordinator{makeProjectData()};
    CHECK(addCoordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& addEmpty =
        *addCoordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef addRule = Logic::makeDefaultRule(nextLogicRuleId(addEmpty));
    CHECK(addCoordinator.execute(AddLogicRuleCommand{"Hero", addRule, 0}).ok);
    CHECK(addCoordinator.apply(OpenLogicBoardIntent{"Hero"}).ok);
    LogicBoardEditorController addController{addCoordinator, nullptr};
    CHECK(addController.handleAction(
        "add-logic-action-type", addRule.id, Logic::kAudioPlaySound, {}));
    CHECK(addCoordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].actions[1].typeId == Logic::kAudioPlaySound);
}

static void testCatalogPickersShareIntentCommandPath() {
    ProjectDoc project = makeProjectData();
    project.objectTypes.at("Hero").platformerController = PlatformerControllerComponent{};
    EditorCoordinator coordinator{std::move(project)};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
    CHECK(coordinator.apply(OpenLogicBoardIntent{"Hero"}).ok);
    LogicBoardEditorController controller{coordinator, nullptr};

    const uint64_t beforeTrigger = coordinator.document().revision();
    CHECK(controller.handleAction(
        "change-logic-trigger", rule.id, Logic::kKeyPressed, {}));
    CHECK(coordinator.document().revision() == beforeTrigger + 1);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].trigger.typeId == Logic::kKeyPressed);

    const uint64_t beforeEvent = coordinator.document().revision();
    CHECK(controller.handleAction(
        "change-logic-trigger", rule.id, Logic::kIsGrounded, {}));
    CHECK(coordinator.document().revision() == beforeEvent + 1);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].trigger.typeId == Logic::kIsGrounded);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].conditions.empty());

    // Re-selecting the same event type is a Command no-op (no artificial revision).
    const uint64_t beforeNoOp = coordinator.document().revision();
    CHECK(controller.handleAction(
        "change-logic-trigger", rule.id, Logic::kIsGrounded, {}));
    CHECK(coordinator.document().revision() == beforeNoOp);

    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].trigger.typeId == Logic::kKeyPressed);
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard
        ->rules[0].trigger.typeId == Logic::kIsGrounded);
}

static void testPlaySoundActionValidation() {
    const ProjectDoc data = makeAudioLogicProjectData();

    ProjectDoc missing = data;
    LogicBoardDef missingBoard;
    missingBoard.id = "logic:Hero";
    LogicRuleDef missingRule = Logic::makeDefaultRule("rule-1");
    missingRule.actions[0] = Logic::makeDefaultBlock(Logic::kAudioPlaySound, Logic::BlockKind::Action);
    missingRule.actions[0].properties[0].value = LogicAssetReference{"missing.wav"};
    missingBoard.rules.push_back(missingRule);
    missing.objectTypes.at("Hero").logicBoard = missingBoard;
    // Missing / stream / bad-volume audio are semantic — StructuralCommit allows.
    CHECK(ProjectValidator::validate(ProjectDocument{missing}).ok);
    CHECK(Logic::hasLogicErrors(Logic::validateBoard(
        "Hero", *missing.objectTypes.at("Hero").logicBoard,
        &missing.objectTypes.at("Hero"), &missing,
        Logic::LogicValidationPurpose::AuthoringDiagnostics)));

    ProjectDoc stream = data;
    LogicBoardDef streamBoard;
    streamBoard.id = "logic:Hero";
    LogicRuleDef streamRule = Logic::makeDefaultRule("rule-1");
    streamRule.actions[0] = Logic::makeDefaultBlock(Logic::kAudioPlaySound, Logic::BlockKind::Action);
    streamRule.actions[0].properties[0].value = LogicAssetReference{"theme.ogg"};
    streamBoard.rules.push_back(streamRule);
    stream.objectTypes.at("Hero").logicBoard = streamBoard;
    CHECK(ProjectValidator::validate(ProjectDocument{stream}).ok);
    CHECK(Logic::hasLogicErrors(Logic::validateBoard(
        "Hero", *stream.objectTypes.at("Hero").logicBoard,
        &stream.objectTypes.at("Hero"), &stream,
        Logic::LogicValidationPurpose::AuthoringDiagnostics)));

    ProjectDoc badVolume = data;
    LogicBoardDef volumeBoard;
    volumeBoard.id = "logic:Hero";
    LogicRuleDef volumeRule = Logic::makeDefaultRule("rule-1");
    volumeRule.actions[0] = Logic::makeDefaultBlock(Logic::kAudioPlaySound, Logic::BlockKind::Action);
    volumeRule.actions[0].properties[0].value = LogicAssetReference{"jump.wav"};
    volumeRule.actions[0].properties[1].value = 1.5;
    volumeBoard.rules.push_back(volumeRule);
    badVolume.objectTypes.at("Hero").logicBoard = volumeBoard;
    CHECK(ProjectValidator::validate(ProjectDocument{badVolume}).ok);
    CHECK(Logic::hasLogicErrors(Logic::validateBoard(
        "Hero", *badVolume.objectTypes.at("Hero").logicBoard,
        &badVolume.objectTypes.at("Hero"), &badVolume,
        Logic::LogicValidationPurpose::AuthoringDiagnostics)));
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

static void testWorkspaceTargetAndSwitchPolicy() {
    ProjectDoc data = makeProjectData();
    EntityDef enemy;
    enemy.name = "Enemy";
    enemy.className = "Enemy";
    data.objectTypes.emplace("Enemy", enemy);
    SceneInstanceDef enemyInstance;
    enemyInstance.id = 2;
    enemyInstance.objectTypeId = "Enemy";
    enemyInstance.instanceName = "Enemy 1";
    enemyInstance.layerId = "layer-1";
    data.scenes.at("scene-1").instances.push_back(enemyInstance);
    data.scenes.at("scene-1").entityIds.push_back(2);

    EditorCoordinator coordinator{std::move(data)};
    const uint64_t revision = coordinator.document().revision();
    const std::size_t undoSize = coordinator.undoSize();

    auto opened = coordinator.apply(OpenLogicBoardIntent{"Hero"});
    CHECK(opened.ok);
    CHECK(coordinator.state().centerWorkspaceMode == CenterWorkspaceMode::Logic);
    CHECK(coordinator.state().logicBoardEditor.objectTypeId == std::optional<ObjectTypeId>{"Hero"});
    CHECK(has(opened.invalidation, EditorInvalidation::Layout));
    CHECK(has(opened.invalidation, EditorInvalidation::Toolbar));
    CHECK(has(opened.invalidation, EditorInvalidation::Viewport));

    // Selection alone never retargets the open board. The Hierarchy controller
    // follows this with one explicit OpenLogicBoardIntent when policy requires.
    CHECK(coordinator.apply(SelectEntityIntent{2}).ok);
    CHECK(coordinator.state().logicBoardEditor.objectTypeId == std::optional<ObjectTypeId>{"Hero"});
    const EditorOperationResult retargeted = coordinator.apply(OpenLogicBoardIntent{"Enemy"});
    CHECK(retargeted.ok);
    CHECK(retargeted.invalidation
          == (EditorInvalidation::LogicBoard | EditorInvalidation::Toolbar));
    CHECK(coordinator.state().logicBoardEditor.objectTypeId == std::optional<ObjectTypeId>{"Enemy"});
    CHECK(coordinator.apply(SetLogicBoardTabIntent{LogicBoardTab::GeneratedLua}).ok);
    CHECK(coordinator.apply(SetLogicBoardSearchIntent{"enemy"}).ok);

    CHECK(coordinator.apply(SwitchCenterWorkspaceIntent{CenterWorkspaceMode::Scene}).ok);
    CHECK(coordinator.apply(SelectEntityIntent{1}).ok);
    CHECK(coordinator.apply(SwitchCenterWorkspaceIntent{CenterWorkspaceMode::Logic}).ok);
    CHECK(coordinator.state().logicBoardEditor.objectTypeId == std::optional<ObjectTypeId>{"Enemy"});
    CHECK(coordinator.state().logicBoardEditor.tab == LogicBoardTab::GeneratedLua);
    CHECK(coordinator.state().logicBoardEditor.search == "enemy");
    CHECK(coordinator.selection().primaryEntity == 1);

    CHECK(coordinator.document().revision() == revision);
    CHECK(!coordinator.document().isDirty());
    CHECK(coordinator.undoSize() == undoSize);
    CHECK(!coordinator.apply(OpenLogicBoardIntent{"Missing"}).ok);
    CHECK(coordinator.state().logicBoardEditor.objectTypeId == std::optional<ObjectTypeId>{"Enemy"});
}

static void testPlayNavigationFromLogicBoard() {
    EditorCoordinator coordinator{makeProjectData()};
    CHECK(coordinator.apply(OpenLogicBoardIntent{"Hero"}).ok);
    CHECK(coordinator.apply(SetLogicBoardTabIntent{LogicBoardTab::GeneratedLua}).ok);
    CHECK(coordinator.apply(SetLogicBoardSearchIntent{"hero rule"}).ok);

    // Starting from Logic is atomic: a successful runtime session switches to
    // Scene, while the exact Logic Board workspace context is retained only in
    // transient coordinator state for Stop.
    CHECK(coordinator.playCurrentScene().ok);
    CHECK(coordinator.isPlaying());
    CHECK(coordinator.state().centerWorkspaceMode == CenterWorkspaceMode::Scene);
    CHECK(coordinator.stopPlaying().ok);
    CHECK(!coordinator.isPlaying());
    CHECK(coordinator.state().centerWorkspaceMode == CenterWorkspaceMode::Logic);
    CHECK(coordinator.state().logicBoardEditor.objectTypeId == std::optional<ObjectTypeId>{"Hero"});
    CHECK(coordinator.state().logicBoardEditor.tab == LogicBoardTab::GeneratedLua);
    CHECK(coordinator.state().logicBoardEditor.search == "hero rule");

    // An explicit workspace click during Play disarms the automatic return,
    // including an explicit click on the currently visible Scene tab.
    CHECK(coordinator.playCurrentScene().ok);
    CHECK(coordinator.apply(SwitchCenterWorkspaceIntent{CenterWorkspaceMode::Scene}).ok);
    CHECK(coordinator.stopPlaying().ok);
    CHECK(coordinator.state().centerWorkspaceMode == CenterWorkspaceMode::Scene);

    ProjectDoc invalid = makeProjectData();
    LogicBoardDef invalidBoard;
    invalidBoard.id = "logic:Hero";
    LogicRuleDef invalidRule = Logic::makeDefaultRule("rule-1");
    invalidRule.trigger.typeId = "unknown.trigger";
    invalidBoard.rules.push_back(invalidRule);
    invalid.objectTypes.at("Hero").logicBoard = std::move(invalidBoard);
    EditorCoordinator rejected{std::move(invalid)};
    CHECK(rejected.apply(OpenLogicBoardIntent{"Hero"}).ok);
    CHECK(!rejected.playCurrentScene().ok);
    CHECK(!rejected.isPlaying());
    CHECK(rejected.state().centerWorkspaceMode == CenterWorkspaceMode::Logic);
}

static void testExecutionModeCommand() {
    EditorCoordinator coordinator{makeProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    // Level trigger (KeyHeld): OncePerActivation must not emit
    // LB_EXECUTION_MODE_PULSE_REDUNDANT, or AssetLoader rejects Play (warnings count).
    LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    rule.trigger = Logic::makeDefaultBlock(Logic::kKeyHeld, Logic::BlockKind::Trigger);
    rule.actions[0] = Logic::makeDefaultBlock(Logic::kSetVisible, Logic::BlockKind::Action);
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
              .executionMode == LogicExecutionMode::EveryOccurrence);

    const uint64_t revision = coordinator.document().revision();
    const bool dirtyBefore = coordinator.document().isDirty();
    CHECK(coordinator.execute(SetLogicRuleExecutionModeCommand{
        "Hero", rule.id, LogicExecutionMode::OncePerActivation}).ok);
    CHECK(coordinator.document().revision() == revision + 1);
    CHECK(coordinator.document().isDirty());
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
              .executionMode == LogicExecutionMode::OncePerActivation);

    // Same mode → no-op (no dirty/revision bump).
    const uint64_t afterSet = coordinator.document().revision();
    CHECK(coordinator.execute(SetLogicRuleExecutionModeCommand{
        "Hero", rule.id, LogicExecutionMode::OncePerActivation}).ok);
    CHECK(coordinator.document().revision() == afterSet);

    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
              .executionMode == LogicExecutionMode::EveryOccurrence);
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
              .executionMode == LogicExecutionMode::OncePerActivation);

    const auto serialized = ProjectSerializer::serialize(coordinator.document());
    CHECK(serialized.ok);
    CHECK(serialized.value.find("once_per_activation") != std::string::npos);
    const auto loaded = ProjectSerializer::deserialize(serialized.value);
    CHECK(loaded.ok);
    CHECK(loaded.value.data().objectTypes.at("Hero").logicBoard->rules[0].executionMode
          == LogicExecutionMode::OncePerActivation);

    CHECK(coordinator.playCurrentScene().ok);
    CHECK(!coordinator.execute(SetLogicRuleExecutionModeCommand{
        "Hero", rule.id, LogicExecutionMode::EveryOccurrence}).ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
              .executionMode == LogicExecutionMode::OncePerActivation);
    CHECK(coordinator.stopPlaying().ok);
    (void)dirtyBefore;
}

// ADR-0004: capture and Search Key are presentation routes only. Both must
// converge on the existing typed property Command, so Undo, JSON and Lua keep
// the same authority as every other Logic key edit.
static void testKeyBindingEditorRoutes() {
    EditorCoordinator coordinator{makeProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
    CHECK(coordinator.apply(OpenLogicBoardIntent{"Hero"}).ok);
    LogicBoardEditorController controller{coordinator, nullptr};
    CHECK(controller.handleAction("change-logic-trigger", rule.id, Logic::kKeyPressed, {}));

    const std::string address = rule.id + "|t|0|key";
    const auto selectedKey = [&]() {
        const LogicBlockDef& trigger = coordinator.document().data().objectTypes.at("Hero")
                                           .logicBoard->rules[0].trigger;
        const LogicPropertyDef* property = Logic::findProperty(trigger, "key");
        return property ? std::get<LogicKey>(property->value) : LogicKey::Space;
    };
    CHECK(selectedKey() == LogicKey::Space);

    // Arming/cancelling is UI-only: no revision and no Undo history mutation.
    const uint64_t beforeCapture = coordinator.document().revision();
    const std::size_t undoBeforeCapture = coordinator.undoSize();
    CHECK(controller.handleAction("begin-logic-key-capture", address, {}, {}));
    CHECK(controller.hasKeyCapture());
    CHECK(coordinator.document().revision() == beforeCapture);
    CHECK(coordinator.undoSize() == undoBeforeCapture);
    CHECK(controller.cancelKeyCapture());
    CHECK(!controller.hasKeyCapture());
    CHECK(selectedKey() == LogicKey::Space);
    CHECK(coordinator.document().revision() == beforeCapture);

    // A supported physical key maps to the canonical LogicKey and has normal
    // command Undo semantics.
    CHECK(controller.handleAction("begin-logic-key-capture", address, {}, {}));
    CHECK(controller.captureKey(LogicKey::A));
    CHECK(!controller.hasKeyCapture());
    CHECK(selectedKey() == LogicKey::A);
    CHECK(coordinator.undo().ok);
    CHECK(selectedKey() == LogicKey::Space);

    // Re-selecting the existing key is a command no-op.
    const uint64_t beforeNoOp = coordinator.document().revision();
    const std::size_t undoBeforeNoOp = coordinator.undoSize();
    CHECK(controller.handleAction("begin-logic-key-capture", address, {}, {}));
    CHECK(controller.captureKey(LogicKey::Space));
    CHECK(coordinator.document().revision() == beforeNoOp);
    CHECK(coordinator.undoSize() == undoBeforeNoOp);

    // Search uses the exact same typed controller path, including the
    // canonical token persisted to JSON and generated Lua.
    CHECK(controller.handleAction("toggle-logic-key-search", address, {}, {}));
    CHECK(controller.handleAction("filter-logic-key-search", address, "ent", {}));
    CHECK(controller.handleAction("pick-logic-key-binding", address, "Enter", {}));
    CHECK(selectedKey() == LogicKey::Enter);
    const auto serialized = ProjectSerializer::serialize(coordinator.document());
    CHECK(serialized.ok);
    CHECK(serialized.value.find("\"value\": \"Enter\"") != std::string::npos);
    const auto loaded = ProjectSerializer::deserialize(serialized.value);
    CHECK(loaded.ok);
    CHECK(std::get<LogicKey>(Logic::findProperty(
              loaded.value.data().objectTypes.at("Hero").logicBoard->rules[0].trigger, "key")->value)
          == LogicKey::Enter);
    const auto compiled = Logic::compileProjectLogic(coordinator.document().data());
    CHECK(compiled.ok());
    CHECK(compiled.programs.front().source.find("\"Enter\"") != std::string::npos);

    // Play rejects a new capture and clears an existing transient target on
    // the next projection refresh without changing authored data.
    CHECK(controller.handleAction("begin-logic-key-capture", address, {}, {}));
    CHECK(controller.hasKeyCapture());
    CHECK(coordinator.playCurrentScene().ok);
    controller.refresh();
    CHECK(!controller.hasKeyCapture());
    CHECK(controller.handleAction("begin-logic-key-capture", address, {}, {}));
    CHECK(!controller.hasKeyCapture());
    CHECK(selectedKey() == LogicKey::Enter);
    CHECK(coordinator.stopPlaying().ok);
}

static void testIncompatibleBoardRecovery() {
    ProjectDoc project = makeProjectData();
    project.objectTypes.at("Hero").topDownController = TopDownControllerComponent{};
    LogicBoardDef board;
    board.id = "logic:Hero";
    for (int i = 0; i < 8; ++i) {
        LogicRuleDef rule = Logic::makeDefaultRule("rule-" + std::to_string(i + 1));
        rule.actions = {
            Logic::makeDefaultBlock(Logic::kTopDownMove, Logic::BlockKind::Action)};
        board.rules.push_back(std::move(rule));
    }
    project.objectTypes.at("Hero").logicBoard = std::move(board);
    EditorCoordinator coordinator{std::move(project)};

    CHECK(coordinator.execute(RemoveTopDownControllerCommand{"Hero"}).ok);
    CHECK(!coordinator.document().data().objectTypes.at("Hero").topDownController.has_value());

    const auto authoringBefore = Logic::validateBoard(
        "Hero", *coordinator.document().data().objectTypes.at("Hero").logicBoard,
        coordinator.document().findObjectType("Hero"), &coordinator.document().data(),
        Logic::LogicValidationPurpose::AuthoringDiagnostics);
    CHECK(std::count_if(authoringBefore.begin(), authoringBefore.end(),
                        [](const Logic::LogicDiagnostic& d) {
                            return d.code == "LB_INCOMPATIBLE_BLOCK";
                        })
          == 8);

    const uint64_t revision = coordinator.document().revision();
    CHECK(coordinator.execute(RemoveLogicRuleCommand{"Hero", "rule-1"}).ok);
    CHECK(coordinator.document().revision() == revision + 1);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules.size() == 7);

    CHECK(coordinator
              .execute(ChangeLogicActionTypeCommand{
                  "Hero", "rule-2", 0, Logic::kSetVisible})
              .ok);
    const auto authoringAfter = Logic::validateBoard(
        "Hero", *coordinator.document().data().objectTypes.at("Hero").logicBoard,
        coordinator.document().findObjectType("Hero"), &coordinator.document().data(),
        Logic::LogicValidationPurpose::AuthoringDiagnostics);
    CHECK(std::count_if(authoringAfter.begin(), authoringAfter.end(),
                        [](const Logic::LogicDiagnostic& d) {
                            return d.code == "LB_INCOMPATIBLE_BLOCK";
                        })
          == 6);

    const auto serialized = ProjectSerializer::serialize(coordinator.document());
    CHECK(serialized.ok);
    const auto loaded = ProjectSerializer::deserialize(serialized.value);
    CHECK(loaded.ok);
    CHECK(loaded.value.data().objectTypes.at("Hero").logicBoard->rules.size() == 7);
    const auto loadedDiags = Logic::validateBoard(
        "Hero", *loaded.value.data().objectTypes.at("Hero").logicBoard,
        loaded.value.findObjectType("Hero"), &loaded.value.data(),
        Logic::LogicValidationPurpose::AuthoringDiagnostics);
    CHECK(std::count_if(loadedDiags.begin(), loadedDiags.end(),
                        [](const Logic::LogicDiagnostic& d) {
                            return d.code == "LB_INCOMPATIBLE_BLOCK";
                        })
          == 6);

    const uint64_t beforePlay = coordinator.document().revision();
    CHECK(!coordinator.playCurrentScene().ok);
    CHECK(coordinator.document().revision() == beforePlay);
    CHECK(!coordinator.document().data().objectTypes.at("Hero").topDownController.has_value());

    const LogicComponentReferenceReport refs = collectComponentLogicReferences(
        coordinator.document(), "Hero", ComponentKind::TopDownController);
    CHECK(refs.actionCount() == 6);
    CHECK(refs.conditionCount() == 0);

    const uint64_t beforeRepair = coordinator.document().revision();
    CHECK(coordinator
              .execute(RepairIncompatibleLogicCommand{
                  "Hero", IncompatibleLogicRepair::DisableAffectedRules})
              .ok);
    CHECK(coordinator.document().revision() == beforeRepair + 1);
    std::size_t disabled = 0;
    for (const LogicRuleDef& rule :
         coordinator.document().data().objectTypes.at("Hero").logicBoard->rules) {
        if (!rule.enabled) ++disabled;
    }
    CHECK(disabled == 6);
    const auto afterDisable = Logic::validateBoard(
        "Hero", *coordinator.document().data().objectTypes.at("Hero").logicBoard,
        coordinator.document().findObjectType("Hero"), &coordinator.document().data(),
        Logic::LogicValidationPurpose::Executable);
    CHECK(!Logic::hasLogicErrors(afterDisable));
    CHECK(Logic::compileBoard(
              "Hero", *coordinator.document().data().objectTypes.at("Hero").logicBoard,
              coordinator.document().findObjectType("Hero"), &coordinator.document().data())
              .ok());

    CHECK(coordinator
              .apply(FocusLogicDiagnosticIntent{
                  "Hero", "rule-3", Logic::kTopDownMove, {}})
              .ok);
    CHECK(coordinator.state().centerWorkspaceMode == CenterWorkspaceMode::Logic);
    CHECK(coordinator.state().logicBoardEditor.objectTypeId == "Hero");
    CHECK(coordinator.state().logicBoardEditor.focusRuleId
          && *coordinator.state().logicBoardEditor.focusRuleId == "rule-3");
    CHECK(coordinator.state().logicBoardEditor.highlightBlockTypeId == Logic::kTopDownMove);

    CHECK(coordinator.undo().ok);
    for (const LogicRuleDef& rule :
         coordinator.document().data().objectTypes.at("Hero").logicBoard->rules) {
        CHECK(rule.enabled);
    }

    const uint64_t beforeRemove = coordinator.document().revision();
    CHECK(coordinator
              .execute(RepairIncompatibleLogicCommand{
                  "Hero", IncompatibleLogicRepair::RemoveAffectedRules})
              .ok);
    CHECK(coordinator.document().revision() > beforeRemove);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules.size() == 1);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0].id
          == "rule-2");
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules.size() == 7);
}

int main() {
    testCommandsAndPersistence();
    testIncompatibleBoardRecovery();
    testDuplicateLogicRule();
    testGlobalVariableCommands();
    testConditionCommands();
    testConditionControllerAndGenericProperties();
    testConditionCompatibility();
    testConditionGatesRuntimeDispatch();
    testIsGroundedEventRunsOnTick();
    testIsFallingEventTrueWhileDescendingFalseWhenGroundedOrRising();
    testIsVisibleEventAndMoveBy();
    testTopDownMovementViaLogicInput();
    testFlipHorizontalProjectsToSceneView();
    testPlayRuntimeIsolation();
    testCollisionEventOtherAndDeferredDestroy();
    testAnimationActions();
    testAnimationActionValidation();
    testPlaySoundAction();
    testPlaySoundCanBeSelectedBeforeImportingAudio();
    testCatalogPickersShareIntentCommandPath();
    testPlaySoundActionValidation();
    testInvalidPlayIsAtomic();
    testWorkspaceTargetAndSwitchPolicy();
    testPlayNavigationFromLogicBoard();
    testExecutionModeCommand();
    testKeyBindingEditorRoutes();
    std::cout << "logic-board-editor-test: " << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
