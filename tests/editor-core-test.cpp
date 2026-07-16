// editor-core-test.cpp — architectural guarantees of the native editor core.
//
// Each CHECK maps to a numbered requirement in the refactor prompt (§24). The
// core has no Raylib / RmlUi dependency, so these run in the plain CTest harness
// with no GL context and no stubs.

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/hierarchy_actions.h"
#include "editor-native/app/input_routing.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/app/new_project_transaction.h"
#include "editor-native/app/asset_import.h"
#include "editor-native/app/project_file.h"
#include "editor-native/app/project_load.h"
#include "editor-native/app/project_script_file_service.h"
#include "editor-native/app/script_syntax_validator.h"
#include "editor-native/app/script_asset_workflow.h"
#include "editor-native/app/unsaved_guard.h"
#include "editor-native/app/pending_edit.h"
#include "editor-native/app/inspector_actions.h"
#include "editor-native/commands/box_collider_commands.h"
#include "editor-native/commands/linear_mover_commands.h"
#include "editor-native/commands/top_down_controller_commands.h"
#include "editor-native/commands/platformer_controller_commands.h"
#include "editor-native/commands/project_commands.h"
#include "editor-native/commands/scene_layer_commands.h"
#include "editor-native/commands/image_asset_commands.h"
#include "editor-native/commands/audio_asset_commands.h"
#include "editor-native/commands/generated_sfx_commands.h"
#include "editor-native/commands/generated_sfx_macros.h"
#include "editor-native/commands/font_asset_commands.h"
#include "editor-native/commands/script_asset_commands.h"
#include "editor-native/commands/script_attachment_commands.h"
#include "editor-native/commands/tileset_commands.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/commands/sprite_animation_commands.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/scene_commands.h"
#include "editor-native/commands/sprite_presentation_commands.h"
#include "editor-native/model/project_io.h"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/script_source_stamp.h"
#include "editor-native/model/box_collider_view.h"
#include "editor-native/model/box_collider_geometry.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_animation_slicing.h"
#include "editor-native/model/tileset_slicing.h"
#include "editor-native/model/tilemap_chunk_math.h"
#include "editor-native/model/tilemap_cell_access.h"
#include "editor-native/model/tilemap_stroke_math.h"
#include "editor-native/model/tilemap_region_math.h"
#include "editor-native/model/tilemap_validation.h"
#include "editor-native/model/tilemap_render_view.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/scene_view_camera.h"
#include "script-runtime.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include "artcade/sfx/presets.hpp"
#include "artcade/sfx/synthesizer.hpp"

#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ArtCade;
using namespace ArtCade::EditorNative;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) ++g_passed;                                                  \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

// ---------------------------------------------------------------------------
// A small two-scene project with one placed instance in the first scene.
// ---------------------------------------------------------------------------
static constexpr EntityId kHero = 42;
static const SceneId kSceneA = "scene-a";
static const SceneId kSceneB = "scene-b";

static ProjectDoc makeDoc() {
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

static ProjectDoc makeReplacementDoc() {
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
static ProjectDoc makeSpriteDoc() {
    ProjectDoc doc = makeDoc();
    ImageAssetDef hero;  hero.assetId = "img-hero";  hero.sourcePath = "sprites/hero.ppm";  doc.imageAssets.push_back(hero);
    ImageAssetDef alt;   alt.assetId  = "img-alt";   alt.sourcePath = "sprites/alt.ppm";   doc.imageAssets.push_back(alt);
    TilesetAsset tiles;  tiles.assetId = "tiles-1";  tiles.imageAssetId = "img-hero";  doc.tilesets.push_back(tiles); // not an image
    return doc;
}

// makeSpriteDoc plus an object type "Hero" whose sprite carries an image, so the
// kHero instance (objectTypeId "Hero") inherits a sprite when it has no override.
static ProjectDoc makeInheritedDoc() {
    ProjectDoc doc = makeSpriteDoc();
    EntityDef hero;
    hero.className = "Hero";
    hero.name = "Hero";
    hero.visible = true;
    hero.spriteRenderer = SpriteRendererComponent{"img-hero", {}, true};
    doc.objectTypes.emplace("Hero", hero);
    return doc;
}

static ProjectDoc makeAnimationDoc() {
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

static ProjectDoc makeInvalidStartDoc() {
    ProjectDoc doc = makeReplacementDoc();
    doc.activeSceneId = "missing-start-scene";
    return doc;
}

// makeInheritedDoc plus an authored LinearMover on the "Hero" object type, so the
// kHero instance drifts during Play. Direction is deliberately non-unit (3,0) to
// exercise normalization.
static ProjectDoc makeMoverDoc() {
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
static ProjectDoc makeTopDownDoc(float speed = 100.f) {
    ProjectDoc doc = makeInheritedDoc();
    TopDownControllerComponent tdc;
    tdc.maxSpeed = speed;
    doc.objectTypes.at("Hero").topDownController = tdc;
    return doc;
}

static ProjectDoc makeEmptyDoc() {
    ProjectDoc doc;
    doc.projectName = "empty";
    doc.activeSceneId = "missing";
    return doc;
}

static std::string validProjectJson() {
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

static std::string danglingStartJson() {
    return R"json({
  "formatVersion": 1,
  "projectName": "Dangling",
  "activeSceneId": "missing",
  "scenes": [
    { "id": "real-scene", "name": "Real Scene" }
  ]
})json";
}

static std::string unsupportedVersionJson() {
    return R"json({
  "formatVersion": 99,
  "projectName": "Future",
  "activeSceneId": "future-scene",
  "scenes": [
    { "id": "future-scene", "name": "Future Scene" }
  ]
})json";
}

static std::string zeroSceneJson() {
    return R"json({
  "formatVersion": 1,
  "projectName": "No Scenes",
  "activeSceneId": "missing",
  "scenes": []
})json";
}

static std::string duplicateSceneJson() {
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

static std::filesystem::path testTempDir() {
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "artcade-editor-core-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    return dir;
}

static void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

static std::string readTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

static bool hasTempSibling(const std::filesystem::path& destination) {
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

static void expectCoordinatorBaseline(const EditorCoordinator& c,
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

static void runSpriteAnimationTests() {
    // -- Sprite Animation Editor model: asset owns clips and serializes -------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.execute(AddSpriteAnimationAssetCommand{"hero.anim", "hero.anim"}).ok);
        CHECK(c.document().hasSpriteAnimationAsset("hero.anim"));
        CHECK(c.execute(AddAnimationClipCommand{"hero.anim", "idle", "Idle", "img-hero"}).ok);
        std::vector<SpriteAnimationFrameDef> frames{
            SpriteAnimationFrameDef{0, 0, 32, 32},
            SpriteAnimationFrameDef{32, 0, 32, 32},
        };
        CHECK(c.execute(SetAnimationClipFramesCommand{"hero.anim", "idle", frames}).ok);
        CHECK(c.execute(SetAnimationClipFrameRateCommand{"hero.anim", "idle", 12.f}).ok);
        CHECK(c.execute(SetAnimationClipPlaybackModeCommand{
            "hero.anim", "idle", AnimationPlaybackMode::Once}).ok);

        const auto saved = ProjectSerializer::serialize(c.document());
        CHECK(saved.ok);
        const auto loaded = ProjectSerializer::deserialize(saved.value);
        CHECK(loaded.ok);
        const auto validated = ProjectValidator::validate(std::move(loaded.value));
        CHECK(validated.ok);
        const SpriteAnimationAssetDef* asset =
            validated.value.findSpriteAnimationAsset("hero.anim");
        CHECK(asset != nullptr);
        CHECK(asset && asset->clips.size() == 1);
        CHECK(asset && asset->clips[0].imageId == "img-hero");
        CHECK(asset && asset->defaultClipId == "idle");
        CHECK(asset && asset->clips[0].frames.size() == 2);
        CHECK(asset && asset->clips[0].framesPerSecond == 12.f);
        CHECK(asset && asset->clips[0].playbackMode == AnimationPlaybackMode::Once);
    }

    // -- Create asset + initial clip is one atomic, undoable operation --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        const std::size_t historyBefore = c.undoSize();
        CHECK(c.execute(CreateSpriteAnimationAssetCommand{
            "hero.anim", "Hero", "clip-1", "Clip 1", "img-hero"}).ok);
        const SpriteAnimationAssetDef* created =
            c.document().findSpriteAnimationAsset("hero.anim");
        CHECK(created != nullptr);
        CHECK(created && created->clips.size() == 1);
        CHECK(created && created->defaultClipId == "clip-1");
        CHECK(c.undoSize() == historyBefore + 1);
        CHECK(c.undo().ok);
        CHECK(!c.document().hasSpriteAnimationAsset("hero.anim"));
        CHECK(c.redo().ok);
        CHECK(c.document().hasSpriteAnimationAsset("hero.anim"));

        const uint64_t revision = c.document().revision();
        const std::size_t history = c.undoSize();
        CHECK(!c.execute(CreateSpriteAnimationAssetCommand{
            "broken.anim", "Broken", "clip-1", "Clip 1", "missing-image"}).ok);
        CHECK(!c.document().hasSpriteAnimationAsset("broken.anim"));
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == history);
    }

    // -- Sprite sheet slicing: frame size + margin + spacing -----------------
    {
        const SpriteAnimationSliceGrid grid{16, 24, 2, 1};
        const std::optional<SpriteAnimationFrameDef> first =
            spriteAnimationFrameForCell(38, 53, grid, 0);
        const std::optional<SpriteAnimationFrameDef> second =
            spriteAnimationFrameForCell(38, 53, grid, 1);
        const std::optional<SpriteAnimationFrameDef> last =
            spriteAnimationFrameForCell(38, 53, grid, 3);
        CHECK(spriteAnimationSliceCellCount(38, 53, grid) == 4);
        CHECK(first.has_value());
        CHECK(first && first->x == 2 && first->y == 2
              && first->width == 16 && first->height == 24);
        CHECK(second.has_value());
        CHECK(second && second->x == 19 && second->y == 2);
        CHECK(last.has_value());
        CHECK(last && last->x == 19 && last->y == 27);
        CHECK(!spriteAnimationFrameForCell(38, 53, grid, 4).has_value());

        // Frame-count driven: 4 columns x 2 rows of a 64x32 sheet -> 8 cells of
        // 16x16 in reading order. The cell size is derived from the dimensions.
        const std::optional<SpriteAnimationSliceGrid> atlas =
            spriteAnimationGridFromCellCounts(64, 32, 4, 2, 0, 0);
        CHECK(atlas.has_value());
        CHECK(atlas && atlas->frameWidth == 16 && atlas->frameHeight == 16);
        const std::vector<SpriteAnimationFrameDef> frames =
            atlas ? spriteAnimationFramesForGrid(64, 32, *atlas)
                  : std::vector<SpriteAnimationFrameDef>{};
        CHECK(frames.size() == 8);
        CHECK(frames.size() > 4 && frames[0].x == 0 && frames[0].y == 0);
        CHECK(frames.size() > 4 && frames[3].x == 48 && frames[3].y == 0);
        CHECK(frames.size() > 4 && frames[4].x == 0 && frames[4].y == 16);

        // The user's core case: a 64x16 strip divided into 4 frames -> 16px each.
        const std::optional<SpriteAnimationSliceGrid> strip =
            spriteAnimationGridFromCellCounts(64, 16, 4, 1, 0, 0);
        CHECK(strip.has_value());
        CHECK(strip && strip->frameWidth == 16 && strip->frameHeight == 16);
        // Non-divisible: floors the cell rather than rejecting (no dead end).
        const std::optional<SpriteAnimationSliceGrid> odd =
            spriteAnimationGridFromCellCounts(65, 16, 4, 1, 0, 0);
        CHECK(odd.has_value());
        CHECK(odd && odd->frameWidth == 16);
        // Rejected only when a cell would be sub-pixel.
        CHECK(!spriteAnimationGridFromCellCounts(3, 16, 4, 1, 0, 0).has_value());
    }

    // -- Animation slicing setup is workspace-only --------------------------
    {
        EditorCoordinator c{makeAnimationDoc()};
        const uint64_t revision = c.document().revision();
        CHECK(c.apply(OpenSpriteAnimationEditorIntent{"hero.anim"}).ok);
        CHECK(c.apply(SetAnimationSliceGridIntent{6, 2, 4, 2}).ok);
        CHECK(c.state().spriteAnimationEditor.sliceColumns == 6);
        CHECK(c.state().spriteAnimationEditor.sliceRows == 2);
        CHECK(c.state().spriteAnimationEditor.sliceMargin == 4);
        CHECK(c.state().spriteAnimationEditor.sliceSpacing == 2);
        CHECK(c.document().revision() == revision);
        CHECK(c.apply(SetAnimationSliceGridIntent{-5, 0, -1, -2}).ok);
        CHECK(c.state().spriteAnimationEditor.sliceColumns == 1);   // clamped >= 1
        CHECK(c.state().spriteAnimationEditor.sliceRows == 1);
        CHECK(c.state().spriteAnimationEditor.sliceMargin == 0);
        CHECK(c.state().spriteAnimationEditor.sliceSpacing == 0);
        CHECK(c.document().revision() == revision);
    }

    // -- Sheet zoom/pan and clip preview are workspace-only ------------------
    {
        EditorCoordinator c{makeAnimationDoc()};
        // A closed editor rejects navigation/preview intents.
        CHECK(!c.apply(SetSpriteSheetZoomIntent{2.f}).ok);
        CHECK(!c.apply(PanSpriteSheetIntent{{4.f, 4.f}}).ok);
        CHECK(!c.apply(SetAnimationPreviewPlayingIntent{true}).ok);

        CHECK(c.apply(OpenSpriteAnimationEditorIntent{"hero.anim"}).ok);
        const uint64_t revision = c.document().revision();
        CHECK(c.apply(SetSpriteSheetZoomIntent{100.f}).ok);   // clamped to max
        CHECK(c.state().spriteAnimationEditor.sheetZoom == 16.f);
        CHECK(c.apply(SetSpriteSheetZoomIntent{0.01f}).ok);   // clamped to min
        CHECK(c.state().spriteAnimationEditor.sheetZoom == 0.25f);
        CHECK(c.apply(PanSpriteSheetIntent{{10.f, -4.f}}).ok);
        CHECK(c.apply(PanSpriteSheetIntent{{-2.f, 1.f}}).ok);
        CHECK(c.state().spriteAnimationEditor.sheetPan.x == 8.f);
        CHECK(c.state().spriteAnimationEditor.sheetPan.y == -3.f);

        // Loop preview wraps; a paused preview holds; the document never moves.
        CHECK(c.apply(SetAnimationPreviewPlayingIntent{true}).ok);
        c.advanceSpriteAnimationPreview(0.125f);   // exactly one 8 fps frame
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 1);
        c.advanceSpriteAnimationPreview(0.f);      // dt 0 advances nothing
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 1);
        CHECK(c.state().spriteAnimationEditor.previewPlaying);
        c.advanceSpriteAnimationPreview(0.125f);
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 0);   // wrapped
        CHECK(c.apply(SetAnimationPreviewPlayingIntent{false}).ok);
        c.advanceSpriteAnimationPreview(1.f);
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 0);   // paused
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == 0);
    }

    // -- Once preview stops holding the last frame; Play restarts it ---------
    {
        EditorCoordinator c{makeAnimationDoc()};
        CHECK(c.execute(SetAnimationClipPlaybackModeCommand{
            "hero.anim", "idle", AnimationPlaybackMode::Once}).ok);
        CHECK(c.apply(OpenSpriteAnimationEditorIntent{"hero.anim"}).ok);
        CHECK(c.apply(SetAnimationPreviewPlayingIntent{true}).ok);
        c.advanceSpriteAnimationPreview(10.f);
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 1);   // held
        CHECK(!c.state().spriteAnimationEditor.previewPlaying);
        CHECK(c.apply(SetAnimationPreviewPlayingIntent{true}).ok);
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 0);   // restarted
        CHECK(c.state().spriteAnimationEditor.previewPlaying);
    }

    // -- Opening the editor adopts the selected clip's frame count -----------
    {
        EditorCoordinator c{makeAnimationDoc()};   // "idle" clip has 2 frames
        CHECK(c.apply(OpenSpriteAnimationEditorIntent{"hero.anim"}).ok);
        CHECK(c.apply(SetAnimationSliceGridIntent{6, 3, 0, 0}).ok);
        CHECK(c.apply(OpenSpriteAnimationEditorIntent{"hero.anim"}).ok);   // reopen
        CHECK(c.state().spriteAnimationEditor.sliceColumns == 2);   // from clip
        CHECK(c.state().spriteAnimationEditor.sliceRows == 1);
    }

    // -- Preview scrub/step pause playback and stay workspace-only -----------
    {
        EditorCoordinator c{makeAnimationDoc()};
        CHECK(!c.apply(SetAnimationPreviewFrameIntent{0}).ok);   // closed editor
        CHECK(!c.apply(StepAnimationPreviewIntent{1}).ok);

        CHECK(c.apply(OpenSpriteAnimationEditorIntent{"hero.anim"}).ok);
        const uint64_t revision = c.document().revision();
        CHECK(c.apply(SetAnimationPreviewPlayingIntent{true}).ok);
        CHECK(c.apply(SetAnimationPreviewFrameIntent{1}).ok);    // scrub pauses
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 1);
        CHECK(!c.state().spriteAnimationEditor.previewPlaying);
        CHECK(c.apply(SetAnimationPreviewFrameIntent{99}).ok);   // clamped to last
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 1);
        CHECK(c.apply(StepAnimationPreviewIntent{1}).ok);        // wraps forward
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 0);
        CHECK(c.apply(StepAnimationPreviewIntent{-1}).ok);       // wraps backward
        CHECK(c.state().spriteAnimationEditor.previewFrameIndex == 1);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == 0);
    }

    // -- Sprite Renderer animation source requires SpriteAnimator ------------
    {
        ProjectDoc doc = makeAnimationDoc();
        doc.objectTypes.at("Hero").spriteRenderer = SpriteRendererComponent{};
        doc.objectTypes.at("Hero").spriteAnimator.reset();
        EditorCoordinator c{std::move(doc)};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(c.execute(SetObjectTypeSpriteSourceCommand{
            "Hero", ObjectTypeSpriteSourceKind::Animation, "hero.anim"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);  // one logical mutation
        const EntityDef& type = c.document().data().objectTypes.at("Hero");
        CHECK(type.spriteRenderer && type.spriteRenderer->imageAssetId.empty());
        CHECK(type.spriteRenderer && type.spriteRenderer->animationAssetId == "hero.anim");
        CHECK(type.spriteAnimator && type.spriteAnimator->initialClipId == "idle");

        const SceneFrameSnapshot frame =
            collectSceneFrameSnapshot(c.document(), kSceneA, kHero);
        CHECK(frame.sprites.size() == 1);
        CHECK(frame.sprites[0].assetId == "img-hero");
        CHECK(frame.sprites[0].hasSource);
        CHECK(frame.sprites[0].source.width == 32.f);

        CHECK(c.undo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->animationAssetId.empty());
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);

        CHECK(c.redo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->animationAssetId
              == "hero.anim");
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->initialClipId == "idle");
    }

    // -- Animation -> image is one atomic renderer+animator state change -----
    {
        EditorCoordinator c{makeAnimationDoc()};
        const uint64_t imageRevisionBefore = c.document().revision();
        CHECK(c.execute(SetObjectTypeSpriteSourceCommand{
            "Hero", ObjectTypeSpriteSourceKind::Image, "img-hero"}).ok);
        CHECK(c.document().revision() == imageRevisionBefore + 1);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->imageAssetId == "img-hero");
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->animationAssetId.empty());
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);
        CHECK(c.undo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->animationAssetId
              == "hero.anim");
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->initialClipId == "idle");
        CHECK(c.redo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->imageAssetId == "img-hero");
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);
    }

    // -- Removing an animation asset clears every reference atomically --------
    {
        ProjectDoc doc = makeAnimationDoc();
        SceneInstanceDef second = doc.scenes.at(kSceneA).instances.front();
        second.id = 77;
        second.instanceName = "Hero 2";
        doc.scenes.at(kSceneA).instances.push_back(second);
        EditorCoordinator c{std::move(doc)};
        CHECK(resolveSpriteRenderer(c.document(), kSceneA, kHero).animationAssetId == "hero.anim");
        const uint64_t revisionBefore = c.document().revision();
        // Delete means delete: the asset goes AND the entity is left clean, not
        // blocked and not dangling.
        CHECK(c.execute(RemoveSpriteAnimationAssetCommand{"hero.anim"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);  // one staged commit
        CHECK(!c.document().hasSpriteAnimationAsset("hero.anim"));
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->animationAssetId.empty());
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);
        // Undo restores the asset, the source, and the animator (with its clip).
        CHECK(c.undo().ok);
        CHECK(c.document().hasSpriteAnimationAsset("hero.anim"));
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->animationAssetId
              == "hero.anim");
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->initialClipId == "idle");
        CHECK(c.redo().ok);
        CHECK(!c.document().hasSpriteAnimationAsset("hero.anim"));
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->animationAssetId.empty());
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);
    }

    // -- Slicing a second animation must not overwrite the first's clip -------
    {
        EditorCoordinator c{makeSpriteDoc()};   // img-hero + img-alt
        // Animation 1 on img-hero, mirroring the UI Import Sheet -> Slice flow.
        CHECK(c.execute(AddSpriteAnimationAssetCommand{"a1.anim", "a1"}).ok);
        c.apply(OpenSpriteAnimationEditorIntent{"a1.anim"});
        CHECK(!c.state().spriteAnimationEditor.selectedClipId.has_value());
        CHECK(c.execute(AddAnimationClipCommand{"a1.anim", "clip-1", "Clip 1", "img-hero"}).ok);
        c.apply(SelectAnimationClipIntent{"a1.anim", "clip-1"});
        CHECK(c.execute(SetAnimationClipFramesCommand{"a1.anim", "clip-1",
            {SpriteAnimationFrameDef{0, 0, 16, 16}, SpriteAnimationFrameDef{16, 0, 16, 16},
             SpriteAnimationFrameDef{32, 0, 16, 16}}}).ok);
        CHECK(c.document().findSpriteAnimationAsset("a1.anim")->clips[0].frames.size() == 3);

        // Animation 2 on img-alt: its auto-clip also gets id "clip-1".
        CHECK(c.execute(AddSpriteAnimationAssetCommand{"a2.anim", "a2"}).ok);
        c.apply(OpenSpriteAnimationEditorIntent{"a2.anim"});
        CHECK(!c.state().spriteAnimationEditor.selectedClipId.has_value());   // reset on open
        CHECK(c.execute(AddAnimationClipCommand{"a2.anim", "clip-1", "Clip 1", "img-alt"}).ok);
        c.apply(SelectAnimationClipIntent{"a2.anim", "clip-1"});
        CHECK(c.execute(SetAnimationClipFramesCommand{"a2.anim", "clip-1",
            {SpriteAnimationFrameDef{0, 0, 16, 16}, SpriteAnimationFrameDef{16, 0, 16, 16}}}).ok);

        // Neither asset's clip is overwritten - each keeps its own frames.
        const SpriteAnimationAssetDef* a1 = c.document().findSpriteAnimationAsset("a1.anim");
        const SpriteAnimationAssetDef* a2 = c.document().findSpriteAnimationAsset("a2.anim");
        CHECK(a1 && a1->clips.size() == 1 && a1->clips[0].frames.size() == 3);
        CHECK(a2 && a2->clips.size() == 1 && a2->clips[0].frames.size() == 2);
    }

    // -- PlaySession owns per-instance playheads, document stays untouched ----
    {
        ProjectDoc doc = makeAnimationDoc();
        SceneInstanceDef second = doc.scenes.at(kSceneA).instances.front();
        second.id = 77;
        second.instanceName = "Hero 2";
        second.transform.position.x += 80.f;
        doc.scenes.at(kSceneA).instances.push_back(second);
        EditorCoordinator c{std::move(doc)};
        const uint64_t revision = c.document().revision();
        CHECK(c.playCurrentScene().ok);
        CHECK(c.playSession() != nullptr);
        c.advanceRuntime(1.f / 8.f);
        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        CHECK(snap.sprites.size() == 2);
        CHECK(snap.sprites[0].hasSource);
        CHECK(snap.sprites[1].hasSource);
        CHECK(snap.sprites[0].source.x == 32.f);
        CHECK(snap.sprites[1].source.x == 32.f);
        CHECK(c.document().revision() == revision);
        CHECK(c.stopPlaying().ok);
        CHECK(c.document().revision() == revision);
    }
}

static void runTilesetTests() {
    // -- computeTilesetSlicing: a perfectly divisible sheet --------------------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const TilesetSliceResult r = computeTilesetSlicing(128, 64, slicing);
        CHECK(r.columns == 4);
        CHECK(r.rows == 2);
        CHECK(r.tileCount == 8);
        CHECK(r.remainderX == 0);
        CHECK(r.remainderY == 0);
    }

    // -- computeTilesetSlicing: a non-divisible sheet leaves a remainder -------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const TilesetSliceResult r = computeTilesetSlicing(100, 70, slicing);
        CHECK(r.columns == 3);   // floors: 100/32 -> 3, 4px left over
        CHECK(r.rows == 2);      // 70/32 -> 2, 6px left over
        CHECK(r.tileCount == 6);
        CHECK(r.remainderX == 4);
        CHECK(r.remainderY == 6);
    }

    // -- computeTilesetSlicing: margin trims usable area on both edges --------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        slicing.marginX = 8;
        slicing.marginY = 4;
        const TilesetSliceResult r = computeTilesetSlicing(80, 40, slicing);
        // usable = 80 - 16 = 64 -> 2 cols; 40 - 8 = 32 -> 1 row
        CHECK(r.columns == 2);
        CHECK(r.rows == 1);
        CHECK(r.tileCount == 2);
    }

    // -- computeTilesetSlicing: spacing between tiles reduces how many fit -----
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        slicing.spacingX = 2;
        slicing.spacingY = 2;
        const TilesetSliceResult r = computeTilesetSlicing(100, 66, slicing);
        // step = 34; 1 + (100-32)/34 = 3 cols; 1 + (66-32)/34 = 2 rows
        CHECK(r.columns == 3);
        CHECK(r.rows == 2);
    }

    // -- computeTilesetSlicing: rectangular (non-square) tiles -----------------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 16;
        slicing.tileHeight = 48;
        const TilesetSliceResult r = computeTilesetSlicing(64, 96, slicing);
        CHECK(r.columns == 4);
        CHECK(r.rows == 2);
        CHECK(r.tileCount == 8);
    }

    // -- computeTilesetSlicing: degenerate/invalid dimensions -> zero tiles ----
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(computeTilesetSlicing(0, 64, slicing).tileCount == 0);
        CHECK(computeTilesetSlicing(128, 64, TilesetSlicing{0, 32, 0, 0, 0, 0}).tileCount == 0);
        CHECK(computeTilesetSlicing(16, 16, slicing).tileCount == 0);  // smaller than one tile
    }

    // -- tilesForSlicing: sequential ids, correct rects, row-major order -------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const std::vector<TileDefinition> tiles = tilesForSlicing(64, 64, slicing);
        CHECK(tiles.size() == 4);
        CHECK(tiles[0].id == "tile-1");
        CHECK(tiles[0].x == 0);
        CHECK(tiles[0].y == 0);
        CHECK(tiles[1].id == "tile-2");
        CHECK(tiles[1].x == 32);
        CHECK(tiles[1].y == 0);
        CHECK(tiles[2].id == "tile-3");
        CHECK(tiles[2].x == 0);
        CHECK(tiles[2].y == 32);
        CHECK(tiles[3].width == 32);
        CHECK(tiles[3].height == 32);
    }

    // -- tilesForSlicing: zero tiles for a sheet smaller than one tile ---------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(tilesForSlicing(10, 10, slicing).empty());
    }

    // -- reconcileTiles: a matching rect keeps its old stable id ---------------
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"wall-corner", 0, 0, 32, 32});
        oldTiles.push_back(TileDefinition{"wall-flat", 32, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});    // same rect as wall-corner
        newTiles.push_back(TileDefinition{"tile-2", 32, 0, 32, 32});   // same rect as wall-flat

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 2);
        CHECK(result[0].id == "wall-corner");
        CHECK(result[1].id == "wall-flat");
    }

    // -- reconcileTiles: a genuinely new rect gets a fresh, non-colliding id ---
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"wall-corner", 0, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});     // matches wall-corner
        newTiles.push_back(TileDefinition{"tile-2", 32, 0, 32, 32});    // new rect

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 2);
        CHECK(result[0].id == "wall-corner");
        CHECK(result[1].id != "wall-corner");
        CHECK(!result[1].id.empty());
    }

    // -- reconcileTiles: an old tile with no matching rect is dropped ----------
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"wall-corner", 0, 0, 32, 32});
        oldTiles.push_back(TileDefinition{"wall-flat", 32, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});   // only wall-corner survives

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 1);
        CHECK(result[0].id == "wall-corner");
    }

    // -- reconcileTiles: a fresh id never collides with a kept old id ----------
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"tile-1", 100, 100, 32, 32});   // old id happens to look generated

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"x", 100, 100, 32, 32});   // matches -> keeps "tile-1"
        newTiles.push_back(TileDefinition{"y", 200, 200, 32, 32});   // new -> must not become "tile-1" too

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 2);
        CHECK(result[0].id == "tile-1");
        CHECK(result[1].id != "tile-1");
    }

    // -- reconcileTiles: a removed tile's id is never recycled by a new rect -
    // a painted cell still referencing it must become an orphan (detected and
    // cleared by the re-slice cascade), never silently show the new content --
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"n", 0, 0, 64, 64});   // no rect match

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 1);
        CHECK(result[0].id != "tile-1");
    }

    // -- reconcileTiles: a fresh rect listed before a kept rect must not take
    // the id the kept rect is about to reuse ------------------------------------
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"tile-1", 32, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"a", 0, 0, 32, 32});    // fresh, listed first
        newTiles.push_back(TileDefinition{"b", 32, 0, 32, 32});   // keeps "tile-1"

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 2);
        CHECK(result[1].id == "tile-1");
        CHECK(result[0].id != result[1].id);
    }

    // -- AddTilesetAssetCommand: success, then duplicate/unknown-image/invalid-
    // slicing all fail without mutation, then undo/redo -------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};   // has image asset "img-hero"
        TilesetSlicing slicing;
        slicing.tileWidth = 16;
        slicing.tileHeight = 16;
        const EditorOperationResult r =
            c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero", slicing});
        CHECK(r.ok);
        CHECK(c.document().hasTilesetAsset("tiles-a"));
        const TilesetAsset* asset = c.document().findTilesetAsset("tiles-a");
        CHECK(asset->name == "Dungeon");
        CHECK(asset->imageAssetId == "img-hero");
        CHECK(asset->slicing.tileWidth == 16);
        CHECK(asset->tiles.empty());   // not sliced yet

        CHECK(!c.execute(AddTilesetAssetCommand{"tiles-a", "Dup", "img-hero", slicing}).ok);
        CHECK(!c.execute(AddTilesetAssetCommand{"tiles-b", "X", "no-such-image", slicing}).ok);
        TilesetSlicing bad; bad.tileWidth = 0; bad.tileHeight = 16;
        CHECK(!c.execute(AddTilesetAssetCommand{"tiles-c", "X", "img-hero", bad}).ok);
        CHECK(!c.document().hasTilesetAsset("tiles-b"));
        CHECK(!c.document().hasTilesetAsset("tiles-c"));

        CHECK(c.undo().ok);
        CHECK(!c.document().hasTilesetAsset("tiles-a"));
        CHECK(c.redo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->name == "Dungeon");
    }

    // -- RemoveTilesetAssetCommand: success, unknown-id rejection, undo restores
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero", slicing}).ok);
        CHECK(!c.execute(RemoveTilesetAssetCommand{"nope"}).ok);
        CHECK(c.execute(RemoveTilesetAssetCommand{"tiles-a"}).ok);
        CHECK(!c.document().hasTilesetAsset("tiles-a"));
        CHECK(c.undo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->imageAssetId == "img-hero");
    }

    // -- RenameTilesetCommand: mirrors RenameObjectTypeCommand's guard shape ---
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero", slicing}).ok);
        CHECK(!c.execute(RenameTilesetCommand{"tiles-a", ""}).ok);          // empty name
        CHECK(!c.execute(RenameTilesetCommand{"nope", "X"}).ok);           // unknown id
        const std::size_t before = c.undoSize();
        CHECK(c.execute(RenameTilesetCommand{"tiles-a", "Dungeon"}).ok);   // same name -> no-op
        CHECK(c.undoSize() == before);
        CHECK(c.execute(RenameTilesetCommand{"tiles-a", "Caves"}).ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->name == "Caves");
        CHECK(c.undo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->name == "Dungeon");
        CHECK(c.redo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->name == "Caves");
    }

    // -- ChangeTilesetSlicingCommand: atomically swaps config + tiles (the
    // caller-supplied tiles, not computed by the command itself); undo
    // restores both the old config and the old tiles ---------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero", slicing}).ok);

        TilesetSlicing next = slicing;
        next.tileWidth = 16;
        std::vector<TileDefinition> nextTiles = tilesForSlicing(64, 64, next);
        CHECK(!nextTiles.empty());
        CHECK(!c.execute(ChangeTilesetSlicingCommand{"nope", next, nextTiles}).ok);
        TilesetSlicing invalid; invalid.tileWidth = -1; invalid.tileHeight = 16;
        CHECK(!c.execute(ChangeTilesetSlicingCommand{"tiles-a", invalid, {}}).ok);

        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-a", next, nextTiles}).ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->slicing.tileWidth == 16);
        CHECK(c.document().findTilesetAsset("tiles-a")->tiles.size() == nextTiles.size());

        CHECK(c.undo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->tiles.empty());   // restored to Add's empty tiles
        CHECK(c.document().findTilesetAsset("tiles-a")->slicing.tileWidth == 32);
    }

    // -- ChangeTilesetSlicingCommand: identical slicing + tiles is a no-op -
    // no revision, no undo entry (AC-MUT-001) ----------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const std::vector<TileDefinition> tiles = tilesForSlicing(64, 64, slicing);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero",
                                               slicing, tiles}).ok);
        const std::size_t before = c.undoSize();
        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-a", slicing, tiles}).ok);
        CHECK(c.undoSize() == before);
    }

    // -- AddTilesetAssetCommand: tiles pre-sliced at creation land on the
    // asset and survive undo/redo (create-from-image's auto-slice path) --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        const TilesetSlicing slicing;   // default 32x32
        const std::vector<TileDefinition> tiles = tilesForSlicing(64, 64, slicing);
        CHECK(tiles.size() == 4);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-b", "Autosliced", "img-hero",
                                               slicing, tiles}).ok);
        CHECK(c.document().findTilesetAsset("tiles-b")->tiles.size() == 4);
        CHECK(c.undo().ok);
        CHECK(!c.document().hasTilesetAsset("tiles-b"));
        CHECK(c.redo().ok);
        CHECK(c.document().findTilesetAsset("tiles-b")->tiles.size() == 4);
        CHECK(c.document().findTilesetAsset("tiles-b")->tiles[0].id == "tile-1");
    }

    // -- uniqueTilesetAssetId: "<image>.tileset" first, then numbered ----------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(uniqueTilesetAssetId(c.document(), "img-hero") == "img-hero.tileset");
        CHECK(c.execute(AddTilesetAssetCommand{"img-hero.tileset", "T", "img-hero",
                                               TilesetSlicing{}}).ok);
        CHECK(uniqueTilesetAssetId(c.document(), "img-hero") == "img-hero-2.tileset");
    }

    // -- adjacentTileIndex: arrow-key selection movement on the pending grid -
    // stays put at every edge, rejects invalid grids/indices ------------------
    {
        // 4 x 3 grid: indices 0..11, row-major.
        CHECK(adjacentTileIndex(4, 3, 0, 1, 0) == std::optional<int>{1});
        CHECK(adjacentTileIndex(4, 3, 0, 0, 1) == std::optional<int>{4});
        CHECK(adjacentTileIndex(4, 3, 5, -1, 0) == std::optional<int>{4});
        CHECK(adjacentTileIndex(4, 3, 5, 0, -1) == std::optional<int>{1});
        CHECK(!adjacentTileIndex(4, 3, 3, 1, 0).has_value());    // right edge
        CHECK(!adjacentTileIndex(4, 3, 0, -1, 0).has_value());   // left edge
        CHECK(!adjacentTileIndex(4, 3, 0, 0, -1).has_value());   // top edge
        CHECK(!adjacentTileIndex(4, 3, 8, 0, 1).has_value());    // bottom edge
        CHECK(!adjacentTileIndex(0, 3, 0, 1, 0).has_value());    // degenerate grid
        CHECK(!adjacentTileIndex(4, 3, 12, 1, 0).has_value());   // index out of range
        CHECK(!adjacentTileIndex(4, 3, -1, 1, 0).has_value());
    }

    // -- sameTilesetSlicing / sameTileDefinitions: the one "unchanged"
    // definition shared by the no-op guard and the close guard's dirty check ---
    {
        TilesetSlicing a;
        TilesetSlicing b;
        CHECK(sameTilesetSlicing(a, b));
        b.spacingY = 1;
        CHECK(!sameTilesetSlicing(a, b));

        std::vector<TileDefinition> ta = {TileDefinition{"t", 0, 0, 32, 32}};
        std::vector<TileDefinition> tb = ta;
        CHECK(sameTileDefinitions(ta, tb));
        tb[0].id = "u";
        CHECK(!sameTileDefinitions(ta, tb));
        tb = ta;
        tb[0].width = 16;
        CHECK(!sameTileDefinitions(ta, tb));
        tb = ta;
        tb.push_back(TileDefinition{"v", 32, 0, 32, 32});
        CHECK(!sameTileDefinitions(ta, tb));
    }

    // -- Save/reload round-trips a tileset, including a populated tiles array --
    {
        ProjectDoc doc = makeDoc();
        ImageAssetDef hero; hero.assetId = "img-hero"; hero.sourcePath = "sprites/hero.ppm";
        doc.imageAssets.push_back(hero);

        TilesetAsset asset;
        asset.assetId = "tiles-a";
        asset.name = "Dungeon";
        asset.imageAssetId = "img-hero";
        asset.slicing.tileWidth = 16;
        asset.slicing.tileHeight = 16;
        asset.slicing.marginX = 1;
        asset.slicing.spacingY = 1;
        asset.tiles = tilesForSlicing(64, 64, asset.slicing);
        CHECK(!asset.tiles.empty());
        doc.tilesets.push_back(asset);

        EditorCoordinator c{doc};
        const std::filesystem::path path = testTempDir() / "tileset.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const TilesetAsset* rt = reloaded.document().findTilesetAsset("tiles-a");
        CHECK(rt != nullptr);
        CHECK(rt->name == "Dungeon");
        CHECK(rt->imageAssetId == "img-hero");
        CHECK(rt->slicing.tileWidth == 16);
        CHECK(rt->slicing.marginX == 1);
        CHECK(rt->slicing.spacingY == 1);
        CHECK(rt->tiles.size() == asset.tiles.size());
        CHECK(rt->tiles[0].id == asset.tiles[0].id);
        CHECK(rt->tiles[0].width == 16);
    }

    // -- Validation: an unknown imageAssetId is rejected (both save and load
    // run the same ProjectValidator::validate) --------------------------------
    {
        ProjectDoc doc = makeDoc();
        TilesetAsset asset;
        asset.assetId = "tiles-a";
        asset.imageAssetId = "no-such-image";
        asset.slicing.tileWidth = 16;
        asset.slicing.tileHeight = 16;
        doc.tilesets.push_back(asset);

        EditorCoordinator c{doc};
        const std::filesystem::path path = testTempDir() / "bad-tileset.artcade-project";
        CHECK(!saveProjectToFile(c, path).ok);
    }

    // -- Open/Close Tileset Editor: open seeds pendingSlicing from the asset's
    // current slicing (workspace-only, no document mutation); close resets ----
    {
        EditorCoordinator c{makeSpriteDoc()};   // "tiles-1" -> img-hero, default 32x32
        CHECK(!c.state().tilesetEditor.openAssetId.has_value());
        CHECK(!c.apply(OpenTilesetEditorIntent{"no-such-tileset"}).ok);

        const uint64_t revision = c.document().revision();
        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.state().tilesetEditor.openAssetId.has_value());
        CHECK(*c.state().tilesetEditor.openAssetId == "tiles-1");
        CHECK(c.state().tilesetEditor.pendingSlicing.tileWidth == 32);
        CHECK(c.document().revision() == revision);   // workspace-only

        CHECK(c.apply(CloseTilesetEditorIntent{}).ok);
        CHECK(!c.state().tilesetEditor.openAssetId.has_value());
    }

    // -- SetPendingTilesetSlicingIntent: live preview, clamped, no document
    // mutation, rejected while the editor is closed ----------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing bad{-5, 0, -1, -1, -1, -1};
        CHECK(!c.apply(SetPendingTilesetSlicingIntent{bad}).ok);   // editor not open

        const uint64_t revision = c.document().revision();
        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.apply(SetPendingTilesetSlicingIntent{bad}).ok);
        CHECK(c.state().tilesetEditor.pendingSlicing.tileWidth == 1);    // clamped >= 1
        CHECK(c.state().tilesetEditor.pendingSlicing.tileHeight == 1);
        CHECK(c.state().tilesetEditor.pendingSlicing.marginX == 0);      // clamped >= 0
        CHECK(c.document().revision() == revision);

        TilesetSlicing ok{16, 16, 2, 2, 1, 1};
        CHECK(c.apply(SetPendingTilesetSlicingIntent{ok}).ok);
        CHECK(c.state().tilesetEditor.pendingSlicing.tileWidth == 16);
        CHECK(c.document().revision() == revision);
        // The asset itself is untouched until a later slice's Apply flow commits it.
        CHECK(c.document().findTilesetAsset("tiles-1")->slicing.tileWidth == 32);
    }

    // -- Tileset Editor zoom/pan: workspace-only, rejected while closed --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(!c.apply(SetTilesetEditorZoomIntent{2.f}).ok);
        CHECK(!c.apply(PanTilesetEditorIntent{{4.f, 4.f}}).ok);

        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.apply(SetTilesetEditorZoomIntent{2.f}).ok);
        CHECK(c.state().tilesetEditor.zoom == 2.f);
        CHECK(c.apply(PanTilesetEditorIntent{{4.f, 6.f}}).ok);
        CHECK(c.state().tilesetEditor.pan.x == 4.f);
        CHECK(c.state().tilesetEditor.pan.y == 6.f);
    }

    // -- Deleting the open tileset asset auto-closes the editor ----------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.state().tilesetEditor.openAssetId.has_value());
        CHECK(c.execute(RemoveTilesetAssetCommand{"tiles-1"}).ok);
        CHECK(!c.state().tilesetEditor.openAssetId.has_value());
    }
}

// Slices "tiles-1" (from makeSpriteDoc) via the real Command path so it has
// actual TileDefinitions to reference - makeSpriteDoc's own "tiles-1" is
// deliberately unsliced (Slice 1 fixture, "not an image").
static void sliceTilesOne(EditorCoordinator& c) {
    TilesetSlicing slicing;
    slicing.tileWidth = 32;
    slicing.tileHeight = 32;
    const std::vector<TileDefinition> tiles = tilesForSlicing(64, 64, slicing);   // "tile-1".."tile-4"
    CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-1", slicing, tiles}).ok);
}

static void runTilemapComponentTests() {
    // -- floorDivChunk/floorModChunk: positive coordinates ---------------------
    {
        CHECK(floorDivChunk(0, 16) == 0);
        CHECK(floorDivChunk(15, 16) == 0);
        CHECK(floorDivChunk(16, 16) == 1);
        CHECK(floorDivChunk(31, 16) == 1);
        CHECK(floorModChunk(0, 16) == 0);
        CHECK(floorModChunk(15, 16) == 15);
        CHECK(floorModChunk(16, 16) == 0);
    }

    // -- floorDivChunk/floorModChunk: negative coordinates - the exact
    // truncating-division gotcha this module exists to avoid ------------------
    {
        CHECK(floorDivChunk(-1, 16) == -1);    // naive -1/16 == 0 would be wrong
        CHECK(floorDivChunk(-16, 16) == -1);
        CHECK(floorDivChunk(-17, 16) == -2);
        CHECK(floorModChunk(-1, 16) == 15);    // naive -1%16 == -1 would be wrong
        CHECK(floorModChunk(-16, 16) == 0);
        CHECK(floorModChunk(-17, 16) == 15);
    }

    // -- cellToChunkCoord/cellToLocalCoord: combined, mixed-sign case ----------
    {
        const TilemapChunkCoord chunk = cellToChunkCoord(-1, 20, 16);
        CHECK(chunk.chunkX == -1);
        CHECK(chunk.chunkY == 1);
        const TilemapLocalCoord local = cellToLocalCoord(-1, 20, 16);
        CHECK(local.localX == 15);
        CHECK(local.localY == 4);
    }

    // -- chunkAndLocalToCellX/Y: exact inverse round-trip, including negatives -
    {
        const int xs[] = {-33, -17, -16, -1, 0, 1, 15, 16, 31, 100};
        const int ys[] = {-40, -16, -1, 0, 7, 16, 63};
        for (const int x : xs) {
            for (const int y : ys) {
                const TilemapChunkCoord chunk = cellToChunkCoord(x, y, 16);
                const TilemapLocalCoord local = cellToLocalCoord(x, y, 16);
                CHECK(chunkAndLocalToCellX(chunk.chunkX, local.localX, 16) == x);
                CHECK(chunkAndLocalToCellY(chunk.chunkY, local.localY, 16) == y);
            }
        }
    }

    // -- tilemapRenderCells: multi-chunk, negative chunk coords, correct
    // destination (origin + cell*cellSize) and source (the tile's own rect) -
    {
        TilesetAsset tileset;
        tileset.assetId = "tiles-1";
        tileset.tiles.push_back(TileDefinition{"grass", 0, 0, 16, 16});
        tileset.tiles.push_back(TileDefinition{"stone", 16, 0, 16, 16});

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        tm.chunkSize = 2;

        TilemapChunk chunkA;   // chunk (0,0): cells (0,0)=grass, (1,0)=empty
        chunkA.chunkX = 0;
        chunkA.chunkY = 0;
        chunkA.cells = {TilemapCellValue{"grass", TileTransformFlags::None},
                       std::nullopt, std::nullopt, std::nullopt};
        TilemapChunk chunkB;   // chunk (-1,-1): local (1,1) -> absolute cell (-1,-1)=stone
        chunkB.chunkX = -1;
        chunkB.chunkY = -1;
        chunkB.cells = {std::nullopt, std::nullopt, std::nullopt,
                       TilemapCellValue{"stone", TileTransformFlags::FlipY}};
        tm.chunks.push_back(chunkA);
        tm.chunks.push_back(chunkB);

        const std::vector<SceneFrameTilemapCell> cells =
            tilemapRenderCells(tm, tileset, Vec2{100.f, 200.f});
        CHECK(cells.size() == 2);

        CHECK(cells[0].destination.x == 100.f);         // origin + cell(0,0)*32
        CHECK(cells[0].destination.y == 200.f);
        CHECK(cells[0].destination.width == 32.f);
        CHECK(cells[0].source.x == 0.f);                 // "grass" tile rect
        CHECK(cells[0].source.width == 16.f);

        CHECK(cells[1].destination.x == 100.f - 32.f);   // origin + cell(-1,-1)*32
        CHECK(cells[1].destination.y == 200.f - 32.f);
        CHECK(cells[1].source.x == 16.f);                // "stone" tile rect
        CHECK(cells[1].source.width == 16.f);
    }

    // -- tilemapRenderCells: a cell referencing an unknown tile id is skipped,
    // not crashed on or substituted with a placeholder ------------------------
    {
        TilesetAsset tileset;
        tileset.assetId = "tiles-1";
        tileset.tiles.push_back(TileDefinition{"grass", 0, 0, 16, 16});

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 1;
        TilemapChunk chunk;
        chunk.cells = {TilemapCellValue{"no-such-tile", TileTransformFlags::None}};
        tm.chunks.push_back(chunk);

        CHECK(tilemapRenderCells(tm, tileset, Vec2{}).empty());
    }

    // -- tilemapAtlasSourceRect: half-texel inset prevents atlas bleeding ------
    {
        const SceneFrameRect atlas{64.f, 128.f, 32.f, 32.f};
        const SceneFrameRect inset = tilemapAtlasSourceRect(atlas);
        CHECK(inset.x == 64.5f);
        CHECK(inset.y == 128.5f);
        CHECK(inset.width == 31.f);
        CHECK(inset.height == 31.f);

        const SceneFrameRect rectangular = tilemapAtlasSourceRect({32.f, 64.f, 16.f, 32.f});
        CHECK(rectangular.x == 32.5f);
        CHECK(rectangular.y == 64.5f);
        CHECK(rectangular.width == 15.f);
        CHECK(rectangular.height == 31.f);

        const SceneFrameRect onePxWide{0.f, 0.f, 1.f, 8.f};
        CHECK(tilemapAtlasSourceRect(onePxWide).x == onePxWide.x);
        CHECK(tilemapAtlasSourceRect(onePxWide).y == onePxWide.y);
        CHECK(tilemapAtlasSourceRect(onePxWide).width == onePxWide.width);
        CHECK(tilemapAtlasSourceRect(onePxWide).height == onePxWide.height);

        const SceneFrameRect tiny{0.f, 0.f, 0.5f, 0.5f};
        CHECK(tilemapAtlasSourceRect(tiny).x == tiny.x);
        CHECK(tilemapAtlasSourceRect(tiny).width == tiny.width);
    }

    // -- AddTilemapComponentCommand: success, then rejections, no partial
    // mutation on any rejection ------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        tm.chunkSize = 16;
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap.has_value());
        CHECK(inst->tilemap->tilesetAssetId == "tiles-1");
        CHECK(inst->tilemap->cellSize.x == 32.f);
        CHECK(inst->tilemap->chunkSize == 16);
        CHECK(inst->tilemap->chunks.empty());

        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);   // double-add

        TilemapComponent unknownTileset = tm;
        unknownTileset.tilesetAssetId = "no-such-tileset";
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, 9999, unknownTileset}).ok);

        TilemapComponent badCellSize = tm;
        badCellSize.cellSize = {0.f, 32.f};
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, 9999, badCellSize}).ok);

        TilemapComponent badChunkSize = tm;
        badChunkSize.chunkSize = 0;
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, 9999, badChunkSize}).ok);
        badChunkSize.chunkSize = kMaxTilemapChunkSize + 1;
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, 9999, badChunkSize}).ok);

        CHECK(!c.document().findInstanceInScene(kSceneA, 9999));   // no rejection left a partial instance
    }

    // -- AddTilemapComponentCommand: a populated-but-invalid component is
    // rejected - proves the command runs the full shared validator, not just
    // top-level field checks ---------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};   // "tiles-1" has no tiles at all yet
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.chunkX = 0;
        chunk.chunkY = 0;
        chunk.cells.assign(4, std::nullopt);
        chunk.cells[0] = TilemapCellValue{"no-such-tile", TileTransformFlags::None};
        tm.chunks.push_back(chunk);
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->tilemap.has_value());
    }

    // -- AddTilemapComponentCommand: undo/redo ---------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        CHECK(c.undo().ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->tilemap.has_value());
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-1");
    }

    // -- RemoveTilemapComponentCommand: success, no-component rejection, undo
    // restores exactly ----------------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(!c.execute(RemoveTilemapComponentCommand{kSceneA, kHero}).ok);   // nothing to remove

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {48.f, 24.f};
        tm.chunkSize = 8;
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        CHECK(c.execute(RemoveTilemapComponentCommand{kSceneA, kHero}).ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->tilemap.has_value());

        CHECK(c.undo().ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap.has_value());
        CHECK(inst->tilemap->cellSize.x == 48.f);
        CHECK(inst->tilemap->cellSize.y == 24.f);
        CHECK(inst->tilemap->chunkSize == 8);
    }

    // -- SetTilemapTilesetCommand: success, same-value no-op, unknown-tileset
    // rejection, missing-component rejection, undo/redo -----------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-2", "Forest", "img-hero",
                                               TilesetSlicing{}}).ok);
        CHECK(!c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "tiles-2"}).ok);   // no component yet

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        const std::size_t before = c.undoSize();
        CHECK(c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "tiles-1"}).ok);   // same value -> no-op
        CHECK(c.undoSize() == before);

        CHECK(!c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "no-such-tileset"}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-1");

        CHECK(c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "tiles-2"}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-2");
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-1");
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-2");
    }

    // -- SetTilemapTilesetCommand: rejected when the new tileset doesn't
    // contain a tile id already used by an existing chunk - no mutation, no
    // undo entry ----------------------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);   // "tiles-1" now has "tile-1".."tile-4"
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-2", "Forest", "img-hero",
                                               TilesetSlicing{}}).ok);
        // "tiles-2" gets a tile list with deliberately non-overlapping ids, not
        // via tilesForSlicing (which would also start at "tile-1").
        std::vector<TileDefinition> forestTiles;
        forestTiles.push_back(TileDefinition{"forest-a", 0, 0, 32, 32});
        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-2", TilesetSlicing{}, forestTiles}).ok);

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.chunkX = 0;
        chunk.chunkY = 0;
        chunk.cells.assign(4, std::nullopt);
        chunk.cells[0] = TilemapCellValue{"tile-1", TileTransformFlags::None};   // real tiles-1 tile
        tm.chunks.push_back(chunk);
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        const std::size_t before = c.undoSize();
        CHECK(!c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "tiles-2"}).ok);   // "tile-1" not in tiles-2
        CHECK(c.undoSize() == before);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-1");
    }

    // -- SetTilemapCellSizeCommand: success, same-value no-op, non-positive/
    // non-finite rejection, missing-component rejection, undo/redo ------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(!c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 16.f}}).ok);

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        const std::size_t before = c.undoSize();
        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {32.f, 32.f}}).ok);   // no-op
        CHECK(c.undoSize() == before);

        CHECK(!c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {0.f, 32.f}}).ok);
        CHECK(!c.execute(SetTilemapCellSizeCommand{kSceneA, kHero,
                                                   {std::numeric_limits<float>::infinity(), 32.f}}).ok);

        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {48.f, 16.f}}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->cellSize.x == 48.f);
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->cellSize.x == 32.f);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->cellSize.x == 48.f);
    }

    // -- CloneInstanceCommand copies tilemap verbatim - regression test, not
    // just an inspection of the whole-struct-copy claim -------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {40.f, 40.f};
        tm.chunkSize = 12;
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        CHECK(c.execute(CloneInstanceCommand{kSceneA, kHero, 900, "Hero Clone", {5.f, 5.f}}).ok);
        const SceneInstanceDef* clone = c.document().findInstanceInScene(kSceneA, 900);
        CHECK(clone->tilemap.has_value());
        CHECK(clone->tilemap->tilesetAssetId == "tiles-1");
        CHECK(clone->tilemap->cellSize.x == 40.f);
        CHECK(clone->tilemap->chunkSize == 12);
    }

    // -- Deleting an entity with a tilemap, then undoing, restores it intact ---
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {20.f, 20.f};
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        CHECK(c.execute(DeleteEntityCommand{kSceneA, kHero}).ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero));
        CHECK(c.undo().ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst != nullptr);
        CHECK(inst->tilemap.has_value());
        CHECK(inst->tilemap->cellSize.x == 20.f);
    }

    // -- RemoveTilesetAssetCommand rejects removal while a TilemapComponent
    // still references it - asset and component both unchanged, no undo entry -
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        const std::size_t before = c.undoSize();
        CHECK(!c.execute(RemoveTilesetAssetCommand{"tiles-1"}).ok);
        CHECK(c.undoSize() == before);
        CHECK(c.document().hasTilesetAsset("tiles-1"));
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap.has_value());
    }

    // -- RemoveTilesetAssetCommand with no referencing tilemap: unchanged,
    // regression guard on the existing no-consumer removal path ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.execute(RemoveTilesetAssetCommand{"tiles-1"}).ok);
        CHECK(!c.document().hasTilesetAsset("tiles-1"));
        CHECK(c.undo().ok);
        CHECK(c.document().hasTilesetAsset("tiles-1"));
    }

    // -- Save/reload round-trips an empty TilemapComponent - the slice's own
    // literal completion criterion. Also confirms the legacy tilemap/
    // tilemapLayers fields are never touched by this editor's own save/load,
    // for any project, not only ones with a TilemapComponent ------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        tm.chunkSize = 16;
        hero.tilemap = tm;

        EditorCoordinator c{doc};
        const std::filesystem::path path = testTempDir() / "tilemap-empty.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const SceneInstanceDef* rt = reloaded.document().findInstanceInScene(kSceneA, kHero);
        CHECK(rt != nullptr);
        CHECK(rt->tilemap.has_value());
        CHECK(rt->tilemap->tilesetAssetId == "tiles-1");
        CHECK(rt->tilemap->cellSize.x == 32.f);
        CHECK(rt->tilemap->cellSize.y == 32.f);
        CHECK(rt->tilemap->chunkSize == 16);
        CHECK(rt->tilemap->chunks.empty());

        const SceneDef* scene = reloaded.document().findScene(kSceneA);
        CHECK(scene->tilemap.cols == 0);            // legacy field, never populated by this editor
        CHECK(scene->tilemapLayers.empty());        // legacy field, never populated by this editor
    }

    // -- Save/reload round-trips a hand-constructed populated chunk: negative
    // chunk coords, a mix of null/filled cells, and a non-None flags value ----
    {
        ProjectDoc doc = makeSpriteDoc();
        TilesetAsset* tiles1 = nullptr;
        for (TilesetAsset& t : doc.tilesets) if (t.assetId == "tiles-1") tiles1 = &t;
        tiles1->tiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});
        tiles1->tiles.push_back(TileDefinition{"tile-2", 32, 0, 32, 32});

        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.chunkX = -3;
        chunk.chunkY = -1;
        chunk.cells = {
            TilemapCellValue{"tile-1", TileTransformFlags::FlipX},
            std::nullopt,
            std::nullopt,
            TilemapCellValue{"tile-2", TileTransformFlags::None},
        };
        tm.chunks.push_back(chunk);
        hero.tilemap = tm;

        EditorCoordinator c{doc};
        const std::filesystem::path path = testTempDir() / "tilemap-populated.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const SceneInstanceDef* rt = reloaded.document().findInstanceInScene(kSceneA, kHero);
        CHECK(rt->tilemap->chunks.size() == 1);
        const TilemapChunk& rtChunk = rt->tilemap->chunks.front();
        CHECK(rtChunk.chunkX == -3);
        CHECK(rtChunk.chunkY == -1);
        CHECK(rtChunk.cells.size() == 4);
        CHECK(rtChunk.cells[0].has_value());
        CHECK(rtChunk.cells[0]->tileId == "tile-1");
        CHECK(rtChunk.cells[0]->flags == TileTransformFlags::FlipX);
        CHECK(!rtChunk.cells[1].has_value());
        CHECK(!rtChunk.cells[2].has_value());
        CHECK(rtChunk.cells[3].has_value());
        CHECK(rtChunk.cells[3]->tileId == "tile-2");
        CHECK(rtChunk.cells[3]->flags == TileTransformFlags::None);
    }

    // -- Validation: unknown tilesetAssetId is rejected ------------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "no-such-tileset";
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-tileset.artcade-project").ok);
    }

    // -- Validation: non-positive cellSize is rejected -------------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {0.f, 32.f};
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-cellsize.artcade-project").ok);
    }

    // -- Validation: chunkSize of 0, negative, and > kMaxTilemapChunkSize are
    // all rejected - the last one specifically guards the overflow fix --------
    {
        for (const int badChunkSize : {0, -1, kMaxTilemapChunkSize + 1}) {
            ProjectDoc doc = makeSpriteDoc();
            SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
            TilemapComponent tm;
            tm.tilesetAssetId = "tiles-1";
            tm.chunkSize = badChunkSize;
            hero.tilemap = tm;
            EditorCoordinator c{doc};
            CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-chunksize.artcade-project").ok);
        }
    }

    // -- Validation: a chunk with the wrong cell count (!= chunkSize squared)
    // is rejected ----------------------------------------------------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 4;   // expects 16 cells
        TilemapChunk chunk;
        chunk.cells.assign(4, std::nullopt);   // wrong: only 4
        tm.chunks.push_back(chunk);
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-cellcount.artcade-project").ok);
    }

    // -- Validation: two chunks sharing the same (chunkX, chunkY) are rejected -
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk a; a.chunkX = 1; a.chunkY = 1; a.cells.assign(4, std::nullopt);
        TilemapChunk b; b.chunkX = 1; b.chunkY = 1; b.cells.assign(4, std::nullopt);
        tm.chunks.push_back(a);
        tm.chunks.push_back(b);
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-dup-chunk.artcade-project").ok);
    }

    // -- Validation: a cell referencing a tile id absent from the tileset is
    // rejected --------------------------------------------------------------------
    {
        ProjectDoc doc = makeSpriteDoc();   // "tiles-1" has zero TileDefinitions
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.cells.assign(4, std::nullopt);
        chunk.cells[0] = TilemapCellValue{"tile-1", TileTransformFlags::None};   // not in tiles-1
        tm.chunks.push_back(chunk);
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-tileid.artcade-project").ok);
    }

    // -- ComponentKind::Tilemap flows correctly through DomainChange -----------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        const EditorOperationResult r = c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ComponentAdded);
        CHECK(r.change.componentKind == ComponentKind::Tilemap);
    }
}

// Adds a TilemapComponent to kHero referencing "tiles-1" (sliced into
// "tile-1".."tile-4" via sliceTilesOne), chunkSize=16, ready to paint.
static void setUpTilemapForPainting(EditorCoordinator& c) {
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

static void runTilemapPaintingTests() {
    // -- rasterizeCellLine: same start/end -> one cell -------------------------
    {
        const auto path = rasterizeCellLine(TilemapCellCoord{3, 5}, TilemapCellCoord{3, 5});
        CHECK(path.size() == 1);
        CHECK(path[0].cellX == 3 && path[0].cellY == 5);
    }

    // -- rasterizeCellLine: horizontal/vertical/diagonal, no gaps --------------
    {
        const auto h = rasterizeCellLine(TilemapCellCoord{0, 0}, TilemapCellCoord{4, 0});
        CHECK(h.size() == 5);
        for (std::size_t i = 0; i < h.size(); ++i) {
            CHECK(h[i].cellX == static_cast<int>(i));
            CHECK(h[i].cellY == 0);
        }
        const auto v = rasterizeCellLine(TilemapCellCoord{2, 0}, TilemapCellCoord{2, 3});
        CHECK(v.size() == 4);
        const auto d = rasterizeCellLine(TilemapCellCoord{0, 0}, TilemapCellCoord{3, 3});
        CHECK(d.size() == 4);
    }

    // -- rasterizeCellLine: a long span (fast mouse move) stays fully 8-
    // connected, no gaps, both endpoints included --------------------------------
    {
        const auto path = rasterizeCellLine(TilemapCellCoord{0, 0}, TilemapCellCoord{20, 3});
        CHECK(path.front().cellX == 0 && path.front().cellY == 0);
        CHECK(path.back().cellX == 20 && path.back().cellY == 3);
        for (std::size_t i = 1; i < path.size(); ++i) {
            int dx = path[i].cellX - path[i - 1].cellX;
            int dy = path[i].cellY - path[i - 1].cellY;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            CHECK(dx <= 1 && dy <= 1);
        }
    }

    // -- rasterizeCellLine: explicit chunk-boundary crossings ------------------
    {
        CHECK(rasterizeCellLine(TilemapCellCoord{-1, 0}, TilemapCellCoord{0, 0}).size() == 2);
        CHECK(rasterizeCellLine(TilemapCellCoord{15, 0}, TilemapCellCoord{16, 0}).size() == 2);
        CHECK(rasterizeCellLine(TilemapCellCoord{0, -16}, TilemapCellCoord{0, -15}).size() == 2);
    }

    // -- readTilemapCell/writeTilemapCell/pruneEmptyChunks ---------------------
    {
        TilemapComponent tm;
        tm.chunkSize = 4;
        CHECK(!readTilemapCell(tm, TilemapCellCoord{0, 0}).has_value());   // no chunk yet
        writeTilemapCell(tm, TilemapCellCoord{0, 0}, TilemapCellValue{"tile-1", TileTransformFlags::None});
        CHECK(tm.chunks.size() == 1);
        CHECK(tm.chunks[0].cells.size() == 16);
        const TilemapCell read = readTilemapCell(tm, TilemapCellCoord{0, 0});
        CHECK(read.has_value());
        CHECK(read->tileId == "tile-1");

        writeTilemapCell(tm, TilemapCellCoord{-1, -1}, TilemapCellValue{"tile-2", TileTransformFlags::None});
        CHECK(tm.chunks.size() == 2);
        CHECK(readTilemapCell(tm, TilemapCellCoord{-1, -1})->tileId == "tile-2");

        writeTilemapCell(tm, TilemapCellCoord{-1, -1}, std::nullopt);
        pruneEmptyChunks(tm);
        CHECK(tm.chunks.size() == 1);   // the (-1,-1) chunk is gone, (0,0)'s chunk remains

        writeTilemapCell(tm, TilemapCellCoord{0, 0}, std::nullopt);
        pruneEmptyChunks(tm);
        CHECK(tm.chunks.empty());
    }

    // -- normalizePaintStrokeChanges: drops before==after, keeps real changes --
    {
        std::unordered_map<std::int64_t, TilemapCellChange> changes;
        changes[packTilemapCellCoord({0, 0})] =
            TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}};
        changes[packTilemapCellCoord({1, 0})] = TilemapCellChange{
            {1, 0}, TilemapCellValue{"tile-2", TileTransformFlags::None},
            TilemapCellValue{"tile-2", TileTransformFlags::None}};   // no-op: same value
        changes[packTilemapCellCoord({2, 0})] = TilemapCellChange{{2, 0}, std::nullopt, std::nullopt};   // no-op
        const std::vector<TilemapCellChange> result = normalizePaintStrokeChanges(changes);
        CHECK(result.size() == 1);
        CHECK(result[0].cell.cellX == 0);
    }

    // -- BeginTilePaintStrokeIntent: success, seeds pendingStroke correctly ----
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.state().tilemapEditor.pendingStroke->changes.size() == 1);
        CHECK(c.state().tilemapEditor.pendingStroke->tool == EditorTool::Brush);
    }

    // -- BeginTilePaintStrokeIntent: missing component rejected ----------------
    {
        EditorCoordinator c{makeSpriteDoc()};   // kHero has no tilemap
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- BeginTilePaintStrokeIntent: locked layer rejected ---------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneDef& scene = doc.scenes.at(kSceneA);
        SceneLayerDef locked;
        locked.id = "locked-layer";
        locked.name = "Locked";
        locked.locked = true;
        scene.layers.push_back(locked);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        // Hand-authored directly on the fixture (not via AddTilemapComponentCommand,
        // which now itself rejects adding a component on a locked layer) - this
        // test's target is BeginTilePaintStrokeIntent's own lock guard.
        scene.instances.front().tilemap = tm;
        scene.instances.front().layerId = "locked-layer";
        EditorCoordinator c{doc};
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- BeginTilePaintStrokeIntent: missing tileset rejected ------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        TilemapComponent tm;
        tm.tilesetAssetId = "no-such-tileset";
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- BeginTilePaintStrokeIntent: rejected while Play is running ------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.playCurrentScene().ok);
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- UpdateTilePaintStrokeIntent: fast movement interpolates with no gaps,
    // across a chunk boundary and into negative coordinates -------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {14, 0}}).ok);
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{18, 0}}).ok);   // crosses chunk boundary (chunkSize=16)
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{-2, 0}}).ok);   // crosses into negative
        CHECK(c.state().tilemapEditor.pendingStroke->changes.size()
              == static_cast<std::size_t>(18 - (-2) + 1));   // every cell from -2..18, no gaps
    }

    // -- UpdateTilePaintStrokeIntent: a revisited cell keeps its first `before`
    // and its last `after` (the exact empty -> tileA -> tileB example) --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{1, 0}}).ok);
        c.apply(SelectPaintTileIntent{"tile-2"});
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{0, 0}}).ok);   // revisit (0,0), tile-2 now selected

        const auto& changes = c.state().tilemapEditor.pendingStroke->changes;
        CHECK(changes.size() == 2);   // (0,0) and (1,0), each exactly once
        bool foundOrigin = false;
        for (const auto& [key, change] : changes) {
            if (change.cell.cellX == 0 && change.cell.cellY == 0) {
                foundOrigin = true;
                CHECK(!change.before.has_value());          // empty before the stroke
                CHECK(change.after.has_value());
                CHECK(change.after->tileId == "tile-2");     // last touch wins
            }
        }
        CHECK(foundOrigin);
    }

    // -- Full stroke lifecycle via the router's own normalize+execute shape:
    // pointer-up produces exactly one Command regardless of cell count --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{5, 0}}).ok);
        const std::size_t before = c.undoSize();
        {
            const PendingTileStroke& stroke = *c.state().tilemapEditor.pendingStroke;
            std::vector<TilemapCellChange> changes = normalizePaintStrokeChanges(stroke.changes);
            CHECK(!changes.empty());
            CHECK(c.execute(PaintTilemapCellsCommand{stroke.sceneId, stroke.entityId,
                                                     std::move(changes)}).ok);
        }
        c.apply(EndTilePaintStrokeIntent{});
        CHECK(c.undoSize() == before + 1);   // exactly one Command, however many cells
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
    }

    // -- No-op stroke (painting a cell with the tile it already has) produces
    // an empty normalized delta and no Command --------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        {
            const PendingTileStroke& stroke = *c.state().tilemapEditor.pendingStroke;
            std::vector<TilemapCellChange> changes = normalizePaintStrokeChanges(stroke.changes);
            CHECK(c.execute(PaintTilemapCellsCommand{stroke.sceneId, stroke.entityId,
                                                     std::move(changes)}).ok);
        }
        c.apply(EndTilePaintStrokeIntent{});

        const std::size_t before = c.undoSize();
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        const PendingTileStroke& stroke = *c.state().tilemapEditor.pendingStroke;
        const std::vector<TilemapCellChange> changes = normalizePaintStrokeChanges(stroke.changes);
        CHECK(changes.empty());   // already tile-1 there - before == after
        c.apply(EndTilePaintStrokeIntent{});
        CHECK(c.undoSize() == before);   // normalization skipped the Command entirely
    }

    // -- Escape/lost-focus-equivalent cancel: no Command, no dirty -------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.apply(CancelTilePaintStrokeIntent{}).ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
    }

    // -- SelectPaintTileIntent (Picker): zero dirty/revision/history impact ----
    {
        EditorCoordinator c{makeSpriteDoc()};
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(c.apply(SelectPaintTileIntent{"tile-1"}).ok);
        CHECK(c.state().tilemapEditor.selectedTileId.has_value());
        CHECK(*c.state().tilemapEditor.selectedTileId == "tile-1");
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
        CHECK(!c.document().isDirty());
    }

    // -- PaintTilemapCellsCommand: paints a multi-chunk, negative-coordinate
    // delta correctly ------------------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);   // chunkSize = 16
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{14, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{18, 0}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{-3, -3}, std::nullopt,
                                            TilemapCellValue{"tile-3", TileTransformFlags::FlipX}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        const TilemapComponent& tm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        CHECK(tm.chunks.size() == 3);
        CHECK(readTilemapCell(tm, {14, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {18, 0})->tileId == "tile-2");
        CHECK(readTilemapCell(tm, {-3, -3})->tileId == "tile-3");
        CHECK(readTilemapCell(tm, {-3, -3})->flags == TileTransformFlags::FlipX);
    }

    // -- PaintTilemapCellsCommand: Undo restores exactly, Redo reproduces ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 1}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.undo().ok);
        const SceneInstanceDef* undone = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(!readTilemapCell(*undone->tilemap, {0, 0}).has_value());
        CHECK(!readTilemapCell(*undone->tilemap, {1, 1}).has_value());
        CHECK(undone->tilemap->chunks.empty());
        CHECK(c.redo().ok);
        const SceneInstanceDef* redone = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(readTilemapCell(*redone->tilemap, {0, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(*redone->tilemap, {1, 1})->tileId == "tile-2");
    }

    // -- PaintTilemapCellsCommand: empty delta, missing entity, missing
    // component, unknown tile id in `after` all rejected cleanly ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(!c.execute(PaintTilemapCellsCommand{kSceneA, kHero, {}}).ok);
        CHECK(!c.execute(PaintTilemapCellsCommand{
            kSceneA, 9999,
            {TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}}}}).ok);
        CHECK(!c.execute(PaintTilemapCellsCommand{
            kSceneA, kHero,
            {TilemapCellChange{{0, 0}, std::nullopt,
                              TilemapCellValue{"no-such-tile", TileTransformFlags::None}}}}).ok);

        EditorCoordinator noTilemap{makeSpriteDoc()};   // kHero has no Tilemap component
        CHECK(!noTilemap.execute(PaintTilemapCellsCommand{
            kSceneA, kHero,
            {TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}}}}).ok);
    }

    // -- PaintTilemapCellsCommand: missing tileset rejected --------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        TilemapComponent tm;
        tm.tilesetAssetId = "no-such-tileset";
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!c.execute(PaintTilemapCellsCommand{
            kSceneA, kHero,
            {TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}}}}).ok);
    }

    // -- PaintTilemapCellsCommand: duplicate cell in the delta rejected --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        CHECK(!c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
    }

    // -- PaintTilemapCellsCommand: before-mismatch fails atomically, the
    // document keeps the value an intervening mutation actually set ----------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> first;
        first.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                          TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, first}).ok);

        // A stale delta claiming (0,0) was still empty - as if built before
        // the paint above landed.
        std::vector<TilemapCellChange> stale;
        stale.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                          TilemapCellValue{"tile-2", TileTransformFlags::None}});
        const std::size_t before = c.undoSize();
        CHECK(!c.execute(PaintTilemapCellsCommand{kSceneA, kHero, stale}).ok);
        CHECK(c.undoSize() == before);
        CHECK(readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap,
                              {0, 0})->tileId == "tile-1");
    }

    // -- PaintTilemapCellsCommand: one invalid change among N valid ones
    // rejects the whole Command - none of the N valid ones land ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 0}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{2, 0}, std::nullopt,
                                            TilemapCellValue{"no-such-tile", TileTransformFlags::None}});
        CHECK(!c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->chunks.empty());
    }

    // -- Eraser removing a chunk's last tile removes the chunk; Undo recreates
    // it with its original cell intact -----------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 1;   // one cell per chunk: simplest possible "last tile in chunk"
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        std::vector<TilemapCellChange> paint;
        paint.push_back(TilemapCellChange{{5, 5}, std::nullopt,
                                          TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, paint}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->chunks.size() == 1);

        std::vector<TilemapCellChange> erase;
        erase.push_back(TilemapCellChange{{5, 5}, TilemapCellValue{"tile-1", TileTransformFlags::None},
                                          std::nullopt});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, erase}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->chunks.empty());

        CHECK(c.undo().ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap->chunks.size() == 1);
        CHECK(readTilemapCell(*inst->tilemap, {5, 5})->tileId == "tile-1");
    }

    // -- Persistence: paint across multiple chunks (including negative
    // coordinates and a non-None flags value), save, reload, verify identical
    // chunk content/layout -----------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{14, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{-3, -3}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::FlipX}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);

        const std::filesystem::path path = testTempDir() / "tilemap-painted.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const SceneInstanceDef* rt = reloaded.document().findInstanceInScene(kSceneA, kHero);
        CHECK(rt->tilemap.has_value());
        CHECK(rt->tilemap->chunks.size() == 2);
        CHECK(readTilemapCell(*rt->tilemap, {14, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(*rt->tilemap, {-3, -3})->tileId == "tile-2");
        CHECK(readTilemapCell(*rt->tilemap, {-3, -3})->flags == TileTransformFlags::FlipX);
    }

    // -- Regression: an unpainted Tilemap still yields an empty-cells snapshot
    // entry (Slice 5's placeholder fallback is unaffected) ---------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        const auto it = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(it != snap.tilemaps.end());
        CHECK(it->cells.empty());
    }

    // -- Regression: a freshly painted cell is pickable via pickEntityAt -------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {0.f, 0.f}}).ok);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        CHECK(pickEntityAt(snap, Vec2{10.f, 10.f}) == kHero);   // inside the painted 32x32 cell at origin
    }

    // ================= Eraser-via-right-click momentary override =================
    // effectiveTilemapTool(): no override -> the persistent tool -------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);   // Brush only sticks with a selection that supports it
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.effectiveTilemapTool() == EditorTool::Brush);
    }

    // BeginTemporaryToolOverrideIntent: layers over the persistent tool without
    // ever writing activeTool - this is the whole point of the design ------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        const auto begin = c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        CHECK(begin.ok);
        CHECK(begin.invalidation == EditorInvalidation::Inspector);
        CHECK(c.effectiveTilemapTool() == EditorTool::Eraser);
        CHECK(c.state().activeTool == EditorTool::Brush);   // persistent selection untouched
    }

    // EndTemporaryToolOverrideIntent: clears it, reverting to the persistent tool -
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        const auto end = c.apply(EndTemporaryToolOverrideIntent{});
        CHECK(end.ok);
        CHECK(c.effectiveTilemapTool() == EditorTool::Brush);
    }

    // EndTemporaryToolOverrideIntent with nothing set: harmless no-op, safe for
    // every termination path (commit/cancel/failed-Begin) to call unconditionally
    {
        EditorCoordinator c{makeSpriteDoc()};
        const auto end = c.apply(EndTemporaryToolOverrideIntent{});
        CHECK(end.ok);
        CHECK(end.invalidation == EditorInvalidation::None);
    }

    // Full gesture: Brush selected, right-click-Eraser overrides for one stroke,
    // paints as Eraser, then ends - the persistent tool is Brush throughout and
    // straight after, matching how a real Brush->right-click->release cycle must
    // feel (no re-click needed to keep painting with Brush) ----------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        std::vector<TilemapCellChange> seed;
        seed.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                         TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, seed}).ok);

        c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        CHECK(c.effectiveTilemapTool() == EditorTool::Eraser);
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
        CHECK(c.state().activeTool == EditorTool::Brush);   // still Brush mid-gesture

        const PendingTileStroke& stroke = *c.state().tilemapEditor.pendingStroke;
        std::vector<TilemapCellChange> changes = normalizePaintStrokeChanges(stroke.changes);
        CHECK(!changes.empty());
        CHECK(c.execute(PaintTilemapCellsCommand{stroke.sceneId, stroke.entityId, changes}).ok);
        c.apply(EndTilePaintStrokeIntent{});
        c.apply(EndTemporaryToolOverrideIntent{});

        CHECK(!readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {0, 0}).has_value());
        CHECK(c.state().activeTool == EditorTool::Brush);
        CHECK(c.effectiveTilemapTool() == EditorTool::Brush);   // back to Brush, no re-click needed
        CHECK(!c.state().tilemapEditor.temporaryToolOverride.has_value());
    }

    // Entity deleted mid-stroke while an override is active: reconcileWorkspace
    // must drop the override too, or it would be stuck on Eraser with no stroke
    // left to ever trigger its own cleanup ----------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.temporaryToolOverride.has_value());
        CHECK(c.execute(DeleteEntityCommand{kSceneA, kHero}).ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(!c.state().tilemapEditor.temporaryToolOverride.has_value());
        // The deleted entity was the selection too - with nothing left to
        // paint, reconcileTilemapEditingContext() (run by reconcileWorkspace
        // after the DeleteEntityCommand) falls the persistent tool back to
        // Select as well, not just the override.
        CHECK(c.effectiveTilemapTool() == EditorTool::Select);
        CHECK(c.state().activeTool == EditorTool::Select);
    }
}

// Re-slice cascade: ChangeTilesetSlicingCommand clears painted cells whose
// tile id disappears, atomically and undoably, so the document never holds a
// cell referencing a missing tile id (AC-DOM-001 explicit-cascade policy).
static void runTilesetResliceCascadeTests() {
    // -- computeTilesetResliceImpact: counts only the orphans - surviving
    // painted ids and unrelated tilesets contribute nothing --------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);   // "tiles-1" sliced tile-1..4, tilemap on kHero
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 0}, std::nullopt,
            TilemapCellValue{"tile-4", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);

        std::vector<TileDefinition> keepOne;   // hypothetical: keeps tile-1, drops tile-4
        keepOne.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});
        const TilesetResliceImpact partial =
            computeTilesetResliceImpact(c.document(), "tiles-1", keepOne);
        CHECK(partial.removedReferencedTiles == 1);
        CHECK(partial.orphanedCells == 1);
        CHECK(partial.affectedTilemaps == 1);

        const TilesetResliceImpact unrelated =
            computeTilesetResliceImpact(c.document(), "no-such-tileset", keepOne);
        CHECK(unrelated.removedReferencedTiles == 0);
        CHECK(unrelated.orphanedCells == 0);
        CHECK(unrelated.affectedTilemaps == 0);
    }

    // -- Cascade: a 64x64 re-slice orphans every painted cell; apply clears
    // them (chunk pruned, document still valid), undo restores cells, tiles
    // and slicing exactly, redo clears again ------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 0}, std::nullopt,
            TilemapCellValue{"tile-4", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);

        TilesetSlicing big;
        big.tileWidth = 64;
        big.tileHeight = 64;
        const std::vector<TileDefinition> fresh = tilesForSlicing(64, 64, big);
        CHECK(fresh.size() == 1);
        const std::vector<TileDefinition> reconciled =
            reconcileTiles(c.document().findTilesetAsset("tiles-1")->tiles, fresh);
        CHECK(reconciled.size() == 1);
        CHECK(reconciled[0].id != "tile-1");   // removed ids are never recycled

        const TilesetResliceImpact impact =
            computeTilesetResliceImpact(c.document(), "tiles-1", reconciled);
        CHECK(impact.removedReferencedTiles == 2);
        CHECK(impact.orphanedCells == 2);
        CHECK(impact.affectedTilemaps == 1);

        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-1", big, reconciled}).ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap->chunks.empty());   // both cells cleared -> chunk pruned
        CHECK(!validateTilemapComponent(c.document(), *inst->tilemap).has_value());

        CHECK(c.undo().ok);
        inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(readTilemapCell(*inst->tilemap, {0, 0}).has_value());
        CHECK(readTilemapCell(*inst->tilemap, {0, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(*inst->tilemap, {1, 0})->tileId == "tile-4");
        CHECK(c.document().findTilesetAsset("tiles-1")->tiles.size() == 4);
        CHECK(c.document().findTilesetAsset("tiles-1")->slicing.tileWidth == 32);
        CHECK(!validateTilemapComponent(c.document(), *inst->tilemap).has_value());

        CHECK(c.redo().ok);
        inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap->chunks.empty());
        CHECK(c.document().findTilesetAsset("tiles-1")->slicing.tileWidth == 64);
    }

    // -- Cascade clears only the orphaned cells: a painted cell whose rect
    // (and so id) survives the re-slice keeps its content ----------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 0}, std::nullopt,
            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);

        // spacingX=32 over the 64x64 sheet keeps one 32x32 column: rects
        // (0,0) and (0,32) survive as tile-1/tile-3; tile-2 and tile-4 go.
        TilesetSlicing spaced;
        spaced.tileWidth = 32;
        spaced.tileHeight = 32;
        spaced.spacingX = 32;
        const std::vector<TileDefinition> fresh = tilesForSlicing(64, 64, spaced);
        CHECK(fresh.size() == 2);
        const std::vector<TileDefinition> reconciled =
            reconcileTiles(c.document().findTilesetAsset("tiles-1")->tiles, fresh);
        CHECK(reconciled[0].id == "tile-1");
        CHECK(reconciled[1].id == "tile-3");

        const TilesetResliceImpact impact =
            computeTilesetResliceImpact(c.document(), "tiles-1", reconciled);
        CHECK(impact.removedReferencedTiles == 1);   // only tile-2 is painted
        CHECK(impact.orphanedCells == 1);

        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-1", spaced, reconciled}).ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap->chunks.size() == 1);   // survivor keeps its chunk alive
        CHECK(readTilemapCell(*inst->tilemap, {0, 0})->tileId == "tile-1");
        CHECK(!readTilemapCell(*inst->tilemap, {1, 0}).has_value());
        CHECK(!validateTilemapComponent(c.document(), *inst->tilemap).has_value());
    }
}

// Slice 7 - Rectangle Solid/Outline and Flood Fill, reusing PaintTilemapCellsCommand.
static void runTilemapRegionTests() {
    // ======================= tryOffsetCell =======================
    {
        constexpr int kMax = std::numeric_limits<int>::max();
        constexpr int kMin = std::numeric_limits<int>::min();
        CHECK(tryOffsetCell(TilemapCellCoord{0, 0}, 1, 0)->cellX == 1);
        CHECK(tryOffsetCell(TilemapCellCoord{5, -5}, -1, 1)->cellX == 4);
        CHECK(tryOffsetCell(TilemapCellCoord{5, -5}, -1, 1)->cellY == -4);
        CHECK(!tryOffsetCell(TilemapCellCoord{kMax, 0}, 1, 0).has_value());
        CHECK(!tryOffsetCell(TilemapCellCoord{kMin, 0}, -1, 0).has_value());
        CHECK(!tryOffsetCell(TilemapCellCoord{0, kMax}, 0, 1).has_value());
        CHECK(!tryOffsetCell(TilemapCellCoord{0, kMin}, 0, -1).has_value());
        CHECK(tryOffsetCell(TilemapCellCoord{kMax, 0}, -1, 0).has_value());   // moving away from the edge is fine
    }

    // ======================= rectangleFillChanges =======================
    // -- 3x3 solid: exactly 9 cells, all empty -> tile-1 -----------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleFillChanges(tm, {0, 0}, {2, 2}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 9);
        for (const TilemapCellChange& c : r.changes) {
            CHECK(!c.before.has_value());
            CHECK(c.after == tile1);
        }
    }

    // -- Inverted corners produce the identical result as a forward drag ------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult forward = rectangleFillChanges(tm, {0, 0}, {2, 2}, tile1);
        const TileRegionBuildResult inverted = rectangleFillChanges(tm, {2, 2}, {0, 0}, tile1);
        CHECK(forward.changes.size() == 9);
        CHECK(inverted.changes.size() == 9);
    }

    // -- Negative coordinates ---------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleFillChanges(tm, {-5, -5}, {-3, -3}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 9);
    }

    // -- No-op: painting a region that's already entirely the target tile -----
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        writeTilemapCell(tm, {0, 0}, tile1);
        writeTilemapCell(tm, {1, 0}, tile1);
        const TileRegionBuildResult r = rectangleFillChanges(tm, {0, 0}, {1, 0}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- Within-limit large area succeeds (200 x 300 = 60000 <= 65536) ---------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleFillChanges(tm, {0, 0}, {199, 299}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 60000);
    }

    // -- Over-limit rectangle rejected, no partial changes ---------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleFillChanges(tm, {0, 0}, {299, 299}, tile1);   // 90000 cells
        CHECK(r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- Extreme coordinates, small area: widened arithmetic gives the exact
    // result with no overflow ---------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        constexpr int kMax = std::numeric_limits<int>::max();
        const TileRegionBuildResult r = rectangleFillChanges(tm, {kMax - 2, 0}, {kMax, 0}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 3);
    }

    // -- Astronomically huge span (near-full int32 range on both axes):
    // rejected immediately, no overflow, no hang ---------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        constexpr int kMax = std::numeric_limits<int>::max();
        constexpr int kMin = std::numeric_limits<int>::min();
        const TileRegionBuildResult r = rectangleFillChanges(tm, {kMin, kMin}, {kMax, kMax}, tile1);
        CHECK(r.error.has_value());
        CHECK(r.changes.empty());
    }

    // ======================= rectangleOutlineChanges =======================
    // -- 5x5 outline: perimeter only (16 cells), not the 25-cell area ----------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {4, 4}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 16);
    }

    // -- 1x1 ---------------------------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {3, 3}, {3, 3}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 1);
    }

    // -- 1xN (single column): the full strip, no interior to exclude -----------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {0, 6}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 7);
    }

    // -- Nx1 (single row) ---------------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {6, 0}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 7);
    }

    // -- 2x2: every cell is border, no interior ----------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {1, 1}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 4);
    }

    // -- Outline stays O(perimeter): a 300x300 outline is nowhere near its own
    // 90000-cell area (which alone would exceed the limit) ----------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {299, 299}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 1196);   // 2*300 + 2*300 - 4
    }

    // -- Outline over the limit rejected (huge perimeter) ------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {40000, 40000}, tile1);
        CHECK(r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- No-op outline: border already uniform -----------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        for (int x = 0; x <= 2; ++x) {
            writeTilemapCell(tm, {x, 0}, tile1);
            writeTilemapCell(tm, {x, 2}, tile1);
        }
        writeTilemapCell(tm, {0, 1}, tile1);
        writeTilemapCell(tm, {2, 1}, tile1);
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {2, 2}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.empty());   // all 8 border cells already tile-1
    }

    // ======================= floodFillChanges =======================
    // -- target == replacement: immediate empty result, no traversal ----------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        writeTilemapCell(tm, {0, 0}, tile1);
        const TileRegionBuildResult r = floodFillChanges(tm, {0, 0}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.empty());
    }
    {
        // Empty -> empty (nullopt == nullopt) is also a same-value no-op.
        TilemapComponent tm; tm.chunkSize = 16;
        const TileRegionBuildResult r = floodFillChanges(tm, {100, 100}, std::nullopt);
        CHECK(!r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- Enclosed region bounded by a different tile id: fill stops at the
    // border, only the interior empty cells change ------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell wall = TilemapCellValue{"tile-2", TileTransformFlags::None};
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        // 5x5 ring of walls (cells (0,0)..(4,4)) around a 3x3 empty interior.
        for (int x = 0; x <= 4; ++x) {
            writeTilemapCell(tm, {x, 0}, wall);
            writeTilemapCell(tm, {x, 4}, wall);
        }
        for (int y = 0; y <= 4; ++y) {
            writeTilemapCell(tm, {0, y}, wall);
            writeTilemapCell(tm, {4, y}, wall);
        }
        const TileRegionBuildResult r = floodFillChanges(tm, {2, 2}, fillTile);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 9);   // the 3x3 interior only
        for (const TilemapCellChange& c : r.changes) {
            CHECK(c.cell.cellX >= 1 && c.cell.cellX <= 3);
            CHECK(c.cell.cellY >= 1 && c.cell.cellY <= 3);
            CHECK(!c.before.has_value());
            CHECK(c.after == fillTile);
        }
    }

    // -- Diagonal-only connectivity does not spread (4-connected only) --------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell wall = TilemapCellValue{"tile-2", TileTransformFlags::None};
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        // 5x5 grid, walls everywhere except four single-cell pockets at the
        // "diagonal" corners (1,1)/(3,1)/(1,3)/(3,3) - each fully enclosed by
        // walls on all four sides, so 4-connectivity cannot cross between
        // them even though they sit diagonally adjacent to one another.
        for (int y = 0; y <= 4; ++y) {
            for (int x = 0; x <= 4; ++x) {
                const bool pocket = (x == 1 || x == 3) && (y == 1 || y == 3);
                if (!pocket) writeTilemapCell(tm, {x, y}, wall);
            }
        }
        const TileRegionBuildResult r = floodFillChanges(tm, {1, 1}, fillTile);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 1);   // stays in its own pocket; the other three untouched
        CHECK(r.changes[0].cell.cellX == 1 && r.changes[0].cell.cellY == 1);
    }

    // -- Negative coordinates -----------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell wall = TilemapCellValue{"tile-2", TileTransformFlags::None};
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        for (int x = -3; x <= -1; ++x) {
            writeTilemapCell(tm, {x, -3}, wall);
            writeTilemapCell(tm, {x, -1}, wall);
        }
        for (int y = -3; y <= -1; ++y) {
            writeTilemapCell(tm, {-3, y}, wall);
            writeTilemapCell(tm, {-1, y}, wall);
        }
        const TileRegionBuildResult r = floodFillChanges(tm, {-2, -2}, fillTile);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 1);
        CHECK(r.changes[0].cell.cellX == -2 && r.changes[0].cell.cellY == -2);
    }

    // -- Open (unenclosed) empty region: cancelled at the limit, zero changes -
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = floodFillChanges(tm, {0, 0}, fillTile);   // fully open grid
        CHECK(r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- Near-INT_MAX origin on an open region: bounded on two sides by the
    // coordinate edge, still hits the operation limit rather than overflowing -
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        constexpr int kMax = std::numeric_limits<int>::max();
        const TileRegionBuildResult r = floodFillChanges(tm, {kMax, kMax}, fillTile);
        CHECK(r.error.has_value());   // still open toward -x/-y -> hits the cap, never overflows
        CHECK(r.changes.empty());
    }

    // -- Rectangle/Fill deltas commit and undo/redo through the same
    // PaintTilemapCellsCommand as Brush/Eraser - no parallel Command --------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        const TilemapComponent& tm0 = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        const TileRegionBuildResult built =
            rectangleFillChanges(tm0, {0, 0}, {2, 2}, TilemapCellValue{"tile-1", TileTransformFlags::None});
        CHECK(!built.error.has_value());
        CHECK(built.changes.size() == 9);
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, built.changes}).ok);
        for (int y = 0; y <= 2; ++y) {
            for (int x = 0; x <= 2; ++x) {
                CHECK(readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {x, y})
                          ->tileId == "tile-1");
            }
        }
        CHECK(c.undo().ok);
        CHECK(!readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {0, 0}).has_value());
        CHECK(c.redo().ok);
        CHECK(readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {0, 0})->tileId
              == "tile-1");
    }

    // -- Persistence: a Rectangle-painted delta survives save/reload ------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        const TilemapComponent& tm0 = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        const TileRegionBuildResult built =
            rectangleFillChanges(tm0, {0, 0}, {2, 2}, TilemapCellValue{"tile-1", TileTransformFlags::None});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, built.changes}).ok);

        const std::filesystem::path path = testTempDir() / "tilemap-rectangle.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const TilemapComponent& tm = *reloaded.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        for (int y = 0; y <= 2; ++y) {
            for (int x = 0; x <= 2; ++x) {
                CHECK(readTilemapCell(tm, {x, y})->tileId == "tile-1");
            }
        }
    }

    // ======================= BeginTileRectangleIntent =======================
    // -- success: captures scene/entity/tile/shape/start+current cell ---------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);   // no tile selected
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {2, 3}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle.has_value());
        const PendingTileRectangle& rect = *c.state().tilemapEditor.pendingRectangle;
        CHECK(rect.sceneId == kSceneA);
        CHECK(rect.entityId == kHero);
        CHECK(rect.startCell.cellX == 2 && rect.startCell.cellY == 3);
        CHECK(rect.currentCell.cellX == 2 && rect.currentCell.cellY == 3);
        CHECK(!rect.outlineOnly);
        CHECK(rect.replacement.has_value() && rect.replacement->tileId == "tile-1");
        CHECK(rect.previewChanges.size() == 1);
    }

    // -- missing component / locked layer / missing tileset / Play running
    // rejected, mirroring BeginTilePaintStrokeIntent's own guards ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};   // kHero has no tilemap
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
    }
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneDef& scene = doc.scenes.at(kSceneA);
        SceneLayerDef locked;
        locked.id = "locked-layer";
        locked.name = "Locked";
        locked.locked = true;
        scene.layers.push_back(locked);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        // Hand-authored directly on the fixture (not via AddTilemapComponentCommand,
        // which now itself rejects adding a component on a locked layer) - this
        // test's target is BeginTileRectangleIntent's own lock guard.
        scene.instances.front().tilemap = tm;
        scene.instances.front().layerId = "locked-layer";
        EditorCoordinator c{doc};
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
    }
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.playCurrentScene().ok);
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
    }

    // -- Shape captured at Begin: flipping the toggle mid-drag never changes
    // the in-progress rectangle --------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetRectangleShapeModeIntent{true});   // Outline mode active
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle->outlineOnly);
        c.apply(SetRectangleShapeModeIntent{false});   // flip mid-drag
        CHECK(c.apply(UpdateTileRectangleIntent{{3, 3}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle->outlineOnly);   // unchanged: still Outline
        CHECK(c.state().tilemapEditor.pendingRectangle->previewChanges.size() == 12);   // 4x4 perimeter
    }

    // -- Tile reselected mid-drag never changes the in-progress rectangle ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        c.apply(SelectPaintTileIntent{"tile-2"});   // reselect mid-drag: must not affect this rectangle
        CHECK(c.apply(UpdateTileRectangleIntent{{1, 1}}).ok);
        CHECK(c.apply(CommitTileRectangleIntent{}).ok);
        const TilemapComponent& tm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        CHECK(readTilemapCell(tm, {0, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {1, 1})->tileId == "tile-1");
    }

    // -- Selecting a different entity mid-drag cancels the pending rectangle,
    // never redirects or auto-commits it: a gesture belongs to whatever was
    // selected when it started, and a selection change always discards it
    // (Scene View Selection & Tool Context slice) -------------------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef other;
        other.id = 99;
        other.objectTypeId = "Other";
        other.instanceName = "Other";
        doc.scenes.at(kSceneA).instances.push_back(other);
        EditorCoordinator c{doc};
        sliceTilesOne(c);
        TilemapComponent tmA; tmA.tilesetAssetId = "tiles-1";
        TilemapComponent tmB; tmB.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tmA}).ok);
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, 99, tmB}).ok);

        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);   // starts on kHero
        CHECK(c.state().tilemapEditor.pendingRectangle.has_value());
        c.apply(SelectEntityIntent{99});   // user clicks the other entity mid-drag
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());   // discarded, not redirected
        CHECK(!c.apply(UpdateTileRectangleIntent{{1, 1}}).ok);          // nothing left to update
        CHECK(!c.apply(CommitTileRectangleIntent{}).ok);                // nothing left to commit

        const TilemapComponent& heroTm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        const TilemapComponent& otherTm = *c.document().findInstanceInScene(kSceneA, 99)->tilemap;
        CHECK(!readTilemapCell(heroTm, {0, 0}).has_value());     // abandoned rectangle never painted
        CHECK(!readTilemapCell(otherTm, {0, 0}).has_value());    // never touched
    }

    // ======================= UpdateTileRectangleIntent =======================
    // -- Preview cache only recomputes when currentCell actually changes -------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.apply(UpdateTileRectangleIntent{{2, 2}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle->previewChanges.size() == 9);
        const EditorOperationResult again = c.apply(UpdateTileRectangleIntent{{2, 2}});   // same cell
        CHECK(again.ok);
        CHECK(again.invalidation == EditorInvalidation::None);
        CHECK(c.state().tilemapEditor.pendingRectangle->previewChanges.size() == 9);
    }

    // ======================= CommitTileRectangleIntent =======================
    // -- Over-limit rectangle rejected at commit: no Command, pending cleared,
    // and the preview never blanked mid-drag (kept the last valid box) ---------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.apply(UpdateTileRectangleIntent{{299, 299}}).ok);   // 90000 cells: over the limit
        CHECK(c.state().tilemapEditor.pendingRectangle->previewChanges.size() == 1);   // unchanged from Begin
        const std::size_t before = c.undoSize();
        CHECK(!c.apply(CommitTileRectangleIntent{}).ok);
        CHECK(c.undoSize() == before);
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
    }

    // -- No-op commit (rectangle over an already-uniform region): no Command,
    // pending still cleared -------------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> seed;
        seed.push_back(TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, seed}).ok);

        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);   // single cell, already tile-1
        const std::size_t before = c.undoSize();
        CHECK(c.apply(CommitTileRectangleIntent{}).ok);
        CHECK(c.undoSize() == before);   // no-op: no Command
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
    }

    // ======================= CancelTileRectangleIntent =======================
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle.has_value());
        CHECK(c.apply(CancelTileRectangleIntent{}).ok);
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
    }

    // ======================= Mutual exclusion =======================
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {1, 1}}).ok);   // stroke already pending
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
        c.apply(EndTilePaintStrokeIntent{});

        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {1, 1}}).ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());   // rectangle pending: stroke rejected
    }

    // -- Switching tool mid-drag cancels whichever operation is pending --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.apply(SetActiveToolIntent{EditorTool::Rectangle}).ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.state().activeTool == EditorTool::Rectangle);

        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.apply(SetActiveToolIntent{EditorTool::Fill}).ok);
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
    }

    // ======================= reconcileWorkspace =======================
    // -- Entity deleted mid-drag: pendingRectangle is cleared, and so is
    // selectedTileId (there is no longer a target tileset it could belong to
    // - Scene View Selection & Tool Context slice); rectangleOutlineMode is a
    // general drawing preference, not tied to any tileset, so it survives ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetRectangleShapeModeIntent{true});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle.has_value());
        CHECK(c.execute(DeleteEntityCommand{kSceneA, kHero}).ok);
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
        CHECK(!c.state().tilemapEditor.selectedTileId.has_value());
        CHECK(c.state().tilemapEditor.rectangleOutlineMode);
    }

    // ======================= FillTilemapIntent =======================
    // -- success: floods a bounded interior region, exactly one Command --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> walls;
        for (int x = 0; x <= 4; ++x) {
            walls.push_back(TilemapCellChange{{x, 0}, std::nullopt, TilemapCellValue{"tile-2", TileTransformFlags::None}});
            walls.push_back(TilemapCellChange{{x, 4}, std::nullopt, TilemapCellValue{"tile-2", TileTransformFlags::None}});
        }
        for (int y = 1; y <= 3; ++y) {
            walls.push_back(TilemapCellChange{{0, y}, std::nullopt, TilemapCellValue{"tile-2", TileTransformFlags::None}});
            walls.push_back(TilemapCellChange{{4, y}, std::nullopt, TilemapCellValue{"tile-2", TileTransformFlags::None}});
        }
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, walls}).ok);

        c.apply(SelectPaintTileIntent{"tile-1"});
        const std::size_t before = c.undoSize();
        CHECK(c.apply(FillTilemapIntent{kSceneA, kHero, {2, 2}}).ok);
        CHECK(c.undoSize() == before + 1);
        const TilemapComponent& tm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        CHECK(readTilemapCell(tm, {2, 2})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {1, 1})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {3, 3})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {0, 0})->tileId == "tile-2");   // wall untouched
    }

    // -- No tile selected rejected ------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(!c.apply(FillTilemapIntent{kSceneA, kHero, {0, 0}}).ok);
    }

    // -- Rejected while Play is running -------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.playCurrentScene().ok);
        CHECK(!c.apply(FillTilemapIntent{kSceneA, kHero, {0, 0}}).ok);
    }

    // -- Open region: cancelled, no Command, no dirty -----------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(!c.apply(FillTilemapIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
    }

    // -- True no-op (target already the selected tile): no Command, no dirty ---
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> seed;
        seed.push_back(TilemapCellChange{{2, 2}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, seed}).ok);
        c.apply(SelectPaintTileIntent{"tile-1"});
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(c.apply(FillTilemapIntent{kSceneA, kHero, {2, 2}}).ok);   // target already tile-1: true no-op
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
    }

    // ======================= SetRectangleShapeModeIntent =======================
    {
        EditorCoordinator c{makeSpriteDoc()};
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(!c.state().tilemapEditor.rectangleOutlineMode);
        CHECK(c.apply(SetRectangleShapeModeIntent{true}).ok);
        CHECK(c.state().tilemapEditor.rectangleOutlineMode);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
        CHECK(!c.document().isDirty());
    }
}

// Slice 8 - TilemapComponent participates in Play, reusing the same
// SceneFrameSnapshot/SceneView path as Edit (no second runtime renderer).
static void runTilemapPlaySessionTests() {
    // -- Play rejected: tilemap references a missing tileset ------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        TilemapComponent tm;
        tm.tilesetAssetId = "no-such-tileset";
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!c.playProject().ok);
        CHECK(!c.isPlaying());
    }

    // -- Play rejected: tileset references a missing image asset ---------------
    {
        ProjectDoc doc = makeSpriteDoc();
        TilesetAsset orphan;
        orphan.assetId = "tiles-orphan";
        orphan.imageAssetId = "missing-image";
        doc.tilesets.push_back(orphan);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-orphan";
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!c.playProject().ok);
    }

    // -- Play materializes a RuntimeTilemap identical to tilemapRenderCells,
    // and registers the tileset's image in the asset catalog ------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{-2, 3}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {100.f, 50.f}}).ok);

        CHECK(c.playCurrentScene().ok);
        const RuntimeEntity* runtime = c.playSession()->findEntity(kHero);
        CHECK(runtime != nullptr);
        CHECK(runtime->tilemap.has_value());
        CHECK(runtime->tilemap->imageAssetId == "img-hero");   // tiles-1's underlying image
        CHECK(c.playSession()->assets().imageAssets.count("img-hero") == 1);

        const TilemapComponent& authored = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        const TilesetAsset* tileset = c.document().findTilesetAsset("tiles-1");
        const std::optional<std::vector<TilemapResolvedCell>> expected =
            resolveTilemapCellsStrict(authored, *tileset);
        CHECK(expected.has_value());
        CHECK(runtime->tilemap->cellSize.x == authored.cellSize.x);
        CHECK(runtime->tilemap->cellSize.y == authored.cellSize.y);
        CHECK(runtime->tilemap->cells.size() == expected->size());
        CHECK(runtime->tilemap->cells.size() == 2);
        for (std::size_t i = 0; i < expected->size(); ++i) {
            CHECK(runtime->tilemap->cells[i].cellX == (*expected)[i].cellX);
            CHECK(runtime->tilemap->cells[i].cellY == (*expected)[i].cellY);
            CHECK(runtime->tilemap->cells[i].sourceRect.x == (*expected)[i].source.x);
            CHECK(runtime->tilemap->cells[i].sourceRect.y == (*expected)[i].source.y);
        }

        // The snapshot's destination is computed from the entity's *current*
        // transform (100, 50) at collect time, not baked in at materialize.
        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        const auto tilemapIt = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(tilemapIt != snap.tilemaps.end());
        const std::vector<SceneFrameTilemapCell> expectedFrame =
            tilemapRenderCells(authored, *tileset, {100.f, 50.f});
        CHECK(tilemapIt->cells.size() == expectedFrame.size());
        for (std::size_t i = 0; i < expectedFrame.size(); ++i) {
            CHECK(tilemapIt->cells[i].destination.x == expectedFrame[i].destination.x);
            CHECK(tilemapIt->cells[i].destination.y == expectedFrame[i].destination.y);
        }
    }

    // -- A tilemap follows its owning entity if it moves during Play
    // (LinearMover): destination is recomputed from the current transform
    // every frame, never cached at materialize time --------------------------
    {
        EditorCoordinator c{makeInheritedDoc()};   // gives kHero a real "Hero" object type
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{1, 1}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.execute(AddLinearMoverCommand{"Hero"}).ok);
        CHECK(c.execute(SetLinearMoverDirectionCommand{"Hero", Vec2{1.f, 0.f}}).ok);
        CHECK(c.execute(SetLinearMoverSpeedCommand{"Hero", 100.f}).ok);
        const uint64_t revision = c.document().revision();

        CHECK(c.playCurrentScene().ok);
        const SceneFrameSnapshot before = collectSceneFrameSnapshot(*c.playSession());
        const auto beforeIt = std::find_if(before.tilemaps.begin(), before.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(beforeIt != before.tilemaps.end());
        CHECK(beforeIt->cells.size() == 1);
        const float startX = beforeIt->cells[0].destination.x;
        const float startY = beforeIt->cells[0].destination.y;

        c.advanceRuntime(0.5f);   // 0.5s * 100 units/s = 50 units along +X

        const SceneFrameSnapshot after = collectSceneFrameSnapshot(*c.playSession());
        const auto afterIt = std::find_if(after.tilemaps.begin(), after.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(afterIt != after.tilemaps.end());
        CHECK(afterIt->cells.size() == 1);
        CHECK(afterIt->cells[0].destination.x == startX + 50.f);
        CHECK(afterIt->cells[0].destination.y == startY);
        CHECK(c.document().revision() == revision);   // simulation never touches ProjectDocument
    }

    // -- An unknown TileId in a chunk rejects Play atomically, rather than
    // silently starting with content quietly missing. Normal authoring
    // commands already reject this state before it can be saved, so this is a
    // hand-crafted ProjectDoc, mirroring the file's own "dangling asset"
    // test pattern (e.g. the missing-tileset/-image cases above) ------------
    {
        ProjectDoc doc = makeSpriteDoc();
        TileDefinition validTile;
        validTile.id = "tile-1";
        validTile.width = 32;
        validTile.height = 32;
        doc.tilesets.front().tiles.push_back(validTile);

        TilemapComponent tm;
        tm.tilesetAssetId = doc.tilesets.front().assetId;
        tm.chunkSize = 16;
        TilemapChunk chunk;
        chunk.chunkX = 0;
        chunk.chunkY = 0;
        chunk.cells.assign(16 * 16, std::nullopt);
        chunk.cells[0] = TilemapCellValue{"no-such-tile", TileTransformFlags::None};   // unresolvable
        tm.chunks.push_back(chunk);
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;

        EditorCoordinator c{doc};
        CHECK(!c.playProject().ok);
        CHECK(!c.isPlaying());
    }

    // -- An entity with a TilemapComponent but no painted cells materializes
    // an empty RuntimeTilemap, not a missing one - the image is still known --
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.playCurrentScene().ok);
        const RuntimeEntity* runtime = c.playSession()->findEntity(kHero);
        CHECK(runtime->tilemap.has_value());
        CHECK(runtime->tilemap->cells.empty());
    }

    // -- Two tilemaps on different tilesets sharing one image asset load it
    // exactly once (same PlayAssetCatalogSnapshot dedup as sprites) -----------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef other;
        other.id = 99;
        other.objectTypeId = "Other";
        other.instanceName = "Other";
        doc.scenes.at(kSceneA).instances.push_back(other);
        EditorCoordinator c{doc};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const std::vector<TileDefinition> tiles1 = tilesForSlicing(64, 64, slicing);
        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-1", slicing, tiles1}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-2", "Second", "img-hero", slicing}).ok);
        const std::vector<TileDefinition> tiles2 = tilesForSlicing(64, 64, slicing);
        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-2", slicing, tiles2}).ok);

        TilemapComponent tmA; tmA.tilesetAssetId = "tiles-1";
        TilemapComponent tmB; tmB.tilesetAssetId = "tiles-2";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tmA}).ok);
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, 99, tmB}).ok);

        CHECK(c.playCurrentScene().ok);
        CHECK(c.playSession()->assets().imageAssets.size() == 1);   // one image, two tilesets
        CHECK(c.playSession()->assets().imageAssets.count("img-hero") == 1);
    }

    // -- Layer order determines Play's RENDER order only, never simulation
    // order: RuntimeScene::entities stays structural (creation order),
    // RuntimeScene::renderOrder carries the visual back-to-front order -------
    {
        EditorCoordinator c{ProjectDoc{}};
        const SceneId sceneId = "layered-scene";
        CHECK(c.execute(CreateSceneCommand{sceneId, "Layered"}).ok);   // creates "layer-1" (default)
        CHECK(c.execute(AddSceneLayerCommand{sceneId, "bg", "Background", 0}).ok);
        CHECK(c.execute(AddSceneLayerCommand{sceneId, "fg", "Foreground", 2}).ok);
        // Created in structural order 501, 502, 503, but placed on layers so
        // the VISUAL order is the reverse: 503 (bg), 501 (default), 502 (fg).
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
            sceneId, 501, "type-mid", "Mid", "Mid", Vec2{}, ""}).ok);          // default layer
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
            sceneId, 502, "type-fg", "Fg", "Fg", Vec2{}, "fg"}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
            sceneId, 503, "type-bg", "Bg", "Bg", Vec2{}, "bg"}).ok);

        CHECK(c.apply(SelectSceneIntent{sceneId}).ok);
        CHECK(c.playCurrentScene().ok);

        // Simulation container: untouched structural order.
        const std::vector<RuntimeEntity>& entities = c.playSession()->entities();
        CHECK(entities.size() == 3);
        CHECK(entities[0].id == 501);
        CHECK(entities[1].id == 502);
        CHECK(entities[2].id == 503);

        // Render order: back-to-front visual order, as indices into entities.
        const std::vector<std::size_t>& renderOrder = c.playSession()->scene().renderOrder;
        CHECK(renderOrder.size() == 3);
        CHECK(entities[renderOrder[0]].id == 503);   // Background first
        CHECK(entities[renderOrder[1]].id == 501);   // default layer, in the middle
        CHECK(entities[renderOrder[2]].id == 502);   // Foreground last

        // The Play snapshot (what actually gets drawn) follows render order.
        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        CHECK(snap.entities.size() == 3);
        CHECK(snap.entities[0].entityId == 503);
        CHECK(snap.entities[1].entityId == 501);
        CHECK(snap.entities[2].entityId == 502);
    }

    // -- collectSceneFrameSnapshot(PlaySession&): a painted tilemap appears in
    // snapshot.tilemaps and in frame.entities (SceneView render order) without
    // relying on the generic editor placeholder ----------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{1, 1}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.playCurrentScene().ok);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        const auto tilemapIt = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(tilemapIt != snap.tilemaps.end());
        CHECK(tilemapIt->cells.size() == 1);
        const auto entityIt = std::find_if(snap.entities.begin(), snap.entities.end(),
            [](const SceneFrameEntity& e) { return e.entityId == kHero; });
        CHECK(entityIt != snap.entities.end());
        // Play snapshot must be pickable at the painted cell (same draw-order
        // contract SceneView uses - regression for tilemaps invisible in Play).
        const SceneFrameRect& dest = tilemapIt->cells[0].destination;
        CHECK(pickEntityAt(snap, Vec2{dest.x + dest.width * 0.5f, dest.y + dest.height * 0.5f})
              == kHero);
    }

    // -- An empty (unpainted) tilemap is fully invisible in Play - unlike Edit,
    // which deliberately shows the generic placeholder for it -----------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.playCurrentScene().ok);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        CHECK(std::none_of(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; }));
        CHECK(std::none_of(snap.entities.begin(), snap.entities.end(),
            [](const SceneFrameEntity& e) { return e.entityId == kHero; }));
        CHECK(std::none_of(snap.sprites.begin(), snap.sprites.end(),
            [](const SceneFrameSprite& s) { return s.entityId == kHero; }));

        // Edit, by contrast, still shows the placeholder for the same document.
        const SceneFrameSnapshot editSnap =
            collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        CHECK(std::any_of(editSnap.entities.begin(), editSnap.entities.end(),
            [](const SceneFrameEntity& e) { return e.entityId == kHero; }));
    }

    // -- Stop leaves ProjectDocument, dirty, revision and Undo untouched -------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        const uint64_t revision = c.document().revision();
        const std::size_t undoBefore = c.undoSize();

        CHECK(c.playCurrentScene().ok);
        CHECK(c.stopPlaying().ok);
        CHECK(!c.isPlaying());
        // Revision is unchanged by Play/Stop - the paint above is the only
        // authoring mutation, so the document is dirty because of that, not
        // because of Play.
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == undoBefore);
        CHECK(readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {0, 0})
                  ->tileId == "tile-1");
    }

    // -- Negative coordinates and a rectangular (non-square) cellSize ----------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {16.f, 48.f};
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{-3, -2}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {0.f, 0.f}}).ok);

        CHECK(c.playCurrentScene().ok);
        const RuntimeEntity* runtime = c.playSession()->findEntity(kHero);
        CHECK(runtime->tilemap.has_value());
        CHECK(runtime->tilemap->cells.size() == 1);
        // Runtime storage is local cell coordinates, not baked world rects.
        CHECK(runtime->tilemap->cells[0].cellX == -3);
        CHECK(runtime->tilemap->cells[0].cellY == -2);
        CHECK(runtime->tilemap->cellSize.x == 16.f);
        CHECK(runtime->tilemap->cellSize.y == 48.f);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        const auto tilemapIt = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(tilemapIt != snap.tilemaps.end());
        CHECK(tilemapIt->cells.size() == 1);
        CHECK(tilemapIt->cells[0].destination.x == -3.f * 16.f);
        CHECK(tilemapIt->cells[0].destination.y == -2.f * 48.f);
        CHECK(tilemapIt->cells[0].destination.width == 16.f);
        CHECK(tilemapIt->cells[0].destination.height == 48.f);
    }

    // -- Save/load round-trip parity (the "export parity" criterion reinterpreted
    // for this repo, which has no separate exported-game runtime): a fresh
    // PlaySession materialized from a reloaded ProjectDocument produces the
    // identical snapshot as the one materialized before saving ----------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{14, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{-3, -3}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::FlipX}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {40.f, 10.f}}).ok);
        CHECK(c.playCurrentScene().ok);
        const SceneFrameSnapshot directPlay = collectSceneFrameSnapshot(*c.playSession());
        CHECK(c.stopPlaying().ok);

        const std::filesystem::path path = testTempDir() / "tilemap-play-roundtrip.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.playCurrentScene().ok);
        const SceneFrameSnapshot reloadedPlay = collectSceneFrameSnapshot(*reloaded.playSession());

        CHECK(directPlay.tilemaps.size() == 1);
        CHECK(directPlay.tilemaps.size() == reloadedPlay.tilemaps.size());
        CHECK(directPlay.tilemaps[0].imageAssetId == reloadedPlay.tilemaps[0].imageAssetId);
        CHECK(directPlay.tilemaps[0].cells.size() == reloadedPlay.tilemaps[0].cells.size());
        for (std::size_t i = 0; i < directPlay.tilemaps[0].cells.size(); ++i) {
            CHECK(directPlay.tilemaps[0].cells[i].destination.x
                  == reloadedPlay.tilemaps[0].cells[i].destination.x);
            CHECK(directPlay.tilemaps[0].cells[i].destination.y
                  == reloadedPlay.tilemaps[0].cells[i].destination.y);
            CHECK(directPlay.tilemaps[0].cells[i].source.x == reloadedPlay.tilemaps[0].cells[i].source.x);
            CHECK(directPlay.tilemaps[0].cells[i].source.y == reloadedPlay.tilemaps[0].cells[i].source.y);
        }
    }
}

static void runGeneratedSfxTests() {
    using artcade::sfx::SfxRecipe;

    EditorCoordinator coordinator{makeDoc()};
    SfxRecipe recipe;
    recipe.durationSeconds = 0.18f;
    recipe.primaryVoice.pitch.startHz = 880.f;
    recipe.primaryVoice.pitch.endHz = 220.f;

    CHECK(coordinator.execute(
        CreateGeneratedSfxCommand{"sfx-jump", "Jump", recipe}).ok);
    CHECK(coordinator.document().hasGeneratedSfx("sfx-jump"));
    CHECK(coordinator.document().data().generatedSfx.size() == 1);
    CHECK(coordinator.document().isDirty());

    const SerializeResult serialized = ProjectSerializer::serialize(coordinator.document());
    CHECK(serialized.ok);
    CHECK(serialized.value.find("\"generatedSfx\"") != std::string::npos);
    CHECK(serialized.value.find("\"generatorVersion\": 2") != std::string::npos);
    const DeserializeResult decoded = ProjectSerializer::deserialize(serialized.value);
    CHECK(decoded.ok);
        CHECK(decoded.value.data().formatVersion == 7);
    CHECK(ProjectValidator::validate(decoded.value).ok);
    CHECK(generatedSfxRecipesEqual(
        decoded.value.findGeneratedSfx("sfx-jump")->recipe, recipe));

    CHECK(!coordinator.execute(
        CreateGeneratedSfxCommand{"sfx-jump-2", "jump", recipe}).ok);
    SfxRecipe invalid = recipe;
    invalid.durationSeconds = -1.f;
    CHECK(!coordinator.execute(
        CreateGeneratedSfxCommand{"bad", "Bad", invalid}).ok);

    AudioAssetDef output;
    output.assetId = "generated-audio-sfx-jump";
    output.name = "Jump";
    output.sourcePath = "assets/audio/generated/sfx-jump.wav";
    output.loadMode = AudioLoadMode::StaticSound;
    CHECK(coordinator.execute(RegisterGeneratedSfxOutputCommand{
        "sfx-jump", recipe, output}).ok);
    CHECK(coordinator.document().hasAudioAsset(output.assetId));
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);
    CHECK(ProjectValidator::validate(coordinator.document()).ok);

    SfxRecipe changed = recipe;
    changed.randomSeed += 1u;
    CHECK(coordinator.execute(
        UpdateGeneratedSfxRecipeCommand{"sfx-jump", changed}).ok);
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId.empty());
    CHECK(coordinator.document().hasAudioAsset(output.assetId));
    const std::uint64_t staleRevision = coordinator.document().revision();
    CHECK(!coordinator.execute(RegisterGeneratedSfxOutputCommand{
        "sfx-jump", recipe, output}).ok);
    CHECK(coordinator.document().revision() == staleRevision);

    CHECK(coordinator.undo().ok); // recipe update
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId.empty());

    CHECK(coordinator.execute(RegisterGeneratedSfxOutputCommand{
        "sfx-jump", changed, output}).ok);
    CHECK(coordinator.execute(RemoveAudioAssetCommand{output.assetId}).ok);
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId.empty());
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);

    CHECK(coordinator.execute(RemoveGeneratedSfxCommand{"sfx-jump"}).ok);
    CHECK(!coordinator.document().hasGeneratedSfx("sfx-jump"));
    CHECK(coordinator.document().hasAudioAsset(output.assetId));
    CHECK(!coordinator.execute(
        CreateGeneratedSfxCommand{"sfx-jump", "Replacement", changed}).ok);
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().hasGeneratedSfx("sfx-jump"));

    ProjectDoc unsupported = makeDoc();
    artcade::sfx::GeneratedSfxDef badVersion;
    badVersion.id = "future";
    badVersion.name = "Future";
    badVersion.recipe.generatorVersion = 999u;
    unsupported.generatedSfx.push_back(std::move(badVersion));
    CHECK(!ProjectValidator::validate(ProjectDocument{std::move(unsupported)}).ok);
}

static void runGeneratedSfxMacrosTests() {
    using artcade::sfx::SfxRecipe;
    using artcade::sfx::SfxSynthesizer;

    // Pitch: slider extremes map exactly to the configured Hz bounds.
    CHECK(std::abs(sfxPitchMacroToHz(0.f) - kSfxPitchMinHz) < 0.01f);
    CHECK(std::abs(sfxPitchMacroToHz(1.f) - kSfxPitchMaxHz) < 0.5f);
    CHECK(std::abs(sfxHzToPitchMacro(kSfxPitchMinHz) - 0.f) < 0.001f);
    CHECK(std::abs(sfxHzToPitchMacro(kSfxPitchMaxHz) - 1.f) < 0.001f);

    // Pitch display value (Hz, for the companion text input) round-trips
    // through setSfxMacroFieldFromDisplay independently of slider space.
    {
        SfxRecipe recipe;
        CHECK(setSfxMacroFieldFromDisplay(recipe, "pitch", 880.f));
        CHECK(std::abs(sfxMacroDisplayValue(recipe, "pitch") - 880.f) < 0.5f);
        CHECK(std::abs(recipe.primaryVoice.pitch.startHz - 880.f) < 0.5f);
        // Every other macro's display value equals its slider-space value.
        CHECK(setSfxMacroFieldFromDisplay(recipe, "duration", 0.3f));
        CHECK(std::abs(sfxMacroDisplayValue(recipe, "duration") - 0.3f) < 1.0e-4f);
    }

    // Sweep: positive and negative semitones move endHz by the expected
    // ratio, and it's always positive by construction — never the additive-
    // Hz-delta bug that could drive endHz negative when startHz shrinks.
    {
        SfxRecipe recipe;
        CHECK(setSfxMacroField(recipe, "pitch", 0.5f));
        const float startHz = recipe.primaryVoice.pitch.startHz;
        CHECK(setSfxMacroField(recipe, "pitchSweep", 12.f)); // +1 octave
        CHECK(recipe.primaryVoice.pitch.endHz > 0.f);
        CHECK(std::abs(recipe.primaryVoice.pitch.endHz - startHz * 2.f) < 1.f);
        CHECK(std::abs(sfxMacroValue(recipe, "pitchSweep") - 12.f) < 0.05f);

        CHECK(setSfxMacroField(recipe, "pitchSweep", -12.f)); // -1 octave
        CHECK(recipe.primaryVoice.pitch.endHz > 0.f);
        CHECK(std::abs(recipe.primaryVoice.pitch.endHz - startHz * 0.5f) < 1.f);

        // Lowering Pitch after a wide negative sweep never drives endHz
        // negative or non-finite.
        CHECK(setSfxMacroField(recipe, "pitchSweep", -24.f));
        CHECK(setSfxMacroField(recipe, "pitch", 0.f)); // startHz -> kSfxPitchMinHz
        CHECK(recipe.primaryVoice.pitch.endHz > 0.f);
        CHECK(std::isfinite(recipe.primaryVoice.pitch.endHz));
    }

    // Every macro, swept across its full domain from a preset base recipe,
    // always leaves the recipe passing SfxSynthesizer::validate().
    for (const SfxMacro& macro : kSfxMacros) {
        SfxRecipe recipe = artcade::sfx::presets::coin();
        if (std::string(macro.id) == "pitchSweep") {
            // Pin startHz to a safe mid-range value first so extreme sweeps
            // (x4 / x0.25) stay well under Nyquist regardless of what the
            // preset's own startHz happened to be.
            CHECK(setSfxMacroField(recipe, "pitch", 0.5f));
        }
        for (int i = 0; i <= 8; ++i) {
            const float t = static_cast<float>(i) / 8.f;
            const float value = macro.sliderMin + t * (macro.sliderMax - macro.sliderMin);
            CHECK(setSfxMacroField(recipe, macro.id, value));
            CHECK(SfxSynthesizer::validate(recipe).ok());
        }
    }

    // Duration shrinking below the current Attack+Decay+Release rescales all
    // three proportionally instead of producing an invalid recipe.
    {
        SfxRecipe recipe;
        recipe.amplitude.attackSeconds = 0.3f;
        recipe.amplitude.decaySeconds = 0.3f;
        recipe.amplitude.releaseSeconds = 0.3f; // occupied = 0.9s
        CHECK(setSfxMacroField(recipe, "duration", 0.1f));
        const float occupied = recipe.amplitude.attackSeconds + recipe.amplitude.decaySeconds
                              + recipe.amplitude.releaseSeconds;
        CHECK(occupied <= recipe.durationSeconds + 1.0e-4f);
        CHECK(SfxSynthesizer::validate(recipe).ok());
        // Proportions preserved: all three were equal, so they still are.
        CHECK(std::abs(recipe.amplitude.attackSeconds - recipe.amplitude.decaySeconds) < 1.0e-5f);
    }

    // Noise: 0 disables, >0 enables and scales gain.
    {
        SfxRecipe recipe;
        recipe.noise.enabled = true;
        recipe.noise.gain = 0.5f;
        CHECK(setSfxMacroField(recipe, "noise", 0.f));
        CHECK(!recipe.noise.enabled);
        CHECK(setSfxMacroField(recipe, "noise", 40.f));
        CHECK(recipe.noise.enabled);
        CHECK(std::abs(recipe.noise.gain - 0.4f) < 1.0e-4f);
        CHECK(std::abs(sfxMacroValue(recipe, "noise") - 40.f) < 0.01f);
    }

    // Crunch: 0 disables the Bit Crusher (bits untouched); >0 enables it and
    // quantizes 16 -> 4 bits as intensity rises.
    {
        SfxRecipe recipe;
        recipe.bitCrusher.enabled = true;
        recipe.bitCrusher.quantizationBits = 10;
        CHECK(setSfxMacroField(recipe, "crunch", 0.f));
        CHECK(!recipe.bitCrusher.enabled);
        CHECK(recipe.bitCrusher.quantizationBits == 10); // untouched

        CHECK(setSfxMacroField(recipe, "crunch", 1.0e-4f));
        CHECK(recipe.bitCrusher.enabled);
        CHECK(recipe.bitCrusher.quantizationBits == 16);

        CHECK(setSfxMacroField(recipe, "crunch", 100.f));
        CHECK(recipe.bitCrusher.enabled);
        CHECK(recipe.bitCrusher.quantizationBits == 4);

        CHECK(setSfxMacroField(recipe, "crunch", 50.f));
        CHECK(recipe.bitCrusher.quantizationBits == 10);
    }

    // Unknown macro id leaves the recipe untouched and reports failure.
    {
        SfxRecipe recipe;
        const SfxRecipe before = recipe;
        CHECK(!setSfxMacroField(recipe, "does-not-exist", 1.f));
        CHECK(generatedSfxRecipesEqual(recipe, before));
    }

    // Randomize: every output stays valid; Noise/Crunch land at exactly-off
    // noticeably more often than a uniform [0,100] draw would (sanity check
    // on the bias, not an exact probability assertion).
    {
        std::mt19937 rng(1234);
        int noiseOffCount = 0;
        int crunchOffCount = 0;
        constexpr int kRuns = 200;
        for (int i = 0; i < kRuns; ++i) {
            SfxRecipe recipe = artcade::sfx::presets::coin();
            randomizeSfxMacros(recipe, rng);
            CHECK(SfxSynthesizer::validate(recipe).ok());
            if (!recipe.noise.enabled) ++noiseOffCount;
            if (!recipe.bitCrusher.enabled) ++crunchOffCount;
        }
        CHECK(noiseOffCount > kRuns / 3);
        CHECK(crunchOffCount > kRuns / 3);
    }
}

static void runScriptAssetAndFileServiceTests() {
    const std::filesystem::path root = testTempDir() / "script-project";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    CHECK(!ec);
    ProjectScriptFileService files{root};
    CHECK(!ProjectScriptFileService{"relative-project"}
        .readScript("scripts/player.lua").ok);

    // File service: one confined UTF-8/LF/atomic boundary.
    const std::string withBomAndWindowsNewlines =
        std::string("\xef\xbb\xbf") + "return {\r\n}\r";
    const auto written = files.writeScriptAtomically(
        "scripts/player.lua", withBomAndWindowsNewlines);
    CHECK(written.ok);
    const auto loaded = files.readScript("scripts/player.lua");
    CHECK(loaded.ok);
    CHECK(loaded.value == "return {\n}\n");
    const auto firstFingerprint = files.fingerprint("scripts/player.lua");
    CHECK(firstFingerprint.ok);
    CHECK(firstFingerprint.value.size == loaded.value.size());
    CHECK(firstFingerprint.value.hash == scriptSourceStamp(loaded.value).hash);
    CHECK(!files.writeScriptAtomically("../escape.lua", "return {}\n").ok);
    CHECK(!files.writeScriptAtomically("scripts/not-lua.txt", "return {}\n").ok);
    CHECK(!files.readImportSource("relative.lua").ok);

    const std::filesystem::path scriptOutside = root.parent_path() / "script-outside";
    std::filesystem::create_directories(scriptOutside, ec);
    CHECK(!ec);
    writeTextFile(scriptOutside / "secret.lua", "return {}\n");
    const std::filesystem::path scriptEscapeLink = root / "scripts" / "external-link";
    ec.clear();
    std::filesystem::create_directory_symlink(scriptOutside, scriptEscapeLink, ec);
    if (!ec) {
        CHECK(!files.readScript("scripts/external-link/secret.lua").ok);
        CHECK(!files.writeScriptAtomically(
            "scripts/external-link/overwrite.lua", "return {}\n").ok);
    } else {
        CHECK(!std::filesystem::exists(scriptEscapeLink));
    }

    // A rejected write never replaces the previous valid file.
    std::string invalidUtf8 = "return ";
    invalidUtf8.push_back(static_cast<char>(0xff));
    CHECK(!files.writeScriptAtomically("scripts/player.lua", invalidUtf8).ok);
    CHECK(files.readScript("scripts/player.lua").value == "return {\n}\n");

    // Saving invalid source is intentionally allowed; diagnostics compile the
    // exact bytes but never execute the produced chunk or open Lua libraries.
    const std::string invalidLua = "return {\n  on_start = function(\n}\n";
    CHECK(files.writeScriptAtomically("scripts/invalid.lua", invalidLua).ok);
    const auto syntaxErrors = validateScriptSyntax(
        "invalid", "scripts/invalid.lua", invalidLua);
    CHECK(syntaxErrors.size() == 1);
    CHECK(syntaxErrors.front().code == "SCRIPT_SYNTAX");
    CHECK(syntaxErrors.front().scriptAssetId == "invalid");
    CHECK(syntaxErrors.front().path == "scripts/invalid.lua");
    CHECK(syntaxErrors.front().line > 0);
    CHECK(syntaxErrors.front().column == 1);
    CHECK(validateScriptSyntax("safe", "scripts/safe.lua",
        "error('this chunk must never execute')\nreturn {}\n").empty());

    ProjectDoc gateDoc = makeDoc();
    gateDoc.scriptAssets = {
        ScriptAssetDef{"safe", "Safe", "scripts/player.lua"},
        ScriptAssetDef{"invalid", "Invalid", "scripts/invalid.lua"},
        ScriptAssetDef{"unreadable", "Unreadable", "scripts/missing.lua"},
    };
    const ProjectDocument gateDocument{std::move(gateDoc)};
    CHECK(validateReferencedScriptSyntax(
        gateDocument, files, {"safe"}).empty());
    const auto strictDiagnostics = validateReferencedScriptSyntax(
        gateDocument, files,
        {"safe", "invalid", "unreadable", "unknown", "invalid"});
    CHECK(strictDiagnostics.size() == 3);
    CHECK(strictDiagnostics[0].code == "SCRIPT_SYNTAX");
    CHECK(strictDiagnostics[1].code == "SCRIPT_SOURCE_UNREADABLE");
    CHECK(strictDiagnostics[2].code == "SCRIPT_REFERENCE_UNKNOWN");

    const std::string savedRuntimeSource =
        "artcade.require_api_version(1)\nreturn { on_update = function(ctx, dt) end }\n";
    CHECK(files.writeScriptAtomically("scripts/player.lua", savedRuntimeSource).ok);
    SavedScriptSnapshotResult savedSnapshot = snapshotReferencedScripts(
        gateDocument, files, {"safe"});
    CHECK(savedSnapshot.ok());
    CHECK(savedSnapshot.programs.size() == 1);
    CHECK(savedSnapshot.programs.front().source == savedRuntimeSource);
    CHECK(files.writeScriptAtomically("scripts/player.lua", "return false\n").ok);
    Scripts::ScriptRuntime snapshotRuntime;
    std::string snapshotError;
    CHECK(snapshotRuntime.validateProgram(savedSnapshot.programs.front(), &snapshotError));
    CHECK(!files.writeScriptAtomically(
        "scripts/player.lua", std::string("return\0{}", 9)).ok);
    CHECK(files.readScript("scripts/player.lua").value == "return false\n");

    // Metadata Command authority and exact history.
    EditorCoordinator coordinator{makeDoc()};
    CHECK(coordinator.execute(AddScriptAssetCommand{
        "player", "Player", "scripts/player.lua"}).ok);
    CHECK(coordinator.document().hasScriptAsset("player"));
    CHECK(!coordinator.execute(AddScriptAssetCommand{
        "player", "Duplicate", "scripts/other.lua"}).ok);
    CHECK(!coordinator.execute(AddScriptAssetCommand{
        "other", "Other", "SCRIPTS/PLAYER.LUA"}).ok);
    CHECK(!coordinator.execute(AddScriptAssetCommand{
        "bad", "Bad", "../bad.lua"}).ok);
    CHECK(coordinator.execute(RenameScriptAssetCommand{"player", "Player Logic"}).ok);
    CHECK(coordinator.document().findScriptAsset("player")->name == "Player Logic");
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().findScriptAsset("player")->name == "Player");
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().findScriptAsset("player")->name == "Player Logic");

    const auto serialized = ProjectSerializer::serialize(coordinator.document());
    CHECK(serialized.ok);
    CHECK(serialized.value.find("\"formatVersion\": 7") != std::string::npos);
    CHECK(serialized.value.find("\"scriptAssets\"") != std::string::npos);
    const auto decoded = ProjectSerializer::deserialize(serialized.value);
    CHECK(decoded.ok);
    CHECK(ProjectValidator::validate(ProjectDocument{decoded.value.data()}).ok);
    CHECK(decoded.value.findScriptAsset("player") != nullptr);
    CHECK(decoded.value.findScriptAsset("player")->sourcePath == "scripts/player.lua");

    // Slice 4: Script attachments are ordered Object-Type data. All mutations
    // cross one Command boundary and Undo restores the exact optional component.
    ProjectDoc attachmentDoc = makeDoc();
    EntityDef heroType;
    heroType.className = "Hero";
    heroType.name = "Hero";
    attachmentDoc.objectTypes.emplace("Hero", std::move(heroType));
    EditorCoordinator attachments{std::move(attachmentDoc)};
    CHECK(attachments.execute(AddScriptAssetCommand{
        "player", "Player", "scripts/player.lua"}).ok);
    CHECK(attachments.execute(AddScriptAssetCommand{
        "camera", "Camera", "scripts/camera.lua"}).ok);
    CHECK(attachments.execute(AddScriptAttachmentCommand{
        "Hero", ScriptAttachmentDef{"script-1", "player", true}, 0}).ok);
    CHECK(attachments.execute(AddScriptAttachmentCommand{
        "Hero", ScriptAttachmentDef{"script-2", "camera", true}, 1}).ok);
    const std::uint64_t beforeRejectedAttachment = attachments.document().revision();
    CHECK(!attachments.execute(AddScriptAttachmentCommand{
        "Hero", ScriptAttachmentDef{"script-1", "camera", true}, 2}).ok);
    CHECK(attachments.document().revision() == beforeRejectedAttachment);
    CHECK(attachments.execute(MoveScriptAttachmentCommand{
        "Hero", "script-1", 1}).ok);
    CHECK(attachments.document().findObjectType("Hero")->scripts->attachments[0].id
          == "script-2");
    CHECK(attachments.execute(SetScriptAttachmentEnabledCommand{
        "Hero", "script-1", false}).ok);
    CHECK(attachments.document().referencedScriptAssetIds(true)
          == std::vector<AssetId>{"camera"});
    CHECK(attachments.document().referencedScriptAssetIds(false)
          == (std::vector<AssetId>{"camera", "player"}));
    const std::uint64_t beforeGuardedDelete = attachments.document().revision();
    CHECK(!attachments.execute(RemoveScriptAssetCommand{"player"}).ok);
    CHECK(attachments.document().revision() == beforeGuardedDelete);
    CHECK(attachments.execute(RemoveScriptAttachmentCommand{
        "Hero", "script-2"}).ok);
    CHECK(attachments.document().findObjectType("Hero")->scripts->attachments.size() == 1);
    CHECK(attachments.undo().ok);
    CHECK(attachments.document().findObjectType("Hero")->scripts->attachments[0].id
          == "script-2");
    CHECK(attachments.redo().ok);
    CHECK(attachments.undo().ok);

    const auto attachmentJson = ProjectSerializer::serialize(attachments.document());
    CHECK(attachmentJson.ok);
    CHECK(attachmentJson.value.find("\"scripts\"") != std::string::npos);
    const auto attachmentRoundTrip = ProjectSerializer::deserialize(attachmentJson.value);
    CHECK(attachmentRoundTrip.ok);
    CHECK(ProjectValidator::validate(attachmentRoundTrip.value).ok);
    const auto& roundTripAttachments = attachmentRoundTrip.value
        .findObjectType("Hero")->scripts->attachments;
    CHECK(roundTripAttachments.size() == 2);
    CHECK(roundTripAttachments[0].id == "script-2");
    CHECK(roundTripAttachments[1].id == "script-1");
    CHECK(!roundTripAttachments[1].enabled);

    ProjectDoc invalidAttachments = attachmentRoundTrip.value.data();
    invalidAttachments.objectTypes.at("Hero").scripts->attachments[1].id = "script-2";
    CHECK(!ProjectValidator::validate(ProjectDocument{invalidAttachments}).ok);
    invalidAttachments = attachmentRoundTrip.value.data();
    invalidAttachments.objectTypes.at("Hero").scripts->attachments[1].scriptAssetId = "missing";
    CHECK(!ProjectValidator::validate(ProjectDocument{invalidAttachments}).ok);
    invalidAttachments = attachmentRoundTrip.value.data();
    invalidAttachments.objectTypes.at("Hero").scripts->attachments.clear();
    CHECK(!ProjectValidator::validate(ProjectDocument{invalidAttachments}).ok);

    ProjectDoc duplicatePath = makeDoc();
    duplicatePath.scriptAssets = {
        ScriptAssetDef{"one", "One", "scripts/a.lua"},
        ScriptAssetDef{"two", "Two", "SCRIPTS/A.LUA"},
    };
    CHECK(!ProjectValidator::validate(ProjectDocument{duplicatePath}).ok);
    duplicatePath.scriptAssets = {ScriptAssetDef{"one", "One", "scripts/a.txt"}};
    CHECK(!ProjectValidator::validate(ProjectDocument{duplicatePath}).ok);

    // Create/import is file first + one metadata Command, with normalized text.
    EditorCoordinator workflows{makeDoc()};
    const auto created = createScriptAsset(workflows, root);
    CHECK(created.ok);
    CHECK(workflows.document().hasScriptAsset(created.assetId));
    const ScriptAssetDef* createdAsset = workflows.document().findScriptAsset(created.assetId);
    CHECK(createdAsset != nullptr);
    CHECK(createdAsset && files.readScript(createdAsset->sourcePath).ok);
    const std::string createdSourcePath = createdAsset ? createdAsset->sourcePath : std::string{};
    CHECK(workflows.undo().ok);
    CHECK(!workflows.document().hasScriptAsset(created.assetId));
    CHECK(!createdSourcePath.empty() && std::filesystem::exists(root / createdSourcePath));
    CHECK(workflows.redo().ok);

    const std::filesystem::path importSource = root / "external.lua";
    writeTextFile(importSource, withBomAndWindowsNewlines);
    const auto imported = importScriptAsset(workflows, root, importSource);
    CHECK(imported.ok);
    const ScriptAssetDef* importedAsset = workflows.document().findScriptAsset(imported.assetId);
    CHECK(importedAsset != nullptr);
    CHECK(importedAsset && files.readScript(importedAsset->sourcePath).value == "return {\n}\n");

    // Script workspace is a separate, non-document authority with local
    // history. Play temporarily reveals Scene and restores the exact workspace.
    ProjectDoc workspaceDoc = makeDoc();
    workspaceDoc.scriptAssets.push_back(
        ScriptAssetDef{"player", "Player.lua", "scripts/player.lua"});
    EditorCoordinator workspace{std::move(workspaceDoc)};
    const std::uint64_t documentRevision = workspace.document().revision();
    CHECK(workspace.apply(OpenScriptBufferIntent{
        "player", "return {\n}\n"}).ok);
    CHECK(workspace.state().centerWorkspaceMode == CenterWorkspaceMode::Script);
    CHECK(workspace.state().scriptEditor.activeAssetId == std::optional<AssetId>{"player"});
    CHECK(!workspace.state().scriptEditor.anyDirty());
    workspace.consumeInvalidations();
    const EditorOperationResult textEdit = workspace.apply(EditScriptBufferIntent{
        "player", "return { on_start = function(ctx) end }\n", 16});
    CHECK(textEdit.ok);
    CHECK(textEdit.invalidation == EditorInvalidation::Toolbar);
    CHECK(!has(textEdit.invalidation, EditorInvalidation::ScriptEditor));
    CHECK(workspace.state().scriptEditor.active()->dirty());
    CHECK(workspace.document().revision() == documentRevision);
    CHECK(!workspace.document().isDirty());
    CHECK(workspace.apply(UndoScriptBufferIntent{}).ok);
    CHECK(workspace.state().scriptEditor.active()->text == "return {\n}\n");
    CHECK(!workspace.state().scriptEditor.active()->dirty());
    CHECK(workspace.apply(RedoScriptBufferIntent{}).ok);
    CHECK(workspace.state().scriptEditor.active()->dirty());
    CHECK(workspace.apply(MarkScriptBufferSavedIntent{
        "player", workspace.state().scriptEditor.active()->text}).ok);
    CHECK(!workspace.state().scriptEditor.active()->dirty());

    ScriptDiagnostic playerDiagnostic = syntaxErrors.front();
    playerDiagnostic.scriptAssetId = "player";
    playerDiagnostic.path = "scripts/player.lua";
    const std::uint64_t editedRevision = workspace.state().scriptEditor.active()->revision;
    CHECK(workspace.apply(SetScriptDiagnosticsIntent{
        "player", editedRevision, {playerDiagnostic}}).ok);
    CHECK(!workspace.state().scriptEditor.active()->validationPending);
    CHECK(workspace.state().scriptEditor.active()->diagnostics.size() == 1);
    CHECK(!workspace.consoleLog().empty());
    CHECK(workspace.consoleLog().back().scriptSource.has_value());
    CHECK(workspace.consoleLog().back().scriptSource->scriptAssetId == "player");
    // A newer edit invalidates the derived result. A late result for the old
    // revision is dropped without console noise or state replacement.
    CHECK(workspace.apply(EditScriptBufferIntent{
        "player", "return {}\n-- newer\n", 10}).ok);
    const std::size_t consoleBeforeStale = workspace.consoleLog().size();
    CHECK(workspace.apply(SetScriptDiagnosticsIntent{
        "player", editedRevision, {playerDiagnostic}}).ok);
    CHECK(workspace.state().scriptEditor.active()->validationPending);
    CHECK(workspace.state().scriptEditor.active()->diagnostics.empty());
    CHECK(workspace.consoleLog().size() == consoleBeforeStale);
    CHECK(workspace.apply(MarkScriptBufferSavedIntent{
        "player", workspace.state().scriptEditor.active()->text}).ok);
    CHECK(workspace.apply(SetScriptCursorIntent{"player", 9, 36.f}).ok);

    CHECK(workspace.playProject().ok);
    CHECK(workspace.state().centerWorkspaceMode == CenterWorkspaceMode::Scene);
    CHECK(workspace.stopPlaying().ok);
    CHECK(workspace.state().centerWorkspaceMode == CenterWorkspaceMode::Script);
    CHECK(workspace.state().scriptEditor.activeAssetId == std::optional<AssetId>{"player"});
    CHECK(workspace.state().scriptEditor.active()->cursorOffset == 9);
    CHECK(workspace.state().scriptEditor.active()->scrollTop == 36.f);

    const std::string utf8CursorText = std::string("\xc3\xa0") + "b\nc";
    CHECK(clampScriptCursorOffset(utf8CursorText, 1) == 0);
    const ScriptCursorPosition utf8Cursor = scriptCursorPosition(utf8CursorText, 3);
    CHECK(utf8Cursor.line == 1);
    CHECK(utf8Cursor.column == 3);

    // Explicit navigation during Play disarms the automatic return.
    CHECK(workspace.playProject().ok);
    CHECK(workspace.apply(SwitchCenterWorkspaceIntent{CenterWorkspaceMode::Scene}).ok);
    CHECK(workspace.stopPlaying().ok);
    CHECK(workspace.state().centerWorkspaceMode == CenterWorkspaceMode::Scene);

    // Slice 7: only a successful Save of a linked source makes the active
    // immutable Play snapshot stale. Restart materializes first, replaces
    // atomically, and re-arms Scene -> Script navigation without touching the
    // authoring document or the editor buffer's cursor/scroll state.
    ProjectDoc applyDoc = makeDoc();
    applyDoc.scriptAssets = {
        ScriptAssetDef{"player", "Player.lua", "scripts/player.lua"},
        ScriptAssetDef{"notes", "Notes.lua", "scripts/notes.lua"},
    };
    applyDoc.objectTypes.at("Hero").scripts = ScriptComponent{{
        ScriptAttachmentDef{"player-script", "player", true}}};
    EditorCoordinator applyWorkflow{std::move(applyDoc)};
    const std::string sourceV1 = R"lua(artcade.require_api_version(1)
return { on_start = function(ctx) ctx.self:set_position(1, 2) end }
)lua";
    const std::string sourceV2 = R"lua(artcade.require_api_version(1)
return { on_start = function(ctx) ctx.self:set_position(7, 8) end }
)lua";
    CHECK(applyWorkflow.apply(OpenScriptBufferIntent{"player", sourceV1}).ok);
    CHECK(applyWorkflow.apply(OpenScriptBufferIntent{"notes", "return {}\n"}).ok);
    CHECK(applyWorkflow.apply(ActivateScriptBufferIntent{"player"}).ok);
    CHECK(applyWorkflow.apply(SetScriptCursorIntent{"player", 12, 42.f}).ok);
    const std::uint64_t applyDocumentRevision = applyWorkflow.document().revision();
    const Scripts::ScriptProgram programV1{
        "player", "scripts/player.lua", sourceV1};
    const Scripts::ScriptProgram programV2{
        "player", "scripts/player.lua", sourceV2};
    CHECK(applyWorkflow.playProject({programV1}).ok);
    CHECK(!applyWorkflow.scriptRestartRequired());
    const RuntimeEntity* initiallyApplied = applyWorkflow.playSession()
        ? applyWorkflow.playSession()->findEntity(kHero) : nullptr;
    CHECK(initiallyApplied && initiallyApplied->transform.position.x == 1.f);

    // Saving an open but unlinked asset never invalidates the runtime.
    CHECK(applyWorkflow.apply(ActivateScriptBufferIntent{"notes"}).ok);
    CHECK(applyWorkflow.apply(EditScriptBufferIntent{
        "notes", "return {}\n-- saved notes\n", 10}).ok);
    CHECK(applyWorkflow.apply(MarkScriptBufferSavedIntent{
        "notes", "return {}\n-- saved notes\n"}).ok);
    CHECK(!applyWorkflow.scriptRestartRequired());

    CHECK(applyWorkflow.apply(ActivateScriptBufferIntent{"player"}).ok);
    CHECK(applyWorkflow.apply(EditScriptBufferIntent{"player", sourceV2, 24}).ok);
    CHECK(!applyWorkflow.scriptRestartRequired()); // dirty buffer is not applied source
    CHECK(applyWorkflow.apply(MarkScriptBufferSavedIntent{"player", sourceV2}).ok);
    CHECK(applyWorkflow.scriptRestartRequired());
    const RuntimeEntity* stillApplied = applyWorkflow.playSession()
        ? applyWorkflow.playSession()->findEntity(kHero) : nullptr;
    CHECK(stillApplied && stillApplied->transform.position.x == 1.f);
    // Re-saving the exact applied bytes removes the derived divergence.
    CHECK(applyWorkflow.apply(EditScriptBufferIntent{"player", sourceV1, 18}).ok);
    CHECK(applyWorkflow.apply(MarkScriptBufferSavedIntent{"player", sourceV1}).ok);
    CHECK(!applyWorkflow.scriptRestartRequired());
    CHECK(applyWorkflow.apply(EditScriptBufferIntent{"player", sourceV2, 24}).ok);
    CHECK(applyWorkflow.apply(MarkScriptBufferSavedIntent{"player", sourceV2}).ok);
    CHECK(applyWorkflow.apply(SetScriptCursorIntent{"player", 24, 42.f}).ok);

    const Scripts::ScriptProgram invalidReplacement{
        "player", "scripts/player.lua",
        "artcade.require_api_version(1)\nreturn { on_start = function( }\n"};
    CHECK(!applyWorkflow.restartPlaying({invalidReplacement}).ok);
    CHECK(applyWorkflow.isPlaying());
    CHECK(applyWorkflow.scriptRestartRequired());
    CHECK(applyWorkflow.state().centerWorkspaceMode == CenterWorkspaceMode::Script);
    const RuntimeEntity* preservedAfterFailure = applyWorkflow.playSession()
        ? applyWorkflow.playSession()->findEntity(kHero) : nullptr;
    CHECK(preservedAfterFailure && preservedAfterFailure->transform.position.x == 1.f);

    CHECK(applyWorkflow.restartPlaying({programV2}).ok);
    CHECK(!applyWorkflow.scriptRestartRequired());
    CHECK(applyWorkflow.state().centerWorkspaceMode == CenterWorkspaceMode::Scene);
    const RuntimeEntity* restartedEntity = applyWorkflow.playSession()
        ? applyWorkflow.playSession()->findEntity(kHero) : nullptr;
    CHECK(restartedEntity && restartedEntity->transform.position.x == 7.f);
    CHECK(applyWorkflow.document().revision() == applyDocumentRevision);
    CHECK(!applyWorkflow.document().isDirty());
    CHECK(applyWorkflow.stopPlaying().ok);
    CHECK(!applyWorkflow.scriptRestartRequired());
    CHECK(applyWorkflow.state().centerWorkspaceMode == CenterWorkspaceMode::Script);
    CHECK(applyWorkflow.state().scriptEditor.activeAssetId
          == std::optional<AssetId>{"player"});
    CHECK(applyWorkflow.state().scriptEditor.active()->cursorOffset == 24);
    CHECK(applyWorkflow.state().scriptEditor.active()->scrollTop == 42.f);

    // Restart Current Scene is pinned to its original launch target even when
    // the editor's active scene changes while Play is running.
    const std::string sourceV3 = R"lua(artcade.require_api_version(1)
return { on_start = function(ctx) ctx.self:set_position(9, 10) end }
)lua";
    const Scripts::ScriptProgram programV3{
        "player", "scripts/player.lua", sourceV3};
    CHECK(applyWorkflow.apply(SelectSceneIntent{kSceneB}).ok);
    CHECK(applyWorkflow.playCurrentScene({programV2}).ok);
    CHECK(applyWorkflow.playSession()
          && applyWorkflow.playSession()->sceneId() == kSceneB);
    CHECK(applyWorkflow.apply(ActivateScriptBufferIntent{"player"}).ok);
    CHECK(applyWorkflow.apply(EditScriptBufferIntent{"player", sourceV3, 30}).ok);
    CHECK(applyWorkflow.apply(MarkScriptBufferSavedIntent{"player", sourceV3}).ok);
    CHECK(applyWorkflow.apply(SelectSceneIntent{kSceneA}).ok);
    CHECK(applyWorkflow.restartPlaying({programV3}).ok);
    CHECK(applyWorkflow.playSession()
          && applyWorkflow.playSession()->sceneId() == kSceneB);
    CHECK(applyWorkflow.stopPlaying().ok);
    CHECK(applyWorkflow.state().centerWorkspaceMode == CenterWorkspaceMode::Script);

    // Metadata removal reconciles dangling workspace buffers in the same
    // command path (the UI guard runs before this for dirty buffers).
    CHECK(workspace.execute(RemoveScriptAssetCommand{"player"}).ok);
    CHECK(workspace.state().scriptEditor.buffers.empty());
    CHECK(!workspace.state().scriptEditor.activeAssetId.has_value());
}

static void runManualScriptRuntimeTests() {
    using Scripts::ScriptProgram;
    using Scripts::ScriptRuntime;
    using Scripts::ScriptRuntimeLimits;

    struct GameplayHost final : IGameplayRuntimeHost {
        bool visible = true;
        Vec2 position{};
        bool grounded = true;
        float moveAxis = 0.f;
        bool jumpRequested = false;
        bool destroyRequested = false;
        AssetId animationAsset;
        std::string animationClip;
        float animationSpeed = 1.f;
        bool animationPlaying = false;
        AssetId audioAsset;
        float audioVolume = 0.f;

        bool setVisible(EntityId, bool value) override { visible = value; return true; }
        bool setPosition(EntityId, Vec2 value) override { position = value; return true; }
        bool translate(EntityId, Vec2 delta) override {
            position.x += delta.x; position.y += delta.y; return true;
        }
        bool isGrounded(EntityId) override { return grounded; }
        bool requestPlatformerMove(EntityId, float axis) override {
            moveAxis = axis; return true;
        }
        bool requestPlatformerJump(EntityId) override { jumpRequested = true; return true; }
        bool isObjectType(EntityId, const ObjectTypeId&) override { return false; }
        bool requestDestroy(EntityId) override { destroyRequested = true; return true; }
        bool playAnimationClip(EntityId, const AssetId& asset,
                               const std::string& clip) override {
            animationAsset = asset; animationClip = clip; animationPlaying = true; return true;
        }
        bool stopAnimation(EntityId) override { animationPlaying = false; return true; }
        bool setAnimationPlaybackSpeed(EntityId, float speed) override {
            animationSpeed = speed; return true;
        }
        bool playSound(EntityId, const AssetId& asset, float volume) override {
            audioAsset = asset; audioVolume = volume; return true;
        }
    };

    const ScriptProgram sandboxed{
        "sandboxed", "scripts/sandboxed.lua", R"lua(
artcade.require_api_version(1)
assert(io == nil and os == nil and debug == nil and package == nil)
assert(dofile == nil and loadfile == nil and load == nil)
assert(print == nil and warn == nil)
assert(Raylib == nil)
local m = require("math")
assert(m.abs(-3) == 3)
local ok = pcall(function() require("filesystem") end)
assert(not ok)
return {
  on_start = function(ctx) assert(ctx.entity_id == 42 and ctx.self ~= nil) end,
  on_update = function(ctx, dt) assert(ctx.entity_id == 42 and dt > 0) end
}
)lua"};

    ScriptRuntime validator;
    std::string error;
    CHECK(validator.validateProgram(sandboxed, &error));
    CHECK(!validator.validateProgram(ScriptProgram{
        "missing-api", "scripts/missing-api.lua", "return {}\n"}, &error));
    CHECK(error.find("require_api_version") != std::string::npos);
    CHECK(!validator.validateProgram(ScriptProgram{
        "future-api", "scripts/future-api.lua",
        "artcade.require_api_version(2)\nreturn {}\n"}, &error));
    CHECK(error.find("Unsupported ArtCade script API version") != std::string::npos);
    CHECK(!validator.validateProgram(ScriptProgram{
        "bad-return", "scripts/bad-return.lua",
        "artcade.require_api_version(1)\nreturn 7\n"}, &error));
    CHECK(error.find("must return a table") != std::string::npos);
    CHECK(!validator.validateProgram(ScriptProgram{
        "bad-callback", "scripts/bad-callback.lua",
        "artcade.require_api_version(1)\nreturn { on_update = true }\n"}, &error));
    CHECK(error.find("on_update") != std::string::npos);
    CHECK(!validator.validateProgram(ScriptProgram{
        "bad-event-callback", "scripts/bad-event-callback.lua",
        "artcade.require_api_version(1)\nreturn { on_collision_enter = 4 }\n"}, &error));
    CHECK(error.find("on_collision_enter") != std::string::npos);

    ScriptRuntimeLimits tight;
    tight.maxSourceBytes = 16;
    ScriptRuntime sourceLimited{tight};
    CHECK(!sourceLimited.validateProgram(sandboxed, &error));
    CHECK(error.find("size limit") != std::string::npos);

    ScriptRuntimeLimits instructionLimits;
    instructionLimits.maxInstructionsPerCallback = 5000;
    ScriptRuntime instructionLimited{instructionLimits};
    CHECK(!instructionLimited.validateProgram(ScriptProgram{
        "load-loop", "scripts/load-loop.lua",
        "artcade.require_api_version(1)\nwhile true do end\nreturn {}\n"}, &error));
    CHECK(error.find("instruction budget") != std::string::npos);

    ScriptRuntimeLimits depthLimits;
    depthLimits.maxCallDepth = 8;
    ScriptRuntime depthLimited{depthLimits};
    const ScriptProgram recursive{
        "recursive", "scripts/recursive.lua", R"lua(
artcade.require_api_version(1)
local function recurse(n)
  if n > 0 then recurse(n - 1); local keep_non_tail = n end
end
return { on_update = function(ctx, dt) recurse(100) end }
)lua"};
    CHECK(depthLimited.install(recursive, 12, "recursive", &error));
    depthLimited.dispatchStart();
    depthLimited.update(1.f / 60.f);
    CHECK(depthLimited.activeScopeCount() == 0);
    CHECK(depthLimited.diagnostics().front().message.find("call depth")
          != std::string::npos);

    ScriptRuntimeLimits memoryLimits;
    memoryLimits.maxMemoryBytesPerScope = 512u * 1024u;
    ScriptRuntime memoryLimited{memoryLimits};
    CHECK(!memoryLimited.validateProgram(ScriptProgram{
        "memory", "scripts/memory.lua",
        "artcade.require_api_version(1)\nlocal x = string.rep('x', 1048576)\nreturn {}\n"},
        &error));
    CHECK(error.find("memory limit") != std::string::npos);
    CHECK(!memoryLimited.validateProgram(ScriptProgram{
        "caught-memory", "scripts/caught-memory.lua", R"lua(
artcade.require_api_version(1)
pcall(function() local x = string.rep("x", 1048576) end)
return {}
)lua"}, &error));
    CHECK(error.find("memory limit") != std::string::npos);

    // Each install owns an isolated VM. Both counters reach 1 on frame one and
    // both independently fail on frame two; a shared VM would fail one scope on
    // the first frame and violate the per-(instance,attachment) contract.
    const ScriptProgram isolated{
        "isolated", "scripts/isolated.lua", R"lua(
artcade.require_api_version(1)
local updates = 0
return {
  on_update = function(ctx, dt)
    updates = updates + 1
    if updates > 1 then error("scope counter reached two") end
  end
}
)lua"};
    ScriptRuntime scopes{instructionLimits};
    CHECK(scopes.install(isolated, 10, "script-1", &error));
    CHECK(scopes.install(isolated, 11, "script-2", &error));
    CHECK(scopes.scopeCount() == 2);
    scopes.update(1.f / 60.f);
    CHECK(scopes.activeScopeCount() == 2);
    CHECK(scopes.diagnostics().empty());
    scopes.dispatchStart();
    scopes.dispatchStart();
    CHECK(!scopes.install(isolated, 12, "late", &error));
    CHECK(error.find("after on_start") != std::string::npos);
    scopes.update(1.f / 60.f);
    CHECK(scopes.activeScopeCount() == 2);
    scopes.update(1.f / 60.f);
    CHECK(scopes.activeScopeCount() == 0);
    CHECK(scopes.diagnostics().size() == 2);
    CHECK(scopes.diagnostics()[0].owner == 10);
    CHECK(scopes.diagnostics()[1].owner == 11);

    const ScriptProgram runawayUpdate{
        "runaway", "scripts/runaway.lua", R"lua(
artcade.require_api_version(1)
return { on_update = function(ctx, dt) while true do end end }
)lua"};
    ScriptRuntime isolatedFailure{instructionLimits};
    CHECK(isolatedFailure.install(sandboxed, 42, "safe", &error));
    CHECK(isolatedFailure.install(runawayUpdate, 43, "runaway", &error));
    isolatedFailure.dispatchStart();
    isolatedFailure.update(1.f / 60.f);
    CHECK(isolatedFailure.activeScopeCount() == 1);
    CHECK(isolatedFailure.diagnostics().size() == 1);
    CHECK(isolatedFailure.diagnostics().front().attachmentId == "runaway");
    isolatedFailure.cancelOwner(42);
    CHECK(isolatedFailure.activeScopeCount() == 0);

    const ScriptProgram caughtBudget{
        "caught-budget", "scripts/caught-budget.lua", R"lua(
artcade.require_api_version(1)
return { on_update = function(ctx, dt)
  pcall(function() while true do end end)
end }
)lua"};
    ScriptRuntime caughtLimit{instructionLimits};
    CHECK(caughtLimit.install(caughtBudget, 44, "caught", &error));
    caughtLimit.dispatchStart();
    caughtLimit.update(1.f / 60.f);
    CHECK(caughtLimit.activeScopeCount() == 0);
    CHECK(caughtLimit.diagnostics().front().message.find("instruction budget")
          != std::string::npos);

    const ScriptProgram startFailure{
        "start-failure", "scripts/start-failure.lua", R"lua(
artcade.require_api_version(1)
return { on_start = function(ctx) error("start failed") end }
)lua"};
    const ScriptProgram startOnce{
        "start-once", "scripts/start-once.lua", R"lua(
artcade.require_api_version(1)
local starts = 0
return { on_start = function(ctx)
  starts = starts + 1
  if starts > 1 then error("on_start repeated") end
end }
)lua"};
    ScriptRuntime isolatedStartFailure;
    CHECK(isolatedStartFailure.install(startFailure, 45, "bad-start", &error));
    CHECK(isolatedStartFailure.install(startOnce, 46, "good-start", &error));
    isolatedStartFailure.dispatchStart();
    isolatedStartFailure.dispatchStart();
    CHECK(isolatedStartFailure.activeScopeCount() == 1);
    CHECK(isolatedStartFailure.diagnostics().size() == 1);
    CHECK(isolatedStartFailure.diagnostics().front().phase
          == Scripts::ScriptRuntimePhase::Start);
    CHECK(isolatedStartFailure.diagnostics().front().callback == "on_start");
    CHECK(isolatedStartFailure.diagnostics().front().line > 0);

    const ScriptProgram gameplay{
        "gameplay", "scripts/gameplay.lua", R"lua(
artcade.require_api_version(1)
return {
  on_key_pressed = function(ctx, key)
    assert(key == "A" and ctx.input:is_key_pressed("A"))
    assert(ctx.input:is_key_down("A"))
    ctx.self:translate(1, 0)
    ctx.platformer:move(0.5)
    ctx.platformer:jump()
    ctx.audio:play("audio-hit", 0.4)
  end,
  on_key_released = function(ctx, key)
    assert(key == "B" and ctx.input:is_key_released("B"))
    ctx.self:translate(0, 1)
  end,
  on_key_held = function(ctx, key)
    assert(key == "A" and ctx.input:is_key_down("A"))
    ctx.self:translate(2, 0)
  end,
  on_update = function(ctx, dt)
    assert(dt > 0 and ctx.input:is_key_down("A"))
    assert(ctx.platformer:is_grounded())
    ctx.self:set_position(7, 8)
    ctx.animation:play("hero.anim", "run")
    ctx.animation:set_speed(1.5)
    ctx.animation:stop()
  end,
  on_collision_enter = function(ctx, other)
    assert(other == 99 and ctx.event.other == 99)
    ctx.self:set_visible(false)
    ctx.self:destroy()
  end,
  on_collision_exit = function(ctx, other)
    assert(other == 99 and ctx.event.other == 99)
    ctx.self:set_visible(true)
  end
}
)lua"};
    GameplayHost gameplayHost;
    ScriptRuntime gameplayRuntime{gameplayHost};
    CHECK(gameplayRuntime.install(gameplay, 42, "gameplay-attachment", &error));
    gameplayRuntime.dispatchStart();
    gameplayRuntime.dispatchInput(Scripts::ScriptInputSnapshot{
        {LogicKey::A, LogicKey::A}, {LogicKey::B}, {LogicKey::A, LogicKey::A}});
    CHECK(gameplayHost.position.x == 3.f && gameplayHost.position.y == 1.f);
    CHECK(gameplayHost.moveAxis == 0.5f);
    CHECK(gameplayHost.jumpRequested);
    CHECK(gameplayHost.audioAsset == "audio-hit");
    CHECK(gameplayHost.audioVolume == 0.4f);
    gameplayRuntime.update(1.f / 60.f);
    CHECK(gameplayHost.position.x == 7.f && gameplayHost.position.y == 8.f);
    CHECK(gameplayHost.animationAsset == "hero.anim");
    CHECK(gameplayHost.animationClip == "run");
    CHECK(gameplayHost.animationSpeed == 1.5f);
    CHECK(!gameplayHost.animationPlaying);
    gameplayRuntime.dispatchCollisionEnter(42, 99);
    CHECK(!gameplayHost.visible);
    CHECK(gameplayHost.destroyRequested);
    // The runtime boundary, not ScriptRuntime, applies deferred destruction.
    // Therefore every callback in the immutable collision snapshot may finish.
    gameplayRuntime.dispatchCollisionExit(42, 99);
    CHECK(gameplayHost.visible);
    CHECK(gameplayRuntime.diagnostics().empty());

    const ScriptProgram invalidKey{
        "invalid-key", "scripts/invalid-key.lua", R"lua(
artcade.require_api_version(1)
return { on_key_pressed = function(ctx, key)
  ctx.input:is_key_down("NotAKey")
end }
)lua"};
    ScriptRuntime invalidKeyRuntime{gameplayHost};
    CHECK(invalidKeyRuntime.install(invalidKey, 42, "invalid-key", &error));
    invalidKeyRuntime.dispatchStart();
    invalidKeyRuntime.dispatchInput(Scripts::ScriptInputSnapshot{{LogicKey::A}, {}, {}});
    CHECK(invalidKeyRuntime.activeScopeCount() == 0);
    CHECK(invalidKeyRuntime.diagnostics().front().callback == "on_key_pressed");
    CHECK(invalidKeyRuntime.diagnostics().front().message.find("Unknown ArtCade input key")
          != std::string::npos);

    const ScriptProgram invalidVolume{
        "invalid-volume", "scripts/invalid-volume.lua", R"lua(
artcade.require_api_version(1)
return { on_start = function(ctx)
  ctx.audio:play("audio-hit", 1.5)
end }
)lua"};
    ScriptRuntime invalidVolumeRuntime{gameplayHost};
    CHECK(invalidVolumeRuntime.install(invalidVolume, 42, "invalid-volume", &error));
    invalidVolumeRuntime.dispatchStart();
    CHECK(invalidVolumeRuntime.activeScopeCount() == 0);
    CHECK(invalidVolumeRuntime.diagnostics().front().callback == "on_start");
    CHECK(invalidVolumeRuntime.diagnostics().front().message.find("ctx.audio:play failed")
          != std::string::npos);

    // PlaySession consumes only an exact immutable saved-source snapshot and
    // materializes enabled attachments in Object-Type order.
    ProjectDoc doc = makeDoc();
    EntityDef hero;
    hero.className = "Hero";
    hero.name = "Hero";
    LogicBoardDef board;
    board.id = "hero-board";
    LogicRuleDef startRule;
    startRule.id = "logic-before-script";
    startRule.trigger = Logic::makeDefaultBlock(Logic::kOnStart, Logic::BlockKind::Trigger);
    LogicBlockDef setPosition =
        Logic::makeDefaultBlock(Logic::kSetPosition, Logic::BlockKind::Action);
    for (LogicPropertyDef& property : setPosition.properties)
        if (property.key == "position") property.value = Vec2{1.f, 2.f};
    startRule.actions.push_back(std::move(setPosition));
    board.rules.push_back(std::move(startRule));
    hero.logicBoard = std::move(board);
    hero.scripts = ScriptComponent{{ScriptAttachmentDef{"script-1", "visibility", true}}};
    doc.objectTypes.emplace("Hero", std::move(hero));
    doc.scriptAssets.push_back(
        ScriptAssetDef{"visibility", "Visibility", "scripts/visibility.lua"});
    const ScriptProgram visibility{
        "visibility", "scripts/visibility.lua", R"lua(
artcade.require_api_version(1)
return { on_start = function(ctx)
  ctx.self:set_visible(false)
  ctx.self:translate(6, 6)
end,
on_key_pressed = function(ctx, key)
  if key == "A" then ctx.self:translate(1, 0) end
end,
on_update = function(ctx, dt)
  if ctx.input:is_key_down("A") then ctx.self:translate(0, 1) end
end }
)lua"};
    const ProjectDocument document{std::move(doc)};
    CHECK(!PlaySession::startProject(document, &error).has_value());
    auto play = PlaySession::startProject(document, {visibility}, &error);
    CHECK(play.has_value());
    CHECK(play && play->scriptRuntime());
    CHECK(play && play->scriptRuntime()->scopeCount() == 1);
    CHECK(play && !play->scene().entities.front().visible);
    CHECK(play && play->scene().entities.front().transform.position.x == 7.f);
    CHECK(play && play->scene().entities.front().transform.position.y == 8.f);
    if (play) {
        RuntimeInputSnapshot input;
        input.pressedLogicKeys = {LogicKey::A};
        input.heldLogicKeys = {LogicKey::A};
        play->update(input, 1.f / 60.f);
        CHECK(play->scene().entities.front().transform.position.x == 8.f);
        CHECK(play->scene().entities.front().transform.position.y == 9.f);
        CHECK(play->drainScriptDiagnostics().empty());
    }
    CHECK(!PlaySession::startProject(document, {ScriptProgram{
        "sandboxed", "scripts/sandboxed.lua",
        "artcade.require_api_version(1)\nreturn false\n"}}, &error).has_value());

    // Collision destruction is applied only after every attachment for the
    // immutable edge snapshot ran: the second scope still queues its audio.
    ProjectDoc collisionDoc = makeDoc();
    EntityDef collisionHero;
    collisionHero.className = "Hero";
    collisionHero.name = "Hero";
    collisionHero.boxCollider2D = BoxCollider2DComponent{
        {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    collisionHero.scripts = ScriptComponent{{
        ScriptAttachmentDef{"destroy", "destroy-on-contact", true},
        ScriptAttachmentDef{"sound", "sound-on-contact", true}}};
    collisionDoc.objectTypes.emplace("Hero", std::move(collisionHero));
    EntityDef enemy;
    enemy.className = "Enemy";
    enemy.name = "Enemy";
    enemy.boxCollider2D = BoxCollider2DComponent{
        {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Trigger};
    collisionDoc.objectTypes.emplace("Enemy", std::move(enemy));
    SceneInstanceDef enemyInstance;
    enemyInstance.id = 99;
    enemyInstance.objectTypeId = "Enemy";
    enemyInstance.instanceName = "Enemy";
    enemyInstance.transform.position = {10.f, 20.f};
    collisionDoc.scenes.at(kSceneA).instances.push_back(enemyInstance);
    collisionDoc.scriptAssets.push_back(
        ScriptAssetDef{"destroy-on-contact", "Destroy", "scripts/destroy.lua"});
    collisionDoc.scriptAssets.push_back(
        ScriptAssetDef{"sound-on-contact", "Sound", "scripts/sound.lua"});
    collisionDoc.audioAssets.push_back(
        AudioAssetDef{"hit", "Hit", "audio/hit.wav", AudioLoadMode::StaticSound});
    const ScriptProgram destroyOnContact{
        "destroy-on-contact", "scripts/destroy.lua", R"lua(
artcade.require_api_version(1)
return { on_collision_enter = function(ctx, other)
  if other == 99 then ctx.self:destroy() end
end }
)lua"};
    const ScriptProgram soundOnContact{
        "sound-on-contact", "scripts/sound.lua", R"lua(
artcade.require_api_version(1)
return { on_collision_enter = function(ctx, other)
  if other == 99 then ctx.audio:play("hit", 0.25) end
end }
)lua"};
    auto collisionPlay = PlaySession::startProject(
        ProjectDocument{std::move(collisionDoc)},
        {destroyOnContact, soundOnContact}, &error);
    CHECK(collisionPlay.has_value());
    if (collisionPlay) {
        collisionPlay->update(RuntimeInputSnapshot{}, 1.f / 60.f);
        CHECK(collisionPlay->findEntity(kHero) == nullptr);
        CHECK(collisionPlay->findEntity(99) != nullptr);
        const std::vector<RuntimeAudioCommand> audio = collisionPlay->drainAudioCommands();
        CHECK(audio.size() == 1);
        CHECK(!audio.empty() && audio.front().owner == kHero);
        CHECK(!audio.empty() && audio.front().audioAssetId == "hit");
    }
}

int main() {
    // -- §24.1  A command modifies a single authority --------------------------
    {
        EditorCoordinator c{makeDoc()};
        const SelectionState selectionBefore = c.selection();
        const EditorUiState  uiBefore = c.uiState();

        const auto r = c.execute(SetEntityPositionCommand{kSceneA, kHero, {99.f, 20.f}});
        CHECK(r.ok);
        // Only the document changed; selection and UI state are untouched.
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 99.f);
        CHECK(c.selection().primaryEntity == selectionBefore.primaryEntity);
        CHECK(c.uiState().leftPanelWidth == uiBefore.leftPanelWidth);
        CHECK(c.document().isDirty());
    }

    // -- §24.2 / §24.3  A failed command changes nothing and invalidates nothing
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revBefore = c.document().revision();
        c.consumeInvalidations(); // clear

        const auto r = c.execute(SetEntityPositionCommand{kSceneA, 9999, {1.f, 2.f}});
        CHECK(!r.ok);
        CHECK(!r.error.empty());
        CHECK(c.document().revision() == revBefore);          // state unchanged
        CHECK(!c.document().isDirty());
        // Only the console error was raised; no Inspector/Viewport invalidation.
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Inspector));
        CHECK(!has(inv, EditorInvalidation::Viewport));
        CHECK(!c.canUndo());                                   // not pushed to undo
    }

    // -- §24.4  SetEntityPositionCommand invalidates only Inspector|Viewport ---
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const auto r = c.execute(SetEntityPositionCommand{kSceneA, kHero, {1.f, 1.f}});
        CHECK(r.invalidation ==
              (EditorInvalidation::Inspector | EditorInvalidation::Viewport));
        CHECK(r.change.kind == DomainChangeKind::EntityChanged);
        CHECK(r.change.sceneId == kSceneA);
        CHECK(r.change.entityId == kHero);
        CHECK(!has(r.invalidation, EditorInvalidation::Hierarchy));
        CHECK(!has(r.invalidation, EditorInvalidation::Project));
    }

    // -- §24.5  A selection does not perform a Replace -------------------------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.selection().primaryEntity == kHero);
        CHECK(c.document().replaceCount() == replacesBefore);  // no Replace
        CHECK(!c.document().isDirty());                        // no authoring mutation

        const auto bad = c.apply(SelectEntityIntent{9999});
        CHECK(!bad.ok);
        CHECK(c.selection().primaryEntity == kHero);           // unchanged on failure
    }

    // -- §24.6  A scene change does not serialize / Replace the project --------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        const uint64_t revBefore = c.document().revision();

        c.apply(SelectEntityIntent{kHero});
        CHECK(c.selection().primaryEntity == kHero);

        const auto r = c.apply(SelectSceneIntent{kSceneB});
        CHECK(r.ok);
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.selection().primaryEntity == INVALID_ENTITY);
        CHECK(c.uiState().leftPanelWidth == 280.0f);
        CHECK(c.document().startSceneId() == kSceneA);         // project unchanged
        CHECK(c.document().replaceCount() == replacesBefore);  // no Replace
        CHECK(c.document().revision() == revBefore);           // no serialization/mutation
        CHECK(!c.document().isDirty());

        const auto bad = c.apply(SelectSceneIntent{"nope"});
        CHECK(!bad.ok);
        CHECK(c.state().activeSceneId == kSceneB);             // unchanged on failure
        CHECK(c.document().startSceneId() == kSceneA);
    }

    // -- §24.8  Nothing accumulates invalidation without an operation ----------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
        CHECK(c.pendingInvalidations() == EditorInvalidation::None);
    }

    // -- Replace project is atomic at the coordinator boundary ----------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 333.f});
        c.apply(SetHierarchyFilterIntent{"keep-me"});
        c.execute(SetEntityPositionCommand{kSceneA, kHero, {99.f, 20.f}});
        CHECK(c.canUndo());
        c.consumeInvalidations();

        const EditorUiState uiBefore = c.uiState();
        const auto r = c.replaceProject(ProjectDocument{makeReplacementDoc()});

        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(r.invalidation == (EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                                 | EditorInvalidation::Viewport | EditorInvalidation::Assets
                                 | EditorInvalidation::Toolbar | EditorInvalidation::Project
                                 | EditorInvalidation::ScriptEditor
                                 | EditorInvalidation::Layout));
        CHECK(c.document().data().projectName == "replacement");
        CHECK(c.state().activeSceneId == "scene-replacement");
        CHECK(c.selection().primaryEntity == INVALID_ENTITY);
        CHECK(!c.canUndo());
        CHECK(c.undoSize() == 0);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == c.document().savedRevision());
        CHECK(c.uiState().leftPanelWidth == uiBefore.leftPanelWidth);
        CHECK(c.uiState().hierarchyFilter == uiBefore.hierarchyFilter);
    }

    // -- Replace normalizes missing and empty scene focus ----------------------
    {
        EditorCoordinator invalidStart{makeDoc()};
        const auto invalid = invalidStart.replaceProject(ProjectDocument{makeInvalidStartDoc()});
        CHECK(invalid.ok);
        CHECK(invalidStart.document().startSceneId() == "missing-start-scene");
        CHECK(invalidStart.state().activeSceneId == "scene-replacement");
        CHECK(!invalidStart.document().isDirty());

        EditorCoordinator empty{makeDoc()};
        const auto emptyReplace = empty.replaceProject(ProjectDocument{makeEmptyDoc()});
        CHECK(emptyReplace.ok);
        CHECK(empty.state().activeSceneId.empty());
        CHECK(empty.selection().primaryEntity == INVALID_ENTITY);
        CHECK(!empty.document().isDirty());
    }

    // -- New Project: replacing with an empty document yields the clean, blank
    //    lifecycle start the application's New action produces. The guard
    //    (Save/Discard/Cancel) and the Play rejection are exercised separately
    //    (resolveUnsavedGuard tests; replaceProject-during-Play test); the path
    //    clearing and "Untitled" title are application state. Here we pin the
    //    domain transition: dirty edits + history + selection + view state in,
    //    a truly empty, clean, history-less project out.
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(SetViewportZoomIntent{kSceneA, 2.0f});   // populate per-scene view state
        c.execute(SetEntityPositionCommand{kSceneA, kHero, {7.f, 8.f}});
        c.execute(SetEntityPositionCommand{kSceneA, kHero, {9.f, 10.f}});
        c.undo();                                   // leave a redo branch too
        CHECK(c.canUndo());
        CHECK(c.canRedo());
        CHECK(c.document().isDirty());
        CHECK(!c.state().sceneViews.empty());
        c.consumeInvalidations();

        const EditorUiState uiBefore = c.uiState();
        const auto r = c.replaceProject(ProjectDocument{ProjectDoc{}});

        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(c.document().startSceneId().empty());           // 0 scenes -> empty start
        CHECK(c.document().data().scenes.empty());
        CHECK(c.document().data().entities.empty());
        CHECK(c.document().data().imageAssets.empty());
        CHECK(c.document().data().audioAssets.empty());
        CHECK(c.document().data().fontAssets.empty());
        CHECK(c.state().activeSceneId.empty());
        CHECK(c.state().sceneViews.empty());                  // view state pruned
        CHECK(c.selection().primaryEntity == INVALID_ENTITY); // selection cleared
        CHECK(!c.canUndo());                                  // history cleared
        CHECK(!c.canRedo());
        CHECK(c.undoSize() == 0);
        CHECK(c.redoSize() == 0);
        CHECK(!c.document().isDirty());                       // clean (no destination yet)
        CHECK(c.document().revision() == c.document().savedRevision());
        CHECK(!c.isPlaying());                                // no session carried over
        CHECK(c.uiState().leftPanelWidth == uiBefore.leftPanelWidth);  // UI prefs kept
    }

    // -- Console copy: clipboard formatting + safe indexed lookup -------------
    //    The selection itself is local panel state (not exercised here); these
    //    pin the editor-core pieces the copy entry point relies on.
    {
        CHECK(formatConsoleMessageForClipboard(
                  ConsoleMessage{ConsoleMessage::Level::Info, "ready"}) == "[Info] ready");
        CHECK(formatConsoleMessageForClipboard(
                  ConsoleMessage{ConsoleMessage::Level::Warning, "watch out"})
              == "[Warning] watch out");
        // The full, unabbreviated model text is copied (UI truncation must not leak in).
        const std::string longText =
            "Open failed: invalid TopDownController speed: -20 in scene-a/entity-7";
        CHECK(formatConsoleMessageForClipboard(
                  ConsoleMessage{ConsoleMessage::Level::Error, longText})
              == "[Error] " + longText);

        EditorCoordinator c{makeDoc()};
        CHECK(c.consoleMessage(std::nullopt) == nullptr);  // nothing selected
        CHECK(c.consoleMessage(0) == nullptr);             // empty log
        c.logInfo("first");
        c.logError("second");
        CHECK(c.consoleMessage(0) != nullptr);
        CHECK(c.consoleMessage(0)->text == "first");
        CHECK(c.consoleMessage(1)->level == ConsoleMessage::Level::Error);
        CHECK(c.consoleMessage(2) == nullptr);             // out of range -> safe
    }

    // -- Load from text mutates only after deserialize/migrate/validate --------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 345.f});
        c.execute(SetEntityPositionCommand{kSceneA, kHero, {44.f, 55.f}});
        c.consumeInvalidations();

        const uint64_t revisionBefore = c.document().revision();
        const uint64_t savedBefore = c.document().savedRevision();
        const bool dirtyBefore = c.document().isDirty();
        const std::size_t undoBefore = c.undoSize();

        const auto malformed = loadProjectFromText(c, "{ not json");
        CHECK(!malformed.ok);
        CHECK(malformed.error.stage == ProjectLoadStage::Deserialize);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const auto migrationFailed = loadProjectFromText(c, unsupportedVersionJson());
        CHECK(!migrationFailed.ok);
        CHECK(migrationFailed.error.stage == ProjectLoadStage::Migration);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const auto validationFailed = loadProjectFromText(c, danglingStartJson());
        CHECK(!validationFailed.ok);
        CHECK(validationFailed.error.stage == ProjectLoadStage::Validation);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const auto duplicateScene = loadProjectFromText(c, duplicateSceneJson());
        CHECK(!duplicateScene.ok);
        CHECK(duplicateScene.error.stage == ProjectLoadStage::Validation);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const auto loaded = loadProjectFromText(c, validProjectJson());
        CHECK(loaded.ok);
        CHECK(loaded.operation.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(c.document().data().projectName == "LoadedProject");
        CHECK(c.document().startSceneId() == "loaded-scene");
        CHECK(c.state().activeSceneId == "loaded-scene");
        CHECK(c.selection().primaryEntity == INVALID_ENTITY);
        CHECK(c.uiState().leftPanelWidth == 345.f);
        CHECK(c.undoSize() == 0);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == c.document().savedRevision());
        CHECK(c.document().findInstanceInScene("loaded-scene", 88) != nullptr);
    }

    // -- Empty projects load without inventing a scene -------------------------
    {
        EditorCoordinator c{makeDoc()};
        const auto loaded = loadProjectFromText(c, zeroSceneJson());
        CHECK(loaded.ok);
        CHECK(c.document().data().scenes.empty());
        CHECK(c.state().activeSceneId.empty());
        CHECK(c.selection().primaryEntity == INVALID_ENTITY);
        CHECK(!c.document().isDirty());
    }

    // -- P0-03: malformed present fields never fall back or escape as throws --
    {
        const std::string formatPrefix = R"({"formatVersion":)";
        for (const std::string& badValue : {std::string{"\"2\""}, std::string{"null"},
                                            std::string{"[]"}, std::string{"{}"}}) {
            const DeserializeResult result =
                ProjectSerializer::deserialize(formatPrefix + badValue + "}");
            CHECK(!result.ok);
            CHECK(result.error.find("formatVersion") != std::string::npos);
        }

        const std::string instancePrefix =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"T","transform":)";
        for (const std::string& badTransform : {std::string{"7"}, std::string{"false"},
                                                std::string{"\"bad\""}, std::string{"[]"}}) {
            const DeserializeResult result = ProjectSerializer::deserialize(
                instancePrefix + badTransform + "}]}]}");
            CHECK(!result.ok);
            CHECK(result.error.find("transform") != std::string::npos);
        }

        CHECK(!ProjectSerializer::deserialize(R"({"scenes":[42]})").ok);
        CHECK(!ProjectSerializer::deserialize(R"({"objectTypes":[null]})").ok);
        CHECK(!ProjectSerializer::deserialize(R"({"imageAssets":{}})").ok);
        CHECK(!ProjectSerializer::deserialize(
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":"bad"}]})").ok);

        const DeserializeResult hugeFloat =
            ProjectSerializer::deserialize(R"({"targetFPS":1e100})");
        CHECK(!hugeFloat.ok);
        CHECK(hugeFloat.error.find("out of range") != std::string::npos);
        CHECK(!ProjectSerializer::deserialize(R"({"formatVersion":2147483648})").ok);
        CHECK(!ProjectSerializer::deserialize(R"({"schemaVersion":"2"})").ok);
        CHECK(!ProjectSerializer::deserialize(
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":4294967296,"objectTypeId":"T"}]}]})").ok);

        CHECK(!ProjectSerializer::deserialize("{\"scenes\":[").ok);   // truncated
        CHECK(!ProjectSerializer::deserialize("{ not json").ok);       // malformed

        const DeserializeResult unicode = ProjectSerializer::deserialize(
            R"({"projectName":"Progetto \u00c8","scenes":[]})");
        CHECK(unicode.ok);
        CHECK(unicode.value.data().projectName == std::string("Progetto \xC3\x88"));

        // Truly absent optional fields retain schema defaults; this is distinct
        // from the malformed-present cases above.
        const DeserializeResult minimal =
            ProjectSerializer::deserialize(R"({"projectName":"Minimal"})");
        CHECK(minimal.ok);
        CHECK(minimal.value.data().targetFPS == 60.f);
        CHECK(minimal.value.data().scenes.empty());
    }

    // -- P0-03: every deserialize failure leaves the live coordinator intact --
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 345.f});
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {44.f, 55.f}}).ok);
        const uint64_t revisionBefore = c.document().revision();
        const uint64_t savedBefore = c.document().savedRevision();
        const bool dirtyBefore = c.document().isDirty();
        const std::size_t undoBefore = c.undoSize();

        for (const std::string& hostile : {
                 std::string{R"({"formatVersion":null})"},
                 std::string{R"({"targetFPS":1e100})"},
                 std::string{R"({"scenes":[{"id":"s","instances":[[]]}]})"}}) {
            const ProjectLoadResult result = loadProjectFromText(c, hostile);
            CHECK(!result.ok);
            CHECK(result.error.stage == ProjectLoadStage::Deserialize);
            expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                      undoBefore, revisionBefore, savedBefore, dirtyBefore);
        }
    }

    // -- Serializer round-trip keeps authoring data, not workspace state ------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(SetActiveToolIntent{EditorTool::Pan});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 410.f});
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {321.f, 20.f}}).ok);

        SerializeResult serialized = ProjectSerializer::serialize(c.document());
        CHECK(serialized.ok);
        CHECK(serialized.value.find("selection") == std::string::npos);
        CHECK(serialized.value.find("sceneViews") == std::string::npos);
        CHECK(serialized.value.find("activeTool") == std::string::npos);
        CHECK(serialized.value.find("leftPanelWidth") == std::string::npos);
        CHECK(serialized.value.find("consoleVisible") == std::string::npos);

        DeserializeResult deserialized = ProjectSerializer::deserialize(serialized.value);
        CHECK(deserialized.ok);
        DeserializeResult validated =
            ProjectValidator::validate(std::move(deserialized.value));
        CHECK(validated.ok);
        CHECK(validated.value.findInstanceInScene(kSceneA, kHero)->transform.position.x
              == 321.f);
    }

    // -- Schema v4 promotes sprite defaults and persists sparse deltas only ---
    {
        const std::string v3 = R"json({
          "formatVersion": 3,
          "activeSceneId": "z",
          "objectTypes": [{"id":"Hero","name":"Hero"}],
          "scenes": [
            {"id":"z","instances":[
              {"id":1,"objectTypeId":"Hero","instanceName":"Absent"}
            ]},
            {"id":"a","instances":[
              {"id":2,"objectTypeId":"Hero","instanceName":"Default",
               "spriteRenderer":{"imageAssetId":"blue","visible":true},
               "spriteAnimator":{"initialClipId":"idle","autoPlay":true,"playbackSpeed":1}},
              {"id":3,"objectTypeId":"Hero","instanceName":"Delta",
               "spriteRenderer":{"imageAssetId":"green","visible":false},
               "spriteAnimator":{"initialClipId":"run","autoPlay":false,"playbackSpeed":2}}
            ]}
          ]
        })json";

        DeserializeResult decoded = ProjectSerializer::deserialize(v3);
        CHECK(decoded.ok);
        DeserializeResult migrated = ProjectMigration::migrate(std::move(decoded.value));
        CHECK(migrated.ok);
        CHECK(migrated.value.data().formatVersion == 7);

        const EntityDef& type = migrated.value.data().objectTypes.at("Hero");
        CHECK(type.spriteRenderer && type.spriteRenderer->imageAssetId == "blue");
        CHECK(type.spriteAnimator && type.spriteAnimator->initialClipId == "idle");

        const SceneInstanceDef* inherited = migrated.value.findInstanceInScene("a", 2);
        const SceneInstanceDef* delta = migrated.value.findInstanceInScene("a", 3);
        const SceneInstanceDef* absent = migrated.value.findInstanceInScene("z", 1);
        CHECK(inherited && !inherited->legacySpriteRendererV3
              && !inherited->spriteRendererOverride);
        CHECK(inherited && !inherited->legacySpriteAnimatorV3
              && !inherited->spriteAnimatorOverride);
        CHECK(delta && delta->spriteRendererOverride);
        CHECK(delta && delta->spriteRendererOverride->imageAssetId
              && *delta->spriteRendererOverride->imageAssetId == "green");
        CHECK(delta && delta->spriteRendererOverride->visible
              && !*delta->spriteRendererOverride->visible);
        CHECK(delta && delta->spriteAnimatorOverride);
        CHECK(delta && delta->spriteAnimatorOverride->initialClipId
              && *delta->spriteAnimatorOverride->initialClipId == "run");
        CHECK(delta && delta->spriteAnimatorOverride->playbackSpeed
              && *delta->spriteAnimatorOverride->playbackSpeed == 2.f);
        CHECK(absent && absent->spriteRendererOverride
              && absent->spriteRendererOverride->capabilityEnabled
              && !*absent->spriteRendererOverride->capabilityEnabled);
        CHECK(absent && absent->spriteAnimatorOverride
              && absent->spriteAnimatorOverride->capabilityEnabled
              && !*absent->spriteAnimatorOverride->capabilityEnabled);

        const SerializeResult once = ProjectSerializer::serialize(migrated.value);
        CHECK(once.ok);
        DeserializeResult migratedAgain = ProjectMigration::migrate(
            ProjectDocument{migrated.value.data()});
        CHECK(migratedAgain.ok);
        const SerializeResult twice = ProjectSerializer::serialize(migratedAgain.value);
        CHECK(twice.ok && twice.value == once.value); // migration is idempotent

        const DeserializeResult roundTrip = ProjectSerializer::deserialize(once.value);
        CHECK(roundTrip.ok);
        const SceneInstanceDef* roundTripDelta =
            roundTrip.value.findInstanceInScene("a", 3);
        CHECK(roundTripDelta && !roundTripDelta->legacySpriteRendererV3);
        CHECK(roundTripDelta && !roundTripDelta->legacySpriteAnimatorV3);
        CHECK(roundTripDelta && roundTripDelta->spriteRendererOverride);
        CHECK(roundTripDelta && roundTripDelta->spriteAnimatorOverride);
    }

    // -- 2D.0C: type-owned capability + sparse override have exact history ---
    {
        ProjectDoc doc = makeAnimationDoc();
        doc.objectTypes.at("Hero").spriteRenderer.reset();
        doc.objectTypes.at("Hero").spriteAnimator.reset();
        EditorCoordinator c{std::move(doc)};

        CHECK(c.execute(AddSpriteRendererToObjectTypeCommand{"Hero"}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer.has_value());
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->spriteRendererOverride);
        CHECK(c.execute(SetObjectTypeSpriteSourceCommand{
            "Hero", ObjectTypeSpriteSourceKind::Animation, "hero.anim"}).ok);
        const EntityDef& animatedType = c.document().data().objectTypes.at("Hero");
        CHECK(animatedType.spriteRenderer->animationAssetId == "hero.anim");
        CHECK(animatedType.spriteAnimator.has_value());
        CHECK(animatedType.spriteAnimator->initialClipId == "idle");

        SpriteAnimatorOverride speedDelta;
        speedDelta.playbackSpeed = 1.5f;
        CHECK(c.execute(SetInstanceAnimatorOverrideCommand{
            kSceneA, kHero, speedDelta}).ok);
        const SceneInstanceDef* instance =
            c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteAnimatorOverride);
        CHECK(instance && instance->spriteAnimatorOverride->playbackSpeed
              && *instance->spriteAnimatorOverride->playbackSpeed == 1.5f);
        const ResolvedSpritePresentation resolved = resolveSpritePresentation(
            c.document().data().objectTypes.at("Hero"), *instance);
        CHECK(resolved.animator && resolved.animator->playbackSpeed == 1.5f);
        CHECK(resolved.animatorOrigin == ComponentOrigin::InstanceOverride);

        CHECK(c.playProject().ok);
        const RuntimeEntity* runtimeHero = c.playSession()
            ? c.playSession()->findEntity(kHero) : nullptr;
        CHECK(runtimeHero && runtimeHero->spriteAnimator);
        CHECK(runtimeHero && runtimeHero->spriteAnimator->playbackSpeed == 1.5f);
        CHECK(c.stopPlaying().ok);

        CHECK(c.undo().ok);
        instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && !instance->spriteAnimatorOverride);
        CHECK(c.redo().ok);
        instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteAnimatorOverride);

        CHECK(c.execute(ClearInstanceAnimatorOverrideCommand{kSceneA, kHero}).ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->spriteAnimatorOverride);
        CHECK(c.undo().ok);
        instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteAnimatorOverride
              && instance->spriteAnimatorOverride->playbackSpeed
              && *instance->spriteAnimatorOverride->playbackSpeed == 1.5f);
        CHECK(c.redo().ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->spriteAnimatorOverride);

        CHECK(c.execute(RemoveSpriteRendererFromObjectTypeCommand{"Hero"}).ok);
        CHECK(!c.document().data().objectTypes.at("Hero").spriteRenderer);
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);
        CHECK(c.undo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer);
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator);

        ProjectDoc guardedDoc = c.document().data();
        LogicBoardDef board;
        LogicRuleDef rule;
        rule.id = "animation-rule";
        rule.actions.push_back(LogicBlockDef{"animation.play_clip", {}});
        rule.actions.push_back(LogicBlockDef{"animation.stop", {}});
        board.rules.push_back(std::move(rule));
        guardedDoc.objectTypes.at("Hero").logicBoard = std::move(board);
        EditorCoordinator guarded{std::move(guardedDoc)};
        const EditorOperationResult rejected = guarded.execute(
            RemoveSpriteAnimatorFromObjectTypeCommand{"Hero"});
        CHECK(!rejected.ok);
        CHECK(rejected.error.find("contains 2 actions") != std::string::npos);
        CHECK(guarded.document().data().objectTypes.at("Hero").spriteAnimator);
    }

    // -- 2D.0D: orphan/invalid deltas and referenced clips fail in core -------
    {
        ProjectDoc orphan = makeInheritedDoc();
        SpriteAnimatorOverride animatorDelta;
        animatorDelta.playbackSpeed = 1.5f;
        orphan.scenes.at(kSceneA).instances.front().spriteAnimatorOverride = animatorDelta;
        CHECK(!ProjectValidator::validate(ProjectDocument{std::move(orphan)}).ok);

        ProjectDoc invalidSpeed = makeAnimationDoc();
        animatorDelta.playbackSpeed = -1.f;
        invalidSpeed.scenes.at(kSceneA).instances.front().spriteAnimatorOverride = animatorDelta;
        CHECK(!ProjectValidator::validate(ProjectDocument{std::move(invalidSpeed)}).ok);

        EditorCoordinator referencedClip{makeAnimationDoc()};
        const uint64_t revision = referencedClip.document().revision();
        CHECK(!referencedClip.execute(RemoveAnimationClipCommand{"hero.anim", "idle"}).ok);
        CHECK(referencedClip.document().revision() == revision);
        CHECK(referencedClip.document().findSpriteAnimationAsset("hero.anim")->clips.size() == 1);
    }

    // -- 2D.0D: image deletion clears/restores a sparse instance delta exactly -
    {
        EditorCoordinator c{makeInheritedDoc()};
        SpriteRendererOverride delta;
        delta.imageAssetId = "img-alt";
        delta.animationAssetId = AssetId{};
        delta.visible = false;
        CHECK(c.execute(SetInstanceSpriteOverrideCommand{kSceneA, kHero, delta}).ok);
        CHECK(c.execute(RemoveImageAssetCommand{"img-alt"}).ok);
        const SceneInstanceDef* instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteRendererOverride
              && instance->spriteRendererOverride->imageAssetId
              && instance->spriteRendererOverride->imageAssetId->empty());
        CHECK(c.undo().ok);
        instance = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(instance && instance->spriteRendererOverride
              && instance->spriteRendererOverride->imageAssetId
              && *instance->spriteRendererOverride->imageAssetId == "img-alt");
        CHECK(instance && instance->spriteRendererOverride->visible
              && !*instance->spriteRendererOverride->visible);
    }

    // -- Project rename is persistent authoring metadata ----------------------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const EditorOperationResult r = c.execute(RenameProjectCommand{"Arcade Quest"});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ProjectChanged);
        CHECK(r.invalidation == (EditorInvalidation::Inspector
                                 | EditorInvalidation::Toolbar
                                 | EditorInvalidation::Project));
        CHECK(c.document().data().projectName == "Arcade Quest");
        CHECK(c.document().isDirty());
        CHECK(c.undo().ok);
        CHECK(c.document().data().projectName == "spike");
        CHECK(c.redo().ok);
        CHECK(c.document().data().projectName == "Arcade Quest");

        const std::filesystem::path path = testTempDir() / "renamed.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().data().projectName == "Arcade Quest");
    }

    // -- File load adapter: filesystem failure leaves the coordinator intact ---
    {
        const std::filesystem::path dir = testTempDir();
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 360.f});
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {44.f, 55.f}}).ok);

        const uint64_t revisionBefore = c.document().revision();
        const uint64_t savedBefore = c.document().savedRevision();
        const bool dirtyBefore = c.document().isDirty();
        const std::size_t undoBefore = c.undoSize();

        const auto missing = loadProjectFromFile(c, dir / "missing.artcade-project");
        CHECK(!missing.ok);
        CHECK(missing.error.stage == ProjectLoadStage::FileRead);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 360.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const std::filesystem::path emptyFile = dir / "empty.artcade-project";
        writeTextFile(emptyFile, "");
        const auto empty = loadProjectFromFile(c, emptyFile);
        CHECK(!empty.ok);
        CHECK(empty.error.stage == ProjectLoadStage::Deserialize);
        expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 360.f,
                                  undoBefore, revisionBefore, savedBefore, dirtyBefore);

        const std::filesystem::path validFile = dir / "valid.artcade-project";
        writeTextFile(validFile, validProjectJson());
        const auto loaded = loadProjectFromFile(c, validFile);
        CHECK(loaded.ok);
        CHECK(loaded.operation.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(c.document().data().projectName == "LoadedProject");
        CHECK(c.state().activeSceneId == "loaded-scene");
        CHECK(!c.document().isDirty());
    }

    // -- Atomic save: failure does not mark saved; success reloads from disk ---
    {
        const std::filesystem::path dir = testTempDir();
        const std::filesystem::path projectPath = dir / "project.artcade-project";

        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(SetViewportZoomIntent{kSceneA, 2.5f});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 390.f});
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {777.f, 20.f}}).ok);
        CHECK(c.canUndo());
        CHECK(c.document().isDirty());

        const uint64_t revisionBeforeFailedSave = c.document().revision();
        const uint64_t savedBeforeFailedSave = c.document().savedRevision();
        const std::size_t undoBeforeFailedSave = c.undoSize();
        const std::filesystem::path blockedDestination = dir / "blocked";
        std::filesystem::create_directory(blockedDestination);

        const auto failedSave = saveProjectToFile(c, blockedDestination);
        CHECK(!failedSave.ok);
        CHECK(failedSave.error.stage == ProjectSaveStage::FileWrite);
        CHECK(c.document().revision() == revisionBeforeFailedSave);
        CHECK(c.document().savedRevision() == savedBeforeFailedSave);
        CHECK(c.document().isDirty());
        CHECK(c.undoSize() == undoBeforeFailedSave);
        CHECK(std::filesystem::is_directory(blockedDestination));
        CHECK(!hasTempSibling(blockedDestination));

        const auto saved = saveProjectToFile(c, projectPath);
        CHECK(saved.ok);
        CHECK(saved.operation.change.kind == DomainChangeKind::None);
        CHECK(saved.operation.invalidation == EditorInvalidation::Toolbar);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == revisionBeforeFailedSave);
        CHECK(c.document().savedRevision() == c.document().revision());
        CHECK(c.undoSize() == undoBeforeFailedSave);
        CHECK(c.canUndo());

        const std::string bytes = readTextFile(projectPath);
        CHECK(bytes.find("activeSceneId") != std::string::npos); // persisted start scene
        CHECK(bytes.find("selection") == std::string::npos);
        CHECK(bytes.find("sceneViews") == std::string::npos);
        CHECK(bytes.find("activeTool") == std::string::npos);
        CHECK(bytes.find("splitter") == std::string::npos);
        CHECK(bytes.find("hierarchyFilter") == std::string::npos);
        CHECK(bytes.find("consoleVisible") == std::string::npos);
        CHECK(bytes.find("PlaySession") == std::string::npos);
        CHECK(bytes.find("Rml") == std::string::npos);

        EditorCoordinator sameCoordinator{makeReplacementDoc()};
        sameCoordinator.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 444.f});
        sameCoordinator.apply(SelectEntityIntent{77});
        CHECK(sameCoordinator.execute(SetEntityPositionCommand{
            "scene-replacement", 77, {1.f, 2.f}}).ok);
        CHECK(sameCoordinator.canUndo());

        const auto reloadedSame = loadProjectFromFile(sameCoordinator, projectPath);
        CHECK(reloadedSame.ok);
        CHECK(sameCoordinator.document().findInstanceInScene(kSceneA, kHero)
                  ->transform.position.x == 777.f);
        CHECK(sameCoordinator.uiState().leftPanelWidth == 444.f);
        CHECK(sameCoordinator.selection().primaryEntity == INVALID_ENTITY);
        CHECK(sameCoordinator.undoSize() == 0);
        CHECK(!sameCoordinator.canUndo());
        CHECK(!sameCoordinator.document().isDirty());

        EditorCoordinator fresh{makeReplacementDoc()};
        const auto reloadedFresh = loadProjectFromFile(fresh, projectPath);
        CHECK(reloadedFresh.ok);
        CHECK(fresh.document().findInstanceInScene(kSceneA, kHero)
                  ->transform.position.x == 777.f);
        CHECK(fresh.uiState().leftPanelWidth == 280.f);
        CHECK(fresh.selection().primaryEntity == INVALID_ENTITY);
        CHECK(fresh.state().activeTool == EditorTool::Select);
        CHECK(!fresh.document().isDirty());
    }

    // -- P0-05: New Project is prepare/save/commit, with exact rollback -------
    {
        const std::filesystem::path base = testTempDir();
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 345.f});
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {44.f, 55.f}}).ok);
        const uint64_t revisionBefore = c.document().revision();
        const uint64_t savedBefore = c.document().savedRevision();
        const bool dirtyBefore = c.document().isDirty();
        const std::size_t undoBefore = c.undoSize();

        const auto freshCandidate = [] {
            ProjectDoc doc;
            doc.projectName = "Fresh";
            return ProjectDocument{std::move(doc)};
        };
        const auto expectUnchanged = [&] {
            expectCoordinatorBaseline(c, "spike", kSceneA, kHero, 345.f,
                                      undoBefore, revisionBefore, savedBefore, dirtyBefore);
        };

        const NewProjectResult cancelled = createNewProjectTransaction(
            c, freshCandidate(), std::nullopt);
        CHECK(!cancelled.ok);
        CHECK(cancelled.cancelled);
        CHECK(cancelled.error.stage == NewProjectStage::Cancelled);
        CHECK(cancelled.destination.empty());
        expectUnchanged();

        const std::filesystem::path blocker = base / "blocker";
        writeTextFile(blocker, "not a directory");
        const NewProjectResult directoryFailed = createNewProjectTransaction(
            c, freshCandidate(), blocker / "nested" / "Fresh.artcade-project");
        CHECK(!directoryFailed.ok);
        CHECK(directoryFailed.error.stage == NewProjectStage::DirectoryCreate);
        expectUnchanged();

        const NewProjectResult validationFailed = createNewProjectTransaction(
            c, ProjectDocument{makeInvalidStartDoc()},
            base / "validation" / "Fresh.artcade-project");
        CHECK(!validationFailed.ok);
        CHECK(validationFailed.error.stage == NewProjectStage::Validation);
        CHECK(!std::filesystem::exists(base / "validation"));
        expectUnchanged();

        NewProjectTransactionHooks serializeHooks;
        serializeHooks.saveCandidate = [](
            const ProjectDocument&, const std::filesystem::path& destination) {
            return ProjectSaveResult::failure(
                ProjectSaveStage::Serialize, destination, "forced serialization failure");
        };
        const NewProjectResult serializeFailed = createNewProjectTransaction(
            c, freshCandidate(), base / "serialize" / "Fresh.artcade-project",
            std::move(serializeHooks));
        CHECK(!serializeFailed.ok);
        CHECK(serializeFailed.error.stage == NewProjectStage::Serialize);
        CHECK(!std::filesystem::exists(base / "serialize"));
        expectUnchanged();

        NewProjectTransactionHooks writeHooks;
        writeHooks.saveCandidate = [](
            const ProjectDocument&, const std::filesystem::path& destination) {
            const ProjectTextFileResult partial =
                writeProjectTextFileAtomically(destination, "partial");
            if (!partial.ok) {
                return ProjectSaveResult::failure(
                    ProjectSaveStage::FileWrite, destination, partial.error.message);
            }
            return ProjectSaveResult::failure(
                ProjectSaveStage::FileWrite, destination, "forced temporary write failure");
        };
        const std::filesystem::path writeDestination =
            base / "write" / "Fresh.artcade-project";
        const NewProjectResult writeFailed = createNewProjectTransaction(
            c, freshCandidate(), writeDestination, std::move(writeHooks));
        CHECK(!writeFailed.ok);
        CHECK(writeFailed.error.stage == NewProjectStage::FileWrite);
        CHECK(!std::filesystem::exists(writeDestination));
        CHECK(!std::filesystem::exists(base / "write"));
        expectUnchanged();

        const std::filesystem::path replaceBlocked = base / "replace-target.artcade-project";
        std::filesystem::create_directory(replaceBlocked);
        const NewProjectResult atomicReplaceFailed = createNewProjectTransaction(
            c, freshCandidate(), replaceBlocked);
        CHECK(!atomicReplaceFailed.ok);
        CHECK(atomicReplaceFailed.error.stage == NewProjectStage::FileWrite);
        CHECK(std::filesystem::is_directory(replaceBlocked));
        CHECK(!hasTempSibling(replaceBlocked));
        expectUnchanged();

        NewProjectTransactionHooks commitHooks;
        commitHooks.commitCandidate = [](
            EditorCoordinator&, ProjectDocument) {
            return EditorOperationResult::failure("forced commit failure");
        };
        const std::filesystem::path uncommittedDestination =
            base / "commit-new" / "Fresh.artcade-project";
        const NewProjectResult commitFailed = createNewProjectTransaction(
            c, freshCandidate(), uncommittedDestination, std::move(commitHooks));
        CHECK(!commitFailed.ok);
        CHECK(commitFailed.error.stage == NewProjectStage::Commit);
        CHECK(!std::filesystem::exists(uncommittedDestination));
        CHECK(!std::filesystem::exists(base / "commit-new"));
        expectUnchanged();

        const std::filesystem::path existingDestination =
            base / "existing.artcade-project";
        writeTextFile(existingDestination, "previous bytes");
        NewProjectTransactionHooks restoreHooks;
        restoreHooks.commitCandidate = [](
            EditorCoordinator&, ProjectDocument) {
            return EditorOperationResult::failure("forced commit failure");
        };
        const NewProjectResult restoreFailed = createNewProjectTransaction(
            c, freshCandidate(), existingDestination, std::move(restoreHooks));
        CHECK(!restoreFailed.ok);
        CHECK(restoreFailed.error.stage == NewProjectStage::Commit);
        CHECK(readTextFile(existingDestination) == "previous bytes");
        expectUnchanged();

        const std::filesystem::path successDestination =
            base / "success" / "Fresh.artcade-project";
        const NewProjectResult created = createNewProjectTransaction(
            c, freshCandidate(), successDestination);
        CHECK(created.ok);
        CHECK(!created.cancelled);
        CHECK(created.destination == std::filesystem::absolute(successDestination));
        CHECK(created.operation.change.kind == DomainChangeKind::ProjectReplaced);
        CHECK(std::filesystem::is_regular_file(successDestination));
        CHECK(c.document().data().projectName == "Fresh");
        CHECK(c.document().data().scenes.empty());
        CHECK(!c.document().isDirty());
        CHECK(c.undoSize() == 0);
        CHECK(!c.selection().hasEntity());
        CHECK(c.state().activeSceneId.empty());
        CHECK(c.uiState().leftPanelWidth == 345.f);

        EditorCoordinator loaded{makeDoc()};
        CHECK(loadProjectFromFile(loaded, successDestination).ok);
        CHECK(loaded.document().data().projectName == "Fresh");
        CHECK(!loaded.document().isDirty());
    }

    // -- §24.11  Play does not modify the ProjectDocument ----------------------
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revBefore = c.document().revision();

        c.apply(SelectSceneIntent{kSceneB});
        std::optional<PlaySession> session = PlaySession::startProject(c.document());
        CHECK(session.has_value());
        CHECK(session->sceneId() == kSceneA);
        CHECK(session->entities().size() == 1);

        std::optional<PlaySession> currentSceneSession =
            PlaySession::startActiveScene(c.document(), c.state().activeSceneId);
        CHECK(currentSceneSession.has_value());
        CHECK(currentSceneSession->sceneId() == kSceneB);
        CHECK(currentSceneSession->entities().empty());

        // The simulation mutates the session freely...
        session->entities()[0].transform.position = {500.f, 600.f};
        // ...the authoring document is untouched.
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        CHECK(c.document().revision() == revBefore);

        // -- §24.12  Stop needs no reload: destroying the session restores
        //            nothing because the document was never changed.
        session->entities().clear();
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);
        CHECK(c.document().revision() == revBefore);
    }

    // -- §24.13  Invalid NumberField parse does not modify the document --------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        const uint64_t revBefore = c.document().revision();

        const auto bad = commitInspectorPositionX(c, kHero, "abc");
        CHECK(!bad.ok);
        CHECK(c.document().revision() == revBefore);           // untouched
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        CHECK(parseNumberField("12.5").has_value());
        CHECK(parseNumberField("12.").has_value());
        CHECK(*parseNumberField("12.") == 12.f);
        CHECK(!parseNumberField("-").has_value());
        CHECK(!parseNumberField(".").has_value());
        CHECK(!parseNumberField("1e").has_value());
        CHECK(!parseNumberField("nan").has_value());
        CHECK(!parseNumberField("inf").has_value());
        CHECK(!parseNumberField("12.5xz").has_value());
        CHECK(!parseNumberField("12px").has_value());
        CHECK(!parseNumberField("").has_value());
    }

    // -- §24.17  Splitter applies min/max clamp --------------------------------
    {
        EditorCoordinator c{makeDoc()};
        const SceneId sceneBefore = c.state().activeSceneId;
        const EntityId selectionBefore = c.selection().primaryEntity;
        c.consumeInvalidations();
        const auto resized = c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 5.f});
        CHECK(resized.invalidation == (EditorInvalidation::Layout | EditorInvalidation::Viewport));
        CHECK(c.uiState().leftPanelWidth == PanelLimits::kLeftMin);
        CHECK(c.state().activeSceneId == sceneBefore);
        CHECK(c.selection().primaryEntity == selectionBefore);
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, 99999.f});
        CHECK(c.uiState().leftPanelWidth == PanelLimits::kLeftMax);
        c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Console, 300.f});
        CHECK(c.uiState().consoleHeight == 300.f);

        c.apply(SelectEntityIntent{kHero});
        c.apply(SetHierarchyFilterIntent{"hero"});
        CHECK(c.uiState().hierarchyFilter == "hero");
        CHECK(c.state().activeSceneId == sceneBefore);
        CHECK(c.selection().primaryEntity == kHero);

        const auto tool = c.apply(SetActiveToolIntent{EditorTool::Pan});
        CHECK(tool.invalidation == (EditorInvalidation::Inspector | EditorInvalidation::Toolbar));
        CHECK(c.state().activeTool == EditorTool::Pan);
        CHECK(!c.uiState().consoleVisible);

        const auto console = c.apply(ToggleConsoleIntent{});
        CHECK(console.invalidation == (EditorInvalidation::Layout | EditorInvalidation::Viewport));
        CHECK(c.uiState().consoleVisible);
        CHECK(c.state().activeTool == EditorTool::Pan);
    }

    // -- Console filter/level intents: workspace-only, Console invalidation ---
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.uiState().consoleShowInfo);
        CHECK(c.uiState().consoleShowWarning);
        CHECK(c.uiState().consoleShowError);

        const auto filter = c.apply(SetConsoleFilterIntent{"missing texture"});
        CHECK(filter.ok);
        CHECK(filter.invalidation == EditorInvalidation::Console);
        CHECK(c.uiState().consoleFilter == "missing texture");

        CHECK(c.apply(SetConsoleShowInfoIntent{false}).ok);
        CHECK(!c.uiState().consoleShowInfo);
        CHECK(c.apply(SetConsoleShowWarningIntent{false}).ok);
        CHECK(!c.uiState().consoleShowWarning);
        const auto errorToggle = c.apply(SetConsoleShowErrorIntent{false});
        CHECK(errorToggle.invalidation == EditorInvalidation::Console);
        CHECK(!c.uiState().consoleShowError);

        // Filters are workspace-only: no revision, no dirty, no undo entry.
        CHECK(c.document().revision() == 0);
        CHECK(!c.document().isDirty());
        CHECK(!c.canUndo());
    }

    // -- clearConsole(): direct mutator like logInfo/Warning/Error, not a
    // Command (console history isn't authoring) or an Intent (nothing in
    // EditorState/EditorUiState changes) ---------------------------------------
    {
        EditorCoordinator c{makeDoc()};
        c.logInfo("one");
        c.logWarning("two");
        c.consumeInvalidations();
        CHECK(c.consoleLog().size() == 2);

        c.clearConsole();
        CHECK(c.consoleLog().empty());
        CHECK(c.consumeInvalidations() == EditorInvalidation::Console);
        CHECK(c.document().revision() == 0);
        CHECK(!c.canUndo());
    }

    // -- Contract: public coordinator access is read-only ----------------------
    {
        EditorCoordinator c{makeDoc()};
        static_assert(std::is_const_v<std::remove_reference_t<decltype(c.document())>>,
                      "EditorCoordinator::document() must be const-only");
        static_assert(std::is_const_v<std::remove_reference_t<decltype(c.state())>>,
                      "EditorCoordinator::state() must be const-only");
        static_assert(std::is_const_v<std::remove_reference_t<decltype(c.uiState())>>,
                      "EditorCoordinator::uiState() must be const-only");
    }

    // -- §24.16  Input captured by a text field never reaches the viewport -----
    {
        CHECK(shouldViewportReceiveInput({/*inRect*/true, false, false, false}));
        CHECK(!shouldViewportReceiveInput({true, false, /*textFocus*/true, false}));
        CHECK(!shouldViewportReceiveInput({true, /*rmlConsumed*/true, false, false}));
        CHECK(!shouldViewportReceiveInput({/*inRect*/false, false, false, false}));
        CHECK(!shouldViewportReceiveInput({true, false, false, /*popup*/true}));
        const GameplayKeyboardInputContext focusedScene{
            /*scene*/true, /*gameplayFocus*/true, /*window*/true,
            /*textFocus*/false, /*popup*/false};
        CHECK(shouldForwardGameplayKeyboardInput(focusedScene));
        // Mouse hover is not keyboard focus: leaving the viewport must not
        // interrupt TopDown, Platformer, or Logic Board keyboard controls.
        CHECK(shouldForwardGameplayKeyboardInput(focusedScene));
        CHECK(!shouldForwardGameplayKeyboardInput({/*logic*/false, true, true, false, false}));
        CHECK(!shouldForwardGameplayKeyboardInput({/*scene*/true, /*noFocus*/false, true, false, false}));
        CHECK(!shouldForwardGameplayKeyboardInput({true, true, true, /*textFocus*/true, false}));
        CHECK(!shouldForwardGameplayKeyboardInput({true, true, /*window*/false, false, false}));
        CHECK(!shouldForwardGameplayKeyboardInput({true, true, true, false, /*popup*/true}));
        CHECK(shouldForwardGameplayPointerInput({focusedScene, /*inRect*/true}));
        CHECK(!shouldForwardGameplayPointerInput({focusedScene, /*outside*/false}));
    }

    // -- Asset filter: live workspace state, narrow invalidation, idempotent --
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revision = c.document().revision();
        const std::size_t undo = c.undoSize();
        c.consumeInvalidations();

        const auto first = c.apply(SetAssetFilterIntent{"hero image"});
        CHECK(first.ok);
        CHECK(first.invalidation == EditorInvalidation::Assets);
        CHECK(c.uiState().assetFilter == "hero image");
        CHECK(c.consumeInvalidations() == EditorInvalidation::Assets);
        CHECK(c.document().revision() == revision);
        CHECK(!c.document().isDirty());
        CHECK(c.undoSize() == undo);

        const auto duplicate = c.apply(SetAssetFilterIntent{"hero image"});
        CHECK(duplicate.ok);
        CHECK(duplicate.invalidation == EditorInvalidation::None);
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);

        CHECK(c.playProject().ok);
        c.consumeInvalidations();
        const auto duringPlay = c.apply(SetAssetFilterIntent{"audio"});
        CHECK(duringPlay.ok);
        CHECK(duringPlay.invalidation == EditorInvalidation::Assets);
        CHECK(c.uiState().assetFilter == "audio");
        CHECK(c.document().revision() == revision);
        CHECK(!c.document().isDirty());
        CHECK(c.undoSize() == undo);
        CHECK(c.stopPlaying().ok);

        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.uiState().assetFilter == "audio");
        CHECK(!c.document().isDirty());
        CHECK(!c.canUndo());
    }

    // -- Replace clears project-scoped editor state ----------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(RevealInspectorPropertyIntent{kHero, InspectorProperty::TilemapCellSize});
        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.state().activeTool == EditorTool::Brush);
        CHECK(c.state().tilesetEditor.openAssetId.has_value());
        CHECK(c.uiState().inspectorRevealRequest.has_value());

        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.state().activeTool == EditorTool::Select);
        CHECK(!c.state().tilesetEditor.openAssetId.has_value());
        CHECK(!c.takeInspectorRevealRequest().has_value());
    }

    // -- §24.18  Position X path: UI callback → command → document → invalidation
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.consumeInvalidations();

        const auto r = commitInspectorPositionX(c, kHero, "256");
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::EntityChanged);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 256.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == 20.f);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(inv == (EditorInvalidation::Inspector | EditorInvalidation::Viewport
                      | EditorInvalidation::Toolbar));
        // The change is undoable and the inverse is exact.
        CHECK(c.canUndo());
        c.undo();
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
    }

    // -- Bring Into Scene: explicit recovery via SetEntityPositionCommand ------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{562.f, -35.f}}).ok);
        const std::size_t undoBefore = c.undoSize();

        const auto r = bringSelectedEntityIntoScene(c);
        CHECK(r.ok);
        CHECK(c.undoSize() == undoBefore + 1);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst != nullptr);
        CHECK(inst->transform.position.x == 496.f);   // 512 - half the 32 wu placeholder
        CHECK(inst->transform.position.y == 16.f);

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 562.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == -35.f);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 496.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == 16.f);
    }

    // -- Bring Into Scene no-ops when already inside; Play still blocks edits --
    {
        EditorCoordinator inside{makeInheritedDoc()};
        inside.apply(SelectEntityIntent{kHero});
        CHECK(inside.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{100.f, 100.f}}).ok);
        const std::size_t undoBefore = inside.undoSize();
        CHECK(bringSelectedEntityIntoScene(inside).ok);
        CHECK(inside.undoSize() == undoBefore);

        EditorCoordinator playing{makeInheritedDoc()};
        playing.apply(SelectEntityIntent{kHero});
        CHECK(playing.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{562.f, -35.f}}).ok);
        CHECK(playing.playProject().ok);
        CHECK(!bringSelectedEntityIntoScene(playing).ok);
        CHECK(playing.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 562.f);
        CHECK(playing.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == -35.f);
    }

    // -- Undo / rename / scene + background commands round-trip ----------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(RenameEntityCommand{kSceneA, kHero, "Champion"}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->instanceName == "Champion");
        CHECK(!c.execute(RenameEntityCommand{kSceneA, kHero, ""}).ok); // empty rejected

        CHECK(c.execute(CreateSceneCommand{"scene-c", "Scene C"}).ok);
        CHECK(c.document().hasScene("scene-c"));
        // New scenes default to the dark neutral background; the vendored
        // struct default (white) is only for files saved without the field.
        CHECK(c.document().findScene("scene-c")->backgroundColor.r == 0.118f);
        CHECK(c.document().findScene("scene-c")->backgroundColor.b == 0.141f);
        CHECK(SceneDef{}.backgroundColor.r == 1.f);
        CHECK(!c.execute(CreateSceneCommand{"scene-a", "dup"}).ok); // duplicate rejected

        CHECK(c.execute(SetSceneBackgroundCommand{kSceneA, {1.f, 0.f, 0.f, 1.f}}).ok);
        CHECK(c.document().findScene(kSceneA)->backgroundColor.r == 1.f);
        c.undo();                                                // undo background
        CHECK(c.document().findScene(kSceneA)->backgroundColor.r == 0.1f);
    }

    // -- CreateEntityCommand: add, invalidation, DomainChange, undo -------------
    {
        EditorCoordinator c{makeDoc()};
        const auto r = c.execute(
            CreateEntityCommand{kSceneA, 100, "Enemy", "Enemy 1", {5.f, 6.f}});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::EntityAdded);
        CHECK(r.change.entityId == 100);
        CHECK(c.consumeInvalidations()
              == (EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                  | EditorInvalidation::Viewport | EditorInvalidation::Toolbar));
        const SceneInstanceDef* added = c.document().findInstanceInScene(kSceneA, 100);
        CHECK(added != nullptr);
        CHECK(added->objectTypeId == "Enemy");
        CHECK(added->transform.position.x == 5.f);
        // Undo removes exactly the placed instance.
        CHECK(c.canUndo());
        c.undo();
        CHECK(c.document().findInstanceInScene(kSceneA, 100) == nullptr);
    }

    // -- CreateEntityCommand: invalid input does not mutate or invalidate ------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const uint64_t revBefore = c.document().revision();
        // Each failed command returns no invalidation of its own (§24.3); the
        // coordinator only raises a Console error, never a structural flag.
        CHECK(c.execute(CreateEntityCommand{kSceneA, kHero, "Dup", "Dup", {}}).invalidation
              == EditorInvalidation::None);                                       // id clash
        CHECK(!c.execute(CreateEntityCommand{kSceneA, 0, "Enemy", "E", {}}).ok);  // zero id
        CHECK(!c.execute(CreateEntityCommand{kSceneA, 5, "", "E", {}}).ok);       // empty type
        CHECK(!c.execute(CreateEntityCommand{"missing", 5, "Enemy", "E", {}}).ok);// no scene
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Hierarchy));
        CHECK(!has(inv, EditorInvalidation::Viewport));
        CHECK(c.document().revision() == revBefore);                  // state untouched
        CHECK(!c.canUndo());
        CHECK(c.document().findScene(kSceneA)->instances.size() == 1); // only kHero
    }

    // -- DeleteEntityCommand: remove, then undo restores order -----------------
    {
        EditorCoordinator c{makeDoc()};
        // Two more instances so order restoration is observable.
        CHECK(c.execute(CreateEntityCommand{kSceneA, 101, "Enemy", "E1", {}}).ok);
        CHECK(c.execute(CreateEntityCommand{kSceneA, 102, "Enemy", "E2", {}}).ok);
        // instances: [kHero, 101, 102]; delete the middle one.
        const auto r = c.execute(DeleteEntityCommand{kSceneA, 101});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::EntityRemoved);
        CHECK(c.document().findInstanceInScene(kSceneA, 101) == nullptr);
        CHECK(c.document().findScene(kSceneA)->instances.size() == 2);
        // Undo restores it at its original index (1), not appended at the end.
        c.undo();
        const auto& instances = c.document().findScene(kSceneA)->instances;
        CHECK(instances.size() == 3);
        CHECK(instances[1].id == 101);
    }

    // -- DeleteEntityCommand: missing instance fails without side effects ------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const uint64_t revBefore = c.document().revision();
        const auto r = c.execute(DeleteEntityCommand{kSceneA, 9999});
        CHECK(!r.ok);
        CHECK(r.invalidation == EditorInvalidation::None);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Hierarchy));
        CHECK(!has(inv, EditorInvalidation::Viewport));
        CHECK(c.document().revision() == revBefore);
        CHECK(!c.canUndo());
    }

    // -- DeleteSceneCommand: removes scene + instances, undo is exact ----------
    {
        EditorCoordinator c{makeDoc()};
        // kSceneA is the start scene and holds kHero.
        CHECK(c.document().startSceneId() == kSceneA);
        const auto r = c.execute(DeleteSceneCommand{kSceneA});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::SceneRemoved);
        // The command declares its own structural flags (incl. Toolbar: the set
        // of valid Play targets can change) …
        CHECK(r.invalidation == (EditorInvalidation::Hierarchy
                                 | EditorInvalidation::Viewport
                                 | EditorInvalidation::Project
                                 | EditorInvalidation::Toolbar));
        // … and the coordinator augments them after reconciling the workspace
        // (the active scene changed, so Inspector and Toolbar refresh too).
        const EditorInvalidation consumed = c.consumeInvalidations();
        CHECK(has(consumed, EditorInvalidation::Hierarchy));
        CHECK(has(consumed, EditorInvalidation::Viewport));
        CHECK(has(consumed, EditorInvalidation::Project));
        CHECK(has(consumed, EditorInvalidation::Inspector));
        CHECK(has(consumed, EditorInvalidation::Toolbar));
        CHECK(!c.document().hasScene(kSceneA));
        // Deleting the start scene reassigns it to a surviving scene.
        CHECK(c.document().startSceneId() == kSceneB);
        // Undo restores the scene, its instance, and the original start scene.
        c.undo();
        CHECK(c.document().hasScene(kSceneA));
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);
        CHECK(c.document().startSceneId() == kSceneA);
    }

    // -- DeleteSceneCommand: structural commands never trigger a Replace -------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        CHECK(c.execute(CreateEntityCommand{kSceneA, 200, "Enemy", "E", {}}).ok);
        CHECK(c.execute(DeleteEntityCommand{kSceneA, 200}).ok);
        CHECK(c.execute(DeleteSceneCommand{kSceneB}).ok);
        CHECK(c.document().replaceCount() == replacesBefore); // Patch, not Replace
    }

    // == Workspace reconciliation after structural deletes =====================
    // The document is the authority; EditorState must stay valid in the SAME
    // operation, with no follow-up callbacks or observers.

    // -- (1)(2) Deleting the active scene normalizes it and clears selection ----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.state().activeSceneId == kSceneA);     // start scene is the default focus
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.state().selection.primaryEntity == kHero);
        c.consumeInvalidations();

        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        // (1) active scene normalized to a surviving scene.
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.document().hasScene(c.state().activeSceneId));
        // (2) selection cleared — it belonged to the deleted scene.
        CHECK(!c.state().selection.hasEntity());
        // The coordinator augmented the command flags during reconciliation.
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Inspector));
    }

    // -- (3) Deleting a non-active scene keeps active scene and selection -------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.apply(SelectSceneIntent{kSceneB}).ok);
        CHECK(c.execute(CreateEntityCommand{kSceneB, 300, "Enemy", "E", {}}).ok);
        CHECK(c.apply(SelectEntityIntent{300}).ok);
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.state().selection.primaryEntity == 300);

        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok); // delete the OTHER scene
        CHECK(c.state().activeSceneId == kSceneB);        // unchanged
        CHECK(c.state().selection.primaryEntity == 300);  // unchanged
    }

    // -- (4) Deleting the selected entity clears the selection -----------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.state().selection.primaryEntity == kHero);
        c.consumeInvalidations();
        CHECK(c.execute(DeleteEntityCommand{kSceneA, kHero}).ok);
        CHECK(!c.state().selection.hasEntity());
        // DeleteEntity alone returns Hierarchy|Viewport; reconciliation adds
        // Inspector so the now-empty inspector refreshes.
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Inspector));
    }

    // -- (5) Deleting a different entity preserves the selection ---------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(CreateEntityCommand{kSceneA, 301, "Enemy", "E", {}}).ok);
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.state().selection.primaryEntity == kHero);
        CHECK(c.execute(DeleteEntityCommand{kSceneA, 301}).ok); // delete the OTHER one
        CHECK(c.state().selection.primaryEntity == kHero);      // preserved
    }

    // -- (6) No per-scene view state outlives its scene ------------------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 2.f});
        c.apply(SetViewportZoomIntent{kSceneB, 3.f});
        CHECK(c.state().sceneViews.count(kSceneA) == 1);
        CHECK(c.state().sceneViews.count(kSceneB) == 1);
        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        CHECK(c.state().sceneViews.count(kSceneA) == 0);  // pruned
        CHECK(c.state().sceneViews.count(kSceneB) == 1);  // kept
    }

    // -- (7) No dangling workspace id after execute OR undo --------------------
    // Undo restores the DOCUMENT, not the workspace: the brought-back scene is
    // not re-activated and the brought-back entity is not re-selected.
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        CHECK(c.document().hasScene(c.state().activeSceneId));        // valid after execute
        CHECK(!c.state().selection.hasEntity());

        c.undo();
        CHECK(c.document().hasScene(kSceneA));                        // document restored
        CHECK(c.state().activeSceneId == kSceneB);                   // workspace NOT rewound
        CHECK(c.document().hasScene(c.state().activeSceneId));        // still valid
        CHECK(!c.state().selection.hasEntity());                     // not auto-reselected
    }

    // -- Empty project is a valid state: no fabricated scene -------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        CHECK(c.execute(DeleteSceneCommand{kSceneB}).ok);
        CHECK(c.document().data().scenes.empty());
        CHECK(c.state().activeSceneId.empty());   // no fake scene invented
        CHECK(!c.state().selection.hasEntity());
        CHECK(c.state().sceneViews.empty());
    }

    // == Hierarchy actions (the restricted UI wiring, tested UI-free) ==========
    // The Hierarchy panel is a thin shim over these functions; testing them here
    // proves the RmlUi click -> command -> reconcile -> invalidation path without
    // any RmlUi dependency.

    // (14) No action can reach a mutable ProjectDocument: document() is const-only.
    static_assert(std::is_same_v<decltype(std::declval<EditorCoordinator>().document()),
                                 const ProjectDocument&>,
                  "actions must only see a read-only document");

    // -- (1)(2)(13) Add Entity runs exactly one command with a non-colliding id -
    {
        EditorCoordinator c{makeDoc()};
        CHECK(nextAvailableEntityId(c.document(), kSceneA) == 43); // past kHero (42)
        const auto r = addEntity(c);
        CHECK(r.ok);
        CHECK(c.undoSize() == 1);                                  // exactly one command
        CHECK(c.document().findInstanceInScene(kSceneA, 43) != nullptr);
        CHECK(c.document().findInstanceInScene(kSceneA, 43)->id != kHero); // no collision
        CHECK(c.document().findInstanceInScene(kSceneA, 43)->transform.position.x == 256.f);
        CHECK(c.document().findInstanceInScene(kSceneA, 43)->transform.position.y == 160.f);
    }

    // -- Default spawn uses the visible viewport centre, not world origin -----
    {
        ViewportRect rect{100, 50, 800, 600};
        EditorSceneViewState view;
        const Vec2 spawn = defaultSpawnPosition(rect, view, Vec2{512.f, 320.f});
        CHECK(spawn.x == 256.f);
        CHECK(spawn.y == 160.f);

        view.zoom = 2.f;
        view.pan = Vec2{40.f, -30.f};
        const Vec2 panned = defaultSpawnPosition(rect, view, Vec2{512.f, 320.f});
        CHECK(panned.x == 296.f);
        CHECK(panned.y == 130.f);
    }

    // -- Spawn normalization snaps then clamps inside the scene ---------------
    {
        SpawnPositionOptions snap;
        snap.snapToGrid = true;
        snap.gridSize = 48.f;
        snap.edgeMargin = 16.f;
        const Vec2 snapped = normalizeSpawnPosition(Vec2{31.f, 73.f}, Vec2{512.f, 320.f}, snap);
        CHECK(snapped.x == 48.f);
        CHECK(snapped.y == 96.f);

        const Vec2 clamped = normalizeSpawnPosition(Vec2{-100.f, 1000.f}, Vec2{512.f, 320.f});
        CHECK(clamped.x == 16.f);
        CHECK(clamped.y == 304.f);
    }

    // -- unoccupiedSpawnPosition: default spawns cascade off occupied spots ---
    {
        const Vec2 sceneSize{512.f, 320.f};
        SceneDef scene;
        scene.id = kSceneA;

        // Free spot: unchanged.
        CHECK(unoccupiedSpawnPosition(scene, Vec2{256.f, 160.f}, sceneSize).x == 256.f);

        // One instance on the candidate: one diagonal step (half the default
        // 32 wu grid cell = 16 wu).
        SceneInstanceDef first;
        first.id = 1;
        first.transform.position = {256.f, 160.f};
        scene.instances.push_back(first);
        const Vec2 nudged = unoccupiedSpawnPosition(scene, Vec2{256.f, 160.f}, sceneSize);
        CHECK(nudged.x == 272.f);
        CHECK(nudged.y == 176.f);

        // The next spot occupied too: cascades a second step.
        SceneInstanceDef second;
        second.id = 2;
        second.transform.position = {272.f, 176.f};
        scene.instances.push_back(second);
        const Vec2 twice = unoccupiedSpawnPosition(scene, Vec2{256.f, 160.f}, sceneSize);
        CHECK(twice.x == 288.f);
        CHECK(twice.y == 192.f);

        // Pinned at the scene corner: the walk terminates and returns the
        // clamped candidate even if it is still occupied.
        SceneInstanceDef corner;
        corner.id = 3;
        corner.transform.position = {496.f, 304.f};   // == clamp target (margin 16)
        scene.instances.push_back(corner);
        const Vec2 pinned = unoccupiedSpawnPosition(scene, Vec2{496.f, 304.f}, sceneSize);
        CHECK(pinned.x == 496.f);
        CHECK(pinned.y == 304.f);

        // An explicit far-away candidate is never dragged toward occupied spots.
        const Vec2 free = unoccupiedSpawnPosition(scene, Vec2{100.f, 100.f}, sceneSize);
        CHECK(free.x == 100.f);
        CHECK(free.y == 100.f);
    }

    // -- Structural commands refresh the Inspector too: the Scene Inspector
    // shows the entity count and the outside-bounds diagnostic, so create and
    // delete must invalidate it even with no entity selected ------------------
    {
        EditorCoordinator c{makeDoc()};
        const auto created = c.execute(
            CreateEntityCommand{kSceneA, 900, "Enemy", "Enemy 900", {5.f, 6.f}});
        CHECK(created.ok);
        CHECK(has(created.invalidation, EditorInvalidation::Inspector));
        CHECK(has(created.invalidation, EditorInvalidation::Hierarchy));
        CHECK(has(created.invalidation, EditorInvalidation::Viewport));

        const auto removed = c.execute(DeleteEntityCommand{kSceneA, 900});
        CHECK(removed.ok);
        CHECK(has(removed.invalidation, EditorInvalidation::Inspector));
    }

    // -- Explicit placement is passed through the single create command path --
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(addEntityAt(c, Vec2{123.f, 234.f}).ok);
        const EntityId id = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, id);
        CHECK(inst != nullptr);
        CHECK(inst->transform.position.x == 123.f);
        CHECK(inst->transform.position.y == 234.f);

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, id) == nullptr);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, id)->transform.position.x == 123.f);
        CHECK(c.document().findInstanceInScene(kSceneA, id)->transform.position.y == 234.f);
    }

    // -- (3) Add Entity invalidates only the expected views --------------------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        CHECK(addEntity(c).ok);
        // CreateEntity declares Hierarchy|Inspector|Viewport (the Scene
        // Inspector shows the entity count); selection unchanged and active
        // scene valid, so reconciliation adds nothing.
        CHECK(c.consumeInvalidations()
              == (EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
                  | EditorInvalidation::Viewport | EditorInvalidation::Toolbar));
    }

    // -- (4) Add Entity without an active scene mutates nothing -----------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(deleteScene(c, kSceneA).ok);
        CHECK(deleteScene(c, kSceneB).ok);     // now the project has no scenes
        CHECK(c.state().activeSceneId.empty());
        const uint64_t revBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();
        c.consumeInvalidations();
        CHECK(!addEntity(c).ok);               // precondition fails, no command runs
        CHECK(c.document().revision() == revBefore);
        CHECK(c.undoSize() == undoBefore);
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
    }

    // -- Empty catalog: Add Entity creates a real object type + instance -------
    //    Regression for "Unknown object type: Entity": the first entity in a
    //    catalog-less project must produce a real, persisted object type (never a
    //    sentinel id) so object-type-scoped components are usable at once.
    {
        ProjectDoc fresh;
        fresh.projectName = "fresh";
        fresh.activeSceneId = "s";
        SceneDef scene; scene.id = "s"; scene.name = "S";
        fresh.scenes.emplace("s", scene);

        EditorCoordinator c{fresh};
        CHECK(c.document().data().objectTypes.empty());     // empty catalog
        CHECK(addEntity(c).ok);
        CHECK(c.undoSize() == 1);                           // (1) one command / one undo entry

        // (1)(2) a real object type now exists and the instance points to it.
        CHECK(c.document().data().objectTypes.size() == 1);
        const std::string typeId = c.document().data().objectTypes.begin()->first;
        CHECK(typeId != "Entity");                          // (9) no sentinel id as objectTypeId
        const SceneInstanceDef* inst = c.document().findInstanceInScene("s", 1);
        CHECK(inst != nullptr);
        CHECK(inst->objectTypeId == typeId);                // (2) references the real type
        CHECK(c.document().hasObjectType(inst->objectTypeId));
        // Visual name mirrors the first instance ("Entity <entityId>") so
        // auto-created types are tellable apart in the Create menu catalog.
        CHECK(c.document().findObjectType(typeId)->name == "Entity 1");
        // Placeholder fill defaults to neutral zinc, never the struct's white
        // (invisible on light scenes, blinding on the dark default).
        CHECK(c.document().findObjectType(typeId)->sprite.fillColor.x == 0.42f);
        CHECK(c.document().findObjectType(typeId)->sprite.fillColor.z == 0.52f);

        // (3) object-type-scoped component commands work immediately (the type
        //     resolves). BoxCollider is not a movement driver, so it coexists with
        //     one driver; a second driver would be rejected (tested separately).
        CHECK(c.execute(AddTopDownControllerCommand{typeId}).ok);
        CHECK(c.document().data().objectTypes.at(typeId).topDownController.has_value());
        CHECK(c.execute(AddBoxColliderCommand{typeId}).ok);
        CHECK(c.document().data().objectTypes.at(typeId).boxCollider2D.has_value());
    }

    // -- Undo/redo of the first entity restores both type and instance ---------
    {
        ProjectDoc fresh;
        fresh.activeSceneId = "s";
        SceneDef scene; scene.id = "s"; scene.name = "S";
        fresh.scenes.emplace("s", scene);

        EditorCoordinator c{fresh};
        CHECK(addEntity(c).ok);
        const std::string typeId = c.document().data().objectTypes.begin()->first;

        // (4) undo removes BOTH the instance and the object type it created.
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene("s", 1) == nullptr);
        CHECK(c.document().data().objectTypes.empty());

        // (5) redo restores both with the same ids (no fresh generation).
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene("s", 1) != nullptr);
        CHECK(c.document().data().objectTypes.size() == 1);
        CHECK(c.document().data().objectTypes.begin()->first == typeId);
        CHECK(c.document().findInstanceInScene("s", 1)->objectTypeId == typeId);
    }

    // -- (6) Add Entity always creates a NEW independent object type -----------
    //    "+Entity" must never reuse an existing type (that is "Add Instance"):
    //    each entity gets its own ObjectTypeId so the object-type-owned components
    //    stay independent across entities.
    {
        EditorCoordinator c{makeInheritedDoc()};            // catalog already has "Hero"
        CHECK(c.document().hasObjectType("Hero"));
        const std::size_t typesBefore = c.document().data().objectTypes.size();

        CHECK(addEntity(c).ok);                             // first +Entity
        const EntityId idA = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::string typeA = c.document().findInstanceInScene(kSceneA, idA)->objectTypeId;
        CHECK(addEntity(c).ok);                             // second +Entity
        const EntityId idB = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::string typeB = c.document().findInstanceInScene(kSceneA, idB)->objectTypeId;

        // (1)(2)(3) two new, distinct types — neither reuses "Hero" nor each other.
        CHECK(c.document().data().objectTypes.size() == typesBefore + 2);
        CHECK(typeA != "Hero");
        CHECK(typeB != "Hero");
        CHECK(typeA != typeB);

        // (4)(5) a component on A's type does not appear on B's type.
        CHECK(c.execute(AddTopDownControllerCommand{typeA}).ok);
        CHECK(c.document().data().objectTypes.at(typeA).topDownController.has_value());
        CHECK(!c.document().data().objectTypes.at(typeB).topDownController.has_value());
        CHECK(c.execute(AddBoxColliderCommand{typeB}).ok);
        CHECK(c.document().data().objectTypes.at(typeB).boxCollider2D.has_value());
        CHECK(!c.document().data().objectTypes.at(typeA).boxCollider2D.has_value());
    }

    // -- (6b) Undo of the second Add removes only its type + instance ----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(addEntity(c).ok);
        const EntityId idA = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::string typeA = c.document().findInstanceInScene(kSceneA, idA)->objectTypeId;
        CHECK(addEntity(c).ok);
        const EntityId idB = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::string typeB = c.document().findInstanceInScene(kSceneA, idB)->objectTypeId;

        // (6) undo the second +Entity: only B's type and instance go.
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, idB) == nullptr);
        CHECK(!c.document().hasObjectType(typeB));
        CHECK(c.document().findInstanceInScene(kSceneA, idA) != nullptr);
        CHECK(c.document().hasObjectType(typeA));            // A untouched

        // (7) redo restores B with the same ids.
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, idB)->objectTypeId == typeB);
        CHECK(c.document().hasObjectType(typeB));
    }

    // -- (7) A failed create-with-default-type makes no partial mutation -------
    {
        EditorCoordinator c{makeInheritedDoc()};            // "Hero" already exists
        const std::size_t typesBefore = c.document().data().objectTypes.size();
        const uint64_t revBefore = c.document().revision();
        // objectTypeId collides -> apply fails before any mutation.
        CHECK(!c.execute(CreateEntityWithDefaultTypeCommand{
                  kSceneA, 999, "Hero", "Entity", "X"}).ok);
        CHECK(c.document().data().objectTypes.size() == typesBefore);
        CHECK(c.document().findInstanceInScene(kSceneA, 999) == nullptr);
        CHECK(c.document().revision() == revBefore);        // no partial state
    }

    // -- Create type + instance is one staged authoring mutation --------------
    {
        ProjectDoc fresh;
        SceneDef scene;
        scene.id = "s";
        scene.name = "S";
        scene.layers.push_back(SceneLayerDef{"layer-1", "Layer 1", false});
        scene.defaultLayerId = "layer-1";
        fresh.scenes.emplace("s", scene);
        fresh.activeSceneId = "s";
        EditorCoordinator c{std::move(fresh)};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Entity", "Entity", {}, "layer-1"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);
        CHECK(c.document().hasObjectType("obj-1"));
        CHECK(c.document().findInstanceInScene("s", 1) != nullptr);
        CHECK(c.undo().ok);
        CHECK(!c.document().hasObjectType("obj-1"));
        CHECK(c.document().findInstanceInScene("s", 1) == nullptr);
        CHECK(c.redo().ok);
        CHECK(c.document().hasObjectType("obj-1"));
        CHECK(c.document().findInstanceInScene("s", 1) != nullptr);
    }

    // -- (8) Save/reload keeps the two created types distinct ------------------
    {
        ProjectDoc fresh;
        fresh.activeSceneId = "s";
        SceneDef scene; scene.id = "s"; scene.name = "S";
        fresh.scenes.emplace("s", scene);

        EditorCoordinator c{fresh};
        CHECK(addEntity(c).ok);
        const EntityId idA = nextAvailableEntityId(c.document(), "s") - 1;
        const std::string typeA = c.document().findInstanceInScene("s", idA)->objectTypeId;
        CHECK(addEntity(c).ok);
        const EntityId idB = nextAvailableEntityId(c.document(), "s") - 1;
        const std::string typeB = c.document().findInstanceInScene("s", idB)->objectTypeId;
        CHECK(typeA != typeB);

        const std::filesystem::path path = testTempDir() / "default-type.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);

        EditorCoordinator reloaded{makeReplacementDoc()};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().hasObjectType(typeA));
        CHECK(reloaded.document().hasObjectType(typeB));    // both types survive, still distinct
        CHECK(reloaded.document().findInstanceInScene("s", idA)->objectTypeId == typeA);
        CHECK(reloaded.document().findInstanceInScene("s", idB)->objectTypeId == typeB);
    }

    // -- Add Instance: another instance of the selected entity's type ----------
    //    Unlike +Entity, +Instance reuses the chosen ObjectTypeId (shared
    //    components, no ObjectTypeDef duplicated) and selects the new instance.
    {
        EditorCoordinator c{makeInheritedDoc()};            // kHero -> "Hero" type w/ sprite
        c.apply(SelectEntityIntent{kHero});
        const std::size_t typesBefore = c.document().data().objectTypes.size();

        CHECK(addInstanceOfSelectedType(c).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(newId != kHero);                                          // (1) new EntityId
        CHECK(inst != nullptr);
        CHECK(inst->objectTypeId == "Hero");                           // (2) reuses the type
        CHECK(c.document().data().objectTypes.size() == typesBefore);  // no type duplicated
        CHECK(c.selection().primaryEntity == newId);                  // (12) selected via intent

        // (3) independent Transform (its own, not the source's {10,20}).
        const SceneInstanceDef* src = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK((inst->transform.position.x != src->transform.position.x)
              || (inst->transform.position.y != src->transform.position.y));

        // (4)(5) a component added to the type materializes on BOTH instances.
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.playProject().ok);
        CHECK(c.playSession()->findEntity(kHero)->collider.has_value());
        CHECK(c.playSession()->findEntity(newId)->collider.has_value());
        CHECK(c.stopPlaying().ok);
    }

    // -- Add Instance uses the same explicit placement path -------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(addInstanceOfSelectedTypeAt(c, Vec2{200.f, 120.f}).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(inst != nullptr);
        CHECK(inst->objectTypeId == "Hero");
        CHECK(inst->transform.position.x == 200.f);
        CHECK(inst->transform.position.y == 120.f);

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId) == nullptr);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId)->transform.position.x == 200.f);
        CHECK(c.document().findInstanceInScene(kSceneA, newId)->transform.position.y == 120.f);
    }

    // -- (7)(8) Undo removes only the instance; the type survives; redo restores
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(addInstanceOfSelectedType(c).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId) == nullptr);  // instance gone
        CHECK(c.document().hasObjectType("Hero"));                            // type kept
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);   // source kept

        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId)->objectTypeId == "Hero");
    }

    // -- (9) Save/reload keeps the instance <-> type relation ------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(addInstanceOfSelectedType(c).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const std::filesystem::path path = testTempDir() / "add-instance.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().findInstanceInScene(kSceneA, newId)->objectTypeId == "Hero");
        CHECK(reloaded.document().findInstanceInScene(kSceneA, kHero)->objectTypeId == "Hero");
        CHECK(reloaded.document().hasObjectType("Hero"));
    }

    // -- Use Existing Type: places an instance of an EXPLICIT type id, no
    // selection required (unlike +Instance, which derives the type from it) ---
    {
        EditorCoordinator c{makeInheritedDoc()};             // catalog has "Hero"
        // Deliberately no SelectEntityIntent applied - proves this path does not
        // depend on the current selection at all.
        CHECK(addInstanceOfType(c, "Hero").ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(inst != nullptr);
        CHECK(inst->objectTypeId == "Hero");
        CHECK(c.selection().primaryEntity == newId);         // still auto-selects the new instance
    }

    // -- Use Existing Type: unknown type id fails without mutation -------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const std::size_t before = c.document().findScene(kSceneA)->instances.size();
        CHECK(!addInstanceOfType(c, "NoSuchType").ok);
        CHECK(c.document().findScene(kSceneA)->instances.size() == before);
    }

    // -- Use Existing Type at an explicit position (the *At variant) -----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(addInstanceOfTypeAt(c, "Hero", Vec2{77.f, 88.f}).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(inst->transform.position.x == 77.f);
        CHECK(inst->transform.position.y == 88.f);
    }

    // -- Clone Instance: copies type, uniquifies name, honors an explicit
    // position; undo removes only the clone, redo restores it -----------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA);
        CHECK(c.execute(CloneInstanceCommand{kSceneA, kHero, newId, "Hero 2", Vec2{34.f, 44.f}}).ok);
        const SceneInstanceDef* clone = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(clone != nullptr);
        CHECK(clone->objectTypeId == "Hero");
        CHECK(clone->instanceName == "Hero 2");
        CHECK(clone->transform.position.x == 34.f);
        CHECK(clone->transform.position.y == 44.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);  // source untouched

        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId) == nullptr);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, newId)->instanceName == "Hero 2");
    }

    // -- Clone Instance: instance deltas and layer survive the struct copy -----
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(addSpriteRenderer(c).ok);
        CHECK(c.execute(AddSceneLayerCommand{kSceneA, "fg", "Foreground", 0}).ok);
        CHECK(c.execute(SetEntityLayerCommand{kSceneA, kHero, "fg"}).ok);

        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA);
        CHECK(c.execute(CloneInstanceCommand{kSceneA, kHero, newId, "Hero Clone", Vec2{}}).ok);
        const SceneInstanceDef* clone = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(clone != nullptr);
        CHECK(clone && !clone->spriteRendererOverride);
        CHECK(resolveSpriteRenderer(c.document(), kSceneA, newId).present);
        CHECK(clone->layerId == "fg");
    }

    // -- Clone Instance: guards mirror CreateEntityCommand's (id/name/scene),
    // plus its own (unknown source instance); none mutate the scene -----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const std::size_t before = c.document().findScene(kSceneA)->instances.size();
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, kHero, kHero, "Dup", {}}).ok);   // id clash
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, kHero, 0, "Zero", {}}).ok);      // zero id
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, kHero, 900, "", {}}).ok);        // empty name
        CHECK(!c.execute(CloneInstanceCommand{"missing", kHero, 900, "X", {}}).ok);     // no scene
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, 9999, 900, "X", {}}).ok);        // no source
        CHECK(c.document().findScene(kSceneA)->instances.size() == before);
    }

    // -- cloneSelectedEntity: no selection -> failure without mutation ---------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const std::size_t before = c.document().findScene(kSceneA)->instances.size();
        CHECK(!cloneSelectedEntity(c).ok);
        CHECK(c.document().findScene(kSceneA)->instances.size() == before);
    }

    // -- cloneSelectedEntity: clones the selection, offsets the position, and
    // selects the new instance --------------------------------------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(cloneSelectedEntity(c).ok);
        const EntityId newId = nextAvailableEntityId(c.document(), kSceneA) - 1;
        const SceneInstanceDef* clone = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(clone != nullptr);
        CHECK(clone->objectTypeId == "Hero");
        CHECK(clone->instanceName != "Hero");             // uniquified
        CHECK(clone->transform.position.x != 10.f);        // offset from source (10, 20)
        CHECK(clone->transform.position.y != 20.f);
        CHECK(c.selection().primaryEntity == newId);       // auto-selected
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);  // source kept
    }

    // -- (10) Empty catalog: Add Instance is a no-op, never a placeholder ------
    {
        ProjectDoc fresh; fresh.activeSceneId = "s";
        SceneDef scene; scene.id = "s"; scene.name = "S";
        fresh.scenes.emplace("s", scene);
        EditorCoordinator c{fresh};
        CHECK(!addInstanceOfSelectedType(c).ok);                  // no selection -> fail
        CHECK(c.document().data().objectTypes.empty());           // no "Entity" placeholder
        CHECK(c.document().findScene("s")->instances.empty());    // no instance
    }

    // -- (11) During Play, Add Instance is rejected without mutation -----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(c.playProject().ok);
        const std::size_t before = c.document().findScene(kSceneA)->instances.size();
        CHECK(!addInstanceOfSelectedType(c).ok);
        CHECK(c.document().findScene(kSceneA)->instances.size() == before);
        CHECK(c.stopPlaying().ok);
    }

    // -- (5) Delete Entity uses the authoritative scene + selection ------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(deleteSelectedEntity(c).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) == nullptr);
    }

    // -- (6) Deleting the selected entity empties the selection (reconcile) -----
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        c.consumeInvalidations();
        CHECK(deleteSelectedEntity(c).ok);
        CHECK(!c.state().selection.hasEntity());
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Inspector));
    }

    // -- (7) A failed Delete Entity triggers no panel refresh ------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(!c.selection().hasEntity());
        c.consumeInvalidations();
        const std::size_t undoBefore = c.undoSize();
        CHECK(!deleteSelectedEntity(c).ok);    // nothing selected -> no command
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Hierarchy));
        CHECK(!has(inv, EditorInvalidation::Viewport));
        CHECK(c.undoSize() == undoBefore);
    }

    // -- (8) Add Scene opens the new scene (create-and-open), via an Intent, not
    // by the command reaching into EditorState itself --------------------------
    {
        EditorCoordinator c{makeDoc()};
        const SceneId expectedId = makeUniqueSceneId(c.document());   // "scene-1"
        const auto r = addScene(c);
        CHECK(r.ok);
        CHECK(c.state().activeSceneId == expectedId);      // new scene is now open
        CHECK(!c.state().selection.hasEntity());
        CHECK(c.document().hasScene(expectedId));          // the new scene now exists
    }

    // -- (9) Delete active scene normalizes the workspace in the same cycle ----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.state().activeSceneId == kSceneA);
        CHECK(deleteScene(c, kSceneA).ok);
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.document().hasScene(c.state().activeSceneId));
    }

    // -- (10) Delete non-active scene preserves scene + selection --------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.apply(SelectSceneIntent{kSceneB}).ok);
        CHECK(addEntity(c).ok);                            // an entity in B
        const EntityId placed = nextAvailableEntityId(c.document(), kSceneB) - 1;
        CHECK(c.apply(SelectEntityIntent{placed}).ok);
        CHECK(deleteScene(c, kSceneA).ok);                 // delete the OTHER scene
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.state().selection.primaryEntity == placed);
    }

    // -- (11)(12) Undo refreshes via command invalidations, not the workspace --
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SelectEntityIntent{kHero});
        CHECK(deleteSelectedEntity(c).ok);                 // selection cleared
        c.consumeInvalidations();
        c.undo();                                          // entity comes back
        // (11) the Hierarchy is told to refresh by the command's own invalidation.
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Hierarchy));
        CHECK(c.document().findInstanceInScene(kSceneA, kHero) != nullptr);
        // (12) the brought-back entity is NOT auto-reselected.
        CHECK(!c.state().selection.hasEntity());
    }

    // -- (12b) Undo of delete-active-scene does not re-activate the scene ------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(deleteScene(c, kSceneA).ok);                 // active -> kSceneB
        CHECK(c.state().activeSceneId == kSceneB);
        c.undo();                                          // scene A back in document
        CHECK(c.document().hasScene(kSceneA));
        CHECK(c.state().activeSceneId == kSceneB);         // workspace not rewound
    }

    // -- (15) No Hierarchy action path performs a Replace ----------------------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        CHECK(addScene(c).ok);
        const SceneId newScene = c.state().activeSceneId;   // addScene opens what it created
        CHECK(addEntity(c).ok);                             // lands in the new scene
        c.apply(SelectEntityIntent{nextAvailableEntityId(c.document(), newScene) - 1});
        CHECK(deleteSelectedEntity(c).ok);
        CHECK(deleteScene(c, kSceneB).ok);
        CHECK(c.document().replaceCount() == replacesBefore);   // Patch, never Replace
    }

    // == Play guards: an action without a valid target must be unavailable =====
    // The guard lives in the coordinator (the point that creates the session),
    // so a shortcut, menu or programmatic call cannot bypass a disabled button.

    // -- (1)(2)(3)(4) Empty project: both modes unavailable and rejected -------
    {
        EditorCoordinator c{ProjectDoc{}};             // zero scenes
        CHECK(c.document().data().scenes.empty());
        CHECK(!c.canPlayProject());                    // (1)
        CHECK(!c.canPlayCurrentScene());               // (2)
        c.consumeInvalidations();
        const auto r = c.playProject();
        CHECK(!r.ok);                                  // (3) application-level rejection
        CHECK(!c.isPlaying());                         // (4) no PlaySession created
        CHECK(c.playSession() == nullptr);
        // The rejection is logged to the Console rather than silently dropped,
        // which is itself an invalidation.
        CHECK(c.consumeInvalidations() == EditorInvalidation::Console);
    }

    // -- (5) Creating the first scene invalidates the Toolbar ------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        c.consumeInvalidations();
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Toolbar));
    }

    // -- (6) Play Project enables when the start scene is valid ----------------
    {
        EditorCoordinator c{makeDoc()};                // startSceneId == kSceneA
        CHECK(c.canPlayProject());
        const auto r = c.playProject();
        CHECK(r.ok);
        CHECK(c.isPlaying());
        CHECK(c.playSession() != nullptr);
        CHECK(c.playSession()->sceneId() == kSceneA);  // Play Project target = start scene
        // Play does not touch the authoring document.
        CHECK(!c.document().isDirty());
    }

    // -- (7) Without an active scene, Play Current Scene stays disabled --------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.state().activeSceneId.empty());
        CHECK(!c.canPlayCurrentScene());
        CHECK(!c.playCurrentScene().ok);
        CHECK(!c.isPlaying());
    }

    // -- Start/Stop Play re-render the authoring-affordance panels -------------
    //    The authoring document is frozen during Play, so the Inspector,
    //    Hierarchy and Assets panels must re-render to disable their authoring
    //    controls on Start — and re-enable them on Stop.
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        CHECK(c.playProject().ok);
        const EditorInvalidation started = c.consumeInvalidations();
        CHECK(has(started, EditorInvalidation::Inspector));
        CHECK(has(started, EditorInvalidation::Hierarchy));
        CHECK(has(started, EditorInvalidation::Assets));
        CHECK(c.stopPlaying().ok);
        const EditorInvalidation stopped = c.consumeInvalidations();
        CHECK(has(stopped, EditorInvalidation::Inspector));
        CHECK(has(stopped, EditorInvalidation::Hierarchy));
        CHECK(has(stopped, EditorInvalidation::Assets));
    }

    // -- (8) Selecting a scene enables Play Current Scene ----------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.apply(SelectSceneIntent{kSceneB}).ok);
        CHECK(c.canPlayCurrentScene());
        const auto r = c.playCurrentScene();
        CHECK(r.ok);
        CHECK(c.playSession()->sceneId() == kSceneB);  // target = active scene
    }

    // -- (9) Deleting the last scene disables both modes in the same cycle -----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(DeleteSceneCommand{kSceneA}).ok);
        CHECK(c.execute(DeleteSceneCommand{kSceneB}).ok);
        CHECK(c.document().data().scenes.empty());
        CHECK(!c.canPlayProject());
        CHECK(!c.canPlayCurrentScene());
    }

    // -- (10) A direct (shortcut/programmatic) Play cannot bypass the guard ----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(!c.playProject().ok);        // no button involved — the app path itself rejects
        CHECK(!c.playCurrentScene().ok);
        CHECK(!c.isPlaying());
    }

    // -- (11) A dangling startSceneId prevents Play Project --------------------
    {
        EditorCoordinator c{makeInvalidStartDoc()};    // startSceneId references no scene
        CHECK(!c.document().hasScene(c.document().startSceneId()));
        CHECK(!c.canPlayProject());
        CHECK(!c.playProject().ok);
    }

    // -- (12) A dangling activeSceneId prevents Play Current Scene -------------
    {
        EditorCoordinator c{makeInvalidStartDoc()};    // active scene also dangling
        CHECK(!c.document().hasScene(c.state().activeSceneId));
        CHECK(!c.canPlayCurrentScene());
        CHECK(!c.playCurrentScene().ok);
    }

    // -- Play/Stop round-trip: Stop only while playing, document untouched -----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(!c.stopPlaying().ok);        // not playing yet
        CHECK(c.playProject().ok);
        CHECK(c.isPlaying());
        CHECK(c.stopPlaying().ok);
        CHECK(!c.isPlaying());
        CHECK(!c.document().isDirty());    // never mutated by Play/Stop
    }

    // -- Play materializes runtime sprite data and used assets only ------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.playProject().ok);
        CHECK(c.playSession() != nullptr);
        CHECK(c.playSession()->entities().size() == 1);
        CHECK(c.playSession()->entities()[0].sprite.has_value());
        CHECK(c.playSession()->entities()[0].sprite->assetId == "img-hero");
        CHECK(c.playSession()->entities()[0].sprite->visible);
        CHECK(c.playSession()->assets().imageAssets.size() == 1);
        CHECK(c.playSession()->assets().imageAssets.count("img-hero") == 1);
        CHECK(c.playSession()->assets().imageAssets.count("img-alt") == 0);

        const SceneFrameSnapshot playFrame = collectSceneFrameSnapshot(*c.playSession());
        CHECK(playFrame.hasScene);
        CHECK(playFrame.sprites.size() == 1);
        CHECK(playFrame.sprites[0].assetId == "img-hero");
        CHECK(playFrame.sprites[0].destination.x == -6.f); // x=10, width=32
    }

    // -- A materialized PlaySession is independent from later authoring edits --
    {
        EditorCoordinator c{makeInheritedDoc()};
        std::optional<PlaySession> session = PlaySession::startProject(c.document());
        CHECK(session.has_value()); // inherited img-hero at x=10
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {50.f, 20.f}}).ok);
        SpriteRendererOverride delta;
        delta.imageAssetId = "img-alt";
        delta.animationAssetId = AssetId{};
        CHECK(c.execute(SetInstanceSpriteOverrideCommand{kSceneA, kHero, delta}).ok);

        const SceneFrameSnapshot editFrame =
            collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        CHECK(editFrame.sprites.size() == 1);
        CHECK(editFrame.sprites[0].assetId == "img-alt");
        CHECK(editFrame.sprites[0].destination.x == 34.f); // x=50, width=32

        const SceneFrameSnapshot playFrame = collectSceneFrameSnapshot(*session);
        CHECK(playFrame.sprites.size() == 1);
        CHECK(playFrame.sprites[0].assetId == "img-hero");
        CHECK(playFrame.sprites[0].destination.x == -6.f); // still x=10

        session->entities()[0].transform.position = {500.f, 600.f};
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 50.f);
        CHECK(c.document().revision() > 0);
    }

    // -- Play materialization covers absence, visibility, and dangling assets ---
    {
        EditorCoordinator noSprite{makeSpriteDoc()};
        CHECK(noSprite.playProject().ok);
        CHECK(noSprite.playSession()->entities().size() == 1);
        CHECK(!noSprite.playSession()->entities()[0].sprite.has_value());
        CHECK(noSprite.playSession()->assets().imageAssets.empty());

        EditorCoordinator invisible{makeInheritedDoc()};
        SpriteRendererOverride invisibleDelta;
        invisibleDelta.imageAssetId = "img-alt";
        invisibleDelta.animationAssetId = AssetId{};
        invisibleDelta.visible = false;
        CHECK(invisible.execute(SetInstanceSpriteOverrideCommand{
            kSceneA, kHero, invisibleDelta}).ok);
        CHECK(invisible.playProject().ok);
        CHECK(invisible.playSession()->entities()[0].sprite.has_value());
        CHECK(invisible.playSession()->entities()[0].sprite->assetId == "img-alt");
        CHECK(!invisible.playSession()->entities()[0].sprite->visible);
        CHECK(invisible.playSession()->assets().imageAssets.size() == 1);
        CHECK(invisible.playSession()->assets().imageAssets.count("img-alt") == 1);

        ProjectDoc danglingDoc = makeInheritedDoc();
        danglingDoc.objectTypes.at("Hero").spriteRenderer->imageAssetId = "missing-image";
        EditorCoordinator dangling{danglingDoc};
        CHECK(!dangling.playProject().ok);
        CHECK(!dangling.isPlaying());
    }

    // -- Project replace has an explicit policy during Play --------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.playProject().ok);
        const uint32_t replacesBefore = c.document().replaceCount();
        CHECK(!c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.isPlaying());
        CHECK(c.document().replaceCount() == replacesBefore);
    }

    // -- Authoring commands and undo are blocked while Play is running --------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {25.f, 20.f}}).ok);
        CHECK(c.canUndo());
        const uint64_t revisionBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();

        CHECK(c.playProject().ok);
        CHECK(c.isPlaying());
        CHECK(!c.execute(SetEntityPositionCommand{kSceneA, kHero, {99.f, 20.f}}).ok);
        CHECK(!c.execute(SetInstanceSpriteOverrideCommand{
            kSceneA, kHero, SpriteRendererOverride{}}).ok);
        CHECK(!c.undo().ok);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.undoSize() == undoBefore);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 25.f);
    }

    // -- A blocked authoring command only emits an intentional Console warning -
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.playProject().ok);
        c.consumeInvalidations(); // discard Start Play Toolbar | Viewport | Console
        const std::size_t logBefore = c.consoleLog().size();
        const uint64_t revisionBefore = c.document().revision();

        CHECK(!c.execute(SetEntityPositionCommand{kSceneA, kHero, {99.f, 20.f}}).ok);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.consoleLog().size() == logBefore + 1);
        CHECK(c.consoleLog().back().level == ConsoleMessage::Level::Warning);
        CHECK(c.consumeInvalidations() == EditorInvalidation::Console);
    }

    // -- A rejected Intent is just as visible as a rejected Command: every
    // apply(SomeIntent) overload funnels through finishIntent(), which logs a
    // Console warning on failure. Unlike execute(), an Intent's return value
    // is fire-and-forget at every UI call site, so this is the only place a
    // rejected intent is ever surfaced. --
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.consumeInvalidations();
        const std::size_t logBefore = c.consoleLog().size();

        CHECK(!c.apply(SelectSceneIntent{"no-such-scene"}).ok);
        CHECK(c.consoleLog().size() == logBefore + 1);
        CHECK(c.consoleLog().back().level == ConsoleMessage::Level::Warning);
        CHECK(c.consumeInvalidations() == EditorInvalidation::Console);

        // A successful apply() logs nothing new.
        const std::size_t logAfterFailure = c.consoleLog().size();
        CHECK(c.apply(SelectSceneIntent{kSceneA}).ok);
        CHECK(c.consoleLog().size() == logAfterFailure);
    }

    // -- Workspace intents remain allowed during Play and do not retarget Play -
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(CreateEntityCommand{kSceneB, 100, "Enemy", "Enemy B", {5.f, 6.f}}).ok);
        CHECK(c.playProject().ok);
        const SceneFrameSnapshot playBefore = collectSceneFrameSnapshot(*c.playSession());
        CHECK(playBefore.sceneId == kSceneA);
        CHECK(playBefore.sprites.size() == 1);

        CHECK(c.apply(SelectSceneIntent{kSceneB}).ok);
        CHECK(c.state().activeSceneId == kSceneB);
        CHECK(c.playSession()->sceneId() == kSceneA);
        const SceneFrameSnapshot playAfterIntent = collectSceneFrameSnapshot(*c.playSession());
        CHECK(playAfterIntent.sceneId == kSceneA);
        CHECK(playAfterIntent.sprites.size() == 1);

        CHECK(c.apply(SelectEntityIntent{100}).ok);
        CHECK(c.selection().primaryEntity == 100);
        const SceneFrameSnapshot playAfterSelection = collectSceneFrameSnapshot(*c.playSession());
        CHECK(playAfterSelection.sceneId == kSceneA);
        CHECK(!playAfterSelection.entities.empty());
        CHECK(!playAfterSelection.entities[0].selected);

        CHECK(c.stopPlaying().ok);
        CHECK(!c.isPlaying());
        const SceneFrameSnapshot editAfterStop =
            collectSceneFrameSnapshot(c.document(), c.state().activeSceneId,
                                      c.selection().primaryEntity);
        CHECK(editAfterStop.sceneId == kSceneB);
        CHECK(editAfterStop.entities.size() == 1);
        CHECK(editAfterStop.entities[0].entityId == 100);
        CHECK(editAfterStop.entities[0].selected);
        CHECK(editAfterStop.sprites.empty());
    }

    // == Authored runtime motion (LinearMover) ================================

    // -- materialize resolves authored direction*speed (normalized) -----------
    {
        EditorCoordinator c{makeMoverDoc()};
        CHECK(c.playProject().ok);
        c.consumeInvalidations();                 // Start Play Toolbar|Viewport|Console
        const RuntimeEntity* hero = c.playSession()->findEntity(kHero);
        CHECK(hero != nullptr);
        CHECK(hero->velocity.x == 100.f);         // (3,0) normalized -> (1,0) * 100
        CHECK(hero->velocity.y == 0.f);
        CHECK(hero->transform.position.x == 10.f);

        c.advanceRuntime(0.5f);
        const RuntimeEntity* moved = c.playSession()->findEntity(kHero);
        CHECK(moved->transform.position.x == 60.f);   // 10 + 100 * 0.5
        CHECK(moved->transform.position.y == 20.f);
        // Runtime tick, not authoring: document untouched, no invalidation.
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
    }

    // -- an entity without a mover has zero velocity and does not drift --------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.playProject().ok);
        const RuntimeEntity* hero = c.playSession()->findEntity(kHero);
        CHECK(hero->velocity.x == 0.f);
        CHECK(hero->velocity.y == 0.f);
        c.advanceRuntime(1.0f);
        CHECK(c.playSession()->findEntity(kHero)->transform.position.x == 10.f);
    }

    // -- a paused mover resolves to zero velocity -----------------------------
    {
        ProjectDoc doc = makeMoverDoc();
        doc.objectTypes.at("Hero").linearMover->_paused = true;
        EditorCoordinator c{doc};
        CHECK(c.playProject().ok);
        CHECK(c.playSession()->findEntity(kHero)->velocity.x == 0.f);
    }

    // -- advanceRuntime is inert when not playing -----------------------------
    {
        EditorCoordinator c{makeMoverDoc()};
        const uint64_t revisionBefore = c.document().revision();
        c.consumeInvalidations();
        CHECK(!c.isPlaying());
        c.advanceRuntime(1.0f);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
    }

    // == Undo availability + toolbar refresh ==================================

    // -- A successful command enables Undo and refreshes the toolbar ----------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(!c.canUndo());
        c.consumeInvalidations();
        const EditorOperationResult moved =
            c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}});
        CHECK(moved.ok);
        CHECK(c.canUndo());
        // Toolbar is invalidated so the Undo button can re-derive its state.
        CHECK(has(c.consumeInvalidations(), EditorInvalidation::Toolbar));
    }

    // -- Undo restores the document, refreshes toolbar, and re-disables -------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 99.f);
        c.consumeInvalidations();

        const EditorOperationResult undone = c.undo();
        CHECK(undone.ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(has(inv, EditorInvalidation::Toolbar));
        CHECK(has(inv, EditorInvalidation::Inspector));   // the edited field re-reads
        CHECK(!c.canUndo());                              // back to empty history
    }

    // -- replaceProject clears the history (Undo disabled) -------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.canUndo());
        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(!c.canUndo());
    }

    // -- Play disables Undo as affordance; Stop restores the existing history -
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.canUndo());
        CHECK(c.playProject().ok);
        CHECK(c.isPlaying());
        CHECK(!c.undo().ok);                 // coordinator guard rejects during Play
        CHECK(c.canUndo());                  // history survives Play
        CHECK(c.stopPlaying().ok);
        CHECK(!c.isPlaying());
        CHECK(c.canUndo());                  // derived state returns after Stop
    }

    // == Redo (history walk) ==================================================

    // -- (1) Execute -> Undo -> Redo restores the document exactly ------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        CHECK(c.canRedo());
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 99.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.y == 20.f);
        CHECK(!c.canRedo());
        CHECK(c.canUndo());
    }

    // -- (2) Redo restores the entry's recorded revision ----------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        const uint64_t revAfter = c.document().revision();
        CHECK(c.undo().ok);
        CHECK(c.document().revision() != revAfter);     // moved off the post-state
        CHECK(c.redo().ok);
        CHECK(c.document().revision() == revAfter);      // exactly the recorded id
    }

    // -- (3) Undo/Redo cross savedRevision (dirty correctness) ---------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.markProjectSaved().ok);
        CHECK(!c.document().isDirty());
        CHECK(c.undo().ok);
        CHECK(c.document().isDirty());                   // undone away from saved
        CHECK(c.redo().ok);
        CHECK(!c.document().isDirty());                  // redo back to the saved state
    }

    // -- (3b) Save taken at the post-undo state: redo dirties, undo cleans ----
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);                              // back to state A
        CHECK(c.markProjectSaved().ok);                 // save AT the post-undo state
        CHECK(!c.document().isDirty());
        CHECK(c.redo().ok);                             // to state B
        CHECK(c.document().isDirty());                  // away from the saved state
        CHECK(c.undo().ok);                             // back to state A
        CHECK(!c.document().isDirty());                 // saved state again
    }

    // -- (4) A new command after Undo discards the redo branch ----------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        CHECK(c.canRedo());
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{50.f, 20.f}}).ok);
        CHECK(!c.canRedo());                             // B is no longer redoable
        CHECK(!c.redo().ok);
    }

    // -- (5) replaceProject clears redo; (6) Save keeps it --------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        CHECK(c.canRedo());
        CHECK(c.markProjectSaved().ok);
        CHECK(c.canRedo());                              // (6) save does not clear redo
        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(!c.canRedo());                             // (5) replace clears redo
    }

    // -- (7) Redo with no entry fails without effect --------------------------
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(!c.redo().ok);
        CHECK(c.document().revision() == revisionBefore);
    }

    // -- (8) Redo is rejected during Play ------------------------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        CHECK(c.playProject().ok);
        CHECK(!c.redo().ok);                             // coordinator guard
        CHECK(c.canRedo());                              // entry preserved
        CHECK(c.stopPlaying().ok);
        CHECK(c.canRedo());
    }

    // -- (10) Redo's invalidation matches the original command ----------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, Vec2{99.f, 20.f}}).ok);
        CHECK(c.undo().ok);
        c.consumeInvalidations();
        CHECK(c.redo().ok);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(has(inv, EditorInvalidation::Inspector));
        CHECK(has(inv, EditorInvalidation::Viewport));
        CHECK(has(inv, EditorInvalidation::Toolbar));
    }

    // == Unsaved-changes guard (decision matrix) ==============================
    {
        // A clean project proceeds immediately, whatever the (unused) choice.
        CHECK(resolveUnsavedGuard(false, UnsavedChoice::Cancel, false) == GuardOutcome::Proceed);
        CHECK(resolveUnsavedGuard(false, UnsavedChoice::Save, false) == GuardOutcome::Proceed);

        // Dirty: Cancel aborts; Discard proceeds.
        CHECK(resolveUnsavedGuard(true, UnsavedChoice::Cancel, false) == GuardOutcome::Abort);
        CHECK(resolveUnsavedGuard(true, UnsavedChoice::Discard, false) == GuardOutcome::Proceed);

        // Dirty + Save: proceed only when the save succeeded.
        CHECK(resolveUnsavedGuard(true, UnsavedChoice::Save, true) == GuardOutcome::Proceed);
        CHECK(resolveUnsavedGuard(true, UnsavedChoice::Save, false) == GuardOutcome::Abort);
    }

    // == P0-06 pending-edit classification and ordering =======================
    {
        CHECK(classifyPendingEdit("commit-pos-x", "12.5").status
              == PendingEditStatus::Resolved);
        CHECK(classifyPendingEdit("commit-project-name", "Arcade").status
              == PendingEditStatus::Resolved);
        CHECK(classifyPendingEdit("commit-console-filter", "").status
              == PendingEditStatus::Resolved); // an empty filter is a valid clear
        CHECK(classifyPendingEdit("select-entity", "-").status
              == PendingEditStatus::Resolved); // only commit fields participate

        for (const std::string& text : {"", "-", "+", ".", "-.", "+.",
                                        "1e", "1E", "1e+", "1e-", "12."}) {
            const PendingEditResult result = classifyPendingEdit("commit-pos-x", text);
            CHECK(result.status == PendingEditStatus::Incomplete);
            CHECK(!result.message.empty());
        }

        for (const std::string& text : {"NaN", "nan", "inf", "infinity",
                                        "12px", "1e9999", "abc", "1e."}) {
            const PendingEditResult result = classifyPendingEdit("commit-pos-y", text);
            CHECK(result.status == PendingEditStatus::Invalid);
            CHECK(!result.message.empty());
        }
        CHECK(classifyPendingEdit("commit-project-name", "").status
              == PendingEditStatus::Invalid);
        CHECK(classifyPendingEdit("commit-layer-rename", "").status
              == PendingEditStatus::Invalid);
    }

    // A valid focused value commits first, making the document dirty before
    // Cancel/Discard/Save is decided. Invalid/incomplete buffers generate no
    // command, so revision and dirty state stay exact.
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revisionBefore = c.document().revision();
        const PendingEditResult incomplete =
            classifyPendingEdit("commit-pos-x", "1e");
        if (incomplete.resolved()) commitInspectorPositionX(c, kHero, "1e");
        CHECK(c.document().revision() == revisionBefore);
        CHECK(!c.document().isDirty());

        const PendingEditResult valid = classifyPendingEdit("commit-pos-x", "33");
        CHECK(valid.resolved());
        if (valid.resolved()) CHECK(commitInspectorPositionX(c, kHero, "33").ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 33.f);
        CHECK(c.document().isDirty());
        CHECK(resolveUnsavedGuard(c.document().isDirty(), UnsavedChoice::Cancel, false)
              == GuardOutcome::Abort);
        CHECK(resolveUnsavedGuard(c.document().isDirty(), UnsavedChoice::Discard, false)
              == GuardOutcome::Proceed);
    }

    // Escape restores the original field value. Resolving that restored buffer
    // is a no-op; changing selection only happens after a real pending commit.
    {
        EditorCoordinator escaped{makeDoc()};
        const uint64_t revisionBefore = escaped.document().revision();
        CHECK(classifyPendingEdit("commit-pos-x", "10").resolved());
        CHECK(!pendingEditNeedsCommit("commit-pos-x", "10", "10"));
        if (pendingEditNeedsCommit("commit-pos-x", "10", "10"))
            commitInspectorPositionX(escaped, kHero, "10");
        CHECK(escaped.document().revision() == revisionBefore);
        CHECK(!escaped.document().isDirty());
        CHECK(pendingEditNeedsCommit("commit-layer-rename", "Gameplay", "Gameplay"));

        EditorCoordinator navigated{makeDoc()};
        CHECK(commitInspectorPositionX(navigated, kHero, "44").ok);
        navigated.apply(SelectSceneIntent{kSceneB});
        CHECK(navigated.state().activeSceneId == kSceneB);
        CHECK(navigated.document().findInstanceInScene(kSceneA, kHero)->transform.position.x
              == 44.f);
    }

    // == LinearMover editing + persistence ====================================

    // -- Add resolves a default mover and invalidates Inspector only ----------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.consumeInvalidations();
        const EditorOperationResult r = c.execute(AddLinearMoverCommand{"Hero"});
        CHECK(r.ok);
        CHECK(r.invalidation == EditorInvalidation::Inspector);   // no edit-viewport visual
        const EntityDef& hero = c.document().data().objectTypes.at("Hero");
        CHECK(hero.linearMover.has_value());
        CHECK(hero.linearMover->speed == 300.f);                  // component default
    }

    // -- Set speed / direction, then exact undo ------------------------------
    {
        EditorCoordinator c{makeMoverDoc()};                      // Hero mover (3,0) @ 100
        CHECK(c.execute(SetLinearMoverSpeedCommand{"Hero", 250.f}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").linearMover->speed == 250.f);
        CHECK(c.execute(SetLinearMoverDirectionCommand{"Hero", Vec2{0.f, 1.f}}).ok);
        const auto& m = *c.document().data().objectTypes.at("Hero").linearMover;
        CHECK(m.directionX == 0.f);
        CHECK(m.directionY == 1.f);
        CHECK(c.undo().ok);                                       // undo direction
        CHECK(c.document().data().objectTypes.at("Hero").linearMover->directionX == 3.f);
        CHECK(c.undo().ok);                                       // undo speed
        CHECK(c.document().data().objectTypes.at("Hero").linearMover->speed == 100.f);
    }

    // -- Invalid edits are rejected and mutate nothing -----------------------
    {
        EditorCoordinator c{makeMoverDoc()};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(!c.execute(SetLinearMoverSpeedCommand{"Hero", -5.f}).ok);
        CHECK(!c.execute(SetLinearMoverDirectionCommand{"Hero",
            Vec2{std::numeric_limits<float>::infinity(), 0.f}}).ok);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.document().data().objectTypes.at("Hero").linearMover->speed == 100.f);
    }

    // -- Inspector action is a no-op without a selected object type -----------
    {
        EditorCoordinator c{makeInheritedDoc()};                  // nothing selected
        CHECK(!addLinearMover(c).ok);
    }

    // -- Mover survives save/load; _paused is not persisted ------------------
    {
        EditorCoordinator c{makeMoverDoc()};
        const std::filesystem::path path = testTempDir() / "mover.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const auto& m = reloaded.document().data().objectTypes.at("Hero").linearMover;
        CHECK(m.has_value());
        CHECK(m->directionX == 3.f);
        CHECK(m->speed == 100.f);
        CHECK(m->_paused == false);
    }

    // == TopDownController (authoring + runtime input) ========================

    // -- Decisive: input moves the runtime entity; authoring untouched --------
    {
        EditorCoordinator c{makeTopDownDoc(100.f)};
        const uint64_t revisionBefore = c.document().revision();
        const bool dirtyBefore = c.document().isDirty();
        const std::size_t undoBefore = c.undoSize();

        CHECK(c.playProject().ok);
        RuntimeInputSnapshot in;
        in.moveRight = true;
        c.updateRuntime(in, 0.5f);                       // 100 * 0.5 = +50

        CHECK(c.playSession()->findEntity(kHero)->transform.position.x == 60.f);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.document().isDirty() == dirtyBefore);
        CHECK(c.undoSize() == undoBefore);

        CHECK(c.stopPlaying().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->transform.position.x == 10.f);
    }

    // -- No controller => no movement; restart re-materializes authoring ------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.playProject().ok);
        RuntimeInputSnapshot in; in.moveRight = true;
        c.updateRuntime(in, 1.0f);
        CHECK(c.playSession()->findEntity(kHero)->transform.position.x == 10.f);
        CHECK(c.stopPlaying().ok);
    }
    {
        EditorCoordinator c{makeTopDownDoc(100.f)};
        CHECK(c.playProject().ok);
        RuntimeInputSnapshot in; in.moveRight = true;
        c.updateRuntime(in, 1.0f);                       // moved to 110
        CHECK(c.stopPlaying().ok);
        CHECK(c.playProject().ok);                       // fresh session from authoring
        CHECK(c.playSession()->findEntity(kHero)->transform.position.x == 10.f);
    }

    // -- Opposite inputs cancel; non-finite/negative dt is a no-op -----------
    {
        EditorCoordinator c{makeTopDownDoc(100.f)};
        CHECK(c.playProject().ok);
        RuntimeInputSnapshot both; both.moveLeft = true; both.moveRight = true;
        c.updateRuntime(both, 1.0f);
        CHECK(c.playSession()->findEntity(kHero)->transform.position.x == 10.f);

        RuntimeInputSnapshot right; right.moveRight = true;
        c.updateRuntime(right, -1.0f);
        c.updateRuntime(right, std::numeric_limits<float>::infinity());
        CHECK(c.playSession()->findEntity(kHero)->transform.position.x == 10.f);
    }

    // -- Diagonal is normalized: never faster than a single axis --------------
    {
        EditorCoordinator c{makeTopDownDoc(100.f)};
        CHECK(c.playProject().ok);
        RuntimeInputSnapshot diag; diag.moveRight = true; diag.moveDown = true;
        c.updateRuntime(diag, 1.0f);
        const Transform& t = c.playSession()->findEntity(kHero)->transform;
        const float dx = t.position.x - 10.f;
        const float dy = t.position.y - 20.f;
        CHECK(dx == dy);                                 // symmetric
        CHECK(dx > 0.f && dx < 100.f);                   // each axis below full speed
        CHECK(std::abs((dx * dx + dy * dy) - 10000.f) < 0.5f);   // total step ≈ speed*dt
    }

    // -- Higher speed produces a larger displacement -------------------------
    {
        EditorCoordinator slow{makeTopDownDoc(100.f)};
        EditorCoordinator fast{makeTopDownDoc(200.f)};
        CHECK(slow.playProject().ok);
        CHECK(fast.playProject().ok);
        RuntimeInputSnapshot in; in.moveRight = true;
        slow.updateRuntime(in, 0.5f);
        fast.updateRuntime(in, 0.5f);
        CHECK(slow.playSession()->findEntity(kHero)->transform.position.x == 60.f);
        CHECK(fast.playSession()->findEntity(kHero)->transform.position.x == 110.f);
    }

    // -- Two controller entities both move -----------------------------------
    {
        EditorCoordinator c{makeTopDownDoc(100.f)};
        CHECK(c.execute(CreateEntityCommand{kSceneA, 201, "Hero", "Hero2", {30.f, 0.f}}).ok);
        CHECK(c.playProject().ok);
        RuntimeInputSnapshot in; in.moveRight = true;
        c.updateRuntime(in, 1.0f);
        CHECK(c.playSession()->findEntity(kHero)->transform.position.x == 110.f);
        CHECK(c.playSession()->findEntity(201)->transform.position.x == 130.f);
    }

    // ===== Runtime AABB collisions ==========================================
    // A "runner" at (0,0) with a 32x32 collider (AABB [-16,16]) moves +x toward a
    // "wall" at (100,0) (AABB [84,116]). Contact is at runner.x = 68 (16+68 = 84).
    {
        // configureRunner sets the mover/collider on the runner's object type.
        const auto makeWorld = [](auto&& configureRunner, bool wallEnabled = true,
                                  bool wallTrigger = false, Vec2 wallOffset = {0.f, 0.f},
                                  bool wallIsMover = false) {
            ProjectDoc doc;
            doc.projectName = "collide";
            doc.activeSceneId = "s";
            SceneDef s; s.id = "s"; s.name = "S"; s.worldSize = {4000.f, 4000.f};
            SceneInstanceDef runner; runner.id = 1; runner.objectTypeId = "runner";
            runner.instanceName = "Runner"; runner.transform.position = {0.f, 0.f};
            SceneInstanceDef wall; wall.id = 2; wall.objectTypeId = "wall";
            wall.instanceName = "Wall"; wall.transform.position = {100.f, 0.f};
            s.instances = {runner, wall};
            doc.scenes.emplace("s", s);

            EntityDef runnerType; runnerType.className = "runner"; runnerType.name = "Runner";
            configureRunner(runnerType);
            doc.objectTypes.emplace("runner", runnerType);

            EntityDef wallType; wallType.className = "wall"; wallType.name = "Wall";
            wallType.boxCollider2D =
                BoxCollider2DComponent{
                    wallOffset,
                    Vec2{32.f, 32.f},
                    wallEnabled,
                    wallTrigger ? BoxColliderMode::Trigger : BoxColliderMode::Solid};
            if (wallIsMover) {   // make the wall a kinematic mover -> not a static solid
                LinearMoverComponent lm; lm.directionX = 0.f; lm.directionY = 0.f; lm.speed = 0.f;
                wallType.linearMover = lm;
            }
            doc.objectTypes.emplace("wall", wallType);
            return doc;
        };
        const auto linearRunner = [](EntityDef& t) {
            LinearMoverComponent lm; lm.directionX = 1.f; lm.directionY = 0.f; lm.speed = 100.f;
            t.linearMover = lm;
            t.boxCollider2D = BoxCollider2DComponent{
                Vec2{0.f, 0.f}, Vec2{32.f, 32.f}, true, BoxColliderMode::Solid};
        };
        const auto near68 = [](float v) { return std::abs(v - 68.f) < 0.01f; };

        // (1) A LinearMover with a collider stops at contact with the static wall.
        {
            EditorCoordinator c{makeWorld(linearRunner)};
            CHECK(c.playProject().ok);
            c.advanceRuntime(10.f);                       // desired +1000, gap is 68
            const RuntimeEntity* r = c.playSession()->findEntity(1);
            CHECK(near68(r->transform.position.x));
            CHECK(r->transform.position.y == 0.f);
            // Authoring is untouched by the runtime resolution.
            CHECK(c.document().findInstanceInScene("s", 1)->transform.position.x == 0.f);
        }

        // (2) High speed / large dt does not tunnel through the thin wall.
        {
            EditorCoordinator c{makeWorld(linearRunner)};
            CHECK(c.playProject().ok);
            c.advanceRuntime(1000.f);                     // desired +100000
            CHECK(near68(c.playSession()->findEntity(1)->transform.position.x));
        }

        // (3) Per-axis resolution slides along the corner: X clamps, Y continues.
        {
            EditorCoordinator c{makeWorld([](EntityDef& t) {
                LinearMoverComponent lm; lm.directionX = 1.f; lm.directionY = 1.f; lm.speed = 100.f;
                t.linearMover = lm;
                t.boxCollider2D = BoxCollider2DComponent{
                    Vec2{0.f, 0.f}, Vec2{32.f, 32.f}, true, BoxColliderMode::Solid};
            })};
            CHECK(c.playProject().ok);
            c.advanceRuntime(10.f);                       // dir normalized (0.707,0.707)*100
            const RuntimeEntity* r = c.playSession()->findEntity(1);
            CHECK(near68(r->transform.position.x));        // X blocked at the wall
            CHECK(r->transform.position.y > 700.f);        // Y unblocked (~707)
        }

        // (4) A disabled wall collider is not solid -> the runner passes through.
        {
            EditorCoordinator c{makeWorld(linearRunner, /*wallEnabled*/ false)};
            CHECK(c.playProject().ok);
            c.advanceRuntime(10.f);
            CHECK(c.playSession()->findEntity(1)->transform.position.x > 900.f);
        }

        // (5) A trigger wall does not block.
        {
            EditorCoordinator c{makeWorld(linearRunner, true, /*wallTrigger*/ true)};
            CHECK(c.playProject().ok);
            c.advanceRuntime(10.f);
            CHECK(c.playSession()->findEntity(1)->transform.position.x > 900.f);
        }

        // (6) A mover without a collider moves freely.
        {
            EditorCoordinator c{makeWorld([](EntityDef& t) {
                LinearMoverComponent lm; lm.directionX = 1.f; lm.directionY = 0.f; lm.speed = 100.f;
                t.linearMover = lm;                        // no boxCollider2D
            })};
            CHECK(c.playProject().ok);
            c.advanceRuntime(10.f);
            CHECK(c.playSession()->findEntity(1)->transform.position.x > 900.f);
        }

        // (7) Mover vs mover is out of scope: a moving "wall" is not a static solid.
        {
            EditorCoordinator c{makeWorld(linearRunner, true, false, {0.f, 0.f}, /*wallIsMover*/ true)};
            CHECK(c.playProject().ok);
            c.advanceRuntime(10.f);
            CHECK(c.playSession()->findEntity(1)->transform.position.x > 900.f);
        }

        // (8) TopDownController routes through the same resolver and stops too.
        {
            EditorCoordinator c{makeWorld([](EntityDef& t) {
                TopDownControllerComponent tdc; tdc.maxSpeed = 100.f;
                t.topDownController = tdc;
                t.boxCollider2D = BoxCollider2DComponent{
                    Vec2{0.f, 0.f}, Vec2{32.f, 32.f}, true, BoxColliderMode::Solid};
            })};
            CHECK(c.playProject().ok);
            RuntimeInputSnapshot in; in.moveRight = true;
            c.updateRuntime(in, 10.f);                    // desired +1000, gap 68
            CHECK(near68(c.playSession()->findEntity(1)->transform.position.x));
        }

        // (9) The collider offset shifts the wall's AABB; contact moves with it.
        //     Wall offset (-20,0) -> center 80 -> AABB [64,96]; contact at x = 48.
        {
            EditorCoordinator c{makeWorld(linearRunner, true, false, /*wallOffset*/ {-20.f, 0.f})};
            CHECK(c.playProject().ok);
            c.advanceRuntime(10.f);
            CHECK(std::abs(c.playSession()->findEntity(1)->transform.position.x - 48.f) < 0.01f);
        }
    }

    // ===== PlatformerController ==============================================
    // Player (platformer + 32x32 collider, AABB [-16,16]) at (0,0). World +Y is
    // down. Optional 200x32 floor / ceiling. Floor at y=100 -> top at 84 ->
    // landing at player.y = 68. Ceiling at y=-40 -> bottom at -24.
    {
        const auto makePlatformWorld = [](bool withFloor, float floorY = 100.f,
                                          bool playerCollider = true, bool floorEnabled = true,
                                          bool floorTrigger = false, bool withCeiling = false,
                                          float ceilingY = -40.f) {
            ProjectDoc doc;
            doc.projectName = "platform";
            doc.activeSceneId = "s";
            SceneDef s; s.id = "s"; s.name = "S"; s.worldSize = {4000.f, 4000.f};
            SceneInstanceDef player; player.id = 1; player.objectTypeId = "player";
            player.instanceName = "Player"; player.transform.position = {0.f, 0.f};
            s.instances.push_back(player);
            if (withFloor) {
                SceneInstanceDef floor; floor.id = 2; floor.objectTypeId = "floor";
                floor.instanceName = "Floor"; floor.transform.position = {0.f, floorY};
                s.instances.push_back(floor);
            }
            if (withCeiling) {
                SceneInstanceDef ceil; ceil.id = 3; ceil.objectTypeId = "ceil";
                ceil.instanceName = "Ceiling"; ceil.transform.position = {0.f, ceilingY};
                s.instances.push_back(ceil);
            }
            doc.scenes.emplace("s", s);

            EntityDef playerType; playerType.className = "player"; playerType.name = "Player";
            PlatformerControllerComponent pc;
            pc.maxSpeed = 180.f; pc.jumpForce = 420.f; pc.customGravity = 1200.f;
            playerType.platformerController = pc;
            if (playerCollider)
                playerType.boxCollider2D = BoxCollider2DComponent{
                    {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Solid};
            doc.objectTypes.emplace("player", playerType);

            if (withFloor) {
                EntityDef floorType; floorType.className = "floor"; floorType.name = "Floor";
                floorType.boxCollider2D =
                    BoxCollider2DComponent{
                        {0.f, 0.f},
                        {200.f, 32.f},
                        floorEnabled,
                        floorTrigger ? BoxColliderMode::Trigger : BoxColliderMode::Solid};
                doc.objectTypes.emplace("floor", floorType);
            }
            if (withCeiling) {
                EntityDef ceilType; ceilType.className = "ceil"; ceilType.name = "Ceiling";
                ceilType.boxCollider2D = BoxCollider2DComponent{
                    {0.f, 0.f}, {200.f, 32.f}, true, BoxColliderMode::Solid};
                doc.objectTypes.emplace("ceil", ceilType);
            }
            return doc;
        };
        const auto platformerOf = [](EditorCoordinator& c) {
            return c.playSession()->findEntity(1)->platformerController;
        };
        RuntimeInputSnapshot none;

        // Gravity pulls a free player down (no floor); not grounded.
        {
            EditorCoordinator c{makePlatformWorld(false)};
            CHECK(c.playProject().ok);
            c.updateRuntime(none, 0.1f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(p->transform.position.y > 0.f);
            CHECK(p->platformerController->verticalVelocity > 0.f);
            CHECK(!p->platformerController->grounded);
        }

        // The floor sets grounded and zeroes vertical velocity at the contact.
        {
            EditorCoordinator c{makePlatformWorld(true)};
            CHECK(c.playProject().ok);
            for (int i = 0; i < 300; ++i) c.updateRuntime(none, 0.05f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(p->platformerController->grounded);
            CHECK(p->platformerController->verticalVelocity == 0.f);
            CHECK(std::abs(p->transform.position.y - 68.f) < 0.01f);   // exact contact
            // Authoring untouched, no dirtying by the runtime.
            CHECK(c.document().findInstanceInScene("s", 1)->transform.position.y == 0.f);
            CHECK(!c.document().isDirty());
        }

        // A high-velocity fall does not tunnel through the floor (swept clamp).
        {
            EditorCoordinator c{makePlatformWorld(true)};
            CHECK(c.playProject().ok);
            c.updateRuntime(none, 100.f);                  // enormous dt
            CHECK(std::abs(c.playSession()->findEntity(1)->transform.position.y - 68.f) < 0.01f);
        }

        // jumpPressed from the ground launches upward (negative vy) and ungrounds.
        {
            EditorCoordinator c{makePlatformWorld(true)};
            CHECK(c.playProject().ok);
            for (int i = 0; i < 300; ++i) c.updateRuntime(none, 0.05f);   // land
            const float yGround = c.playSession()->findEntity(1)->transform.position.y;
            RuntimeInputSnapshot jump; jump.jumpPressed = true;
            c.updateRuntime(jump, 0.05f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(p->transform.position.y < yGround);                 // moved up
            CHECK(p->platformerController->verticalVelocity < 0.f);   // rising
            CHECK(!p->platformerController->grounded);
        }

        // A jump while airborne is ignored (no re-jump): vy keeps accelerating down.
        {
            EditorCoordinator c{makePlatformWorld(false)};
            CHECK(c.playProject().ok);
            c.updateRuntime(none, 0.1f);                              // now falling
            const float vyBefore = platformerOf(c)->verticalVelocity;
            RuntimeInputSnapshot jump; jump.jumpPressed = true;
            c.updateRuntime(jump, 0.1f);
            CHECK(platformerOf(c)->verticalVelocity > vyBefore);      // no upward jump
        }

        // A ceiling zeroes the rising velocity. Floor flush under the player grounds
        // it on frame 1; the ceiling at y=-40 stops the jump almost immediately.
        {
            EditorCoordinator c{makePlatformWorld(true, /*floorY*/ 32.f, true, true, false,
                                                  /*withCeiling*/ true, /*ceilingY*/ -40.f)};
            CHECK(c.playProject().ok);
            c.updateRuntime(none, 0.05f);                            // ground on the flush floor
            CHECK(platformerOf(c)->grounded);
            RuntimeInputSnapshot jump; jump.jumpPressed = true;
            c.updateRuntime(jump, 0.05f);                            // jump straight into ceiling
            CHECK(platformerOf(c)->verticalVelocity == 0.f);
        }

        // A trigger floor never grounds (no block); the player falls through.
        {
            EditorCoordinator c{makePlatformWorld(true, 100.f, true, true, /*floorTrigger*/ true)};
            CHECK(c.playProject().ok);
            for (int i = 0; i < 50; ++i) c.updateRuntime(none, 0.05f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(!p->platformerController->grounded);
            CHECK(p->transform.position.y > 84.f);                   // passed the floor line
        }

        // A disabled floor collider never grounds either.
        {
            EditorCoordinator c{makePlatformWorld(true, 100.f, true, /*floorEnabled*/ false)};
            CHECK(c.playProject().ok);
            for (int i = 0; i < 50; ++i) c.updateRuntime(none, 0.05f);
            CHECK(!platformerOf(c)->grounded);
        }

        // A player without a collider falls freely (never grounds).
        {
            EditorCoordinator c{makePlatformWorld(true, 100.f, /*playerCollider*/ false)};
            CHECK(c.playProject().ok);
            for (int i = 0; i < 50; ++i) c.updateRuntime(none, 0.05f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(!p->platformerController->grounded);
            CHECK(p->transform.position.y > 84.f);
        }

        const auto makeOneWayWorld = [](BoxColliderMode floorMode = BoxColliderMode::OneWayPlatform,
                                        bool floorEnabled = true,
                                        Vec2 playerStart = {0.f, 0.f},
                                        Vec2 floorPosition = {0.f, 100.f},
                                        Vec2 floorOffset = {0.f, 0.f},
                                        Vec2 floorSize = {200.f, 32.f}) {
            ProjectDoc doc;
            doc.projectName = "one-way";
            doc.activeSceneId = "s";
            SceneDef s; s.id = "s"; s.name = "S"; s.worldSize = {4000.f, 4000.f};
            SceneInstanceDef player; player.id = 1; player.objectTypeId = "player";
            player.instanceName = "Player"; player.transform.position = playerStart;
            SceneInstanceDef floor; floor.id = 2; floor.objectTypeId = "floor";
            floor.instanceName = "Floor"; floor.transform.position = floorPosition;
            s.instances = {player, floor};
            doc.scenes.emplace("s", s);

            EntityDef playerType; playerType.className = "player"; playerType.name = "Player";
            PlatformerControllerComponent pc;
            pc.maxSpeed = 180.f; pc.jumpForce = 420.f; pc.customGravity = 1200.f;
            playerType.platformerController = pc;
            playerType.boxCollider2D = BoxCollider2DComponent{
                {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Solid};
            doc.objectTypes.emplace("player", playerType);

            EntityDef floorType; floorType.className = "floor"; floorType.name = "Floor";
            floorType.boxCollider2D = BoxCollider2DComponent{
                floorOffset, floorSize, floorEnabled, floorMode};
            doc.objectTypes.emplace("floor", floorType);
            return doc;
        };

        // One-way platform: falling from above lands and grounds the platformer.
        {
            EditorCoordinator c{makeOneWayWorld()};
            CHECK(c.playProject().ok);
            CHECK(c.playSession()->findEntity(2)->collider->mode
                  == BoxColliderMode::OneWayPlatform);
            for (int i = 0; i < 300; ++i) c.updateRuntime(none, 0.05f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(p->platformerController->grounded);
            CHECK(p->platformerController->verticalVelocity == 0.f);
            CHECK(std::abs(p->transform.position.y - 68.f) < 0.01f);
        }

        // One-way platform still catches a fast fall; no tunneling through top.
        {
            EditorCoordinator c{makeOneWayWorld()};
            CHECK(c.playProject().ok);
            c.updateRuntime(none, 100.f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(p->platformerController->grounded);
            CHECK(std::abs(p->transform.position.y - 68.f) < 0.01f);
        }

        // Disabled and Trigger modes do not block even with the same geometry.
        {
            EditorCoordinator disabled{makeOneWayWorld(BoxColliderMode::OneWayPlatform,
                                                       /*floorEnabled*/ false)};
            CHECK(disabled.playProject().ok);
            disabled.updateRuntime(none, 100.f);
            CHECK(!disabled.playSession()->findEntity(1)->platformerController->grounded);
            CHECK(disabled.playSession()->findEntity(1)->transform.position.y > 84.f);

            EditorCoordinator trigger{makeOneWayWorld(BoxColliderMode::Trigger)};
            CHECK(trigger.playProject().ok);
            trigger.updateRuntime(none, 100.f);
            CHECK(!trigger.playSession()->findEntity(1)->platformerController->grounded);
            CHECK(trigger.playSession()->findEntity(1)->transform.position.y > 84.f);
        }

        // Offset and size define the platform top; the one-way clamp uses that
        // exact geometry, not the entity pivot.
        {
            EditorCoordinator c{makeOneWayWorld(BoxColliderMode::OneWayPlatform, true,
                                                {0.f, 0.f}, {0.f, 100.f},
                                                {0.f, -20.f}, {96.f, 16.f})};
            CHECK(c.playProject().ok);
            c.updateRuntime(none, 100.f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(p->platformerController->grounded);
            CHECK(std::abs(p->transform.position.y - 56.f) < 0.01f);
        }

        // Starting already penetrated / below a one-way platform does not
        // depenetrate or clamp; the mover can leave the shape.
        {
            EditorCoordinator c{makeOneWayWorld(BoxColliderMode::OneWayPlatform, true,
                                                {0.f, 90.f})};
            CHECK(c.playProject().ok);
            c.updateRuntime(none, 0.5f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(!p->platformerController->grounded);
            CHECK(p->transform.position.y > 90.f);
        }

        // Multiple valid one-way platforms choose the nearest contact along the
        // downward sweep, independent of scene order.
        {
            ProjectDoc doc = makeOneWayWorld();
            SceneInstanceDef upper; upper.id = 3; upper.objectTypeId = "upper";
            upper.instanceName = "Upper"; upper.transform.position = {0.f, 60.f};
            doc.scenes.at("s").instances.push_back(upper);
            EntityDef upperType; upperType.className = "upper"; upperType.name = "Upper";
            upperType.boxCollider2D = BoxCollider2DComponent{
                {0.f, 0.f}, {200.f, 32.f}, true, BoxColliderMode::OneWayPlatform};
            doc.objectTypes.emplace("upper", upperType);

            EditorCoordinator c{doc};
            CHECK(c.playProject().ok);
            c.updateRuntime(none, 100.f);
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(p->platformerController->grounded);
            CHECK(std::abs(p->transform.position.y - 28.f) < 0.01f);
        }

        const auto makeOneWayLinearWorld = [](Vec2 runnerStart, Vec2 direction) {
            ProjectDoc doc;
            doc.projectName = "one-way-linear";
            doc.activeSceneId = "s";
            SceneDef s; s.id = "s"; s.name = "S"; s.worldSize = {4000.f, 4000.f};
            SceneInstanceDef runner; runner.id = 1; runner.objectTypeId = "runner";
            runner.instanceName = "Runner"; runner.transform.position = runnerStart;
            SceneInstanceDef platform; platform.id = 2; platform.objectTypeId = "platform";
            platform.instanceName = "Platform"; platform.transform.position = {100.f, 100.f};
            s.instances = {runner, platform};
            doc.scenes.emplace("s", s);

            EntityDef runnerType; runnerType.className = "runner"; runnerType.name = "Runner";
            LinearMoverComponent lm; lm.directionX = direction.x; lm.directionY = direction.y;
            lm.speed = 100.f;
            runnerType.linearMover = lm;
            runnerType.boxCollider2D = BoxCollider2DComponent{
                {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::Solid};
            doc.objectTypes.emplace("runner", runnerType);

            EntityDef platformType; platformType.className = "platform";
            platformType.name = "Platform";
            platformType.boxCollider2D = BoxCollider2DComponent{
                {0.f, 0.f}, {200.f, 32.f}, true, BoxColliderMode::OneWayPlatform};
            doc.objectTypes.emplace("platform", platformType);
            return doc;
        };

        // Moving upward from below crosses the platform; one-way is ignored on Y up.
        {
            EditorCoordinator c{makeOneWayLinearWorld({100.f, 150.f}, {0.f, -1.f})};
            CHECK(c.playProject().ok);
            c.advanceRuntime(10.f);
            CHECK(c.playSession()->findEntity(1)->transform.position.y < -800.f);
        }

        // Moving laterally through a one-way platform is not blocked.
        {
            EditorCoordinator c{makeOneWayLinearWorld({0.f, 100.f}, {1.f, 0.f})};
            CHECK(c.playProject().ok);
            c.advanceRuntime(10.f);
            CHECK(c.playSession()->findEntity(1)->transform.position.x > 900.f);
        }

        // Stop restores the authoring position; the next Play re-materializes with
        // velocity and grounded reset.
        {
            EditorCoordinator c{makePlatformWorld(true)};
            CHECK(c.playProject().ok);
            for (int i = 0; i < 20; ++i) c.updateRuntime(none, 0.05f);
            CHECK(c.playSession()->findEntity(1)->transform.position.y > 0.f);
            const uint64_t revBefore = c.document().revision();
            CHECK(c.stopPlaying().ok);
            CHECK(c.document().findInstanceInScene("s", 1)->transform.position.y == 0.f);
            CHECK(c.document().revision() == revBefore);             // runtime never dirtied
            CHECK(c.playProject().ok);                              // restart
            const RuntimeEntity* p = c.playSession()->findEntity(1);
            CHECK(p->transform.position.y == 0.f);
            CHECK(p->platformerController->verticalVelocity == 0.f);
            CHECK(!p->platformerController->grounded);
        }
    }

    // ===== PlatformerController authoring ====================================
    {
        // Add creates the component with the editor's recommended starting values.
        {
            EditorCoordinator c{makeInheritedDoc()};
            CHECK(c.execute(AddPlatformerControllerCommand{"Hero"}).ok);
            const auto& pc = c.document().data().objectTypes.at("Hero").platformerController;
            CHECK(pc.has_value());
            CHECK(pc->maxSpeed == 180.f);
            CHECK(pc->jumpForce == 420.f);
            CHECK(pc->customGravity == 1200.f);
        }

        // One movement writer: incompatible drivers are rejected both ways.
        {
            EditorCoordinator c{makeInheritedDoc()};
            CHECK(c.execute(AddTopDownControllerCommand{"Hero"}).ok);
            CHECK(!c.execute(AddPlatformerControllerCommand{"Hero"}).ok);   // topdown present
            CHECK(!c.execute(AddLinearMoverCommand{"Hero"}).ok);
            CHECK(c.execute(RemoveTopDownControllerCommand{"Hero"}).ok);
            CHECK(c.execute(AddPlatformerControllerCommand{"Hero"}).ok);    // now free
            CHECK(!c.execute(AddTopDownControllerCommand{"Hero"}).ok);      // platformer present
            CHECK(!c.execute(AddLinearMoverCommand{"Hero"}).ok);
        }

        // Set values: validation, undo/redo.
        {
            EditorCoordinator c{makeInheritedDoc()};
            CHECK(c.execute(AddPlatformerControllerCommand{"Hero"}).ok);
            CHECK(!c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::Gravity, -1.f}).ok);
            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::JumpSpeed, 500.f}).ok);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->jumpForce == 500.f);
            CHECK(c.undo().ok);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->jumpForce == 420.f);
            CHECK(c.redo().ok);
            CHECK(c.document().data().objectTypes.at("Hero").platformerController->jumpForce == 500.f);
        }

        // Save/reload preserves the authored subset.
        {
            EditorCoordinator c{makeInheritedDoc()};
            CHECK(c.execute(AddPlatformerControllerCommand{"Hero"}).ok);
            CHECK(c.execute(SetPlatformerValueCommand{"Hero", PlatformerField::MoveSpeed, 90.f}).ok);
            const std::filesystem::path path = testTempDir() / "platformer.artcade-project";
            CHECK(saveProjectToFile(c, path).ok);
            EditorCoordinator reloaded{ProjectDoc{}};
            CHECK(loadProjectFromFile(reloaded, path).ok);
            const auto& pc = reloaded.document().data().objectTypes.at("Hero").platformerController;
            CHECK(pc.has_value());
            CHECK(pc->maxSpeed == 90.f);
            CHECK(pc->jumpForce == 420.f);
            CHECK(pc->customGravity == 1200.f);
        }
    }

    // -- save/load preserves the component; Add/Remove/SetSpeed undo+redo -----
    {
        EditorCoordinator c{makeTopDownDoc(140.f)};
        const std::filesystem::path path = testTempDir() / "topdown.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const auto& tdc = reloaded.document().data().objectTypes.at("Hero").topDownController;
        CHECK(tdc.has_value());
        CHECK(tdc->maxSpeed == 140.f);
    }
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddTopDownControllerCommand{"Hero"}).ok);
        CHECK(c.execute(SetTopDownControllerSpeedCommand{"Hero", 250.f}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").topDownController->maxSpeed == 250.f);
        CHECK(c.undo().ok);   // undo speed
        CHECK(c.document().data().objectTypes.at("Hero").topDownController->maxSpeed == 260.f);
        CHECK(c.undo().ok);   // undo add
        CHECK(!c.document().data().objectTypes.at("Hero").topDownController.has_value());
        CHECK(c.redo().ok);   // redo add
        CHECK(c.document().data().objectTypes.at("Hero").topDownController.has_value());
        CHECK(c.redo().ok);   // redo speed
        CHECK(c.document().data().objectTypes.at("Hero").topDownController->maxSpeed == 250.f);
    }

    // -- Invalid speed rejected; Add invalidates Inspector only --------------
    {
        EditorCoordinator c{makeTopDownDoc(100.f)};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(!c.execute(SetTopDownControllerSpeedCommand{"Hero", -1.f}).ok);
        CHECK(c.document().revision() == revisionBefore);
        EditorCoordinator c2{makeInheritedDoc()};
        c2.consumeInvalidations();
        const EditorOperationResult r = c2.execute(AddTopDownControllerCommand{"Hero"});
        CHECK(r.ok);
        CHECK(r.invalidation == EditorInvalidation::Inspector);
    }

    // == Image asset catalog (import target) ==================================

    // -- Add: catalog gains the asset; duplicate rejected; undo/redo ----------
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const EditorOperationResult r =
            c.execute(AddImageAssetCommand{"img-x", "assets/images/x.png"});
        CHECK(r.ok);
        CHECK(r.invalidation == (EditorInvalidation::Assets | EditorInvalidation::Inspector));
        CHECK(c.document().hasImageAsset("img-x"));
        CHECK(c.document().findImageAsset("img-x")->name == "img-x");
        CHECK(c.document().findImageAsset("img-x")->sourcePath == "assets/images/x.png");

        CHECK(!c.execute(AddImageAssetCommand{"img-x", "assets/images/y.png"}).ok);  // dup
        CHECK(c.undo().ok);
        CHECK(!c.document().hasImageAsset("img-x"));
        CHECK(c.redo().ok);
        CHECK(c.document().hasImageAsset("img-x"));
    }

    // -- Remove: exact undo restores the source path; unknown id fails --------
    {
        EditorCoordinator c{makeSpriteDoc()};   // img-hero -> sprites/hero.ppm
        CHECK(c.document().hasImageAsset("img-hero"));
        CHECK(c.execute(RemoveImageAssetCommand{"img-hero"}).ok);
        CHECK(!c.document().hasImageAsset("img-hero"));
        CHECK(c.undo().ok);
        CHECK(c.document().findImageAsset("img-hero")->sourcePath == "sprites/hero.ppm");
        CHECK(!c.execute(RemoveImageAssetCommand{"nope"}).ok);
    }

    // -- Remove clears the sprite-renderer reference (delete means delete) -----
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.document().data().objectTypes.at("Hero")
                  .spriteRenderer->imageAssetId == "img-hero");
        const uint64_t revisionBefore = c.document().revision();
        // Removing the image drops the reference - the entity keeps no dangling id.
        CHECK(c.execute(RemoveImageAssetCommand{"img-hero"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);
        CHECK(!c.document().hasImageAsset("img-hero"));
        CHECK(c.document().data().objectTypes.at("Hero")
                  .spriteRenderer->imageAssetId.empty());
        // Undo restores the asset AND the exact reference; redo clears it again.
        CHECK(c.undo().ok);
        CHECK(c.document().hasImageAsset("img-hero"));
        CHECK(c.document().data().objectTypes.at("Hero")
                  .spriteRenderer->imageAssetId == "img-hero");
        CHECK(c.redo().ok);
        CHECK(c.document().data().objectTypes.at("Hero")
                  .spriteRenderer->imageAssetId.empty());
    }

    // -- save/load preserves the catalog with the relative path ---------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(AddImageAssetCommand{"img-x", "assets/images/x.png"}).ok);
        const std::filesystem::path path = testTempDir() / "assets.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const ImageAssetDef* asset = reloaded.document().findImageAsset("img-x");
        CHECK(asset != nullptr);
        CHECK(asset->name == "img-x");
        CHECK(asset->sourcePath == "assets/images/x.png");
    }

    // == Audio + Font catalogs (commands + persistence) =======================
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        const EditorOperationResult ra =
            c.execute(AddAudioAssetCommand{"sfx", "assets/audio/sfx.wav",
                                           AudioLoadMode::StaticSound});
        CHECK(ra.ok);
        CHECK(ra.invalidation == EditorInvalidation::Assets);
        CHECK(c.document().findAudioAsset("sfx")->name == "sfx");
        CHECK(c.document().findAudioAsset("sfx")->loadMode == AudioLoadMode::StaticSound);
        CHECK(!c.execute(AddAudioAssetCommand{"sfx", "a/b.ogg", AudioLoadMode::Stream}).ok); // dup

        const EditorOperationResult rf =
            c.execute(AddFontAssetCommand{"ui", "assets/fonts/ui.ttf", 32,
                                          FontGlyphPreset::European});
        CHECK(rf.ok);
        CHECK(c.document().findFontAsset("ui")->name == "ui");
        CHECK(c.document().findFontAsset("ui")->defaultPixelSize == 32);

        // undo/redo across both catalogs
        CHECK(c.undo().ok);  CHECK(!c.document().hasFontAsset("ui"));
        CHECK(c.redo().ok);  CHECK(c.document().hasFontAsset("ui"));

        const std::filesystem::path path = testTempDir() / "media.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().findAudioAsset("sfx")->name == "sfx");
        CHECK(reloaded.document().findAudioAsset("sfx")->sourcePath == "assets/audio/sfx.wav");
        CHECK(reloaded.document().findAudioAsset("sfx")->loadMode == AudioLoadMode::StaticSound);
        CHECK(reloaded.document().findFontAsset("ui")->name == "ui");
        CHECK(reloaded.document().findFontAsset("ui")->glyphPreset == FontGlyphPreset::European);
    }

    // -- Project asset catalog is saved from ProjectDocument, not cache state -
    {
        const std::filesystem::path base = testTempDir();
        const std::filesystem::path root = base / "catalog-roundtrip";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path image = base / "hero.png";
        const std::filesystem::path audio = base / "jump.wav";
        const std::filesystem::path font = base / "ui.ttf";
        { std::ofstream f(image, std::ios::binary); f << "PNG"; }
        { std::ofstream f(audio, std::ios::binary); f << "WAV"; }
        { std::ofstream f(font, std::ios::binary); f << "TTF"; }

        ProjectDoc catalogDoc = makeDoc();
        EntityDef heroType; heroType.className = "Hero"; heroType.name = "Hero";
        catalogDoc.objectTypes.emplace("Hero", std::move(heroType));
        EditorCoordinator c{std::move(catalogDoc)};
        CHECK(importAsset(c, root, {AssetKind::Image, image}).ok);
        CHECK(importAsset(c, root, {AssetKind::Audio, audio}).ok);
        CHECK(importAsset(c, root, {AssetKind::Font, font}).ok);
        CHECK(c.execute(AddSpriteRendererToObjectTypeCommand{"Hero"}).ok);
        CHECK(c.execute(SetObjectTypeSpriteSourceCommand{
            "Hero", ObjectTypeSpriteSourceKind::Image, "hero"}).ok);

        const std::filesystem::path path = root / "catalog.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const ImageAssetDef* reloadedImage = reloaded.document().findImageAsset("hero");
        const AudioAssetDef* reloadedAudio = reloaded.document().findAudioAsset("jump");
        const FontAssetDef* reloadedFont = reloaded.document().findFontAsset("ui");
        CHECK(reloadedImage != nullptr);
        CHECK(reloadedAudio != nullptr);
        CHECK(reloadedFont != nullptr);
        CHECK(reloadedImage && reloadedImage->name == "hero");
        CHECK(reloadedImage && reloadedImage->sourcePath == "assets/images/hero.png");
        CHECK(reloadedAudio && reloadedAudio->sourcePath == "assets/audio/jump.wav");
        CHECK(reloadedAudio && reloadedAudio->loadMode == AudioLoadMode::StaticSound);
        CHECK(reloadedFont && reloadedFont->sourcePath == "assets/fonts/ui.ttf");
        const SpriteRenderView reloadedSprite =
            resolveSpriteRenderer(reloaded.document(), kSceneA, kHero);
        CHECK(reloadedSprite.present);
        CHECK(reloadedSprite.assetId == "hero");
        CHECK(reloadedImage && std::filesystem::exists(root / reloadedImage->sourcePath));

        std::filesystem::remove(root / "assets" / "images" / "hero.png", ec);
        EditorCoordinator missingFileReload{ProjectDoc{}};
        CHECK(loadProjectFromFile(missingFileReload, path).ok);
        const ImageAssetDef* missingImage =
            missingFileReload.document().findImageAsset("hero");
        CHECK(missingImage != nullptr);
        CHECK(missingImage && missingImage->sourcePath == "assets/images/hero.png");
    }

    // -- P0-04: project paths remain inside their canonical root --------------
    {
        const std::filesystem::path base = testTempDir();
        const std::filesystem::path root = base / "project";
        const std::filesystem::path outside = base / "project-outside";
        std::error_code ec;
        std::filesystem::create_directories(root / "assets" / "images", ec);
        CHECK(!ec);
        std::filesystem::create_directories(outside, ec);
        CHECK(!ec);
        { std::ofstream file(outside / "secret.png", std::ios::binary); file << "secret"; }

        const PathConfinementResult inside =
            resolvePathInsideRoot(root, "assets/images/hero.png");
        CHECK(inside.ok);
        CHECK(inside.value.parent_path().filename() == "images");

        const PathConfinementResult mixed =
            resolvePathInsideRoot(root, std::filesystem::u8path("assets\\images/hero.png"));
        CHECK(mixed.ok);
        CHECK(mixed.value == inside.value);

        for (const std::filesystem::path& hostile : {
                 std::filesystem::u8path("../secret.png"),
                 std::filesystem::u8path("assets/../../secret.png"),
                 std::filesystem::u8path("..\\project-outside\\secret.png"),
                 std::filesystem::absolute(outside / "secret.png"),
                 std::filesystem::u8path("Z:/secret.png"),
                 std::filesystem::u8path("assets/images/hero.png:payload")}) {
            const PathConfinementResult rejected = resolvePathInsideRoot(root, hostile);
            CHECK(!rejected.ok);
            CHECK(!rejected.error.empty());
            CHECK(!rejected.remediation.empty());
        }

        const std::filesystem::path unicodeRelative =
            std::filesystem::u8path("assets/immagini-\xC3\xA8/eroe.png");
        const PathConfinementResult unicode = resolvePathInsideRoot(root, unicodeRelative);
        CHECK(unicode.ok);

        // A same-prefix sibling is not a child. The lexical traversal is
        // rejected before any I/O, independently from string-prefix tricks.
        CHECK(!resolvePathInsideRoot(
            root, std::filesystem::u8path("../project-outside/secret.png")).ok);

        const std::filesystem::path link = root / "assets" / "external-link";
        std::filesystem::create_directory_symlink(outside, link, ec);
        if (!ec) {
            CHECK(!resolvePathInsideRoot(root, "assets/external-link/secret.png").ok);
        } else {
            // Windows without Developer Mode/SeCreateSymbolicLinkPrivilege cannot
            // create the fixture. Do not weaken production behavior: assert no
            // partial reparse point exists; CI hosts that permit symlinks execute
            // the escape assertion above.
            CHECK(!std::filesystem::exists(link));
        }

        ProjectDoc hostileDoc = makeDoc();
        ImageAssetDef escaped;
        escaped.assetId = "escaped";
        escaped.sourcePath = "../secret.png";
        hostileDoc.imageAssets.push_back(escaped);
        CHECK(!ProjectValidator::validate(ProjectDocument{hostileDoc}).ok);

        EditorCoordinator c{makeDoc()};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(!c.execute(AddImageAssetCommand{"escaped", "../secret.png"}).ok);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(!c.document().hasImageAsset("escaped"));
    }

    // -- Asset enum values are validated instead of defaulted silently --------
    {
        const std::string modernKeys =
            R"({"activeSceneId":"s","scenes":[{"id":"s"}],"imageAssets":[{"id":"img","name":"Hero","relativePath":"assets/images/hero.png"}]})";
        const std::string badAudio =
            R"({"activeSceneId":"s","scenes":[{"id":"s"}],"audioAssets":[{"assetId":"a","sourcePath":"assets/audio/a.wav","loadMode":"mmap"}]})";
        const std::string badFont =
            R"({"activeSceneId":"s","scenes":[{"id":"s"}],"fontAssets":[{"assetId":"f","sourcePath":"assets/fonts/f.ttf","glyphPreset":"kanji"}]})";
        const auto loaded = ProjectSerializer::deserialize(modernKeys);
        CHECK(loaded.ok);
        const ImageAssetDef* loadedImage =
            loaded.ok ? loaded.value.findImageAsset("img") : nullptr;
        CHECK(loadedImage != nullptr);
        CHECK(loadedImage && loadedImage->name == "Hero");
        CHECK(loadedImage && loadedImage->sourcePath == "assets/images/hero.png");
        CHECK(!ProjectSerializer::deserialize(badAudio).ok);
        CHECK(!ProjectSerializer::deserialize(badFont).ok);
    }

    // -- import audio/font: copy, kind dir, defaults, override ----------------
    {
        const std::filesystem::path base = testTempDir();   // call once: it wipes on each call
        const std::filesystem::path root = base / "import-media";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path ogg = base / "theme.ogg";
        { std::ofstream f(ogg, std::ios::binary); f << "OGG"; }
        const std::filesystem::path ttf = base / "type.ttf";
        { std::ofstream f(ttf, std::ios::binary); f << "TTF"; }

        EditorCoordinator c{makeDoc()};
        const ImportAssetResult a = importAsset(c, root, {AssetKind::Audio, ogg});
        CHECK(a.ok);
        CHECK(c.document().findAudioAsset("theme")->sourcePath == "assets/audio/theme.ogg");
        CHECK(c.document().findAudioAsset("theme")->loadMode == AudioLoadMode::Stream); // ogg default
        CHECK(std::filesystem::exists(root / "assets" / "audio" / "theme.ogg"));

        const ImportAssetResult f = importAsset(c, root, {AssetKind::Font, ttf});
        CHECK(f.ok);
        CHECK(c.document().findFontAsset("type")->sourcePath == "assets/fonts/type.ttf");
        CHECK(std::filesystem::exists(root / "assets" / "fonts" / "type.ttf"));

        // explicit load-mode override wins over the extension default
        const std::filesystem::path ogg2 = base / "blip.ogg";
        { std::ofstream g(ogg2, std::ios::binary); g << "OGG"; }
        ImportAssetRequest req;
        req.kind = AssetKind::Audio;
        req.sourcePath = ogg2;
        req.audioMode = AudioLoadMode::StaticSound;
        CHECK(importAsset(c, root, req).ok);
        CHECK(c.document().findAudioAsset("blip")->loadMode == AudioLoadMode::StaticSound);

        // wrong kind/format rejected
        CHECK(!importAsset(c, root, {AssetKind::Audio, ttf}).ok);   // .ttf as audio
    }

    // == Asset import pipeline (single canonical entry point) =================
    {
        const std::filesystem::path base = testTempDir();   // call once: it wipes on each call
        const std::filesystem::path root = base / "import-basic";
        std::error_code ec;
        std::filesystem::create_directories(root, ec);
        const std::filesystem::path src = base / "src.png";
        { std::ofstream f(src, std::ios::binary); f << "PNGDATA"; }

        EditorCoordinator c{makeDoc()};
        const ImportAssetResult r = importAsset(c, root, {AssetKind::Image, src});
        CHECK(r.ok);
        CHECK(r.assetId == "src");
        CHECK(c.document().findImageAsset("src")->sourcePath == "assets/images/src.png");
        CHECK(std::filesystem::exists(root / "assets" / "images" / "src.png"));
        CHECK(c.canUndo());   // import is an authoring command

        // Re-import the same source: unique file name + AssetId, no overwrite.
        const ImportAssetResult r2 = importAsset(c, root, {AssetKind::Image, src});
        CHECK(r2.ok);
        CHECK(r2.assetId == "src_2");
        CHECK(std::filesystem::exists(root / "assets" / "images" / "src_2.png"));
    }

    // -- import rejects: unsaved project, unsupported kind/format, during Play -
    {
        const std::filesystem::path base = testTempDir();   // call once: it wipes on each call
        const std::filesystem::path root = base / "import-reject";
        std::error_code ec; std::filesystem::create_directories(root, ec);
        const std::filesystem::path src = base / "reject.png";
        { std::ofstream f(src, std::ios::binary); f << "PNGDATA"; }
        const std::filesystem::path gif = base / "x.gif";
        { std::ofstream f(gif, std::ios::binary); f << "GIF"; }

        EditorCoordinator c{makeDoc()};
        CHECK(!importAsset(c, {}, {AssetKind::Image, src}).ok);          // unsaved
        CHECK(!importAsset(c, root, {AssetKind::Audio, src}).ok);        // .png as audio
        CHECK(!importAsset(c, root, {AssetKind::Image, gif}).ok);        // bad image format
        CHECK(c.document().data().imageAssets.empty());

        CHECK(c.playProject().ok);
        CHECK(!importAsset(c, root, {AssetKind::Image, src}).ok);        // during Play
    }

    // == Viewport camera transform + picking ==================================

    // -- screenToWorld inverts the renderer camera ----------------------------
    {
        ViewportRect rect{100, 50, 800, 600};
        EditorSceneViewState view;
        view.zoom = 2.f;
        view.pan = Vec2{10.f, -20.f};
        const SceneViewCamera cam = makeSceneViewCamera(rect, view, Vec2{640.f, 480.f});
        // Viewport centre maps to the camera target (world centre + pan).
        const Vec2 centre = screenToWorld(cam, Vec2{500.f, 350.f});
        CHECK(centre.x == 330.f);   // 640/2 + 10
        CHECK(centre.y == 220.f);   // 480/2 - 20
        // Two screen pixels right of centre is 1 world unit at zoom 2.
        const Vec2 off = screenToWorld(cam, Vec2{502.f, 350.f});
        CHECK(off.x == 331.f);
    }

    // -- pickEntityAt: topmost hit; miss returns INVALID ----------------------
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{7, "A", {}, SceneFrameRect{0, 0, 50, 50}, false});
        f.entities.push_back(SceneFrameEntity{8, "B", {}, SceneFrameRect{40, 40, 50, 50}, false});
        CHECK(pickEntityAt(f, Vec2{10.f, 10.f}) == 7);
        CHECK(pickEntityAt(f, Vec2{45.f, 45.f}) == 8);   // overlap -> later wins
        CHECK(pickEntityAt(f, Vec2{200.f, 200.f}) == INVALID_ENTITY);
    }

    // -- pickEntityAt: a visible sprite occludes a placeholder -----------------
    // frame.entities is the pick order authority (collectSceneFrameSnapshot
    // always emits every instance into it, in render order) - 1 is background,
    // 3 and 2 are foreground of it, so this exercises both "later in
    // frame.entities wins" and "invisible sprite never hits regardless".
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{1, "P", {}, SceneFrameRect{0, 0, 50, 50}, false});
        f.entities.push_back(SceneFrameEntity{3, "S3", {}, SceneFrameRect{10, 10, 50, 50}, false});
        f.entities.push_back(SceneFrameEntity{2, "S2", {}, SceneFrameRect{10, 10, 50, 50}, false});
        f.sprites.push_back(SceneFrameSprite{2, "img", SceneFrameRect{10, 10, 50, 50}, {}, true, false});
        f.sprites.push_back(SceneFrameSprite{3, "img", SceneFrameRect{10, 10, 50, 50}, {}, false, false});
        CHECK(pickEntityAt(f, Vec2{20.f, 20.f}) == 2);   // sprite over placeholder
        CHECK(pickEntityAt(f, Vec2{5.f, 5.f}) == 1);     // invisible sprite ignored
    }

    // -- editorBoundsForEntity: sprite/collider union, else placeholder -------
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.worldSize = Vec2{100.f, 100.f};
        f.entities.push_back(SceneFrameEntity{1, "P", {}, SceneFrameRect{0, 0, 10, 10}, false});
        f.entities.push_back(SceneFrameEntity{2, "Q", {}, SceneFrameRect{70, 70, 10, 10}, false});
        f.sprites.push_back(SceneFrameSprite{1, "img", SceneFrameRect{20, 0, 10, 10}, {}, true, false});
        f.sprites.push_back(SceneFrameSprite{1, "hidden", SceneFrameRect{-100, -100, 10, 10}, {}, false, false});
        f.colliders.push_back(SceneFrameCollider{
            1, WorldRect{0, 20, 10, 10}, true, BoxColliderMode::Solid, false});

        const std::optional<WorldRect> bounds = editorBoundsForEntity(f, 1);
        CHECK(bounds.has_value());
        CHECK(bounds->x == 0.f);
        CHECK(bounds->y == 0.f);
        CHECK(bounds->width == 30.f);
        CHECK(bounds->height == 30.f);

        const std::optional<WorldRect> fallback = editorBoundsForEntity(f, 2);
        CHECK(fallback.has_value());
        CHECK(fallback->x == 70.f);
        CHECK(fallback->y == 70.f);
    }

    // -- pickEntityAt: a tilemap cell is hit-testable, and a sprite-owning
    // entity later in frame.entities (foreground) still wins over one earlier
    // (background) that owns a tilemap ------------------------------------------
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{1, "Ground", {}, SceneFrameRect{0, 0, 10, 10}, false});
        f.entities.push_back(SceneFrameEntity{2, "Hero", {}, SceneFrameRect{10, 10, 10, 10}, false});
        f.tilemaps.push_back(SceneFrameTilemap{
            1, "tiles-img",
            {SceneFrameTilemapCell{SceneFrameRect{0, 0, 32, 32}, SceneFrameRect{0, 0, 16, 16}}},
            false});
        f.sprites.push_back(SceneFrameSprite{2, "img", SceneFrameRect{10, 10, 10, 10}, {}, true, false});
        CHECK(pickEntityAt(f, Vec2{5.f, 5.f}) == 1);      // hits the tile, not just the placeholder
        CHECK(pickEntityAt(f, Vec2{15.f, 15.f}) == 2);    // sprite entity, later in render order, wins
        CHECK(pickEntityAt(f, Vec2{500.f, 500.f}) == INVALID_ENTITY);
    }

    // -- editorBoundsForEntity: tilemap cells union like sprite/collider
    // bounds, and an empty tilemap (no cells) falls through to the
    // placeholder, same as any other content-less entity ----------------------
    {
        SceneFrameSnapshot f;
        f.hasScene = true;
        f.entities.push_back(SceneFrameEntity{1, "T", {}, SceneFrameRect{5, 5, 1, 1}, false});
        f.tilemaps.push_back(SceneFrameTilemap{
            1, "tiles-img",
            {SceneFrameTilemapCell{SceneFrameRect{0, 0, 20, 20}, {}},
             SceneFrameTilemapCell{SceneFrameRect{20, 20, 20, 20}, {}}},
            false});
        const std::optional<WorldRect> bounds = editorBoundsForEntity(f, 1);
        CHECK(bounds.has_value());
        CHECK(bounds->x == 0.f);
        CHECK(bounds->y == 0.f);
        CHECK(bounds->width == 40.f);
        CHECK(bounds->height == 40.f);

        SceneFrameSnapshot empty;
        empty.hasScene = true;
        empty.entities.push_back(SceneFrameEntity{1, "T", {}, SceneFrameRect{5, 5, 1, 1}, false});
        empty.tilemaps.push_back(SceneFrameTilemap{1, "tiles-img", {}, false});
        const std::optional<WorldRect> emptyBounds = editorBoundsForEntity(empty, 1);
        CHECK(emptyBounds.has_value());
        CHECK(emptyBounds->x == 5.f);   // no cells -> falls through to the placeholder bounds
    }

    // -- collectSceneFrameSnapshot: an instance's TilemapComponent resolves
    // into snapshot.tilemaps with world-space cells; an empty component still
    // produces an entry, just with no cells --------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);   // "tiles-1" now has "tile-1".."tile-4"
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.cells = {TilemapCellValue{"tile-1", TileTransformFlags::None},
                      std::nullopt, std::nullopt, std::nullopt};
        tm.chunks.push_back(chunk);
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        // kHero's transform.position is {10,20} (makeDoc's fixture) - the world
        // origin for this tilemap's cell (0,0).
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {10.f, 20.f}}).ok);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        const auto it = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(it != snap.tilemaps.end());
        CHECK(it->imageAssetId == "img-hero");
        CHECK(it->cells.size() == 1);
        CHECK(it->cells[0].destination.x == 10.f);
        CHECK(it->cells[0].destination.y == 20.f);

        CHECK(c.execute(RemoveTilemapComponentCommand{kSceneA, kHero}).ok);
        TilemapComponent emptyTm;
        emptyTm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, emptyTm}).ok);
        const SceneFrameSnapshot emptySnap =
            collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        const auto emptyIt = std::find_if(emptySnap.tilemaps.begin(), emptySnap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(emptyIt != emptySnap.tilemaps.end());
        CHECK(emptyIt->cells.empty());
    }

    // -- scene containment and explicit bring-inside math ---------------------
    {
        CHECK(classifySceneContainment(WorldRect{10, 10, 20, 20}, Vec2{100, 100})
              == SceneContainment::Inside);
        CHECK(classifySceneContainment(WorldRect{90, 10, 20, 20}, Vec2{100, 100})
              == SceneContainment::PartiallyOutside);
        CHECK(classifySceneContainment(WorldRect{120, 10, 20, 20}, Vec2{100, 100})
              == SceneContainment::FullyOutside);

        const std::optional<Vec2> right =
            positionToBringBoundsInsideScene(WorldRect{90, 10, 20, 20},
                                             Vec2{100, 20}, Vec2{100, 100});
        CHECK(right.has_value());
        CHECK(right->x == 90.f);
        CHECK(right->y == 20.f);

        const std::optional<Vec2> large =
            positionToBringBoundsInsideScene(WorldRect{-30, 0, 200, 20},
                                             Vec2{70, 10}, Vec2{100, 100});
        CHECK(large.has_value());
        CHECK(large->x == 50.f);   // entity wider than scene -> centered on X
        CHECK(large->y == 10.f);

        const std::optional<Vec2> bad =
            positionToBringBoundsInsideScene(WorldRect{0, 0, 10, 10},
                                             Vec2{std::numeric_limits<float>::quiet_NaN(), 0},
                                             Vec2{100, 100});
        CHECK(!bad.has_value());
    }

    // == Scene properties (Scene Inspector) ===================================

    // -- Rename scene: applies, rejects empty, undo/redo ----------------------
    {
        EditorCoordinator c{makeDoc()};                  // kSceneA name "Scene A"
        CHECK(c.execute(RenameSceneCommand{kSceneA, "Level 1"}).ok);
        CHECK(c.document().findScene(kSceneA)->name == "Level 1");
        CHECK(!c.execute(RenameSceneCommand{kSceneA, ""}).ok);   // empty rejected
        CHECK(c.undo().ok);
        CHECK(c.document().findScene(kSceneA)->name == "Scene A");
        CHECK(c.redo().ok);
        CHECK(c.document().findScene(kSceneA)->name == "Level 1");
    }

    // -- Rename object type: applies (shared by every instance of that type),
    // rejects empty/unknown id, same name is a no-op, undo/redo ---------------
    {
        EditorCoordinator c{makeInheritedDoc()};   // catalog already has "Hero"
        CHECK(c.execute(RenameObjectTypeCommand{"Hero", "Champion"}).ok);
        CHECK(c.document().findObjectType("Hero")->name == "Champion");

        CHECK(!c.execute(RenameObjectTypeCommand{"Hero", ""}).ok);        // empty rejected
        CHECK(!c.execute(RenameObjectTypeCommand{"Missing", "X"}).ok);    // unknown id rejected
        CHECK(c.document().findObjectType("Hero")->name == "Champion");  // unaffected by rejects

        // Same name is a no-op: succeeds, but nothing invalidates and no undo entry.
        const std::size_t undoBefore = c.undoSize();
        CHECK(c.execute(RenameObjectTypeCommand{"Hero", "Champion"}).ok);
        CHECK(c.undoSize() == undoBefore);

        CHECK(c.undo().ok);
        CHECK(c.document().findObjectType("Hero")->name == "Hero");      // back to original
        CHECK(c.redo().ok);
        CHECK(c.document().findObjectType("Hero")->name == "Champion");
    }

    // -- Set scene size: validation, integer normalize, never moves instances -
    {
        EditorCoordinator c{makeDoc()};
        const Vec2 heroBefore = c.document().findInstanceInScene(kSceneA, kHero)->transform.position;

        CHECK(c.execute(SetSceneSizeCommand{kSceneA, {320.f, 180.f}}).ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 320.f);
        CHECK(c.document().findScene(kSceneA)->worldSize.y == 180.f);
        // Resizing must not move an instance (Outside Scene UX flags it instead).
        const Vec2 heroAfter = c.document().findInstanceInScene(kSceneA, kHero)->transform.position;
        CHECK(heroAfter.x == heroBefore.x && heroAfter.y == heroBefore.y);

        // Non-positive / non-finite are rejected without mutation.
        CHECK(!c.execute(SetSceneSizeCommand{kSceneA, {0.f, 100.f}}).ok);
        CHECK(!c.execute(SetSceneSizeCommand{kSceneA, {100.f, -5.f}}).ok);
        CHECK(!c.execute(SetSceneSizeCommand{
            kSceneA, {std::numeric_limits<float>::infinity(), 100.f}}).ok);
        CHECK(!c.execute(SetSceneSizeCommand{
            kSceneA, {100.f, std::numeric_limits<float>::quiet_NaN()}}).ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 320.f);   // unchanged

        // Committed values normalize to whole pixels.
        CHECK(c.execute(SetSceneSizeCommand{kSceneA, {199.4f, 150.6f}}).ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 199.f);
        CHECK(c.document().findScene(kSceneA)->worldSize.y == 151.f);

        CHECK(c.undo().ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 320.f);
        CHECK(c.redo().ok);
        CHECK(c.document().findScene(kSceneA)->worldSize.x == 199.f);
    }

    // -- Persistent numeric boundaries reject NaN/Inf atomically ------------
    {
        EditorCoordinator c{makeDoc()};
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const float negInf = -std::numeric_limits<float>::infinity();
        const uint64_t revisionBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();
        const Vec2 positionBefore =
            c.document().findInstanceInScene(kSceneA, kHero)->transform.position;
        const Vec4 backgroundBefore = c.document().findScene(kSceneA)->backgroundColor;

        CHECK(!c.execute(SetEntityPositionCommand{kSceneA, kHero, {nan, 10.f}}).ok);
        CHECK(!c.execute(SetEntityPositionCommand{kSceneA, kHero, {10.f, inf}}).ok);
        CHECK(!c.execute(SetEntityPositionCommand{kSceneA, kHero, {negInf, 10.f}}).ok);
        CHECK(!c.execute(CreateEntityCommand{kSceneA, 1001, "Hero", "Bad", {inf, 0.f}}).ok);
        CHECK(!c.execute(CloneInstanceCommand{kSceneA, kHero, 1002, "Bad clone", {0.f, nan}}).ok);
        CHECK(!c.execute(CreateEntityWithDefaultTypeCommand{
            kSceneA, 1003, "BadType", "Bad", "Bad", {nan, 0.f}}).ok);
        CHECK(!c.execute(SetSceneBackgroundCommand{kSceneA, {0.f, nan, 0.f, 1.f}}).ok);

        const SceneInstanceDef* hero = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(hero->transform.position.x == positionBefore.x);
        CHECK(hero->transform.position.y == positionBefore.y);
        CHECK(c.document().findInstanceInScene(kSceneA, 1001) == nullptr);
        CHECK(c.document().findInstanceInScene(kSceneA, 1002) == nullptr);
        CHECK(c.document().findInstanceInScene(kSceneA, 1003) == nullptr);
        CHECK(!c.document().hasObjectType("BadType"));
        const Vec4 backgroundAfter = c.document().findScene(kSceneA)->backgroundColor;
        CHECK(backgroundAfter.r == backgroundBefore.r && backgroundAfter.g == backgroundBefore.g);
        CHECK(backgroundAfter.b == backgroundBefore.b && backgroundAfter.a == backgroundBefore.a);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.undoSize() == undoBefore);
    }

    // -- Scene name + size survive save/reload --------------------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(RenameSceneCommand{kSceneA, "Arena"}).ok);
        CHECK(c.execute(SetSceneSizeCommand{kSceneA, {640.f, 360.f}}).ok);
        const std::filesystem::path path = testTempDir() / "scene-props.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().findScene(kSceneA)->name == "Arena");
        CHECK(reloaded.document().findScene(kSceneA)->worldSize.x == 640.f);
        CHECK(reloaded.document().findScene(kSceneA)->worldSize.y == 360.f);
    }

    // == Scene view navigation (workspace camera) =============================

    // -- Zoom under the cursor keeps the world point beneath the mouse fixed ---
    //    Mirrors routeViewportInput: read world-before, apply zoom, compensate
    //    with one pan. Uses the single makeSceneViewCamera/screenToWorld.
    {
        const ViewportRect rect{0, 0, 800, 600};
        const Vec2 worldSize{1000.f, 1000.f};
        const Vec2 mouse{220.f, 140.f};   // off-centre, so a centre-zoom would drift
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 1.0f});
        c.apply(PanViewportIntent{kSceneA, {30.f, -20.f}});

        const EditorSceneViewState before = c.sceneView(kSceneA);
        const Vec2 worldBefore = screenToWorld(makeSceneViewCamera(rect, before, worldSize), mouse);
        c.apply(SetViewportZoomIntent{kSceneA, before.zoom * 1.5f});
        const EditorSceneViewState mid = c.sceneView(kSceneA);
        const Vec2 worldMid = screenToWorld(makeSceneViewCamera(rect, mid, worldSize), mouse);
        c.apply(PanViewportIntent{kSceneA, {worldBefore.x - worldMid.x, worldBefore.y - worldMid.y}});

        const EditorSceneViewState fixed = c.sceneView(kSceneA);
        const Vec2 worldFinal = screenToWorld(makeSceneViewCamera(rect, fixed, worldSize), mouse);
        CHECK(std::abs(worldFinal.x - worldBefore.x) < 0.01f);
        CHECK(std::abs(worldFinal.y - worldBefore.y) < 0.01f);
    }

    // -- Zoom is clamped to [10%, 800%] ---------------------------------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 100.0f});
        CHECK(c.sceneView(kSceneA).zoom == SceneViewLimits::kZoomMax);
        c.apply(SetViewportZoomIntent{kSceneA, 0.0001f});
        CHECK(c.sceneView(kSceneA).zoom == SceneViewLimits::kZoomMin);
    }

    // -- Workspace numeric state rejects NaN/Inf; finite extremes clamp ------
    {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        EditorCoordinator c{makeAnimationDoc()};
        const uint64_t revisionBefore = c.document().revision();

        CHECK(!c.apply(SetViewportZoomIntent{kSceneA, nan}).ok);
        CHECK(!c.apply(SetViewportZoomIntent{kSceneA, 0.f}).ok);
        CHECK(!c.apply(PanViewportIntent{kSceneA, {inf, 0.f}}).ok);
        CHECK(c.sceneView(kSceneA).zoom == 1.f);
        CHECK(c.sceneView(kSceneA).pan.x == 0.f);

        CHECK(c.apply(OpenSpriteAnimationEditorIntent{"hero.anim"}).ok);
        CHECK(!c.apply(SetSpriteSheetZoomIntent{nan}).ok);
        CHECK(!c.apply(PanSpriteSheetIntent{{0.f, inf}}).ok);
        CHECK(c.state().spriteAnimationEditor.sheetZoom == 1.f);
        CHECK(c.state().spriteAnimationEditor.sheetPan.y == 0.f);

        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(!c.apply(SetTilesetEditorZoomIntent{inf}).ok);
        CHECK(!c.apply(PanTilesetEditorIntent{{nan, 0.f}}).ok);
        CHECK(c.state().tilesetEditor.zoom == 1.f);
        CHECK(c.state().tilesetEditor.pan.x == 0.f);

        const float panelBefore = c.uiState().leftPanelWidth;
        CHECK(!c.apply(ResizePanelIntent{ResizePanelIntent::Panel::Left, nan}).ok);
        CHECK(c.uiState().leftPanelWidth == panelBefore);
        CHECK(c.apply(ResizePanelIntent{
            ResizePanelIntent::Panel::Left, std::numeric_limits<float>::max()}).ok);
        CHECK(c.uiState().leftPanelWidth == PanelLimits::kLeftMax);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(c.undoSize() == 0);
    }

    // -- Reset to 100% changes only the zoom, never the target (pan) ----------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(PanViewportIntent{kSceneA, {50.f, 30.f}});
        c.apply(SetViewportZoomIntent{kSceneA, 3.0f});
        c.apply(SetViewportZoomIntent{kSceneA, 1.0f});   // "Reset 100%"
        CHECK(c.sceneView(kSceneA).zoom == 1.0f);
        CHECK(c.sceneView(kSceneA).pan.x == 50.f);       // target unchanged
        CHECK(c.sceneView(kSceneA).pan.y == 30.f);
    }

    // -- Each scene keeps its own pan and zoom --------------------------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 2.0f});
        c.apply(PanViewportIntent{kSceneA, {10.f, 0.f}});
        c.apply(SetViewportZoomIntent{kSceneB, 0.5f});
        c.apply(PanViewportIntent{kSceneB, {-5.f, 5.f}});
        CHECK(c.sceneView(kSceneA).zoom == 2.0f);
        CHECK(c.sceneView(kSceneA).pan.x == 10.f);
        CHECK(c.sceneView(kSceneB).zoom == 0.5f);
        CHECK(c.sceneView(kSceneB).pan.y == 5.f);
    }

    // -- Camera ops are workspace-only: no dirty / revision / undo ------------
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t rev = c.document().revision();
        c.apply(SetViewportZoomIntent{kSceneA, 2.0f});
        c.apply(PanViewportIntent{kSceneA, {5.f, 5.f}});
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == 0);
    }

    // -- Scene grid/snap intents reject a scene that doesn't exist -------------
    // (defence in depth: the toolbar/menu also disable these with no active
    // scene, but the coordinator must not rely on that alone).
    {
        EditorCoordinator c{makeDoc()};
        const SceneId bogus{"no-such-scene"};
        CHECK(!c.apply(SetSceneGridVisibilityIntent{bogus, false}).ok);
        CHECK(!c.apply(SetSceneGridSnapEnabledIntent{bogus, true}).ok);
        CHECK(!c.apply(SetSceneGridCellSizeIntent{bogus, 32.0f}).ok);
        CHECK(c.sceneView(bogus).gridVisible);   // default, untouched by the map
    }

    // -- Zoom/Pan intents reject a scene that doesn't exist, same reason ------
    {
        EditorCoordinator c{makeDoc()};
        const SceneId bogus{"no-such-scene"};
        CHECK(!c.apply(SetViewportZoomIntent{bogus, 2.0f}).ok);
        CHECK(!c.apply(PanViewportIntent{bogus, {10.f, 10.f}}).ok);
        CHECK(c.sceneView(bogus).zoom == 1.0f);   // default, untouched by the map
        CHECK(c.sceneView(bogus).pan.x == 0.f);
    }

    // -- Scene grid/snap toggles are workspace-only and per-scene -------------
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t rev = c.document().revision();
        CHECK(c.sceneView(kSceneA).gridVisible);
        CHECK(!c.sceneView(kSceneA).gridSnapEnabled);
        CHECK(c.sceneView(kSceneA).gridCellSize == 32.0f);

        CHECK(c.apply(SetSceneGridVisibilityIntent{kSceneA, false}).ok);
        CHECK(c.apply(SetSceneGridSnapEnabledIntent{kSceneA, true}).ok);
        CHECK(c.apply(SetSceneGridCellSizeIntent{kSceneA, 48.0f}).ok);
        CHECK(!c.sceneView(kSceneA).gridVisible);
        CHECK(c.sceneView(kSceneA).gridSnapEnabled);
        CHECK(c.sceneView(kSceneA).gridCellSize == 48.0f);
        CHECK(c.sceneView(kSceneB).gridVisible);
        CHECK(!c.sceneView(kSceneB).gridSnapEnabled);
        CHECK(c.sceneView(kSceneB).gridCellSize == 32.0f);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == 0);

        CHECK(c.apply(SetSceneGridVisibilityIntent{kSceneB, false}).ok);
        CHECK(c.apply(SetSceneGridCellSizeIntent{kSceneB, 64.0f}).ok);
        CHECK(!c.sceneView(kSceneB).gridVisible);
        CHECK(!c.sceneView(kSceneB).gridSnapEnabled);
        CHECK(c.sceneView(kSceneB).gridCellSize == 64.0f);

        const std::filesystem::path path = testTempDir() / "grid-snap-workspace.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator loaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(loaded, path).ok);
        CHECK(loaded.sceneView(kSceneA).gridVisible);
        CHECK(!loaded.sceneView(kSceneA).gridSnapEnabled);
        CHECK(loaded.sceneView(kSceneA).gridCellSize == 32.0f);

        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.state().sceneViews.count(kSceneA) == 0);
        CHECK(c.sceneView(kSceneA).gridVisible);
        CHECK(!c.sceneView(kSceneA).gridSnapEnabled);
        CHECK(c.sceneView(kSceneA).gridCellSize == 32.0f);
    }

    // -- Grid cell size rejects invalid values and no-op changes stay quiet ---
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t rev = c.document().revision();
        c.consumeInvalidations();

        CHECK(!c.apply(SetSceneGridCellSizeIntent{kSceneA, 0.0f}).ok);
        CHECK(!c.apply(SetSceneGridCellSizeIntent{kSceneA, -4.0f}).ok);
        CHECK(!c.apply(SetSceneGridCellSizeIntent{
            kSceneA, std::numeric_limits<float>::quiet_NaN()}).ok);
        CHECK(!c.apply(SetSceneGridCellSizeIntent{
            kSceneA, std::numeric_limits<float>::infinity()}).ok);
        CHECK(c.sceneView(kSceneA).gridCellSize == 32.0f);
        // Each rejected intent is still a real, user-visible failure (never
        // silent) - it logs a Console warning, which is itself an invalidation.
        CHECK(c.pendingInvalidations() == EditorInvalidation::Console);
        c.consumeInvalidations();

        const EditorOperationResult same =
            c.apply(SetSceneGridCellSizeIntent{kSceneA, 32.0f});
        CHECK(same.ok);
        CHECK(same.invalidation == EditorInvalidation::None);
        CHECK(c.pendingInvalidations() == EditorInvalidation::None);
        CHECK(!c.document().isDirty());
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == 0);
    }

    // -- Grid snap uses nearest world grid point, including negative values ---
    {
        const SceneGridDefinition grid = worldAuthoringGrid(EditorSceneViewState{});
        CHECK(grid.kind == SceneGridKind::World);
        CHECK(grid.cellSize.x == 32.0f);
        CHECK(grid.cellSize.y == 32.0f);
        CHECK(snapWorldPositionToGrid(Vec2{31.f, 73.f}, grid).x == 32.f);
        CHECK(snapWorldPositionToGrid(Vec2{31.f, 73.f}, grid).y == 64.f);
        CHECK(snapWorldPositionToGrid(Vec2{-15.f, -26.f}, grid).x == 0.f);
        CHECK(snapWorldPositionToGrid(Vec2{-25.f, -26.f}, grid).x == -32.f);
        CHECK(snapWorldPositionToGrid(Vec2{-25.f, -26.f}, grid).y == -32.f);
        CHECK(snapWorldPositionToGrid(Vec2{96.f, 144.f}, grid).x == 96.f);
        CHECK(snapWorldPositionToGrid(Vec2{16.f, -16.f}, grid).x == 32.f);
        CHECK(snapWorldPositionToGrid(Vec2{16.f, -16.f}, grid).y == -32.f);

        const SceneGridDefinition shifted{
            SceneGridKind::World,
            Vec2{16.0f, 16.0f},
            Vec2{8.0f, 8.0f},
        };
        const Vec2 snapped = snapWorldPositionToGrid(Vec2{15.f, -1.f}, shifted);
        CHECK(snapped.x == 8.f);
        CHECK(snapped.y == -8.f);

        EditorSceneViewState view;
        view.gridCellSize = 32.0f;
        const SceneGridDefinition fromView = worldAuthoringGrid(view);
        CHECK(fromView.cellSize.x == 32.0f);
        CHECK(fromView.origin.x == 0.0f);
        CHECK(fromView.origin.y == 0.0f);
        CHECK(snapWorldPositionToGrid(Vec2{47.f, -15.f}, fromView).x == 32.f);
        CHECK(snapWorldPositionToGrid(Vec2{47.f, -15.f}, fromView).y == 0.f);
        CHECK(snapWorldPositionToGrid(Vec2{47.f, -17.f}, fromView).y == -32.f);

        CHECK(visualGridStrideForZoom(1.0f, 0.1f) > 1);
        const SceneGridDefinition tiny{
            SceneGridKind::World,
            Vec2{1.0f, 1.0f},
            Vec2{0.0f, 0.0f},
        };
        CHECK(snapWorldPositionToGrid(Vec2{2.4f, 0.0f}, tiny).x == 2.0f);
    }

    // -- Contextual grid resolver: world vs tilemap by active tool -------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {213.f, 119.f}}).ok);
        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 16.f}}).ok);
        c.apply(SetSceneGridCellSizeIntent{kSceneA, 32.0f});

        const auto worldToCell = [](Vec2 world, Vec2 origin, Vec2 cellSize) {
            return TilemapCellCoord{
                static_cast<int>(std::floor((world.x - origin.x) / cellSize.x)),
                static_cast<int>(std::floor((world.y - origin.y) / cellSize.y)),
            };
        };

        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.state().activeTool == EditorTool::Brush);
        CHECK(selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        const SceneGridDefinition brushGrid =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(brushGrid.kind == SceneGridKind::Tilemap);
        CHECK(brushGrid.origin.x == 213.f);
        CHECK(brushGrid.origin.y == 119.f);
        CHECK(brushGrid.cellSize.x == 16.f);
        CHECK(brushGrid.cellSize.y == 16.f);

        const SceneGridDefinition worldWhileBrush = worldAuthoringGrid(c.sceneView(kSceneA));
        CHECK(worldWhileBrush.kind == SceneGridKind::World);
        CHECK(worldWhileBrush.cellSize.x == 32.f);
        CHECK(snapWorldPositionToGrid(Vec2{220.f, 130.f}, worldWhileBrush).x == 224.f);
        CHECK(snapWorldPositionToGrid(Vec2{220.f, 130.f}, worldWhileBrush).y == 128.f);

        const TilemapCellCoord painted =
            worldToCell(Vec2{220.f, 130.f}, brushGrid.origin, brushGrid.cellSize);
        CHECK(painted.cellX == 0);
        CHECK(painted.cellY == 0);

        const TilemapCellCoord withSnapOn = painted;
        c.apply(SetSceneGridSnapEnabledIntent{kSceneA, true});
        CHECK(withSnapOn == worldToCell(Vec2{220.f, 130.f}, brushGrid.origin, brushGrid.cellSize));
        c.apply(SetSceneGridSnapEnabledIntent{kSceneA, false});
        CHECK(withSnapOn == worldToCell(Vec2{220.f, 130.f}, brushGrid.origin, brushGrid.cellSize));

        const uint64_t revBeforeContext = c.document().revision();
        c.apply(SetActiveToolIntent{EditorTool::Select});
        CHECK(c.document().revision() == revBeforeContext);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.document().revision() == revBeforeContext);
        c.apply(SetSceneGridVisibilityIntent{kSceneA, false});
        CHECK(viewportDisplayGrid(c.document(), c.state(), kSceneA).kind == SceneGridKind::Tilemap);
        c.apply(SetSceneGridVisibilityIntent{kSceneA, true});

        c.apply(SetActiveToolIntent{EditorTool::Select});
        const SceneGridDefinition selectGrid =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(selectGrid.kind == SceneGridKind::World);
        CHECK(selectGrid.origin.x == 0.f);
        CHECK(selectGrid.origin.y == 0.f);
        CHECK(selectGrid.cellSize.x == 32.f);

        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {100.f, 50.f}}).ok);
        const SceneGridDefinition moved =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(moved.origin.x == 100.f);
        CHECK(moved.origin.y == 50.f);

        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 32.f}}).ok);
        const SceneGridDefinition rectangular =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(rectangular.cellSize.x == 16.f);
        CHECK(rectangular.cellSize.y == 32.f);

        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {-48.f, -32.f}}).ok);
        const SceneGridDefinition negative =
            viewportDisplayGrid(c.document(), c.state(), kSceneA);
        CHECK(negative.origin.x == -48.f);
        CHECK(negative.origin.y == -32.f);
        const TilemapCellCoord negCell =
            worldToCell(Vec2{-40.f, -20.f}, negative.origin, negative.cellSize);
        CHECK(negCell.cellX == 0);
        CHECK(negCell.cellY == 0);

        c.apply(SetActiveToolIntent{EditorTool::Select});
        CHECK(tilemapCellGrid(c.document(), c.state(), kSceneA).has_value());
        CHECK(viewportDisplayGrid(c.document(), c.state(), kSceneA).kind == SceneGridKind::World);
    }

    // -- selectionSupportsTilemapEditing: legacy layers, layer scope, locks ---
    {
        // Legacy scene (makeDoc): no SceneDef::layers — empty layer id is valid.
        {
            EditorCoordinator c{makeSpriteDoc()};
            const SceneDef* scene = c.document().findScene(kSceneA);
            CHECK(scene != nullptr);
            CHECK(scene->layers.empty());
            CHECK(c.state().activeSceneId == kSceneA);
            setUpTilemapForPainting(c);
            CHECK(selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        }

        // Legacy scene, entity without TilemapComponent.
        {
            EditorCoordinator c{makeSpriteDoc()};
            CHECK(c.state().activeSceneId == kSceneA);
            CHECK(c.apply(SelectEntityIntent{kHero}).ok);
            CHECK(!selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        }

        // Modern scene: active layer matches entity layer.
        {
            EditorCoordinator c{ProjectDoc{}};
            CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
            c.apply(SelectSceneIntent{"s"});
            CHECK(c.state().activeSceneId == "s");
            CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
            CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{}}).ok);
            sliceTilesOne(c);
            TilemapComponent tm;
            tm.tilesetAssetId = "tiles-1";
            tm.chunkSize = 16;
            CHECK(c.execute(CreateEntityCommand{"s", 1, "Hero", "Hero", {}}).ok);
            CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
            CHECK(c.apply(SelectEntityIntent{1}).ok);
            CHECK(selectionSupportsTilemapEditing(c.document(), c.state(), "s"));
        }

        // Modern scene: entity layer differs from the workspace active layer.
        {
            EditorCoordinator c{ProjectDoc{}};
            CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
            c.apply(SelectSceneIntent{"s"});
            CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
            CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{}}).ok);
            CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
            sliceTilesOne(c);
            TilemapComponent tm;
            tm.tilesetAssetId = "tiles-1";
            tm.chunkSize = 16;
            CHECK(c.execute(CreateEntityCommand{"s", 1, "Hero", "Hero", {}, "layer-1"}).ok);
            CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
            CHECK(c.apply(SelectEntityIntent{1}).ok);
            EditorState mismatched = c.state();
            mismatched.sceneViews["s"].activeLayerId = "fg";
            CHECK(!selectionSupportsTilemapEditing(c.document(), mismatched, "s"));
        }

        // Matching layer but locked.
        {
            ProjectDoc doc = makeSpriteDoc();
            SceneDef& scene = doc.scenes.at(kSceneA);
            SceneLayerDef locked;
            locked.id = "locked-layer";
            locked.name = "Locked";
            locked.locked = true;
            scene.layers.push_back(locked);
            scene.defaultLayerId = "locked-layer";
            TilemapComponent tm;
            tm.tilesetAssetId = "tiles-1";
            tm.chunkSize = 16;
            scene.instances.front().tilemap = tm;
            scene.instances.front().layerId = "locked-layer";
            EditorCoordinator c{doc};
            CHECK(c.apply(SelectEntityIntent{kHero}).ok);
            CHECK(!selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        }

        // sceneId must match the active scene.
        {
            EditorCoordinator c{makeSpriteDoc()};
            setUpTilemapForPainting(c);
            CHECK(!selectionSupportsTilemapEditing(c.document(), c.state(), kSceneB));
        }

        // No selection.
        {
            EditorCoordinator c{makeSpriteDoc()};
            setUpTilemapForPainting(c);
            CHECK(c.apply(SelectEntityIntent{INVALID_ENTITY}).ok);
            CHECK(!selectionSupportsTilemapEditing(c.document(), c.state(), kSceneA));
        }
    }

    // -- Grid hidden: overlay off, tilemap paint input unchanged ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(SetSceneGridVisibilityIntent{kSceneA, false});
        CHECK(!c.sceneView(kSceneA).gridVisible);
        CHECK(viewportDisplayGrid(c.document(), c.state(), kSceneA).kind == SceneGridKind::Tilemap);
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());
        c.apply(EndTilePaintStrokeIntent{});
    }

    // -- SceneGridPresentation: toolbar projection (no document mutation) ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {213.f, 119.f}}).ok);
        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 32.f}}).ok);
        c.apply(SetActiveToolIntent{EditorTool::Brush});

        const SceneGridPresentation brushPres =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(brushPres.kind == SceneGridKind::Tilemap);
        CHECK(brushPres.contextName == "Hero");
        CHECK(brushPres.toolbarContextName == "Hero");
        CHECK(brushPres.sizeEditable == false);
        CHECK(brushPres.cellSize.x == 16.f);
        CHECK(brushPres.cellSize.y == 32.f);
        CHECK(brushPres.sourceEntityId == kHero);
        CHECK(brushPres.toolbarTooltip == "Tilemap grid from \"Hero\"");

        CHECK(c.execute(RenameEntityCommand{kSceneA, kHero, "Terrain"}).ok);
        const SceneGridPresentation renamed =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(renamed.contextName == "Terrain");
        CHECK(renamed.toolbarContextName == "Terrain");

        c.apply(SetActiveToolIntent{EditorTool::Select});
        const SceneGridPresentation selectPres =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(selectPres.kind == SceneGridKind::World);
        CHECK(selectPres.contextName == "World");
        CHECK(selectPres.sizeEditable == true);
        CHECK(selectPres.sourceEntityId == std::nullopt);

        CHECK(c.execute(RenameEntityCommand{kSceneA, kHero, "Environment Decorations"}).ok);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        const SceneGridPresentation longName =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(longName.contextName == "Environment Decorations");
        CHECK(longName.toolbarContextName.size() < longName.contextName.size());
        CHECK(longName.toolbarContextName != longName.contextName);

        CHECK(c.execute(RenameEntityCommand{kSceneA, kHero, "Decorazioni città meravigliosa"}).ok);
        const SceneGridPresentation accented =
            makeSceneGridPresentation(c.document(), c.state(), kSceneA);
        CHECK(accented.contextName == "Decorazioni città meravigliosa");
        CHECK(accented.toolbarContextName.size() < accented.contextName.size());
        CHECK(accented.toolbarContextName.find("Decorazioni") == 0);
        // Truncation must not split the multibyte "à" (0xC3 0xA0).
        CHECK(accented.toolbarContextName.find("cit\xC3") == std::string::npos);
        CHECK(accented.toolbarContextName.find("\xE2\x80\xA6") != std::string::npos);
    }

    // -- ViewportPointerReadout: world + cell from contextual grid -------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.execute(SetEntityPositionCommand{kSceneA, kHero, {213.f, 119.f}}).ok);
        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 16.f}}).ok);
        c.apply(SetActiveToolIntent{EditorTool::Brush});

        const ViewportRect rect{0, 0, 800, 600};
        const SceneViewCamera camera =
            makeSceneViewCamera(rect, c.sceneView(kSceneA), Vec2{1920.f, 1080.f});
        const Vec2 screen = Vec2{400.f, 300.f};
        const ViewportPointerReadout readout =
            makePointerReadout(screen, camera, c.document(), c.state(), kSceneA);
        CHECK(readout.valid);
        CHECK(readout.tilemapCell.has_value());
        CHECK(readout.tilemapEntityId == kHero);
        CHECK(readout.tilemapCell->cellX
              == worldPositionToTilemapCell(readout.worldPosition, {213.f, 119.f}, {16.f, 16.f}).cellX);
        CHECK(readout.tilemapCell->cellY
              == worldPositionToTilemapCell(readout.worldPosition, {213.f, 119.f}, {16.f, 16.f}).cellY);

        const TilemapCellCoord fromWorld =
            worldPositionToTilemapCell(Vec2{245.f, 183.f}, {213.f, 119.f}, {16.f, 16.f});
        CHECK(fromWorld.cellX == 2);
        CHECK(fromWorld.cellY == 4);

        const std::string formatted = formatViewportPointerReadout(readout);
        CHECK(formatted.find("World ") == 0);
        CHECK(formatted.find("Cell ") != std::string::npos);

        c.apply(SetActiveToolIntent{EditorTool::Select});
        const ViewportPointerReadout selectOnly =
            makePointerReadout(screen, camera, c.document(), c.state(), kSceneA);
        CHECK(!selectOnly.tilemapCell.has_value());
        CHECK(formatViewportPointerReadout(selectOnly).find("Cell ") == std::string::npos);

        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(SetSceneGridVisibilityIntent{kSceneA, false});
        const ViewportPointerReadout gridHidden =
            makePointerReadout(screen, camera, c.document(), c.state(), kSceneA);
        CHECK(gridHidden.tilemapCell.has_value());

        const TilemapCellCoord neg =
            worldPositionToTilemapCell(Vec2{-40.f, -20.f}, {213.f, 119.f}, {16.f, 16.f});
        CHECK(neg.cellX == -16);
        CHECK(neg.cellY == -9);
    }

    // -- RevealInspectorPropertyIntent: navigation only, no dirty/history ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        const uint64_t revision = c.document().revision();
        const std::size_t undoBefore = c.undoSize();
        const bool dirtyBefore = c.document().isDirty();

        CHECK(c.apply(RevealInspectorPropertyIntent{kHero, InspectorProperty::TilemapCellSize}).ok);
        CHECK(c.uiState().inspectorRevealRequest.has_value());
        CHECK(c.uiState().inspectorRevealRequest->entityId == kHero);
        CHECK(c.uiState().inspectorRevealRequest->property == InspectorProperty::TilemapCellSize);
        CHECK(c.selection().primaryEntity == kHero);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == undoBefore);
        CHECK(c.document().isDirty() == dirtyBefore);

        const std::optional<InspectorRevealRequest> taken = c.takeInspectorRevealRequest();
        CHECK(taken.has_value());
        CHECK(!c.uiState().inspectorRevealRequest.has_value());

        CHECK(!c.apply(RevealInspectorPropertyIntent{9999, InspectorProperty::TilemapCellSize}).ok);
    }

    // -- Auto-fit flag lives in the scene view state, cleared by replaceProject
    {
        EditorCoordinator c{makeDoc()};
        CHECK(!c.sceneView(kSceneA).initialized);
        c.markSceneViewInitialized(kSceneA);
        CHECK(c.sceneView(kSceneA).initialized);
        c.apply(SetViewportZoomIntent{kSceneA, 2.0f});
        CHECK(c.sceneView(kSceneA).initialized);          // survives camera edits
        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(!c.sceneView(kSceneA).initialized);          // sceneViews cleared on replace
    }

    // -- Start/Stop Play does not disturb the Edit camera ---------------------
    {
        EditorCoordinator c{makeDoc()};
        c.apply(SetViewportZoomIntent{kSceneA, 2.5f});
        c.apply(PanViewportIntent{kSceneA, {12.f, -8.f}});
        c.apply(SetSceneGridVisibilityIntent{kSceneA, false});
        c.apply(SetSceneGridSnapEnabledIntent{kSceneA, true});
        c.apply(SetSceneGridCellSizeIntent{kSceneA, 8.0f});
        CHECK(c.playProject().ok);
        CHECK(c.stopPlaying().ok);
        CHECK(c.sceneView(kSceneA).zoom == 2.5f);
        CHECK(c.sceneView(kSceneA).pan.x == 12.f);
        CHECK(c.sceneView(kSceneA).pan.y == -8.f);
        CHECK(!c.sceneView(kSceneA).gridVisible);
        CHECK(c.sceneView(kSceneA).gridSnapEnabled);
        CHECK(c.sceneView(kSceneA).gridCellSize == 8.0f);
    }

    // == Scene layers =========================================================

    // -- New scene has Layer 1 as the default layer; new entities use it -------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        const SceneDef* scene = c.document().findScene("s");
        CHECK(scene->layers.size() == 1);
        CHECK(scene->defaultLayerId == "layer-1");
        CHECK(scene->layers[0].name == "Layer 1");
        CHECK(c.document().hasLayer("s", "layer-1"));
        CHECK(!c.document().hasLayer("s", "nope"));

        c.apply(SelectSceneIntent{"s"});
        CHECK(addEntity(c).ok);
        const EntityId id = nextAvailableEntityId(c.document(), "s") - 1;
        CHECK(c.document().findInstanceInScene("s", id)->layerId == "layer-1");
    }

    // -- Add / rename / move / remove; default + non-empty protected ----------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "bg", "Background", 0}).ok);
        {
            const SceneDef* s = c.document().findScene("s");
            CHECK(s->layers.size() == 3);
            CHECK(s->layers[0].id == "bg");
            CHECK(s->layers[1].id == "layer-1");
            CHECK(s->layers[2].id == "fg");
        }
        CHECK(!c.execute(AddSceneLayerCommand{"s", "fg", "Dup", 0}).ok);       // dup id
        CHECK(!c.execute(AddSceneLayerCommand{"s", "x", "Foreground", 0}).ok); // dup name
        CHECK(c.execute(RenameSceneLayerCommand{"s", "fg", "Top"}).ok);
        CHECK(c.document().findScene("s")->layers[2].name == "Top");
        CHECK(c.execute(MoveSceneLayerCommand{"s", "bg", 2}).ok);              // bg -> top
        CHECK(c.document().findScene("s")->layers[2].id == "bg");
        CHECK(!c.execute(RemoveSceneLayerCommand{"s", "layer-1"}).ok);        // default protected
        CHECK(c.execute(RemoveSceneLayerCommand{"s", "fg"}).ok);              // empty -> removed
        CHECK(c.document().findScene("s")->layers.size() == 2);
        // undo the remove restores it at its original index
        CHECK(c.undo().ok);
        CHECK(c.document().hasLayer("s", "fg"));
    }

    // -- A non-empty layer cannot be removed ----------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "fg"}).ok);
        CHECK(!c.execute(RemoveSceneLayerCommand{"s", "fg"}).ok);
    }

    // -- Rename validates names and default identity is by id ------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "bg", "Background", 0}).ok);

        CHECK(!c.execute(RenameSceneLayerCommand{"s", "fg", ""}).ok);
        CHECK(!c.execute(RenameSceneLayerCommand{"s", "fg", "background"}).ok);
        CHECK(!c.execute(RenameSceneLayerCommand{"s", "missing", "Gameplay"}).ok);

        const uint64_t beforeNoopRevision = c.document().revision();
        const std::size_t beforeNoopUndo = c.undoSize();
        CHECK(c.execute(RenameSceneLayerCommand{"s", "fg", "Foreground"}).ok);
        CHECK(c.document().revision() == beforeNoopRevision);
        CHECK(c.undoSize() == beforeNoopUndo);

        CHECK(c.execute(RenameSceneLayerCommand{"s", "layer-1", "Gameplay"}).ok);
        const SceneDef* scene = c.document().findScene("s");
        CHECK(scene->defaultLayerId == "layer-1");
        CHECK(scene->layers[1].id == "layer-1");
        CHECK(scene->layers[1].name == "Gameplay");
        CHECK(!c.execute(RemoveSceneLayerCommand{"s", "layer-1"}).ok);

        CHECK(c.undo().ok);
        CHECK(c.document().findScene("s")->defaultLayerId == "layer-1");
        CHECK(c.document().findScene("s")->layers[1].name == "Layer 1");
        CHECK(c.redo().ok);
        CHECK(c.document().findScene("s")->defaultLayerId == "layer-1");
        CHECK(c.document().findScene("s")->layers[1].name == "Gameplay");
    }

    // -- SetEntityLayer: same type on different layers; undo/redo -------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "A", {}, "layer-1"}).ok);
        CHECK(c.execute(CreateEntityCommand{"s", 2, "obj-1", "B", {}, "fg"}).ok);  // shares type
        CHECK(c.document().findInstanceInScene("s", 1)->objectTypeId
              == c.document().findInstanceInScene("s", 2)->objectTypeId);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "layer-1");
        CHECK(c.document().findInstanceInScene("s", 2)->layerId == "fg");

        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "fg");
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "layer-1");
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "fg");
        CHECK(!c.execute(SetEntityLayerCommand{"s", 1, "nope"}).ok);   // dest must exist
    }

    // -- Render order follows scene.layers; hidden layers are skipped ---------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);  // [layer-1, fg]
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Bg", "Bg", {}, "layer-1"}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Fg", "Fg", {}, "fg"}).ok);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(c.document(), "s", INVALID_ENTITY);
        CHECK(snap.entities.size() == 2);
        CHECK(snap.entities.front().entityId == 1);   // background drawn first
        CHECK(snap.entities.back().entityId == 2);    // foreground on top

        std::unordered_set<std::string> hidden{"fg"};
        const SceneFrameSnapshot vis =
            collectSceneFrameSnapshot(c.document(), "s", INVALID_ENTITY, hidden);
        CHECK(vis.entities.size() == 1);
        CHECK(vis.entities.front().entityId == 1);    // hidden fg excluded
    }

    // -- Editor visibility is workspace-only; Play renders the layer anyway ----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Fg", "Fg", {}, "fg"}).ok);
        const uint64_t rev = c.document().revision();
        const bool dirtyBefore = c.document().isDirty();
        c.apply(ToggleLayerEditorVisibilityIntent{"s", "fg"});
        CHECK(c.sceneView("s").hiddenLayerIds.count("fg") == 1);
        CHECK(c.document().isDirty() == dirtyBefore);   // workspace only: dirty unchanged
        CHECK(c.document().revision() == rev);
        CHECK(c.playProject().ok);                     // "s" is the start scene (first scene)
        const SceneFrameSnapshot play = collectSceneFrameSnapshot(*c.playSession());
        CHECK(play.entities.size() == 1);              // editor-hidden does not affect Play
        CHECK(c.stopPlaying().ok);
    }

    // -- replaceProject drops stale active/hidden layer workspace -------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        c.apply(SetActiveLayerIntent{"s", "fg"});
        c.apply(ToggleLayerEditorVisibilityIntent{"s", "fg"});
        CHECK(c.sceneView("s").activeLayerId == "fg");
        CHECK(c.replaceProject(ProjectDocument{makeReplacementDoc()}).ok);
        CHECK(c.sceneView("s").activeLayerId.empty());
        CHECK(c.sceneView("s").hiddenLayerIds.empty());
    }

    // -- Save/reload preserves layers, order and assignment -------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);  // [layer-1, fg]
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "fg"}).ok);
        const std::filesystem::path path = testTempDir() / "layers.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator r{ProjectDoc{}};
        CHECK(loadProjectFromFile(r, path).ok);
        const SceneDef* s = r.document().findScene("s");
        CHECK(s->layers.size() == 2);
        CHECK(s->layers[0].id == "layer-1");
        CHECK(s->layers[1].id == "fg");
        CHECK(s->defaultLayerId == "layer-1");
        CHECK(r.document().findInstanceInScene("s", 1)->layerId == "fg");
    }

    // -- A legacy file with no layers migrates to Layer 1 ----------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        const auto loaded = loadProjectFromText(c,
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[)"
            R"({"id":1,"objectTypeId":"T","instanceName":"T"}]}],)"
            R"("objectTypes":[{"id":"T"}]})");
        CHECK(loaded.ok);
        const SceneDef* s = c.document().findScene("s");
        CHECK(!s->layers.empty());
        CHECK(s->layers.front().id == "layer-1");
        CHECK(s->layers.front().name == "Layer 1");
        CHECK(s->defaultLayerId == s->layers.front().id);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == s->defaultLayerId);
    }

    // -- The old untouched Default layer migrates to Layer 1 -------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        const auto loaded = loadProjectFromText(c,
            R"({"activeSceneId":"s","scenes":[{"id":"s",)"
            R"("defaultLayerId":"default",)"
            R"("layers":[{"id":"default","name":"Default"}],)"
            R"("instances":[{"id":1,"objectTypeId":"T","instanceName":"T","layerId":"default"}]}],)"
            R"("objectTypes":[{"id":"T"}]})");
        CHECK(loaded.ok);
        const SceneDef* s = c.document().findScene("s");
        CHECK(s->layers.size() == 1);
        CHECK(s->layers[0].id == "layer-1");
        CHECK(s->layers[0].name == "Layer 1");
        CHECK(s->defaultLayerId == "layer-1");
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "layer-1");
    }

    // == Layer locking ===========================================================

    // -- SetLayerLockedCommand: lock, unlock, no-op, undo/redo, invalid targets -
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(!c.document().findScene("s")->layers[0].locked);

        CHECK(!c.execute(SetLayerLockedCommand{"nope", "layer-1", true}).ok);   // missing scene
        CHECK(!c.execute(SetLayerLockedCommand{"s", "missing", true}).ok);      // missing layer

        const uint64_t revBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);      // no-op
        CHECK(c.document().revision() == revBefore);
        CHECK(c.undoSize() == undoBefore);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);
        CHECK(c.document().findScene("s")->layers[0].locked);
        CHECK(c.document().isLayerLocked("s", "layer-1"));
        CHECK(c.undo().ok);
        CHECK(!c.document().findScene("s")->layers[0].locked);
        CHECK(c.redo().ok);
        CHECK(c.document().findScene("s")->layers[0].locked);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);
        CHECK(!c.document().isLayerLocked("s", "layer-1"));
    }

    // -- Persistence: locked round-trips; an absent field defaults to false ----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", true}).ok);
        const std::filesystem::path path = testTempDir() / "layer-lock.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator r{ProjectDoc{}};
        CHECK(loadProjectFromFile(r, path).ok);
        const SceneDef* s = r.document().findScene("s");
        CHECK(s->layers[0].id == "layer-1" && !s->layers[0].locked);   // absent -> false
        CHECK(s->layers[1].id == "fg" && s->layers[1].locked);         // round-tripped true
    }

    // -- Locked layer blocks RemoveSceneLayerCommand and both directions of
    // SetEntityLayerCommand ----------------------------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", true}).ok);
        CHECK(!c.execute(RemoveSceneLayerCommand{"s", "fg"}).ok);       // locked, even if empty
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", false}).ok);
        CHECK(c.execute(RemoveSceneLayerCommand{"s", "fg"}).ok);        // unlocked: removable again

        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", true}).ok);
        CHECK(!c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);      // target locked
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", false}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);
        CHECK(!c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);      // source locked
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);
        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);       // both unlocked: succeeds
    }

    // -- Regression: SetEntityLayerCommand's lock checks must gate on captured_,
    // exactly like every other command's lock check - not re-run on redo. Drives
    // the Command objects directly (apply/undo/apply again) to mirror exactly
    // what EditorCoordinator's history does on redo: reuse the same object,
    // call apply() a second time - without going through the coordinator's own
    // undo/redo stack, since executing SetLayerLockedCommand in between would
    // itself clear that stack's redo entry and defeat the point of the test. --
    {
        ProjectDocument doc{ProjectDoc{}};
        CreateSceneCommand createScene{"s", "S"};
        CHECK(createScene.apply(doc).ok);
        AddSceneLayerCommand addFg{"s", "fg", "Foreground", 1};
        CHECK(addFg.apply(doc).ok);
        CreateEntityWithDefaultTypeCommand createHero{
            "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"};
        CHECK(createHero.apply(doc).ok);

        SetEntityLayerCommand moveToFg{"s", 1, "fg"};
        CHECK(moveToFg.apply(doc).ok);         // both layers unlocked at the time
        CHECK(doc.findInstanceInScene("s", 1)->layerId == "fg");
        CHECK(moveToFg.undo(doc).ok);
        CHECK(doc.findInstanceInScene("s", 1)->layerId == "layer-1");

        SetLayerLockedCommand lockLayer1{"s", "layer-1", true};
        CHECK(lockLayer1.apply(doc).ok);   // now-source is locked
        CHECK(moveToFg.apply(doc).ok);     // "redo": must still succeed, not re-validated
        CHECK(doc.findInstanceInScene("s", 1)->layerId == "fg");
    }

    // -- Regression: RemoveSceneLayerCommand's lock check must gate on captured_
    // too - not re-run on redo (same direct-Command technique as above) --------
    {
        ProjectDocument doc{ProjectDoc{}};
        CreateSceneCommand createScene{"s", "S"};
        CHECK(createScene.apply(doc).ok);
        AddSceneLayerCommand addFg{"s", "fg", "Foreground", 1};
        CHECK(addFg.apply(doc).ok);

        RemoveSceneLayerCommand removeFg{"s", "fg"};
        CHECK(removeFg.apply(doc).ok);   // unlocked and empty at the time
        CHECK(!doc.hasLayer("s", "fg"));
        CHECK(removeFg.undo(doc).ok);    // restores "fg", still unlocked
        CHECK(doc.hasLayer("s", "fg"));

        SetLayerLockedCommand lockFg{"s", "fg", true};
        CHECK(lockFg.apply(doc).ok);
        CHECK(removeFg.apply(doc).ok);   // "redo": must still succeed despite the current lock
        CHECK(!doc.hasLayer("s", "fg"));
    }

    // -- Locked layer blocks every instance-owned mutation ----------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        CHECK(c.execute(AddSpriteRendererToObjectTypeCommand{"obj-1"}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);

        CHECK(!c.execute(SetEntityPositionCommand{"s", 1, {5.f, 5.f}}).ok);
        CHECK(!c.execute(RenameEntityCommand{"s", 1, "New Name"}).ok);
        CHECK(!c.execute(CloneInstanceCommand{"s", 1, 2, "Clone", {}}).ok);
        SpriteRendererOverride lockedDelta; lockedDelta.visible = false;
        CHECK(!c.execute(SetInstanceSpriteOverrideCommand{"s", 1, lockedDelta}).ok);
        CHECK(!c.execute(AddTilemapComponentCommand{"s", 1, TilemapComponent{}}).ok);
        CHECK(!c.execute(DeleteEntityCommand{"s", 1}).ok);
        // Creating a new instance directly into the locked layer is rejected too.
        CHECK(!c.execute(CreateEntityCommand{"s", 2, "obj-1", "B", {}, "layer-1"}).ok);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);
        CHECK(c.execute(SetEntityPositionCommand{"s", 1, {5.f, 5.f}}).ok);   // unlocked: succeeds
    }

    // -- Object-type-owned components stay editable regardless of the instance's
    // layer lock - including when the type is shared with an unlocked instance --
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "A", "A", {}, "layer-1"}).ok);
        CHECK(c.execute(CreateEntityCommand{"s", 2, "obj-1", "B", {}, "fg"}).ok);  // shares obj-1
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);
        // Entity 1 sits on the now-locked layer-1, but Box Collider 2D belongs
        // to the shared object type obj-1, not to entity 1 specifically.
        CHECK(c.execute(AddBoxColliderCommand{"obj-1"}).ok);
        CHECK(c.execute(SetBoxColliderSizeCommand{"obj-1", {10.f, 10.f}}).ok);
        CHECK(c.execute(AddLinearMoverCommand{"obj-1"}).ok);
        CHECK(c.execute(SetLinearMoverSpeedCommand{"obj-1", 5.f}).ok);
        CHECK(c.execute(RemoveLinearMoverCommand{"obj-1"}).ok);
    }

    // -- FillTilemapIntent: locked layer rejected (mirrors BeginTilePaintStrokeIntent
    // and BeginTileRectangleIntent's own guards, tested earlier in this file) --
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneDef& scene = doc.scenes.at(kSceneA);
        SceneLayerDef locked;
        locked.id = "locked-layer";
        locked.name = "Locked";
        locked.locked = true;
        scene.layers.push_back(locked);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        scene.instances.front().tilemap = tm;
        scene.instances.front().layerId = "locked-layer";
        EditorCoordinator c{doc};
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(!c.apply(FillTilemapIntent{kSceneA, kHero, {0, 0}}).ok);
    }

    // -- Play, Hierarchy selection, visibility/rename/reorder/unlock all ignore
    // the lock - only instance-owned authoring commands respect it -------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);

        c.apply(SelectEntityIntent{1});                       // Hierarchy selection unaffected
        CHECK(c.selection().primaryEntity == 1);
        c.apply(ToggleLayerEditorVisibilityIntent{"s", "layer-1"});
        CHECK(c.sceneView("s").hiddenLayerIds.count("layer-1") == 1);
        CHECK(c.execute(RenameSceneLayerCommand{"s", "layer-1", "Gameplay"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(MoveSceneLayerCommand{"s", "layer-1", 1}).ok);   // reorder still allowed

        CHECK(c.playProject().ok);   // Play is authoring-only protection - never blocked by lock
        CHECK(c.playSession() != nullptr);
        CHECK(c.stopPlaying().ok);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", false}).ok);
        CHECK(!c.document().isLayerLocked("s", "layer-1"));
    }

    // -- Undo/Redo of an action taken before the lock still work after the layer
    // is locked - the gate only guards the new user action, not history replay -
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        CHECK(c.execute(SetEntityPositionCommand{"s", 1, {5.f, 5.f}}).ok);   // before the lock
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);

        CHECK(c.undo().ok);   // undoes SetLayerLocked (LIFO) - back to unlocked
        CHECK(!c.document().isLayerLocked("s", "layer-1"));
        CHECK(c.undo().ok);   // undoes the position change from before the lock
        CHECK(c.document().findInstanceInScene("s", 1)->transform.position.x == 0.f);
        CHECK(c.redo().ok);   // redo the position change
        CHECK(c.document().findInstanceInScene("s", 1)->transform.position.x == 5.f);
        CHECK(c.redo().ok);   // redo the lock itself
        CHECK(c.document().isLayerLocked("s", "layer-1"));
    }

    // -- Delete-then-undo across a lock: locking after a delete must not corrupt
    // the undo path (undo/redo apply to ProjectDocument directly, never gated) -
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "fg"}).ok);
        CHECK(c.execute(DeleteEntityCommand{"s", 1}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "fg", true}).ok);
        CHECK(c.undo().ok);                                  // undoes SetLayerLocked
        CHECK(c.undo().ok);                                  // undoes DeleteEntity -> entity restored
        CHECK(c.document().findInstanceInScene("s", 1) != nullptr);
    }

    // == Active layer scoping ====================================================
    // activeLayerId(): normalizes to the scene's default layer -----------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.activeLayerId("no-such-scene").empty());
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        CHECK(c.activeLayerId("s") == "layer-1");   // nothing selected yet -> scene default
    }

    // -- SelectEntityIntent syncs activeLayerId to the entity's own layer ------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "fg"}).ok);
        CHECK(c.activeLayerId("s") == "layer-1");   // still the default until selected
        c.apply(SelectEntityIntent{1});
        CHECK(c.activeLayerId("s") == "fg");        // synced to the entity's own layer

        // Deselecting leaves activeLayerId exactly where it was.
        c.apply(SelectEntityIntent{INVALID_ENTITY});
        CHECK(c.activeLayerId("s") == "fg");
    }

    // -- SetActiveLayerIntent clears a selection that belongs to the layer just
    // left, and cancels any pending stroke/rectangle regardless of which entity
    // it belongs to; a selection already on the new active layer survives -----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        c.apply(SelectEntityIntent{1});   // activeLayerId -> layer-1
        CHECK(c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());

        c.apply(SetActiveLayerIntent{"s", "fg"});   // switch away from entity 1's layer
        CHECK(c.activeLayerId("s") == "fg");
        CHECK(!c.selection().hasEntity());                          // selection cleared
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());  // pending stroke cancelled
    }
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveLayerIntent{"s", "layer-1"});   // the layer entity 1 is already on
        CHECK(c.selection().primaryEntity == 1);         // survives: still the active layer
    }

    // -- BeginTilePaintStrokeIntent/BeginTileRectangleIntent/FillTilemapIntent
    // all reject an entity outside the active layer, even when unlocked, and
    // all three succeed again once the active layer is switched back ----------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{}}).ok);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);

        c.apply(SetActiveLayerIntent{"s", "fg"});   // active is "fg"; entity 1 is on "layer-1"
        const auto begin = c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}});
        CHECK(!begin.ok);
        CHECK(begin.error.find("active layer") != std::string::npos);
        CHECK(!c.apply(BeginTileRectangleIntent{"s", 1, {0, 0}}).ok);
        CHECK(!c.apply(FillTilemapIntent{"s", 1, {0, 0}}).ok);

        // Switch back: the active-layer gate now passes, and everything else
        // about entity 1's tilemap is genuinely valid, so it fully succeeds.
        c.apply(SetActiveLayerIntent{"s", "layer-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- Regression: the pre-existing lock check used inst->layerId directly,
    // which silently never matched a locked *default* layer for an instance
    // whose own layerId is "" (meaning "use the default"). isInstanceLayerLocked
    // resolves effectiveLayerId first, so this must now correctly reject. -----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{}}).ok);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, ""}).ok);   // "" -> default layer
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);   // locks the default

        const auto begin = c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}});
        CHECK(!begin.ok);
        CHECK(begin.error.find("locked") != std::string::npos);
    }

    // == Entity layer move reconciliation ========================================
    // reconcileWorkspace() keeps activeLayerId == effectiveLayerId(selection)
    // across a SetEntityLayerCommand's apply/undo/redo, not just at select time.

    // -- Move follows the selected entity: selection survives, activeLayerId
    // switches to the destination layer -----------------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SelectEntityIntent{1});
        CHECK(c.activeLayerId("s") == "layer-1");

        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);
        CHECK(c.selection().primaryEntity == 1);   // still selected
        CHECK(c.activeLayerId("s") == "fg");       // active layer followed the move

        // -- Undo: both the entity and the active layer go back together -------
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "layer-1");
        CHECK(c.selection().primaryEntity == 1);
        CHECK(c.activeLayerId("s") == "layer-1");

        // -- Redo: both move forward together again -----------------------------
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene("s", 1)->layerId == "fg");
        CHECK(c.selection().primaryEntity == 1);
        CHECK(c.activeLayerId("s") == "fg");
    }

    // -- Same-layer no-op: no history entry, no console message, no workspace
    // change --------------------------------------------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SelectEntityIntent{1});
        const std::size_t undoBefore = c.undoSize();
        const std::size_t consoleBefore = c.consoleLog().size();
        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "layer-1"}).ok);   // already there
        CHECK(c.undoSize() == undoBefore);         // no-op: not recorded
        CHECK(c.consoleLog().size() == consoleBefore);
        CHECK(c.activeLayerId("s") == "layer-1");
    }

    // -- Move to a hidden target layer: succeeds, activates the hidden layer,
    // keeps the selection, and posts exactly one Info message. A later unrelated
    // reconciliation (a second no-op-free command) does not repeat it ----------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "bg", "Background", 1}).ok);
        c.apply(ToggleLayerEditorVisibilityIntent{"s", "bg"});   // hide it
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SelectEntityIntent{1});
        const std::size_t consoleBefore = c.consoleLog().size();

        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "bg"}).ok);
        CHECK(c.selection().primaryEntity == 1);
        CHECK(c.activeLayerId("s") == "bg");
        CHECK(c.consoleLog().size() == consoleBefore + 1);
        CHECK(c.consoleLog().back().level == ConsoleMessage::Level::Info);
        CHECK(c.consoleLog().back().text.find("hidden layer") != std::string::npos);
        CHECK(c.consoleLog().back().text.find("Background") != std::string::npos);
        // The layer itself was not made visible as a side effect of the move.
        CHECK(c.sceneView("s").hiddenLayerIds.count("bg") == 1);

        const std::size_t consoleAfterMove = c.consoleLog().size();
        CHECK(c.execute(RenameEntityCommand{"s", 1, "Hero Renamed"}).ok);   // unrelated command
        CHECK(c.consoleLog().size() == consoleAfterMove);   // no repeated announcement
    }

    // -- Tilemap entity: moving layers preserves the component and its cells
    // exactly; only layerId changes ----------------------------------------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        TilesetSlicing slicing{32, 32};
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", slicing}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", slicing, tilesForSlicing(64, 64, slicing)}).ok);   // "tile-1".."tile-4"
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{"s", 1, changes}).ok);

        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);
        const SceneInstanceDef* moved = c.document().findInstanceInScene("s", 1);
        CHECK(moved->layerId == "fg");
        CHECK(moved->tilemap.has_value());
        CHECK(moved->tilemap->tilesetAssetId == "tiles-1");
        CHECK(readTilemapCell(*moved->tilemap, {0, 0})->tileId == "tile-1");
    }

    // == Scene View Selection & Tool Context =====================================
    // reconcileTilemapEditingContext() keeps EditorState::activeTool /
    // pendingStroke|Rectangle / temporaryToolOverride / selectedTileId in line
    // with whether the current selection actually supports a tilemap tool -
    // called after every Command (via reconcileWorkspace) and directly from
    // the selection/tool-changing Intents.

    // -- End-to-end: Eraser stuck on a tilemap selection no longer blocks
    // dragging a different entity selected from the Hierarchy afterwards -----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm; tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Hero", "Hero", {5.f, 5.f}, "layer-1"}).ok);

        // 1. Eraser active on the tilemap entity.
        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Eraser});
        CHECK(c.state().activeTool == EditorTool::Eraser);
        const uint64_t revBefore = c.document().revision();
        const std::size_t undoBefore = c.undoSize();

        // 2. Select Entity 2 (no tilemap) from the Hierarchy.
        CHECK(c.apply(SelectEntityIntent{2}).ok);
        // 3. Tool falls back to Select.
        CHECK(c.state().activeTool == EditorTool::Select);
        CHECK(c.effectiveTilemapTool() == EditorTool::Select);
        // Pure workspace reconciliation: no authoring mutation happened.
        CHECK(c.document().revision() == revBefore);
        CHECK(c.undoSize() == undoBefore);

        // 4-6. Pointer down/drag/up equivalent - exactly what
        // routeViewportPickDrag issues on release, now reachable because its
        // own "activeTool == Select" guard passes again.
        CHECK(c.execute(SetEntityPositionCommand{"s", 2, {50.f, 50.f}}).ok);
        CHECK(c.document().findInstanceInScene("s", 2)->transform.position.x == 50.f);

        // 7. Undo restores the position.
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene("s", 2)->transform.position.x == 5.f);
    }

    // -- Selecting the scene (Hierarchy "Scene 1" row) cancels any pending
    // gesture, falls the tool back to Select, and clears the selection -------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm; tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);

        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());

        CHECK(c.apply(SelectSceneIntent{"s"}).ok);   // re-selecting the scene itself
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.state().activeTool == EditorTool::Select);
        CHECK(!c.selection().hasEntity());
    }

    // -- Switching between two tilemaps keeps the tool, retargets, keeps a
    // still-valid tile pick but clears one that doesn't exist in the new
    // tileset (never substitutes one), and never touches the old tilemap -----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "a.png"}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-2", "b.png"}).ok);
        // tiles-1: 64x64 sliced at 32x32 -> "tile-1".."tile-4".
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles A", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        // tiles-2: 32x32 sliced at 32x32 -> "tile-1" only - deliberately does
        // not include "tile-4".
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-2", "Tiles B", "img-2", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-2", TilesetSlicing{32, 32},
                  tilesForSlicing(32, 32, TilesetSlicing{32, 32})}).ok);

        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tmA; tmA.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tmA}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Decoration", "Decoration", {}, "layer-1"}).ok);
        TilemapComponent tmB; tmB.tilesetAssetId = "tiles-2";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 2, tmB}).ok);

        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(SelectPaintTileIntent{"tile-4"});
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-4", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{"s", 1, changes}).ok);

        CHECK(c.apply(SelectEntityIntent{2}).ok);   // switch to the other tilemap
        CHECK(c.state().activeTool == EditorTool::Brush);         // tool stays
        CHECK(!c.state().tilemapEditor.selectedTileId.has_value());   // "tile-4" doesn't exist in tiles-2

        const TilemapComponent& tmAfter = *c.document().findInstanceInScene("s", 1)->tilemap;
        CHECK(readTilemapCell(tmAfter, {0, 0})->tileId == "tile-4");   // old tilemap untouched

        // Switching back to a tile that IS valid in the new target keeps it.
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(SelectEntityIntent{1}).ok);   // back to tiles-1, which also has "tile-1"
        CHECK(c.state().tilemapEditor.selectedTileId.has_value());
        CHECK(*c.state().tilemapEditor.selectedTileId == "tile-1");
    }

    // -- Locking the active layer while its tilemap is selected: selection
    // survives (Hierarchy/Inspector may still show it), but the tool falls
    // back to Select and painting is rejected with no pending residue --------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm; tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);

        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.state().activeTool == EditorTool::Brush);

        CHECK(c.execute(SetLayerLockedCommand{"s", "layer-1", true}).ok);
        CHECK(c.selection().primaryEntity == 1);           // selection itself survives
        CHECK(c.state().activeTool == EditorTool::Select);  // but the tool falls back
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());

        const auto begin = c.apply(BeginTilePaintStrokeIntent{"s", 1, EditorTool::Eraser, {0, 0}});
        CHECK(!begin.ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
    }

    // -- Pan is not a tilemap tool: a selection change never resets it --------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Hero", "Hero", {}, "layer-1"}).ok);
        c.apply(SetActiveToolIntent{EditorTool::Pan});
        CHECK(c.apply(SelectEntityIntent{1}).ok);
        CHECK(c.state().activeTool == EditorTool::Pan);
        CHECK(c.apply(SelectSceneIntent{"s"}).ok);
        CHECK(c.state().activeTool == EditorTool::Pan);
    }

    // -- A momentary Eraser override is always cleared by a selection change,
    // even when the newly selected entity also supports tilemap tools --------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {}, "layer-1"}).ok);
        TilemapComponent tm1; tm1.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm1}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Decoration", "Decoration", {}, "layer-1"}).ok);
        TilemapComponent tm2; tm2.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{"s", 2, tm2}).ok);

        c.apply(SelectEntityIntent{1});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        CHECK(c.state().tilemapEditor.temporaryToolOverride.has_value());

        CHECK(c.apply(SelectEntityIntent{2}).ok);   // entity 2 also has a tilemap
        CHECK(!c.state().tilemapEditor.temporaryToolOverride.has_value());
        CHECK(c.state().activeTool == EditorTool::Brush);   // persistent tool untouched
    }

    // -- Front-to-back picking respects real layer order: a sprite on the
    // foreground layer wins over an overlapping tilemap on the background
    // layer, and inverting which entity is on which layer flips the result --
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"s", "S"}).ok);
        c.apply(SelectSceneIntent{"s"});
        CHECK(c.execute(AddSceneLayerCommand{"s", "fg", "Foreground", 1}).ok);
        CHECK(c.execute(AddImageAssetCommand{"img-1", "some/path.png"}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-1", "Tiles", "img-1", TilesetSlicing{32, 32}}).ok);
        CHECK(c.execute(ChangeTilesetSlicingCommand{
                  "tiles-1", TilesetSlicing{32, 32},
                  tilesForSlicing(64, 64, TilesetSlicing{32, 32})}).ok);

        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 1, "obj-1", "Ground", "Ground", {16.f, 16.f}, "layer-1"}).ok);
        TilemapComponent tm; tm.tilesetAssetId = "tiles-1"; tm.cellSize = {32.f, 32.f};
        CHECK(c.execute(AddTilemapComponentCommand{"s", 1, tm}).ok);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{"s", 1, changes}).ok);

        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
                  "s", 2, "obj-2", "Hero", "Hero", {16.f, 16.f}, "fg"}).ok);
        CHECK(c.execute(AddSpriteRendererToObjectTypeCommand{"obj-2"}).ok);
        CHECK(c.execute(SetObjectTypeSpriteSourceCommand{
            "obj-2", ObjectTypeSpriteSourceKind::Image, "img-1"}).ok);

        const SceneFrameSnapshot before =
            collectSceneFrameSnapshot(c.document(), "s", INVALID_ENTITY);
        CHECK(pickEntityAt(before, Vec2{16.f, 16.f}) == 2);   // sprite: foreground layer wins

        // Invert which entity is on which layer - the pick must flip too.
        CHECK(c.execute(SetEntityLayerCommand{"s", 2, "layer-1"}).ok);
        CHECK(c.execute(SetEntityLayerCommand{"s", 1, "fg"}).ok);
        const SceneFrameSnapshot after =
            collectSceneFrameSnapshot(c.document(), "s", INVALID_ENTITY);
        CHECK(pickEntityAt(after, Vec2{16.f, 16.f}) == 1);   // tilemap: now foreground
    }

    // == Start-scene invariant: scenes exist => startSceneId is valid ==========

    // -- (1)(2)(3) First scene becomes the start scene; workspace untouched ----
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.document().startSceneId().empty());
        const SceneId activeBefore = c.state().activeSceneId;
        const uint64_t revisionBefore = c.document().revision();
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);
        CHECK(c.document().startSceneId() == "scene-1");   // (1) invariant maintained
        CHECK(c.state().activeSceneId == activeBefore);    // (2) no auto-select
        CHECK(!c.state().selection.hasEntity());           // (3) selection untouched
    }

    // -- (4) Undo of the first scene restores an empty start scene -------------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        c.undo();
        CHECK(c.document().data().scenes.empty());
        CHECK(c.document().startSceneId().empty());
    }

    // -- (5) Creating a second scene does not change the start scene -----------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        CHECK(c.execute(CreateSceneCommand{"scene-2", "Scene 2"}).ok);
        CHECK(c.document().startSceneId() == "scene-1");   // unchanged
    }

    // -- (6)(7)(8)(9) After the first scene: valid, saveable, playable ---------
    {
        EditorCoordinator c{ProjectDoc{}};
        CHECK(c.execute(CreateSceneCommand{"scene-1", "Scene 1"}).ok);
        // (6) the document now satisfies the validator (start scene is valid).
        CHECK(ProjectValidator::validate(ProjectDocument{c.document().data()}).ok);
        // (7) and saves successfully.
        const std::filesystem::path dir = testTempDir();
        const auto saved = saveProjectToFile(c, dir / "first.artcade-project");
        CHECK(saved.ok);
        // (8) Play Project is available, (9) Play Current Scene is not (no active scene).
        CHECK(c.canPlayProject());
        CHECK(!c.canPlayCurrentScene());
    }

    // -- (10) SetStartScene (valid) changes only the document ------------------
    {
        EditorCoordinator c{makeDoc()};                    // start scene = kSceneA
        const SceneId activeBefore = c.state().activeSceneId;
        c.apply(SelectEntityIntent{kHero});
        const auto r = c.execute(SetStartSceneCommand{kSceneB});
        CHECK(r.ok);
        CHECK(c.document().startSceneId() == kSceneB);
        CHECK(c.state().activeSceneId == activeBefore);    // workspace unchanged
        CHECK(c.state().selection.primaryEntity == kHero); // selection unchanged
    }

    // -- (11) SetStartScene to the current start is a no-op (no undo, no rev) --
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revBefore = c.document().revision();
        c.consumeInvalidations();
        const auto r = c.execute(SetStartSceneCommand{kSceneA}); // already the start
        CHECK(r.ok);
        CHECK(c.document().revision() == revBefore);       // no mutation
        CHECK(!c.canUndo());                               // not recorded
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
    }

    // -- (12) SetStartScene to a missing scene fails without structural change -
    {
        EditorCoordinator c{makeDoc()};
        const uint64_t revBefore = c.document().revision();
        c.consumeInvalidations();
        const auto r = c.execute(SetStartSceneCommand{"missing"});
        CHECK(!r.ok);
        CHECK(c.document().startSceneId() == kSceneA);     // unchanged
        CHECK(c.document().revision() == revBefore);
        CHECK(!c.canUndo());
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(!has(inv, EditorInvalidation::Hierarchy));
        CHECK(!has(inv, EditorInvalidation::Toolbar));
        CHECK(!has(inv, EditorInvalidation::Project));
    }

    // -- (13)(14) Undo restores the previous start; Hierarchy+Toolbar refresh --
    {
        EditorCoordinator c{makeDoc()};
        c.consumeInvalidations();
        CHECK(c.execute(SetStartSceneCommand{kSceneB}).ok);
        const EditorInvalidation inv = c.consumeInvalidations();
        CHECK(has(inv, EditorInvalidation::Hierarchy));    // (14)
        CHECK(has(inv, EditorInvalidation::Toolbar));      // (14)
        c.undo();                                          // (13)
        CHECK(c.document().startSceneId() == kSceneA);
    }

    // -- (15) SetStartScene is a Patch, never a Replace ------------------------
    {
        EditorCoordinator c{makeDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        CHECK(c.execute(SetStartSceneCommand{kSceneB}).ok);
        CHECK(c.document().replaceCount() == replacesBefore);
    }

    // -- (16) Save/reload preserves the chosen start scene --------------------
    {
        EditorCoordinator c{makeDoc()};
        CHECK(c.execute(SetStartSceneCommand{kSceneB}).ok);
        const std::filesystem::path dir = testTempDir();
        const std::filesystem::path path = dir / "start.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);

        EditorCoordinator reloaded{makeReplacementDoc()};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.document().startSceneId() == kSceneB);
    }

    // == BoxCollider2D component: object type only ============================

    // -- (1) Add targets objectTypeId, not scene/entity -----------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        const uint32_t replacesBefore = c.document().replaceCount();
        const auto r = c.execute(AddBoxColliderCommand{"Hero"});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ComponentAdded);
        CHECK(r.change.componentKind == ComponentKind::BoxCollider2D);
        CHECK(r.change.objectTypeId == "Hero");
        CHECK(r.change.entityId == INVALID_ENTITY);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D.has_value());
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->spriteRendererOverride);
        CHECK(c.document().replaceCount() == replacesBefore);
        CHECK(c.undoSize() == 1);

        const uint64_t revBeforeDuplicate = c.document().revision();
        const std::size_t undoBeforeDuplicate = c.undoSize();
        c.consumeInvalidations();
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.document().revision() == revBeforeDuplicate);
        CHECK(c.undoSize() == undoBeforeDuplicate);
        CHECK(c.consumeInvalidations() == EditorInvalidation::None);
    }

    // -- (2) Setters validate finite/positive values and invalidate narrowly --
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        c.consumeInvalidations();

        CHECK(c.execute(SetBoxColliderOffsetCommand{"Hero", Vec2{4.f, -6.f}}).ok);
        CHECK(c.consumeInvalidations()
              == (EditorInvalidation::Inspector | EditorInvalidation::Viewport
                  | EditorInvalidation::Toolbar));
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D->offset.x == 4.f);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D->offset.y == -6.f);

        const uint64_t revBeforeBadSize = c.document().revision();
        CHECK(!c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{0.f, 10.f}}).ok);
        CHECK(c.document().revision() == revBeforeBadSize);
        CHECK(!c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{-1.f, 10.f}}).ok);
        CHECK(!c.execute(SetBoxColliderSizeCommand{
            "Hero", Vec2{std::numeric_limits<float>::quiet_NaN(), 10.f}}).ok);
        CHECK(!c.execute(SetBoxColliderSizeCommand{
            "Hero", Vec2{std::numeric_limits<float>::infinity(), 10.f}}).ok);
        CHECK(!c.execute(SetBoxColliderOffsetCommand{
            "Hero", Vec2{std::numeric_limits<float>::quiet_NaN(), 0.f}}).ok);
        CHECK(c.document().revision() == revBeforeBadSize);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{64.f, 24.f}}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D->size.x == 64.f);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D->size.y == 24.f);
    }

    // -- (3) No-op setters do not mutate or enter undo ------------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        const uint64_t rev = c.document().revision();
        const std::size_t undo = c.undoSize();
        CHECK(c.execute(SetBoxColliderEnabledCommand{"Hero", true}).ok);
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == undo);
        CHECK(c.execute(SetBoxColliderOffsetCommand{"Hero", Vec2{0.f, 0.f}}).ok);
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == undo);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{32.f, 32.f}}).ok);
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == undo);
        CHECK(c.execute(SetBoxColliderModeCommand{"Hero", BoxColliderMode::Solid}).ok);
        CHECK(c.document().revision() == rev);
        CHECK(c.undoSize() == undo);
    }

    // -- (4) Undo restores exact object-type component state ------------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{80.f, 30.f}}).ok);
        CHECK(c.execute(SetBoxColliderModeCommand{"Hero", BoxColliderMode::Trigger}).ok);
        CHECK(c.execute(RemoveBoxColliderCommand{"Hero"}).ok);
        CHECK(!c.document().data().objectTypes.at("Hero").boxCollider2D.has_value());
        c.undo();
        const BoxCollider2DComponent& collider =
            *c.document().data().objectTypes.at("Hero").boxCollider2D;
        CHECK(collider.size.x == 80.f);
        CHECK(collider.size.y == 30.f);
        CHECK(collider.mode == BoxColliderMode::Trigger);
    }

    // -- (5) Inspector action resolves selection -> objectTypeId --------------
    {
        EditorCoordinator c{makeInheritedDoc()};
        c.apply(SelectEntityIntent{kHero});
        const std::size_t undoBefore = c.undoSize();
        CHECK(addBoxCollider(c).ok);
        CHECK(c.undoSize() == undoBefore + 1);
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D.has_value());

        EditorCoordinator empty{makeInheritedDoc()};
        // A rejected inspector_actions helper is not silent either: every one
        // of these select-target-then-execute wrappers logs through the same
        // fail() helper before returning, whether or not the caller (a UI
        // action handler) happens to check the result.
        const std::size_t logBefore = empty.consoleLog().size();
        CHECK(!addBoxCollider(empty).ok);
        CHECK(empty.undoSize() == 0);
        CHECK(empty.consoleLog().size() == logBefore + 1);
        CHECK(empty.consoleLog().back().level == ConsoleMessage::Level::Error);
    }

    // -- (6) Bounds projection updates every instance of the same object type -
    {
        ProjectDoc doc = makeInheritedDoc();
        SceneInstanceDef second;
        second.id = 314;
        second.objectTypeId = "Hero";
        second.instanceName = "Hero 2";
        second.transform.position = {100.f, 200.f};
        doc.scenes.at(kSceneA).instances.push_back(second);

        EditorCoordinator c{doc};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.execute(SetBoxColliderOffsetCommand{"Hero", Vec2{2.f, 3.f}}).ok);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{10.f, 20.f}}).ok);

        c.execute(CreateEntityCommand{kSceneA, 777, "Enemy", "Enemy", {300.f, 400.f}});

        const std::vector<SceneFrameCollider> bounds =
            collectBoxColliderBounds(c.document(), kSceneA, kHero);
        CHECK(bounds.size() == 2);
        CHECK(bounds[0].worldBounds.width == 10.f);
        CHECK(bounds[0].worldBounds.height == 20.f);
        CHECK(bounds[0].worldBounds.x == 7.f);
        CHECK(bounds[0].worldBounds.y == 13.f);
        CHECK(bounds[0].selected);
        CHECK(bounds[0].enabled);
        CHECK(bounds[1].worldBounds.x == 97.f);
        CHECK(bounds[1].worldBounds.y == 193.f);
        CHECK(!bounds[1].selected);

        CHECK(c.execute(SetBoxColliderModeCommand{"Hero", BoxColliderMode::Trigger}).ok);
        const std::vector<SceneFrameCollider> triggerBounds =
            collectBoxColliderBounds(c.document(), kSceneA, INVALID_ENTITY);
        CHECK(triggerBounds.size() == 2);
        CHECK(triggerBounds[0].mode == BoxColliderMode::Trigger);

        CHECK(c.execute(SetBoxColliderEnabledCommand{"Hero", false}).ok);
        CHECK(collectBoxColliderBounds(c.document(), kSceneA, INVALID_ENTITY).empty());
        CHECK(c.document().data().objectTypes.at("Hero").boxCollider2D.has_value());
        c.undo();
        CHECK(collectBoxColliderBounds(c.document(), kSceneA, INVALID_ENTITY).size() == 2);
    }

    // -- (6b) Editor overlay and runtime share the same world-bounds formula --
    {
        Transform transform;
        transform.position = {25.f, 40.f};
        const Vec2 offset{5.f, -10.f};
        const Vec2 size{20.f, 12.f};
        const WorldRect editorBounds = boxColliderWorldBounds(transform, offset, size);

        RuntimeEntity entity;
        entity.transform = transform;
        entity.collider = RuntimeBoxCollider{offset, size, true, BoxColliderMode::Solid};
        const Aabb runtimeBounds = runtimeColliderBounds(entity);

        CHECK(runtimeBounds.minX == editorBounds.x);
        CHECK(runtimeBounds.minY == editorBounds.y);
        CHECK(runtimeBounds.maxX == editorBounds.x + editorBounds.width);
        CHECK(runtimeBounds.maxY == editorBounds.y + editorBounds.height);
    }

    // -- (7) Save/reload persists object-type collider, never per instance ----
    {
        EditorCoordinator c{makeInheritedDoc()};
        CHECK(c.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(c.execute(SetBoxColliderOffsetCommand{"Hero", Vec2{5.f, 6.f}}).ok);
        CHECK(c.execute(SetBoxColliderSizeCommand{"Hero", Vec2{70.f, 32.f}}).ok);
        CHECK(c.execute(SetBoxColliderEnabledCommand{"Hero", false}).ok);
        CHECK(c.execute(SetBoxColliderModeCommand{"Hero", BoxColliderMode::OneWayPlatform}).ok);

        const auto ser = ProjectSerializer::serialize(c.document());
        CHECK(ser.ok);
        CHECK(ser.value.find("boxCollider2D") != std::string::npos);
        CHECK(ser.value.find("\"mode\"") != std::string::npos);
        CHECK(ser.value.find("oneWayPlatform") != std::string::npos);
        CHECK(ser.value.find("isTrigger") == std::string::npos);
        CHECK(ser.value.find("spriteRendererOverride") == std::string::npos);
        const std::size_t instancePos = ser.value.find("\"instances\"");
        const std::size_t colliderPos = ser.value.find("boxCollider2D");
        CHECK(colliderPos < instancePos);

        const std::filesystem::path dir = testTempDir();
        const std::filesystem::path path = dir / "box-collider.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);

        EditorCoordinator reloaded{makeReplacementDoc()};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const auto& type = reloaded.document().data().objectTypes.at("Hero");
        CHECK(type.boxCollider2D.has_value());
        CHECK(type.boxCollider2D->offset.x == 5.f);
        CHECK(type.boxCollider2D->offset.y == 6.f);
        CHECK(type.boxCollider2D->size.x == 70.f);
        CHECK(type.boxCollider2D->size.y == 32.f);
        CHECK(!type.boxCollider2D->enabled);
        CHECK(type.boxCollider2D->mode == BoxColliderMode::OneWayPlatform);
    }

    // -- (8) Legacy isTrigger migrates to mode; unknown mode is rejected ------
    {
        const std::string legacySolid =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"enabled":true,"isTrigger":false}}]})";
        const auto loadedSolid = ProjectSerializer::deserialize(legacySolid);
        CHECK(loadedSolid.ok);
        CHECK(loadedSolid.value.data().objectTypes.at("Hero").boxCollider2D->mode
              == BoxColliderMode::Solid);

        const std::string legacyTrigger =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"enabled":true,"isTrigger":true}}]})";
        const auto loaded = ProjectSerializer::deserialize(legacyTrigger);
        CHECK(loaded.ok);
        const auto& type = loaded.value.data().objectTypes.at("Hero");
        CHECK(type.boxCollider2D.has_value());
        CHECK(type.boxCollider2D->mode == BoxColliderMode::Trigger);

        const std::string badMode =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"mode":"ghost"}}]})";
        CHECK(!ProjectSerializer::deserialize(badMode).ok);

        const std::string conflictingLegacy =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"mode":"oneWayPlatform","isTrigger":true}}]})";
        CHECK(!ProjectSerializer::deserialize(conflictingLegacy).ok);
    }

    // -- (9) Invalid persisted collider is rejected during validation ---------
    {
        const std::string bad =
            R"({"activeSceneId":"s","scenes":[{"id":"s","instances":[{"id":1,"objectTypeId":"Hero","instanceName":"Hero"}]}],"objectTypes":[{"id":"Hero","boxCollider2D":{"size":{"x":-1,"y":10}}}]})";
        const auto loaded = ProjectSerializer::deserialize(bad);
        CHECK(loaded.ok);
        CHECK(!ProjectValidator::validate(std::move(loaded.value)).ok);
    }

    // -- (10) OneWayPlatform + movement driver is explicitly unsupported ------
    {
        ProjectDoc doc = makeInheritedDoc();
        EntityDef& hero = doc.objectTypes.at("Hero");
        hero.boxCollider2D = BoxCollider2DComponent{
            {0.f, 0.f}, {32.f, 32.f}, true, BoxColliderMode::OneWayPlatform};
        LinearMoverComponent mover;
        mover.speed = 10.f;
        hero.linearMover = mover;
        CHECK(!ProjectValidator::validate(ProjectDocument{std::move(doc)}).ok);
    }

    // -- (11) Authoring commands prevent OneWayPlatform + movement driver -----
    {
        EditorCoordinator withMover{makeInheritedDoc()};
        CHECK(withMover.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(withMover.execute(AddLinearMoverCommand{"Hero"}).ok);
        CHECK(!withMover.execute(
            SetBoxColliderModeCommand{"Hero", BoxColliderMode::OneWayPlatform}).ok);

        EditorCoordinator oneWay{makeInheritedDoc()};
        CHECK(oneWay.execute(AddBoxColliderCommand{"Hero"}).ok);
        CHECK(oneWay.execute(
            SetBoxColliderModeCommand{"Hero", BoxColliderMode::OneWayPlatform}).ok);
        CHECK(!oneWay.execute(AddLinearMoverCommand{"Hero"}).ok);
        CHECK(!oneWay.execute(AddTopDownControllerCommand{"Hero"}).ok);
        CHECK(!oneWay.execute(AddPlatformerControllerCommand{"Hero"}).ok);
    }

    runSpriteAnimationTests();
    runTilesetTests();
    runTilemapComponentTests();
    runTilemapPaintingTests();
    runTilesetResliceCascadeTests();
    runTilemapRegionTests();
    runTilemapPlaySessionTests();
    runGeneratedSfxTests();
    runGeneratedSfxMacrosTests();
    runScriptAssetAndFileServiceTests();
    runManualScriptRuntimeTests();

    std::cout << "editor-core-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
