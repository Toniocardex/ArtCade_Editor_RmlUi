#include "editor-native/app/visual_fixture.h"

#include "editor-native/model/project_document.h"
#include "editor-native/model/project_io.h"

#include "logic-core.h"

#include <fstream>

namespace ArtCade::EditorNative {
namespace {

EntityDef makeObjectType(const std::string& name) {
    EntityDef type;
    type.name = name;
    type.className = name;
    // A sprite renderer is what makes an instance appear as a real drawable
    // rather than an empty placeholder box.
    type.spriteRenderer = SpriteRendererComponent{{}, true};
    type.boxCollider2D = BoxCollider2DComponent{
        {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    return type;
}

SceneInstanceDef makeInstance(EntityId id, const std::string& type,
                              const std::string& name, const std::string& layer,
                              Vec2 position) {
    SceneInstanceDef instance;
    instance.id = id;
    instance.objectTypeId = type;
    instance.instanceName = name;
    instance.layerId = layer;
    instance.transform.position = position;
    return instance;
}

/** Key Pressed → Set Visible, gated by a condition, so the WHEN column shows
    the key picker, the IF column is non-empty and Execution is visible. */
LogicRuleDef makeKeyRule() {
    LogicRuleDef rule = Logic::makeDefaultRule("rule-hide");
    rule.name = "Hide on Space";
    rule.trigger = {Logic::kKeyPressed, {{"key", LogicKey::Space}}};
    rule.actions[0] = {Logic::kSetVisible,
                       {{"target", LogicEntityReference{}}, {"visible", false}}};
    LogicBlockDef visible =
        Logic::makeDefaultBlock(Logic::kIsVisible, Logic::BlockKind::Condition);
    rule.conditions.push_back(
        LogicConditionClause{LogicConditionJoin::And, false, visible});
    return rule;
}

/** Collision → Destroy Other + Add to Number: two actions in one THEN column,
    an Object Type reference and a variable reference (ADR-0026). */
LogicRuleDef makeCollectRule() {
    LogicRuleDef rule = Logic::makeDefaultRule("rule-collect");
    rule.name = "Collect coin";
    rule.trigger =
        Logic::makeDefaultBlock(Logic::kCollisionEnter, Logic::BlockKind::Trigger);
    rule.trigger.properties[0].value = LogicStringValue{"Coin"};
    rule.actions[0] =
        Logic::makeDefaultBlock(Logic::kDestroyOther, Logic::BlockKind::Action);
    rule.actions.push_back(LogicBlockDef{
        Logic::kStateAdd,
        {{"key", LogicVariableReference{"score"}}, {"amount", 1.0}}});
    return rule;
}

/** Key Pressed → Spawn Object of the board's OWN type, at an explicit
    position: the reported repro for the reentrant-install crash, and the only
    rule in the fixture with a Vec2 property, so the two-field value row stays
    covered by the visual reference. */
LogicRuleDef makeCloneRule() {
    LogicRuleDef rule = Logic::makeDefaultRule("rule-clone");
    rule.name = "Clone self";
    rule.trigger = {Logic::kKeyPressed, {{"key", LogicKey::Enter}}};
    rule.actions[0] = {Logic::kSpawnObject,
                       {{"objectTypeId", LogicStringValue{"Player"}},
                        {"position", Vec2{64.f, 32.f}}}};
    return rule;
}

} // namespace

ProjectDoc makeVisualFixtureProject() {
    ProjectDoc doc;
    doc.projectName = "ArtCade Visual Fixture";

    EntityDef player = makeObjectType("Player");
    LogicBoardDef board;
    board.id = "logic:Player";
    board.rules.push_back(makeKeyRule());
    // Second, not last: the capture is one window tall, and this is the rule
    // whose Vec2 row the reference exists to watch.
    board.rules.push_back(makeCloneRule());
    board.rules.push_back(makeCollectRule());
    player.logicBoard = board;
    doc.objectTypes.emplace("Player", player);
    doc.objectTypes.emplace("Coin", makeObjectType("Coin"));

    // A Number global so the Logic Board's variable picker has something to
    // resolve, and the Variables drawer is not empty.
    doc.globalVariables.push_back(
        GameVariableDefinition{"score", GameVariableDefinition::Type::Number, 0.0,
                               "Coins collected"});

    SceneDef scene;
    scene.id = "scene-1";
    scene.name = "Level 1";
    scene.worldSize = {512.f, 320.f};
    // Explicit dark backdrop: the struct default is white, which would make
    // the reference capture look nothing like a real project.
    scene.backgroundColor = {30.f / 255.f, 30.f / 255.f, 36.f / 255.f, 1.f};
    scene.defaultLayerId = "layer-main";
    // Two layers, one locked: the Hierarchy and the Layer Manager both render
    // states (lock, count, active) that a single-layer scene never shows.
    scene.layers.push_back(SceneLayerDef{"layer-bg", "Background", true});
    scene.layers.push_back(SceneLayerDef{"layer-main", "Main", false});
    scene.instances.push_back(
        makeInstance(1, "Player", "Player 1", "layer-main", {96.f, 160.f}));
    scene.instances.push_back(
        makeInstance(2, "Coin", "Coin 1", "layer-main", {224.f, 160.f}));
    scene.instances.push_back(
        makeInstance(3, "Coin", "Coin 2", "layer-bg", {320.f, 96.f}));
    scene.entityIds = {1, 2, 3};
    doc.scenes.emplace(scene.id, scene);
    doc.activeSceneId = scene.id;

    // A second scene so the scene tab bar and Go To Scene (ADR-0025) have a
    // target to offer.
    SceneDef second;
    second.id = "scene-2";
    second.name = "Level 2";
    second.worldSize = {512.f, 320.f};
    second.backgroundColor = {30.f / 255.f, 30.f / 255.f, 36.f / 255.f, 1.f};
    second.defaultLayerId = "layer-main-2";
    second.layers.push_back(SceneLayerDef{"layer-main-2", "Main", false});
    doc.scenes.emplace(second.id, second);

    return doc;
}

std::string writeVisualFixtureProject(const std::string& path) {
    const SerializeResult serialized =
        ProjectSerializer::serialize(ProjectDocument{makeVisualFixtureProject()});
    if (!serialized.ok) return "serialize failed: " + serialized.error;
    std::ofstream out(path, std::ios::binary);
    if (!out) return "cannot open " + path;
    out << serialized.value;
    if (!out) return "write failed: " + path;
    return {};
}

} // namespace ArtCade::EditorNative
