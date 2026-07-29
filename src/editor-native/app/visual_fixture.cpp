#include "editor-native/app/visual_fixture.h"

#include "editor-native/model/project_document.h"
#include "editor-native/model/project_io.h"
#include "editor-native/model/tileset_slicing.h"

#include "logic-core.h"

#include <raylib.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace ArtCade::EditorNative {
namespace {

EntityDef makeObjectType(const std::string& name, const AssetId& imageAssetId) {
    EntityDef type;
    type.name = name;
    type.className = name;
    // A SpriteRendererComponent with an empty imageAssetId is "no static
    // image" (types.h) — Play and exported builds share the same gameplay
    // render pass (scene_entities_pass.cpp), which draws nothing for a
    // renderable with no sprite sheet; there is no missing-texture
    // placeholder there. An actual image asset is required for the fixture
    // to render as a real drawable in either context.
    type.spriteRenderer = SpriteRendererComponent{imageAssetId, true};
    type.boxCollider2D = BoxCollider2DComponent{
        {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    return type;
}

SceneInstanceDef makeInstance(EntityId id, const std::string& type,
                              const std::string& layer, Vec2 position) {
    SceneInstanceDef instance;
    instance.id = id;
    instance.objectTypeId = type;
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
    rule.actions.at(0).block = {Logic::kSetVisible,
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
    rule.actions.at(0).block =
        Logic::makeDefaultBlock(Logic::kDestroyOther, Logic::BlockKind::Action);
    rule.actions.push_back(LogicActionDef{
        "action-2", LogicExecutionMode::EveryOccurrence,
        LogicBlockDef{Logic::kStateAdd,
            {{"key", LogicVariableReference{"score"}},
             {"amount", NumberExpression::literal(1.0)}}}});
    return rule;
}

/** On Start → Set Position, the one property that takes typed expressions
    (ADR-0029). One axis dynamic and one literal in the same row, because the
    point of the redesign is that both are now the same control — the reference
    would not show a regression back to two. */
LogicRuleDef makeCloneRule() {
    LogicRuleDef rule = Logic::makeDefaultRule("rule-place");
    rule.name = "Place randomly";
    rule.trigger = Logic::makeDefaultBlock(Logic::kOnStart, Logic::BlockKind::Trigger);
    NumberRandomRangeExpression random;
    random.minimum = boxNumberExpression(NumberExpression::literal(0.0));
    random.maximum = boxNumberExpression(
        NumberExpression{NumberPropertyExpression{NumberProperty::SceneWorldWidth}});
    LogicVec2Value position;
    position.x = NumberExpression{std::move(random)};
    position.y = NumberExpression::literal(160.0);
    rule.actions.at(0).block = {Logic::kSetPosition,
                               {{"target", LogicEntityReference{}}, {"position", position}}};
    // Move By and Set Velocity took expressions in 14b1c18. They render through
    // the generic property editor, unlike Set Position which used to have its
    // own path — so the reference is what proves all three offer the field.
    rule.actions.push_back(LogicActionDef{
        "action-2", LogicExecutionMode::EveryOccurrence,
        LogicBlockDef{Logic::kTranslateBy,
                      {{"offset", LogicVec2Value::literal(4.0, 0.0)}}}});
    rule.actions.push_back(LogicActionDef{
        "action-3", LogicExecutionMode::EveryOccurrence,
        LogicBlockDef{Logic::kSetVelocity,
                      {{"velocity", LogicVec2Value::literal(0.0, -120.0)}}}});
    return rule;
}

std::string writeFixtureSheet(const std::filesystem::path& projectPath) {
    constexpr int width = 32;
    constexpr int height = 16;
    // "assets" is the one project-asset directory both the export packer
    // (project_pack_service.cpp) and Save As (project_session_controller.cpp)
    // allowlist — a different name here means the file is never bundled into
    // an exported build, so it loads fine from the loose project on disk
    // (editor, Play) and falls back to the missing-texture placeholder from
    // the packed .artcade (export) even though both share the same loader.
    const std::filesystem::path assetPath =
        projectPath.parent_path() / "assets" / "design-system-sheet.png";
    std::error_code error;
    std::filesystem::create_directories(assetPath.parent_path(), error);
    if (error) return "cannot create fixture asset directory: " + error.message();

    constexpr Color colours[8]{
        {37, 99, 235, 255}, {79, 70, 229, 255}, {216, 180, 74, 255},
        {79, 155, 104, 255}, {229, 112, 107, 255}, {63, 63, 70, 255},
        {161, 161, 170, 255}, {39, 39, 42, 255},
    };
    std::vector<Color> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int tile = (y / 8) * 4 + (x / 8);
            const bool border = x % 8 == 0 || x % 8 == 7 || y % 8 == 0 || y % 8 == 7;
            pixels[static_cast<std::size_t>(y * width + x)] =
                border ? colours[tile] : Color{244, 244, 245, 255};
        }
    }
    Image image{pixels.data(), width, height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    if (!ExportImage(image, assetPath.string().c_str()))
        return "write failed: " + assetPath.string();
    return {};
}

} // namespace

ProjectDoc makeVisualFixtureProject() {
    ProjectDoc doc;
    doc.projectName = "ArtCade Visual Fixture";

    // Declared before the object types so Player/Coin can reference it as
    // their static sprite — see makeObjectType's comment for why an actual
    // asset (not just a SpriteRendererComponent) is required to render.
    ImageAssetDef sheet;
    sheet.assetId = "fixture-sheet";
    sheet.name = "Design System Sheet";
    sheet.sourcePath = "assets/design-system-sheet.png";
    doc.imageAssets.push_back(sheet);

    EntityDef player = makeObjectType("Player", sheet.assetId);
    LogicBoardDef board;
    board.id = "logic:Player";
    board.rules.push_back(makeKeyRule());
    // Second, not last: the capture is one window tall, and this is the rule
    // whose Vec2 row the reference exists to watch.
    board.rules.push_back(makeCloneRule());
    board.rules.push_back(makeCollectRule());
    player.logicBoard = board;
    doc.objectTypes.emplace("Player", player);
    doc.objectTypes.emplace("Coin", makeObjectType("Coin", sheet.assetId));
    // Minimal Tilemap host for --shot-escape (tool → Escape). Painting needs a
    // selected instance with TilemapComponent on an unlocked layer; without it
    // Rectangle never arms and Escape is decorative.
    EntityDef ground;
    ground.name = "Ground";
    ground.className = "Ground";
    ground.sprite.fillColor = Vec3{0.42f, 0.45f, 0.52f};
    doc.objectTypes.emplace("Ground", ground);

    // A Number global so the Logic Board's variable picker has something to
    // resolve, and the Variables drawer is not empty.
    doc.globalVariables.push_back(
        GameVariableDefinition{"score", GameVariableDefinition::Type::Number, 0.0,
                               "Coins collected"});

    SpriteAnimationAssetDef animation;
    animation.id = "fixture-animation";
    animation.name = "Fixture Animation";
    animation.sourceImageAssetId = sheet.assetId;
    animation.frames.push_back(SpriteFrameDef{"frame-1", 0, 0, 16, 16});
    animation.frames.push_back(SpriteFrameDef{"frame-2", 16, 0, 16, 16});
    SpriteAnimationClipDef idle;
    idle.id = "idle";
    idle.name = "Idle";
    idle.frameIds = {"frame-1", "frame-2"};
    idle.framesPerSecond = 8.f;
    idle.playbackMode = AnimationPlaybackMode::Loop;
    animation.clips.push_back(idle);
    doc.spriteAnimationAssets.push_back(animation);

    TilesetAsset tileset;
    tileset.assetId = "fixture-tileset";
    tileset.name = "Fixture Tileset";
    tileset.imageAssetId = sheet.assetId;
    tileset.slicing = TilesetSlicing{8, 8};
    tileset.tiles = tilesForSlicing(32, 16, tileset.slicing);
    doc.tilesets.push_back(tileset);

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
        makeInstance(1, "Player", "layer-main", {96.f, 160.f}));
    scene.instances.push_back(
        makeInstance(2, "Coin", "layer-main", {224.f, 160.f}));
    scene.instances.push_back(
        makeInstance(3, "Coin", "layer-bg", {320.f, 96.f}));
    {
        SceneInstanceDef groundInstance =
            makeInstance(4, "Ground", "layer-main", {0.f, 240.f});
        TilemapComponent tilemap;
        tilemap.tilesetAssetId = tileset.assetId;
        tilemap.cellSize = {
            static_cast<float>(tileset.slicing.tileWidth),
            static_cast<float>(tileset.slicing.tileHeight)};
        tilemap.chunkSize = 16;
        // Keep the committed fixture useful for exported-runtime regression:
        // Ground is genuinely painted, not merely a tilemap host with empty
        // chunks. The pattern is deterministic and uses stable sliced IDs.
        TilemapChunk chunk;
        chunk.cells.resize(static_cast<std::size_t>(tilemap.chunkSize * tilemap.chunkSize));
        for (int x = 0; x < tilemap.chunkSize; ++x) {
            chunk.cells[static_cast<std::size_t>(x)] = TilemapCellValue{
                tileset.tiles[static_cast<std::size_t>(x) % tileset.tiles.size()].id,
                TileTransformFlags::None};
        }
        tilemap.chunks.push_back(std::move(chunk));
        groundInstance.tilemap = std::move(tilemap);
        scene.instances.push_back(std::move(groundInstance));
    }
    scene.entityIds = {1, 2, 3, 4};
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
    out.close();
    return writeFixtureSheet(std::filesystem::path{path});
}

} // namespace ArtCade::EditorNative
