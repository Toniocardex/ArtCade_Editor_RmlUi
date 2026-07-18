// Shared fixtures for editor-core-* tests.
#include "editor_core_test_harness.h"

#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/linear_mover_commands.h"
#include "editor-native/commands/platformer_controller_commands.h"
#include "editor-native/commands/sprite_animation_commands.h"
#include "editor-native/commands/sprite_presentation_commands.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/commands/top_down_controller_commands.h"
#include "editor-native/model/project_document.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace ArtCade;
using namespace ArtCade::EditorNative;

namespace ArtCade::EditorNative::CoreTest {

int g_passed = 0;
int g_failed = 0;

ProjectDoc makeDoc() {
    ProjectDoc doc;
    doc.projectName = "spike";
    doc.activeSceneId = kSceneA; // persisted gameplay start scene, not editor focus

    SceneDef a;
    a.id = kSceneA;
    a.name = "Scene A";
    a.backgroundColor = {0.1f, 0.1f, 0.1f, 1.f};
    SceneInstanceDef hero;
    hero.id = kHero;
    hero.objectTypeId = "Hero";
    hero.instanceName = "Hero";
    hero.transform.position = {10.f, 20.f};
    a.instances.push_back(hero);

    SceneDef b;
    b.id = kSceneB;
    b.name = "Scene B";

    doc.scenes.emplace(kSceneA, a);
    doc.scenes.emplace(kSceneB, b);
    return doc;
}

ProjectDoc makeReplacementDoc() {
    ProjectDoc doc;
    doc.projectName = "replacement";
    doc.activeSceneId = "scene-replacement";

    SceneDef scene;
    scene.id = "scene-replacement";
    scene.name = "Replacement";
    SceneInstanceDef instance;
    instance.id = 77;
    instance.objectTypeId = "Enemy";
    instance.instanceName = "Enemy";
    instance.transform.position = {7.f, 8.f};
    scene.instances.push_back(instance);
    doc.scenes.emplace(scene.id, scene);
    return doc;
}

// makeDoc plus an image-asset catalog and a non-image (tileset) asset id, for
// the sprite-renderer slice.
ProjectDoc makeSpriteDoc() {
    ProjectDoc doc = makeDoc();
    ImageAssetDef hero;  hero.assetId = "img-hero";  hero.sourcePath = "sprites/hero.ppm";  doc.imageAssets.push_back(hero);
    ImageAssetDef alt;   alt.assetId  = "img-alt";   alt.sourcePath = "sprites/alt.ppm";   doc.imageAssets.push_back(alt);
    TilesetAsset tiles;  tiles.assetId = "tiles-1";  tiles.imageAssetId = "img-hero";  doc.tilesets.push_back(tiles); // not an image
    return doc;
}

// makeSpriteDoc plus an object type "Hero" whose sprite carries an image, so the
// kHero instance (objectTypeId "Hero") inherits a sprite when it has no override.
ProjectDoc makeInheritedDoc() {
    ProjectDoc doc = makeSpriteDoc();
    EntityDef hero;
    hero.className = "Hero";
    hero.name = "Hero";
    hero.visible = true;
    hero.spriteRenderer = SpriteRendererComponent{"img-hero", {}, true};
    doc.objectTypes.emplace("Hero", hero);
    return doc;
}

ProjectDoc makeAnimationDoc() {
    ProjectDoc doc = makeSpriteDoc();
    SpriteAnimationAssetDef anim;
    anim.id = "hero.anim";
    anim.name = "hero.anim";
    anim.defaultClipId = "idle";
    SpriteAnimationClipDef idle;
    idle.id = "idle";
    idle.name = "Idle";
    idle.imageId = "img-hero";
    idle.framesPerSecond = 8.f;
    idle.playbackMode = AnimationPlaybackMode::Loop;
    idle.frames.push_back(SpriteAnimationFrameDef{0, 0, 32, 32});
    idle.frames.push_back(SpriteAnimationFrameDef{32, 0, 32, 32});
    anim.clips.push_back(idle);
    doc.spriteAnimationAssets.push_back(anim);
    EntityDef hero;
    hero.className = "Hero";
    hero.name = "Hero";
    hero.spriteRenderer = SpriteRendererComponent{{}, "hero.anim", true};
    hero.spriteAnimator = SpriteAnimatorComponent{"idle", true, 1.f};
    doc.objectTypes.emplace("Hero", std::move(hero));
    return doc;
}

ProjectDoc makeInvalidStartDoc() {
    ProjectDoc doc = makeReplacementDoc();
    doc.activeSceneId = "missing-start-scene";
    return doc;
}

// makeInheritedDoc plus an authored LinearMover on the "Hero" object type, so the
// kHero instance drifts during Play. Direction is deliberately non-unit (3,0) to
// exercise normalization.
ProjectDoc makeMoverDoc() {
    ProjectDoc doc = makeInheritedDoc();
    LinearMoverComponent lm;
    lm.directionX = 3.f;
    lm.directionY = 0.f;
    lm.speed = 100.f;
    doc.objectTypes.at("Hero").linearMover = lm;
    return doc;
}

// makeInheritedDoc plus a TopDownController on "Hero" (speed = maxSpeed), so the
// kHero instance is input-driven during Play.
ProjectDoc makeTopDownDoc(float speed) {
    ProjectDoc doc = makeInheritedDoc();
    TopDownControllerComponent tdc;
    tdc.maxSpeed = speed;
    doc.objectTypes.at("Hero").topDownController = tdc;
    return doc;
}

ProjectDoc makeEmptyDoc() {
    ProjectDoc doc;
    doc.projectName = "empty";
    doc.activeSceneId = "missing";
    return doc;
}

std::string validProjectJson() {
    return R"json({
  "formatVersion": 1,
  "projectName": "LoadedProject",
  "activeSceneId": "loaded-scene",
  "scenes": [
    {
      "id": "loaded-scene",
      "name": "Loaded Scene",
      "instances": [
        {
          "id": 88,
          "objectTypeId": "LoadedType",
          "instanceName": "Loaded Entity",
          "transform": { "position": { "x": 123, "y": 456 } }
        }
      ]
    }
  ]
})json";
}

