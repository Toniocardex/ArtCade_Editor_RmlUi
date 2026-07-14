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
    doc.formatVersion = 4;
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
    CHECK(serialized.value.find("\"formatVersion\": 5") != std::string::npos);
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
    const std::string currentVersion = "\"formatVersion\": 5";
    const std::size_t version = v2.find(currentVersion);
    if (version != std::string::npos) {
        v2.replace(version, currentVersion.size(), "\"formatVersion\": 2");
    }
    auto migratedRaw = ProjectSerializer::deserialize(v2);
    CHECK(migratedRaw.ok);
    auto migrated = ProjectMigration::migrate(std::move(migratedRaw.value));
    CHECK(migrated.ok);
    CHECK(migrated.value.data().formatVersion == 5);
    CHECK(!migrated.value.data().objectTypes.at("Hero").logicBoard.has_value());

    std::string malformed = serialized.value;
    const std::size_t trigger = malformed.find("event.on_start");
    if (trigger != std::string::npos) malformed.replace(trigger, 14, "unknown.event!");
    CHECK(!ProjectSerializer::deserialize(malformed).ok);
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
        ->rules[0].conditions[0].properties[0].value) == false);

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

static void testConditionCompatibility() {
    EditorCoordinator coordinator{makeProjectData()};
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& empty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef rule = Logic::makeDefaultRule(nextLogicRuleId(empty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
    const auto result = coordinator.execute(AddLogicConditionCommand{
        "Hero", rule.id, Logic::makeDefaultCondition(), 0});
    CHECK(!result.ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0]
        .conditions.empty());
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
    heroAnim.defaultClipId = "idle";
    SpriteAnimationClipDef idle;
    idle.id = "idle";
    idle.name = "Idle";
    idle.imageId = "img-hero";
    idle.frames.push_back(SpriteAnimationFrameDef{0, 0, 32, 32});
    heroAnim.clips.push_back(idle);
    doc.spriteAnimationAssets.push_back(heroAnim);

    SpriteAnimationAssetDef altAnim;
    altAnim.id = "alt.anim";
    altAnim.name = "Alt Anim";
    altAnim.defaultClipId = "run";
    SpriteAnimationClipDef run;
    run.id = "run";
    run.name = "Run";
    run.imageId = "img-alt";
    run.framesPerSecond = 12.f;
    run.frames.push_back(SpriteAnimationFrameDef{64, 0, 32, 32});
    altAnim.clips.push_back(run);
    doc.spriteAnimationAssets.push_back(altAnim);

    EntityDef& hero = doc.objectTypes.at("Hero");
    hero.spriteRenderer = SpriteRendererComponent{{}, "hero.anim", true};
    hero.spriteAnimator = SpriteAnimatorComponent{"idle", true, 1.f};
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
    coordinator.updateRuntime(jump, 1.f / 60.f);
    CHECK(coordinator.playSession()->findEntity(1)->visible);

    // Settle onto the floor.
    RuntimeInputSnapshot none;
    for (int i = 0; i < 300; ++i) coordinator.updateRuntime(none, 0.05f);
    CHECK(coordinator.playSession()->findEntity(1)->platformerController->grounded);

    // Grounded now: the condition passes and the action fires.
    coordinator.updateRuntime(jump, 1.f / 60.f);
    CHECK(!coordinator.playSession()->findEntity(1)->visible);
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

static void testCollisionEventOtherAndDeferredDestroy() {
    ProjectDoc data = makeProjectData();
    data.objectTypes.at("Hero").boxCollider2D =
        BoxCollider2DComponent{{0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    EntityDef pickup;
    pickup.name = "Pickup";
    pickup.className = "Pickup";
    pickup.boxCollider2D = BoxCollider2DComponent{
        {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    data.objectTypes.emplace("Pickup", pickup);
    EntityDef sensor;
    sensor.name = "Sensor";
    sensor.className = "Sensor";
    sensor.boxCollider2D = BoxCollider2DComponent{
        {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    sensor.topDownController = TopDownControllerComponent{600.f, 0.f, 0.f, true};
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

    // EventOther is not valid outside collision triggers, and the failed Command
    // leaves the authoring board unchanged.
    CHECK(coordinator.execute(CreateLogicBoardCommand{"Hero"}).ok);
    const LogicBoardDef& heroEmpty = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicRuleDef heroRule = Logic::makeDefaultRule(nextLogicRuleId(heroEmpty));
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", heroRule, 0}).ok);
    CHECK(!coordinator.execute(AddLogicConditionCommand{"Hero", heroRule.id, other, 0}).ok);
    CHECK(coordinator.document().data().objectTypes.at("Hero").logicBoard->rules[0].conditions.empty());

    const LogicBoardDef& heroBoard = *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    LogicRuleDef exitRule = Logic::makeDefaultRule(nextLogicRuleId(heroBoard));
    exitRule.trigger = Logic::makeDefaultBlock(Logic::kCollisionExit, Logic::BlockKind::Trigger);
    exitRule.actions[0] = {Logic::kSetVisible,
                           {{"target", LogicEntityReference{}}, {"visible", false}}};
    CHECK(coordinator.execute(AddLogicRuleCommand{"Hero", exitRule, 1}).ok);

    CHECK(coordinator.playCurrentScene().ok);
    RuntimeInputSnapshot none;
    coordinator.updateRuntime(none, 1.f / 60.f);
    // The collision snapshot finishes dispatching before destruction is applied;
    // then the scope and collider disappear without touching authoring data.
    CHECK(coordinator.playSession()->findEntity(2) == nullptr);
    CHECK(coordinator.document().findInstanceInScene("scene-1", 2) != nullptr);
    // The exit subscription fires once for the pair transition and is not
    // emitted while the pair remains absent on later frames.
    RuntimeInputSnapshot moveSensor;
    moveSensor.moveRight = true;
    coordinator.updateRuntime(moveSensor, 0.2f);
    CHECK(!coordinator.playSession()->findEntity(1)->visible);
    CHECK(coordinator.playSession()->findEntity(2) == nullptr);
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
    const RuntimeEntity* hero = coordinator.playSession()
        ? coordinator.playSession()->findEntity(1) : nullptr;
    CHECK(hero && hero->sprite);
    CHECK(hero && hero->spriteAnimator);
    CHECK(hero && hero->sprite->assetId == "img-alt");
    CHECK(hero && hero->spriteAnimator->animationAssetId == "alt.anim");
    CHECK(hero && hero->spriteAnimator->currentClipId == "run");
    CHECK(hero && hero->spriteAnimator->playbackSpeed == 2.f);
    CHECK(hero && hero->spriteAnimator->playing);
    CHECK(coordinator.playSession()->assets().imageAssets.count("img-alt") == 1);

    RuntimeInputSnapshot input;
    input.pressedLogicKeys.push_back(LogicKey::Space);
    coordinator.updateRuntime(input, 1.f / 60.f);
    hero = coordinator.playSession()->findEntity(1);
    CHECK(hero && hero->spriteAnimator && !hero->spriteAnimator->playing);
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
    CHECK(!ProjectValidator::validate(ProjectDocument{data}).ok);

    ProjectDoc noAnimator = makeProjectData();
    noAnimator.spriteAnimationAssets = data.spriteAnimationAssets;
    noAnimator.imageAssets = data.imageAssets;
    noAnimator.objectTypes.at("Hero").logicBoard = std::move(board);
    CHECK(!ProjectValidator::validate(ProjectDocument{std::move(noAnimator)}).ok);
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
    CHECK(coordinator.state().logicBoardEditor.mode == CenterWorkspaceMode::Logic);
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
    CHECK(coordinator.state().logicBoardEditor.mode == CenterWorkspaceMode::Scene);
    CHECK(coordinator.stopPlaying().ok);
    CHECK(!coordinator.isPlaying());
    CHECK(coordinator.state().logicBoardEditor.mode == CenterWorkspaceMode::Logic);
    CHECK(coordinator.state().logicBoardEditor.objectTypeId == std::optional<ObjectTypeId>{"Hero"});
    CHECK(coordinator.state().logicBoardEditor.tab == LogicBoardTab::GeneratedLua);
    CHECK(coordinator.state().logicBoardEditor.search == "hero rule");

    // An explicit workspace click during Play disarms the automatic return,
    // including an explicit click on the currently visible Scene tab.
    CHECK(coordinator.playCurrentScene().ok);
    CHECK(coordinator.apply(SwitchCenterWorkspaceIntent{CenterWorkspaceMode::Scene}).ok);
    CHECK(coordinator.stopPlaying().ok);
    CHECK(coordinator.state().logicBoardEditor.mode == CenterWorkspaceMode::Scene);

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
    CHECK(rejected.state().logicBoardEditor.mode == CenterWorkspaceMode::Logic);
}

int main() {
    testCommandsAndPersistence();
    testConditionCommands();
    testConditionCompatibility();
    testConditionGatesRuntimeDispatch();
    testPlayRuntimeIsolation();
    testCollisionEventOtherAndDeferredDestroy();
    testAnimationActions();
    testAnimationActionValidation();
    testInvalidPlayIsAtomic();
    testWorkspaceTargetAndSwitchPolicy();
    testPlayNavigationFromLogicBoard();
    std::cout << "logic-board-editor-test: " << passed << " passed, "
              << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
