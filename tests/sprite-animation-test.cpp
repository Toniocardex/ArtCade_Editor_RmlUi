// sprite-animation-test.cpp — Sprite Animation Editor model / commands.

#include "editor_core_test_harness.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/generated_sfx_generation_preflight.h"
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
#include "artcade/sfx/recipe_json.hpp"
#include "editor-native/commands/generated_sfx_macros.h"
#include "editor-native/commands/font_asset_commands.h"
#include "editor-native/commands/script_asset_commands.h"
#include "editor-native/commands/script_attachment_commands.h"
#include "editor-native/commands/tileset_commands.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/commands/sprite_animation_commands.h"
#include "editor-native/commands/logic_board_commands.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/scene_commands.h"
#include "editor-native/commands/sprite_presentation_commands.h"
#include "logic-core.h"
#include "editor-native/model/project_io.h"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/script_source_stamp.h"
#include "editor-native/model/box_collider_view.h"
#include "editor-native/model/box_collider_geometry.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_animation_slicing.h"
#include "editor-native/model/sprite_animation_references.h"
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
using namespace ArtCade::EditorNative::CoreTest;

int main() {

    // -- Sprite Animation Editor model: asset owns clips and serializes -------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.execute(CreateSpriteAnimationAssetCommand{
            "hero.anim", "hero.anim", "idle", "Idle", "img-hero"}).ok);
        CHECK(c.document().hasSpriteAnimationAsset("hero.anim"));
        std::vector<SpriteFrameDef> frames{
            SpriteFrameDef{"f0", 0, 0, 32, 32},
            SpriteFrameDef{"f1", 32, 0, 32, 32},
        };
        CHECK(c.execute(ReplaceAnimationFramesCommand{"hero.anim", frames}).ok);
        CHECK(c.execute(SetAnimationClipFrameIdsCommand{
            "hero.anim", "idle", {"f0", "f1"}}).ok);
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
        CHECK(asset && asset->sourceImageAssetId == "img-hero");
        CHECK(asset && asset->frames.size() == 2);
        CHECK(asset && asset->clips[0].frameIds.size() == 2);
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
        CHECK(created && created->clips[0].id == "clip-1");
        CHECK(created && created->sourceImageAssetId == "img-hero");
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
        const std::optional<SpriteFrameDef> first =
            spriteAnimationFrameForCell(38, 53, grid, 0);
        const std::optional<SpriteFrameDef> second =
            spriteAnimationFrameForCell(38, 53, grid, 1);
        const std::optional<SpriteFrameDef> last =
            spriteAnimationFrameForCell(38, 53, grid, 3);
        CHECK(spriteAnimationSliceCellCount(38, 53, grid) == 4);
        CHECK(first.has_value());
        CHECK(first && first->x == 2 && first->y == 2
              && first->width == 16 && first->height == 24);
        CHECK(first && first->id == "frame-1");
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
        const std::vector<SpriteFrameDef> frames =
            atlas ? spriteAnimationFramesForGrid(64, 32, *atlas)
                  : std::vector<SpriteFrameDef>{};
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
        CHECK(type.spriteAnimator && type.spriteAnimator->animationAssetId == "hero.anim");
        CHECK(type.spriteAnimator && type.spriteAnimator->defaultClipId == "idle");

        const SceneFrameSnapshot frame =
            collectSceneFrameSnapshot(c.document(), kSceneA, kHero);
        CHECK(frame.sprites.size() == 1);
        CHECK(frame.sprites[0].assetId == "img-hero");
        CHECK(frame.sprites[0].hasSource);
        CHECK(frame.sprites[0].source.width == 32.f);

        CHECK(c.undo().ok);
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);

        CHECK(c.redo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->animationAssetId
              == "hero.anim");
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->defaultClipId == "idle");
    }

    // -- Animation -> image is one atomic renderer+animator state change -----
    {
        EditorCoordinator c{makeAnimationDoc()};
        const uint64_t imageRevisionBefore = c.document().revision();
        CHECK(c.execute(SetObjectTypeSpriteSourceCommand{
            "Hero", ObjectTypeSpriteSourceKind::Image, "img-hero"}).ok);
        CHECK(c.document().revision() == imageRevisionBefore + 1);
        CHECK(c.document().data().objectTypes.at("Hero").spriteRenderer->imageAssetId == "img-hero");
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);
        CHECK(c.undo().ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->animationAssetId
              == "hero.anim");
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->defaultClipId == "idle");
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
        // Inherited override: clip only — no explicit animationAssetId.
        SpriteAnimatorOverride inheritedClip;
        inheritedClip.defaultClipId = std::string{"idle"};
        doc.scenes.at(kSceneA).instances.front().spriteAnimatorOverride = inheritedClip;
        EditorCoordinator c{std::move(doc)};
        CHECK(resolveSpriteRenderer(c.document(), kSceneA, kHero).animationAssetId == "hero.anim");
        const uint64_t revisionBefore = c.document().revision();
        // Delete means delete: the asset goes AND the entity is left clean, not
        // blocked and not dangling.
        CHECK(c.execute(RemoveSpriteAnimationAssetCommand{"hero.anim"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);  // one staged commit
        CHECK(!c.document().hasSpriteAnimationAsset("hero.anim"));
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);
        CHECK(!c.document().data().scenes.at(kSceneA).instances.front()
                   .spriteAnimatorOverride);
        // Undo restores the asset, the source, and the animator (with its clip).
        CHECK(c.undo().ok);
        CHECK(c.document().hasSpriteAnimationAsset("hero.anim"));
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->animationAssetId
              == "hero.anim");
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->defaultClipId == "idle");
        CHECK(c.document().data().scenes.at(kSceneA).instances.front()
                  .spriteAnimatorOverride->defaultClipId
              && *c.document().data().scenes.at(kSceneA).instances.front()
                      .spriteAnimatorOverride->defaultClipId
                  == "idle");
        CHECK(c.redo().ok);
        CHECK(!c.document().hasSpriteAnimationAsset("hero.anim"));
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator);
        CHECK(!c.document().data().scenes.at(kSceneA).instances.front()
                   .spriteAnimatorOverride);
    }

    // -- Slicing a second animation must not overwrite the first's clip -------
    {
        EditorCoordinator c{makeSpriteDoc()};   // img-hero + img-alt
        // Animation 1 on img-hero, mirroring the UI Import Sheet -> Slice flow.
        CHECK(c.execute(CreateSpriteAnimationAssetCommand{
            "a1.anim", "a1", "clip-1", "Clip 1", "img-hero"}).ok);
        c.apply(OpenSpriteAnimationEditorIntent{"a1.anim"});
        CHECK(c.state().spriteAnimationEditor.selectedClipId.has_value());
        CHECK(c.execute(ReplaceAnimationFramesCommand{"a1.anim",
            {SpriteFrameDef{"f0", 0, 0, 16, 16}, SpriteFrameDef{"f1", 16, 0, 16, 16},
             SpriteFrameDef{"f2", 32, 0, 16, 16}}}).ok);
        CHECK(c.execute(SetAnimationClipFrameIdsCommand{
            "a1.anim", "clip-1", {"f0", "f1", "f2"}}).ok);
        CHECK(c.document().findSpriteAnimationAsset("a1.anim")->clips[0].frameIds.size() == 3);

        // Animation 2 on img-alt: its auto-clip also gets id "clip-1".
        CHECK(c.execute(CreateSpriteAnimationAssetCommand{
            "a2.anim", "a2", "clip-1", "Clip 1", "img-alt"}).ok);
        c.apply(OpenSpriteAnimationEditorIntent{"a2.anim"});
        CHECK(c.state().spriteAnimationEditor.selectedClipId.has_value());
        CHECK(c.execute(ReplaceAnimationFramesCommand{"a2.anim",
            {SpriteFrameDef{"f0", 0, 0, 16, 16}, SpriteFrameDef{"f1", 16, 0, 16, 16}}}).ok);
        CHECK(c.execute(SetAnimationClipFrameIdsCommand{
            "a2.anim", "clip-1", {"f0", "f1"}}).ok);

        // Neither asset's clip is overwritten - each keeps its own frames.
        const SpriteAnimationAssetDef* a1 = c.document().findSpriteAnimationAsset("a1.anim");
        const SpriteAnimationAssetDef* a2 = c.document().findSpriteAnimationAsset("a2.anim");
        CHECK(a1 && a1->clips.size() == 1 && a1->clips[0].frameIds.size() == 3);
        CHECK(a2 && a2->clips.size() == 1 && a2->clips[0].frameIds.size() == 2);
    }

    // -- Replace source image clears frames/sequences atomically -------------
    {
        EditorCoordinator c{makeAnimationDoc()};
        const uint64_t revisionBefore = c.document().revision();
        CHECK(c.execute(ReplaceAnimationSourceImageCommand{"hero.anim", "img-alt"}).ok);
        CHECK(c.document().revision() == revisionBefore + 1);
        const SpriteAnimationAssetDef* asset =
            c.document().findSpriteAnimationAsset("hero.anim");
        CHECK(asset && asset->sourceImageAssetId == "img-alt");
        CHECK(asset && asset->frames.empty());
        CHECK(asset && asset->clips.size() == 1 && asset->clips[0].frameIds.empty());
        CHECK(c.undo().ok);
        asset = c.document().findSpriteAnimationAsset("hero.anim");
        CHECK(asset && asset->sourceImageAssetId == "img-hero");
        CHECK(asset && asset->frames.size() == 2);
        CHECK(asset && asset->clips[0].frameIds.size() == 2);
    }

    // -- Duplicate asset remaps frame/clip ids and keeps source sheet --------
    {
        EditorCoordinator c{makeAnimationDoc()};
        CHECK(c.execute(DuplicateSpriteAnimationAssetCommand{
            "hero.anim", "hero-copy.anim", "Hero Copy"}).ok);
        const SpriteAnimationAssetDef* copy =
            c.document().findSpriteAnimationAsset("hero-copy.anim");
        CHECK(copy && copy->sourceImageAssetId == "img-hero");
        CHECK(copy && copy->frames.size() == 2);
        CHECK(copy && copy->frames[0].id == "hero-copy.anim:f0");
        CHECK(copy && copy->clips.size() == 1);
        CHECK(copy && copy->clips[0].id == "hero-copy.anim:idle");
        CHECK(copy && copy->clips[0].frameIds.size() == 2);
        CHECK(copy && copy->clips[0].frameIds[0] == "hero-copy.anim:f0");
        CHECK(c.undo().ok);
        CHECK(!c.document().hasSpriteAnimationAsset("hero-copy.anim"));
    }

    // -- Canonical clip references block delete ------------------------------
    {
        EditorCoordinator c{makeAnimationDoc()};
        CHECK(!collectAnimationClipReferences(c.document(), "hero.anim", "idle").empty());
        CHECK(!c.execute(RemoveAnimationClipCommand{"hero.anim", "idle"}).ok);
        CHECK(c.document().findSpriteAnimationAsset("hero.anim")->clips.size() == 1);
        CHECK(!collectAnimationSourceReferences(c.document(), "img-hero").empty());
        CHECK(!c.execute(RemoveImageAssetCommand{"img-hero"}).ok);
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

    // -- Edit falls back to static image when animator asset is missing --------
    {
        ProjectDoc doc = makeAnimationDoc();
        doc.objectTypes.at("Hero").spriteRenderer->imageAssetId = "img-alt";
        doc.objectTypes.at("Hero").spriteAnimator->animationAssetId = "missing.anim";
        EditorCoordinator c{std::move(doc)};
        const SpriteRenderView view = resolveSpriteRenderer(c.document(), kSceneA, kHero);
        CHECK(view.present);
        CHECK(view.animatorInvalid);
        CHECK(view.assetId == "img-alt");
        CHECK(view.diagnosticCode == "ANIMATION_MISSING_ASSET");
        std::string playError;
        CHECK(!PlaySession::startActiveScene(c.document(), kSceneA, &playError).has_value());
        CHECK(!playError.empty());
    }

    // -- Play Started / Finished pulse; hold must not re-Finished --------------
    {
        ProjectDoc doc = makeAnimationDoc();
        doc.objectTypes.at("Hero").spriteAnimator->autoPlay = true;
        doc.objectTypes.at("Hero").spriteAnimator->defaultClipId = "idle";
        doc.spriteAnimationAssets.front().clips.front().playbackMode =
            AnimationPlaybackMode::Once;
        doc.spriteAnimationAssets.front().clips.front().framesPerSecond = 10.f;
        ProjectDocument document{std::move(doc)};
        std::string playError;
        auto session = PlaySession::startActiveScene(document, kSceneA, &playError);
        CHECK(session.has_value());
        auto started = session->drainAnimationEvents();
        CHECK(!started.empty());
        CHECK(started.front().kind == AnimationRuntimeEventKind::Started);
        for (int i = 0; i < 30; ++i) session->advance(0.1f);
        auto finished = session->drainAnimationEvents();
        bool sawFinished = false;
        for (const AnimationRuntimeEvent& event : finished) {
            if (event.kind == AnimationRuntimeEventKind::Finished) sawFinished = true;
        }
        CHECK(sawFinished);
        session->advance(0.5f);
        auto held = session->drainAnimationEvents();
        for (const AnimationRuntimeEvent& event : held) {
            CHECK(event.kind != AnimationRuntimeEventKind::Finished);
        }
    }

    // -- OT Inspector path mutates Object Type, not instance override -----------
    {
        EditorCoordinator c{makeAnimationDoc()};
        CHECK(c.execute(SetObjectTypePlaybackSpeedCommand{"Hero", 2.f}).ok);
        CHECK(c.document().data().objectTypes.at("Hero").spriteAnimator->playbackSpeed == 2.f);
        CHECK(!c.document().data().scenes.at(kSceneA).instances.front().spriteAnimatorOverride);
        CHECK(c.execute(SetObjectTypeAutoPlayCommand{"Hero", false}).ok);
        CHECK(!c.document().data().objectTypes.at("Hero").spriteAnimator->autoPlay);
    }

    // -- Remove animation clears Logic playClip refs; undo restores -----------
    {
        EditorCoordinator c{makeAnimationDoc()};
        CHECK(c.execute(CreateLogicBoardCommand{"Hero"}).ok);
        LogicRuleDef rule = Logic::makeDefaultRule("rule-play");
        rule.actions[0] = Logic::makeDefaultBlock(
            Logic::kAnimationPlayClip, Logic::BlockKind::Action);
        rule.actions[0].properties[0].value = LogicAssetReference{"hero.anim"};
        rule.actions[0].properties[1].value = LogicStringValue{"idle"};
        CHECK(c.execute(AddLogicRuleCommand{"Hero", rule, 0}).ok);
        CHECK(c.execute(RemoveSpriteAnimationAssetCommand{"hero.anim"}).ok);
        const LogicBlockDef& cleared = c.document().data().objectTypes.at("Hero")
            .logicBoard->rules[0].actions[0];
        CHECK(std::get<LogicAssetReference>(cleared.properties[0].value).id.empty());
        CHECK(std::get<LogicStringValue>(cleared.properties[1].value).value.empty());
        CHECK(c.undo().ok);
        const LogicBlockDef& restored = c.document().data().objectTypes.at("Hero")
            .logicBoard->rules[0].actions[0];
        CHECK(std::get<LogicAssetReference>(restored.properties[0].value).id == "hero.anim");
        CHECK(std::get<LogicStringValue>(restored.properties[1].value).value == "idle");
    }

    // -- Invalid defaultClipId: Edit diagnostic + Play atomic fail ------------
    {
        ProjectDoc doc = makeAnimationDoc();
        doc.objectTypes.at("Hero").spriteAnimator->defaultClipId = "missing-clip";
        EditorCoordinator c{std::move(doc)};
        const SpriteRenderView view = resolveSpriteRenderer(c.document(), kSceneA, kHero);
        CHECK(view.animatorInvalid);
        CHECK(view.diagnosticCode == "ANIMATION_MISSING_CLIP");
        std::string playError;
        CHECK(!PlaySession::startActiveScene(c.document(), kSceneA, &playError).has_value());
        CHECK(playError.find("defaultClipId") != std::string::npos);
    }

    // -- Logic play_clip during update drains Started same frame --------------
    {
        ProjectDoc doc = makeAnimationDoc();
        doc.objectTypes.at("Hero").spriteAnimator->autoPlay = false;
        LogicBoardDef board;
        board.id = "logic:Hero";
        LogicRuleDef rule = Logic::makeDefaultRule("rule-key-play");
        rule.trigger = {Logic::kKeyPressed, {{"key", LogicKey::Space}}};
        rule.actions[0] = Logic::makeDefaultBlock(
            Logic::kAnimationPlayClip, Logic::BlockKind::Action);
        rule.actions[0].properties[0].value = LogicAssetReference{"hero.anim"};
        rule.actions[0].properties[1].value = LogicStringValue{"idle"};
        board.rules.push_back(rule);
        doc.objectTypes.at("Hero").logicBoard = board;
        ProjectDocument document{std::move(doc)};
        std::string playError;
        auto session = PlaySession::startActiveScene(document, kSceneA, &playError);
        CHECK(session.has_value());
        (void)session->drainAnimationEvents();
        RuntimeInputSnapshot input;
        input.pressedLogicKeys.push_back(LogicKey::Space);
        session->update(input, 1.f / 60.f);
        const auto events = session->drainAnimationEvents();
        bool sawStarted = false;
        for (const AnimationRuntimeEvent& event : events) {
            if (event.kind == AnimationRuntimeEventKind::Started
                && event.clipId == "idle") {
                sawStarted = true;
            }
        }
        CHECK(sawStarted);
    }

    // -- v9 ownership migrate: no global first-wins on unrelated assets -------
    {
        ProjectDoc doc = makeAnimationDoc();
        doc.formatVersion = 8;
        SpriteAnimationAssetDef other;
        other.id = "other.anim";
        other.name = "Other";
        other.sourceImageAssetId = "img-alt";
        other.frames = {SpriteFrameDef{"f0", 0, 0, 8, 8}};
        SpriteAnimationClipDef stolen;
        stolen.id = "stolen";
        stolen.name = "Stolen";
        stolen.frameIds = {"f0"};
        other.clips.push_back(stolen);
        doc.spriteAnimationAssets.push_back(other);
        doc.objectTypes.at("Hero").spriteAnimator->animationAssetId = "hero.anim";
        doc.objectTypes.at("Hero").spriteAnimator->defaultClipId = "stolen";
        auto migrated = ProjectMigration::migrate(ProjectDocument{std::move(doc)});
        CHECK(migrated.ok);
        const auto& animator =
            *migrated.value.data().objectTypes.at("Hero").spriteAnimator;
        // Must NOT remap to other.anim just because it owns clip "stolen".
        CHECK(animator.animationAssetId == "hero.anim");
        CHECK(animator.defaultClipId == "stolen");
    }

    // -- Confirm source-image Intent is workspace-only (no document mutate) ---
    {
        EditorCoordinator c{makeAnimationDoc()};
        c.apply(OpenSpriteAnimationEditorIntent{"hero.anim"});
        CHECK(c.apply(RequestAnimationSourceImageIntent{"img-alt"}).ok);
        const uint64_t revisionBefore = c.document().revision();
        CHECK(c.apply(ConfirmAnimationSourceImageIntent{}).ok);
        CHECK(c.document().revision() == revisionBefore);
        CHECK(!c.state().spriteAnimationEditor.pendingSourceImageId);
        CHECK(c.document().findSpriteAnimationAsset("hero.anim")->sourceImageAssetId
              == "img-hero");
    }

    return reportAndExit("sprite-animation-test");
}