std::string danglingStartJson() {
    return R"json({
  "formatVersion": 1,
  "projectName": "Dangling",
  "activeSceneId": "missing",
  "scenes": [
    { "id": "real-scene", "name": "Real Scene" }
  ]
})json";
}

std::string unsupportedVersionJson() {
    return R"json({
  "formatVersion": 99,
  "projectName": "Future",
  "activeSceneId": "future-scene",
  "scenes": [
    { "id": "future-scene", "name": "Future Scene" }
  ]
})json";
}

std::string zeroSceneJson() {
    return R"json({
  "formatVersion": 1,
  "projectName": "No Scenes",
  "activeSceneId": "missing",
  "scenes": []
})json";
}

std::string duplicateSceneJson() {
    return R"json({
  "formatVersion": 1,
  "projectName": "Duplicate Scene",
  "activeSceneId": "dupe",
  "scenes": [
    { "id": "dupe", "name": "First" },
    { "id": "dupe", "name": "Second" }
  ]
})json";
}

std::filesystem::path testTempDir() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path()
        / ("artcade-editor-core-test-"
           + std::to_string(
#ifdef _WIN32
                 static_cast<unsigned long>(::GetCurrentProcessId())
#else
                 static_cast<unsigned long>(::getpid())
#endif
           ));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

bool hasTempSibling(const std::filesystem::path& destination) {
    const std::filesystem::path dir = destination.parent_path();
    const std::string prefix = destination.filename().string() + ".tmp-";
    if (!std::filesystem::exists(dir)) return false;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().filename().string().rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

void expectCoordinatorBaseline(const EditorCoordinator& c,
                                      const std::string& projectName,
                                      const SceneId& activeScene,
                                      EntityId selection,
                                      float leftPanel,
                                      std::size_t undoSize,
                                      uint64_t revision,
                                      uint64_t savedRevision,
                                      bool dirty) {
    CHECK(c.document().data().projectName == projectName);
    CHECK(c.state().activeSceneId == activeScene);
    CHECK(c.selection().primaryEntity == selection);
    CHECK(c.uiState().leftPanelWidth == leftPanel);
    CHECK(c.undoSize() == undoSize);
    CHECK(c.document().revision() == revision);
    CHECK(c.document().savedRevision() == savedRevision);
    CHECK(c.document().isDirty() == dirty);
}

void sliceTilesOne(EditorCoordinator& c) {
    TilesetSlicing slicing;
    slicing.tileWidth = 32;
    slicing.tileHeight = 32;
    const std::vector<TileDefinition> tiles = tilesForSlicing(64, 64, slicing);   // "tile-1".."tile-4"
    CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-1", slicing, tiles}).ok);
}

void setUpTilemapForPainting(EditorCoordinator& c) {
    sliceTilesOne(c);
    TilemapComponent tm;
    tm.tilesetAssetId = "tiles-1";
    tm.cellSize = {32.f, 32.f};
    tm.chunkSize = 16;
    CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
    // Real production paint strokes always target coordinator.selection()
    // (see routeViewportTilemapPaint), never an arbitrary entityId - select
    // kHero here so EditorCoordinator::reconcileTilemapEditingContext() (run
    // after every Command) sees a selection that supports the tilemap tool,
    // matching how every test in this suite actually paints kHero.
    CHECK(c.apply(SelectEntityIntent{kHero}).ok);
}

int reportAndExit(const char* suiteName) {
    std::cout << suiteName << ": " << g_passed << " passed, "
              << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}

} // namespace ArtCade::EditorNative::CoreTest
