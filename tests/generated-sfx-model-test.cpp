// generated-sfx-model-test.cpp — Generated SFX document/commands/macros.

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
using namespace ArtCade::EditorNative::CoreTest;

namespace {

class PreflightFilesystemProbe final : public GeneratedSfxOutputRepository {
public:
    GeneratedSfxFileInspection inspect(
        const std::filesystem::path& path) const override {
        std::error_code error;
        const bool exists = std::filesystem::exists(path, error);
        return error
            ? GeneratedSfxFileInspection{false, false, false, error.message()}
            : GeneratedSfxFileInspection{true, exists, exists, {}};
    }

    GeneratedSfxFileOperationResult moveNoReplace(
        const std::filesystem::path&,
        const std::filesystem::path&) override {
        return GeneratedSfxFileOperationResult::failure(
            "Preflight probe does not move files");
    }

    GeneratedSfxFileOperationResult removeIfExists(
        const std::filesystem::path&) override {
        return GeneratedSfxFileOperationResult::failure(
            "Preflight probe does not remove files");
    }
};

} // namespace

int main() {

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
        CHECK(decoded.value.data().formatVersion == 10);
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
    output.name = "ShouldBeReplacedByDefault";
    output.sourcePath = "assets/audio/generated/generated-audio-sfx-jump.wav";
    output.loadMode = AudioLoadMode::StaticSound;
    CHECK(coordinator.execute(RegisterGeneratedSfxOutputCommand{
        "sfx-jump", recipe, output}).ok);
    CHECK(coordinator.document().hasAudioAsset(output.assetId));
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);
    CHECK(coordinator.document().findAudioAsset(output.assetId)->name.empty());
    CHECK(resolveAudioAssetDisplayName(
            coordinator.document(),
            *coordinator.document().findAudioAsset(output.assetId)) == "Jump");
    CHECK(ProjectValidator::validate(coordinator.document()).ok);

    CHECK(coordinator.execute(RenameGeneratedSfxCommand{"sfx-jump", "Hero Jump"}).ok);
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->name == "Hero Jump");
    CHECK(coordinator.document().findAudioAsset(output.assetId)->name.empty());
    CHECK(resolveAudioAssetDisplayName(
            coordinator.document(),
            *coordinator.document().findAudioAsset(output.assetId)) == "Hero Jump");

    SfxRecipe changed = recipe;
    changed.randomSeed += 1u;
    CHECK(coordinator.execute(
        UpdateGeneratedSfxRecipeCommand{"sfx-jump", changed}).ok);
    // Recipe edit keeps the stable output link; status becomes Stale via fingerprint.
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);
    CHECK(coordinator.document().findAudioAsset(output.assetId)->name.empty());
    CHECK(generatedSfxOutputStatus(coordinator.document(),
            *coordinator.document().findGeneratedSfx("sfx-jump"))
          == GeneratedSfxOutputStatus::Stale);
    CHECK(resolveAudioAssetDisplayName(
            coordinator.document(),
            *coordinator.document().findAudioAsset(output.assetId)) == "Hero Jump");
    const std::uint64_t staleRevision = coordinator.document().revision();
    CHECK(!coordinator.execute(RegisterGeneratedSfxOutputCommand{
        "sfx-jump", recipe, output}).ok);
    CHECK(coordinator.document().revision() == staleRevision);

    CHECK(coordinator.undo().ok); // recipe update
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);
    CHECK(generatedSfxOutputStatus(coordinator.document(),
            *coordinator.document().findGeneratedSfx("sfx-jump"))
          == GeneratedSfxOutputStatus::Ready);
    CHECK(coordinator.document().findAudioAsset(output.assetId)->name.empty());
    CHECK(coordinator.redo().ok);
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);
    CHECK(generatedSfxOutputStatus(coordinator.document(),
            *coordinator.document().findGeneratedSfx("sfx-jump"))
          == GeneratedSfxOutputStatus::Stale);

    CHECK(coordinator.execute(RegisterGeneratedSfxOutputCommand{
        "sfx-jump", changed, output}).ok);
    CHECK(generatedSfxOutputStatus(coordinator.document(),
            *coordinator.document().findGeneratedSfx("sfx-jump"))
          == GeneratedSfxOutputStatus::Ready);
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->generatedRecipeFingerprint
          == artcade::sfx::recipeFingerprint(changed));
    CHECK(coordinator.document().findAudioAsset(output.assetId)->name.empty());
    CHECK(resolveAudioAssetDisplayName(
            coordinator.document(),
            *coordinator.document().findAudioAsset(output.assetId))
          == coordinator.document().findGeneratedSfx("sfx-jump")->name);

    // 1:1 regenerate: same AudioAssetDef + path; fingerprint updates; no second WAV.
    SfxRecipe changedAgain = changed;
    changedAgain.randomSeed += 1u;
    CHECK(coordinator.execute(
        UpdateGeneratedSfxRecipeCommand{"sfx-jump", changedAgain}).ok);
    CHECK(generatedSfxOutputStatus(coordinator.document(),
            *coordinator.document().findGeneratedSfx("sfx-jump"))
          == GeneratedSfxOutputStatus::Stale);
    AudioAssetDef retarget;
    retarget.assetId = "generated-audio-sfx-jump-other";
    retarget.sourcePath = "assets/audio/generated/generated-audio-sfx-jump-other.wav";
    retarget.loadMode = AudioLoadMode::StaticSound;
    CHECK(!coordinator.execute(RegisterGeneratedSfxOutputCommand{
        "sfx-jump", changedAgain, retarget}).ok); // must not change outputAssetId
    CHECK(coordinator.execute(RegisterGeneratedSfxOutputCommand{
        "sfx-jump", changedAgain, output}).ok);
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);
    CHECK(coordinator.document().data().audioAssets.size() == 1);
    CHECK(generatedSfxOutputStatus(coordinator.document(),
            *coordinator.document().findGeneratedSfx("sfx-jump"))
          == GeneratedSfxOutputStatus::Ready);
    CHECK(coordinator.document().findAudioAsset(output.assetId)->generatedFromSfxId
          == std::optional<std::string>{"sfx-jump"});

    const auto identity = stableGeneratedSfxOutputIdentity(
        coordinator.document(),
        *coordinator.document().findGeneratedSfx("sfx-jump"),
        {});
    CHECK(identity.has_value());
    CHECK(identity->assetId == output.assetId);
    CHECK(identity->relativePath == output.sourcePath);
    CHECK(generatedAudioAssetId("sfx-jump") == "generated-audio-sfx-jump");
    CHECK(generatedAudioRelativePath("sfx-jump")
          == "assets/audio/generated/generated-audio-sfx-jump.wav");

    // Filesystem preflight: a canonical WAV not represented by the document is
    // an external collision and blocks before render or Duplicate. A linked
    // output is Regenerate and may replace its own existing destination.
    {
        const std::filesystem::path root = testTempDir() / "sfx-generation-preflight";
        std::error_code ec;
        std::filesystem::create_directories(root / "assets" / "audio" / "generated", ec);
        CHECK(!ec);

        artcade::sfx::GeneratedSfxDef prospective;
        prospective.id = "prospective";
        PreflightFilesystemProbe repository;
        const auto available = preflightGeneratedSfxGeneration(
            coordinator.document(), prospective, root, repository);
        CHECK(available.allowed());
        CHECK(!available.regenerating());
        CHECK(!available.identity.finalPath.empty());
        {
            std::ofstream existing{available.identity.finalPath, std::ios::binary};
            existing << "existing";
        }
        const std::uint64_t revisionBeforeBlockedPreflight =
            coordinator.document().revision();
        const auto blocked = preflightGeneratedSfxGeneration(
            coordinator.document(), prospective, root, repository);
        CHECK(!blocked.allowed());
        CHECK(blocked.error.find("already exists") != std::string::npos);
        CHECK(coordinator.document().revision() == revisionBeforeBlockedPreflight);

        const auto linked = preflightGeneratedSfxGeneration(
            coordinator.document(),
            *coordinator.document().findGeneratedSfx("sfx-jump"), root,
            repository);
        CHECK(linked.allowed());
        CHECK(linked.regenerating());
        {
            std::ofstream existingLinked{linked.identity.finalPath, std::ios::binary};
            existingLinked << "owned";
        }
        const auto linkedExisting = preflightGeneratedSfxGeneration(
            coordinator.document(),
            *coordinator.document().findGeneratedSfx("sfx-jump"), root,
            repository);
        CHECK(linkedExisting.allowed());
        CHECK(linkedExisting.regenerating());
    }

    // Cross-catalog case-insensitive name uniqueness.
    CHECK(audioDisplayNameExists(coordinator.document().data(), "HERO JUMP"));
    CHECK(uniqueGeneratedSfxName(coordinator.document(), "Hero Jump") == "Hero Jump 02");
    CHECK(!coordinator.execute(
        CreateGeneratedSfxCommand{"sfx-other", "hero jump", changedAgain}).ok);
    CHECK(coordinator.execute(AddAudioAssetCommand{
        "imported-coin", "assets/audio/coin.wav", AudioLoadMode::StaticSound}).ok);
    CHECK(audioDisplayNameExists(coordinator.document().data(), "imported-coin"));
    CHECK(audioDisplayNameExists(coordinator.document().data(), " IMPORTED-COIN "));
    CHECK(!coordinator.execute(
        CreateGeneratedSfxCommand{"sfx-coin", "IMPORTED-COIN", changedAgain}).ok);
    CHECK(uniqueGeneratedSfxName(coordinator.document(), "imported-coin")
          == "imported-coin 02");
    // Numbered source uses the stem: Coin 02 ÔåÆ next free after Coin / Coin 02ÔÇª
    CHECK(uniqueGeneratedSfxName(coordinator.document(), "Hero Jump 02")
          == "Hero Jump 02");

    // A linked output cannot be removed through the generic Audio writer.
    // Hard delete remains owned by RemoveGeneratedSfxCommand below.
    CHECK(!coordinator.execute(RemoveAudioAssetCommand{output.assetId}).ok);
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);
    CHECK(generatedSfxOutputStatus(coordinator.document(),
            *coordinator.document().findGeneratedSfx("sfx-jump"))
          == GeneratedSfxOutputStatus::Ready);

    CHECK(coordinator.execute(RemoveGeneratedSfxCommand{"sfx-jump"}).ok);
    CHECK(!coordinator.document().hasGeneratedSfx("sfx-jump"));
    CHECK(!coordinator.document().hasAudioAsset(output.assetId));
    CHECK(coordinator.undo().ok);
    CHECK(coordinator.document().hasGeneratedSfx("sfx-jump"));
    CHECK(coordinator.document().findAudioAsset(output.assetId)->name.empty());
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);

    // Duplicate copies recipe only ÔÇö never the linked WAV identity.
    const std::string dupId = nextGeneratedSfxId(coordinator.document());
    const std::string dupName = uniqueGeneratedSfxName(coordinator.document(), "Hero Jump");
    CHECK(dupName == "Hero Jump 02");
    CHECK(coordinator.execute(
        DuplicateGeneratedSfxCommand{"sfx-jump", dupId, dupName}).ok);
    const auto* duplicate = coordinator.document().findGeneratedSfx(dupId);
    CHECK(duplicate != nullptr);
    CHECK(duplicate->name == "Hero Jump 02");
    CHECK(duplicate->outputAssetId.empty());
    CHECK(duplicate->outputPath.empty());
    CHECK(duplicate->generatedRecipeFingerprint.empty());
    CHECK(generatedSfxOutputStatus(coordinator.document(), *duplicate)
          == GeneratedSfxOutputStatus::NeedsGeneration);
    CHECK(generatedSfxRecipesEqual(duplicate->recipe,
        coordinator.document().findGeneratedSfx("sfx-jump")->recipe));
    CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
          == output.assetId);
    const auto firstIdentity = stableGeneratedSfxOutputIdentity(
        coordinator.document(), *duplicate, {});
    CHECK(firstIdentity.has_value());
    CHECK(firstIdentity->assetId == generatedAudioAssetId(dupId));
    CHECK(firstIdentity->relativePath == generatedAudioRelativePath(dupId));
    // Create from Current domain path: Duplicate + Register on the copy leaves
    // the source link/fingerprint untouched.
    {
        const std::size_t audioBefore = coordinator.document().data().audioAssets.size();
        AudioAssetDef copyOut;
        copyOut.assetId = generatedAudioAssetId(dupId);
        copyOut.sourcePath = generatedAudioRelativePath(dupId);
        copyOut.loadMode = AudioLoadMode::StaticSound;
        CHECK(coordinator.execute(RegisterGeneratedSfxOutputCommand{
            dupId, changedAgain, copyOut}).ok);
        CHECK(coordinator.document().data().audioAssets.size() == audioBefore + 1);
        CHECK(coordinator.document().findGeneratedSfx("sfx-jump")->outputAssetId
              == output.assetId);
        CHECK(coordinator.document().findGeneratedSfx(dupId)->outputAssetId
              == copyOut.assetId);
        CHECK(!coordinator.execute(RegisterGeneratedSfxOutputCommand{
            dupId, changedAgain, output}).ok); // non-canonical for dupId
    }
    CHECK(coordinator.undo().ok); // undo Register on duplicate
    CHECK(coordinator.undo().ok); // undo Duplicate
    CHECK(!coordinator.document().hasGeneratedSfx(dupId));

    ProjectDoc unsupported = makeDoc();
    artcade::sfx::GeneratedSfxDef badVersion;
    badVersion.id = "future";
    badVersion.name = "Future";
    badVersion.recipe.generatorVersion = 999u;
    unsupported.generatedSfx.push_back(std::move(badVersion));
    CHECK(!ProjectValidator::validate(ProjectDocument{std::move(unsupported)}).ok);

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
    // ratio, and it's always positive by construction ÔÇö never the additive-
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
    return reportAndExit("generated-sfx-model-test");
}
