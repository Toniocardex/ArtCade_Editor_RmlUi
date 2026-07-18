#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/editor_command_side_effect.h"
#include "editor-native/app/generated_sfx_generation_service.h"
#include "editor-native/app/generated_sfx_output_transaction.h"
#include "editor-native/app/generated_sfx_status_projection.h"
#include "editor-native/commands/generated_sfx_commands.h"

#include "artcade/sfx/recipe_json.hpp"
#include "logic-core.h"

#include <array>
#include <chrono>
#include <iostream>
#include <fstream>
#include <iterator>
#include <thread>

using namespace ArtCade;
using namespace ArtCade::EditorNative;

namespace {

int failures = 0;

struct SideEffectProbe {
    bool initialRolledBack = false;
    bool rejectUndo = false;
    bool rejectRebase = false;
    bool rebased = false;
};

class ProbeSideEffect final : public EditorCommandSideEffect {
public:
    explicit ProbeSideEffect(std::shared_ptr<SideEffectProbe> probe)
        : probe_(std::move(probe)) {}
    EditorCommandSideEffectResult rollbackInitial() override {
        probe_->initialRolledBack = true;
        return EditorCommandSideEffectResult::success();
    }
    EditorCommandSideEffectResult prepareUndo() override {
        return probe_->rejectUndo
            ? EditorCommandSideEffectResult::failure("Injected Undo failure")
            : EditorCommandSideEffectResult::success();
    }
    EditorCommandSideEffectResult rollbackUndo() override {
        return EditorCommandSideEffectResult::success();
    }
    void commitUndo() override {}
    EditorCommandSideEffectResult prepareRedo() override {
        return EditorCommandSideEffectResult::success();
    }
    EditorCommandSideEffectResult rollbackRedo() override {
        return EditorCommandSideEffectResult::success();
    }
    void commitRedo() override {}
    EditorCommandSideEffectResult validateProjectRootRebase(
        const std::filesystem::path&,
        const std::filesystem::path&) const override {
        return probe_->rejectRebase
            ? EditorCommandSideEffectResult::failure("Injected rebase failure")
            : EditorCommandSideEffectResult::success();
    }
    void rebaseProjectRoot(const std::filesystem::path&,
                           const std::filesystem::path&) override {
        probe_->rebased = true;
    }
private:
    std::shared_ptr<SideEffectProbe> probe_;
};

class FixedInspectionRepository final : public GeneratedSfxOutputRepository {
public:
    explicit FixedInspectionRepository(GeneratedSfxFileInspection inspection)
        : inspection_(std::move(inspection)) {}
    GeneratedSfxFileInspection inspect(const std::filesystem::path&) const override {
        return inspection_;
    }
    GeneratedSfxFileOperationResult moveNoReplace(
        const std::filesystem::path&, const std::filesystem::path&) override {
        return GeneratedSfxFileOperationResult::failure("Not used");
    }
    GeneratedSfxFileOperationResult removeIfExists(
        const std::filesystem::path&) override {
        return GeneratedSfxFileOperationResult::failure("Not used");
    }
private:
    GeneratedSfxFileInspection inspection_;
};

class SelectiveInspectionRepository final : public GeneratedSfxOutputRepository {
public:
    explicit SelectiveInspectionRepository(std::string failingId)
        : failingId_(std::move(failingId)) {}
    GeneratedSfxFileInspection inspect(const std::filesystem::path& path) const override {
        if (path.string().find(failingId_) != std::string::npos)
            return {false, false, false, "Injected inspection failure"};
        return delegate_.inspect(path);
    }
    GeneratedSfxFileOperationResult moveNoReplace(
        const std::filesystem::path& source,
        const std::filesystem::path& destination) override {
        return delegate_.moveNoReplace(source, destination);
    }
    GeneratedSfxFileOperationResult removeIfExists(
        const std::filesystem::path& path) override {
        return delegate_.removeIfExists(path);
    }
private:
    std::string failingId_;
    mutable FilesystemGeneratedSfxOutputRepository delegate_;
};

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAIL: " #condition " at line " << __LINE__ << '\n'; \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

ProjectDocument makeDocument() {
    ProjectDoc project;
    project.projectName = "SFX service test";
    SceneDef scene;
    scene.id = "scene";
    scene.name = "Scene";
    project.scenes.emplace(scene.id, scene);
    project.activeSceneId = scene.id;
    EditorCoordinator coordinator{std::move(project)};
    artcade::sfx::SfxRecipe recipe;
    const auto created = coordinator.execute(
        CreateGeneratedSfxCommand{"sfx-test", "Test", recipe});
    CHECK(created.ok);
    return ProjectDocument{coordinator.document().data()};
}

void writeFile(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << bytes;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace

int main() {
    ProjectDocument document = makeDocument();
    const auto* definition = document.findGeneratedSfx("sfx-test");
    CHECK(definition != nullptr);
    if (!definition) return 1;

    // Reopening the same path is still a distinct project session.
    ProjectSessionIdentity sessions;
    const ProjectSessionId firstOpen = sessions.current();
    const ProjectSessionId reopenedSamePath = sessions.advance();
    CHECK(firstOpen != reopenedSamePath);

    GeneratedSfxJobStamp stamp;
    stamp.token = 41;
    stamp.projectSessionId = reopenedSamePath;
    stamp.documentRevision = document.revision();
    stamp.assetId = definition->id;
    stamp.recipe = definition->recipe;
    stamp.recipeFingerprint = artcade::sfx::recipeFingerprint(definition->recipe);
    CHECK(validateGeneratedSfxCompletion(
        stamp, 41, reopenedSamePath, document)
        == GeneratedSfxCompletionRejection::None);
    CHECK(validateGeneratedSfxCompletion(
        stamp, 42, reopenedSamePath, document)
        == GeneratedSfxCompletionRejection::Superseded);
    CHECK(validateGeneratedSfxCompletion(
        stamp, 41, sessions.advance(), document)
        == GeneratedSfxCompletionRejection::ProjectChanged);

    // A new authoring revision invalidates even an otherwise matching recipe.
    EditorCoordinator edited{document.data()};
    CHECK(edited.execute(RenameGeneratedSfxCommand{"sfx-test", "Renamed"}).ok);
    CHECK(validateGeneratedSfxCompletion(
        stamp, 41, reopenedSamePath, edited.document())
        == GeneratedSfxCompletionRejection::DocumentChanged);

    EditorCoordinator recipeEdited{document.data()};
    auto changedRecipe = definition->recipe;
    ++changedRecipe.randomSeed;
    CHECK(recipeEdited.execute(
        UpdateGeneratedSfxRecipeCommand{"sfx-test", changedRecipe}).ok);
    CHECK(validateGeneratedSfxCompletion(
        stamp, 41, reopenedSamePath, recipeEdited.document())
        == GeneratedSfxCompletionRejection::RecipeChanged);

    EditorCoordinator deleted{document.data()};
    CHECK(deleted.execute(RemoveGeneratedSfxCommand{"sfx-test"}).ok);
    CHECK(validateGeneratedSfxCompletion(
        stamp, 41, reopenedSamePath, deleted.document())
        == GeneratedSfxCompletionRejection::AssetDeleted);

    // Play rejection is at the service boundary, not merely disabled UI.
    GeneratedSfxGenerationService service;
    CHECK(!service.requestPreview(document, reopenedSamePath,
                                  PreviewGeneratedSfxIntent{"sfx-test"}, true).accepted);

    // Cancellation and shutdown deterministically join the worker and leave no
    // active workspace state.
    CHECK(service.requestPreview(
        document, reopenedSamePath, PreviewGeneratedSfxIntent{"sfx-test"}, false).accepted);
    CHECK(service.busy());
    service.cancelAll("Test cancellation");
    service.shutdown();
    CHECK(!service.busy());

    // The coordinator keeps a failed external prepare outside the document
    // mutation and preserves the history entry for a later retry.
    {
        EditorCoordinator guarded{document.data()};
        auto failedApplyProbe = std::make_shared<SideEffectProbe>();
        CHECK(!guarded.executeWithSideEffect(
            RenameGeneratedSfxCommand{"missing", "Missing"},
            std::make_unique<ProbeSideEffect>(failedApplyProbe)).ok);
        CHECK(failedApplyProbe->initialRolledBack);

        auto undoProbe = std::make_shared<SideEffectProbe>();
        undoProbe->rejectUndo = true;
        CHECK(guarded.executeWithSideEffect(
            RenameGeneratedSfxCommand{"sfx-test", "Guarded"},
            std::make_unique<ProbeSideEffect>(undoProbe)).ok);
        const std::uint64_t guardedRevision = guarded.document().revision();
        CHECK(!guarded.undo().ok);
        CHECK(guarded.document().revision() == guardedRevision);
        CHECK(guarded.document().findGeneratedSfx("sfx-test")->name == "Guarded");
        CHECK(guarded.canUndo());

        EditorCoordinator rebaseGuarded{document.data()};
        auto rebaseProbe = std::make_shared<SideEffectProbe>();
        CHECK(rebaseGuarded.executeWithSideEffect(
            RenameGeneratedSfxCommand{"sfx-test", "Rebase Guarded"},
            std::make_unique<ProbeSideEffect>(rebaseProbe)).ok);
        rebaseProbe->rejectRebase = true;
        CHECK(!rebaseGuarded.validateCommandSideEffectRebase(
            "old-root", "new-root").ok);
        CHECK(!rebaseProbe->rebased);
        rebaseProbe->rejectRebase = false;
        CHECK(rebaseGuarded.validateCommandSideEffectRebase(
            "old-root", "new-root").ok);
        rebaseGuarded.rebaseCommandSideEffects("old-root", "new-root");
        CHECK(rebaseProbe->rebased);
    }

    // SFX-R3 transaction: document registration and the derived WAV share the
    // same history entry without giving the Command concrete filesystem access.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "artcade-sfx-output-transaction-test";
    std::error_code cleanupError;
    std::filesystem::remove_all(root, cleanupError);
    std::filesystem::create_directories(root);

    EditorCoordinator outputCoordinator{document.data()};
    auto repository = std::make_shared<FilesystemGeneratedSfxOutputRepository>();
    GeneratedSfxOutputTransaction transaction{outputCoordinator, repository};
    auto outputIdentity = stableGeneratedSfxOutputIdentity(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-test"), root);
    CHECK(outputIdentity.has_value());
    if (!outputIdentity) return 1;

    writeFile(outputIdentity->stagingPath, "generated-v1");
    CHECK(transaction.commit(
        "sfx-test", definition->recipe, *outputIdentity, false, 100).ok);
    CHECK(readFile(outputIdentity->finalPath) == "generated-v1");
    CHECK(outputCoordinator.document().hasAudioAsset(outputIdentity->assetId));

    // SFX-R5 status is a pure projection of document + repository + immutable
    // service snapshot. It is never persisted or pushed into Undo history.
    GeneratedSfxGenerationSnapshot emptySnapshot;
    auto observed = projectGeneratedSfxStatus(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-test"), root,
        *repository, emptySnapshot);
    CHECK(observed.status == GeneratedSfxObservedStatus::UpToDate);

    std::filesystem::remove(outputIdentity->finalPath, cleanupError);
    observed = projectGeneratedSfxStatus(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-test"), root,
        *repository, emptySnapshot);
    CHECK(observed.status == GeneratedSfxObservedStatus::MissingOutput);
    writeFile(outputIdentity->finalPath, "generated-v1");

    FixedInspectionRepository unreadable{{true, true, false, "Access denied"}};
    observed = projectGeneratedSfxStatus(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-test"), root,
        unreadable, emptySnapshot);
    CHECK(observed.status == GeneratedSfxObservedStatus::MissingOutput);
    CHECK(observed.message.find("not readable") != std::string::npos);

    const std::string outputFingerprint = artcade::sfx::recipeFingerprint(
        outputCoordinator.document().findGeneratedSfx("sfx-test")->recipe);
    GeneratedSfxGenerationSnapshot generatingSnapshot;
    generatingSnapshot.generating = GeneratedSfxGenerationActivity{
        "sfx-test", outputFingerprint};
    CHECK(projectGeneratedSfxStatus(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-test"), root,
        *repository, generatingSnapshot).status
        == GeneratedSfxObservedStatus::Generating);

    GeneratedSfxGenerationSnapshot failedSnapshot;
    failedSnapshot.diagnostics.emplace("sfx-test", GeneratedSfxGenerationDiagnostic{
        GeneratedSfxGenerationFailureKind::Failed, outputFingerprint,
        "Injected encode failure"});
    CHECK(projectGeneratedSfxStatus(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-test"), root,
        *repository, failedSnapshot).status
        == GeneratedSfxObservedStatus::GenerationFailed);
    failedSnapshot.diagnostics["sfx-test"].kind =
        GeneratedSfxGenerationFailureKind::Collision;
    CHECK(projectGeneratedSfxStatus(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-test"), root,
        *repository, failedSnapshot).status
        == GeneratedSfxObservedStatus::Collision);

    EditorCoordinator modifiedProjection{outputCoordinator.document().data()};
    auto projectionRecipe = modifiedProjection.document()
        .findGeneratedSfx("sfx-test")->recipe;
    ++projectionRecipe.randomSeed;
    CHECK(modifiedProjection.execute(UpdateGeneratedSfxRecipeCommand{
        "sfx-test", projectionRecipe}).ok);
    CHECK(projectGeneratedSfxStatus(
        modifiedProjection.document(),
        *modifiedProjection.document().findGeneratedSfx("sfx-test"), root,
        *repository, emptySnapshot).status
        == GeneratedSfxObservedStatus::RecipeModified);

    CHECK(outputCoordinator.undo().ok);
    CHECK(!std::filesystem::exists(outputIdentity->finalPath));
    CHECK(!outputCoordinator.document().hasAudioAsset(outputIdentity->assetId));
    CHECK(projectGeneratedSfxStatus(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-test"), root,
        *repository, emptySnapshot).status
        == GeneratedSfxObservedStatus::MissingOutput);
    CHECK(outputCoordinator.redo().ok);
    CHECK(readFile(outputIdentity->finalPath) == "generated-v1");
    CHECK(outputCoordinator.document().hasAudioAsset(outputIdentity->assetId));

    // Undo + a fresh Command drops Redo and its retained generated bytes; the
    // canonical destination becomes generatable again instead of an orphan.
    CHECK(outputCoordinator.undo().ok);
    CHECK(outputCoordinator.execute(
        RenameGeneratedSfxCommand{"sfx-test", "Renamed after Undo"}).ok);
    CHECK(!outputCoordinator.canRedo());
    CHECK(!std::filesystem::exists(outputIdentity->finalPath));
    writeFile(outputIdentity->stagingPath, "generated-v2");
    CHECK(transaction.commit(
        "sfx-test", definition->recipe, *outputIdentity, false, 101).ok);
    CHECK(readFile(outputIdentity->finalPath) == "generated-v2");

    // Regenerate preserves the prior WAV for exact Undo and swaps it back on
    // Redo. A stale recipe is rejected without touching that prior WAV.
    auto recipeV2 = definition->recipe;
    ++recipeV2.randomSeed;
    CHECK(outputCoordinator.execute(
        UpdateGeneratedSfxRecipeCommand{"sfx-test", recipeV2}).ok);
    writeFile(outputIdentity->stagingPath, "regenerated-v3");
    CHECK(transaction.commit(
        "sfx-test", recipeV2, *outputIdentity, true, 102).ok);
    CHECK(readFile(outputIdentity->finalPath) == "regenerated-v3");
    CHECK(outputCoordinator.undo().ok);
    CHECK(readFile(outputIdentity->finalPath) == "generated-v2");
    CHECK(outputCoordinator.redo().ok);
    CHECK(readFile(outputIdentity->finalPath) == "regenerated-v3");

    auto recipeV3 = recipeV2;
    ++recipeV3.randomSeed;
    CHECK(outputCoordinator.execute(
        UpdateGeneratedSfxRecipeCommand{"sfx-test", recipeV3}).ok);
    writeFile(outputIdentity->stagingPath, "must-roll-back");
    CHECK(!transaction.commit(
        "sfx-test", recipeV2, *outputIdentity, true, 103).ok);
    CHECK(readFile(outputIdentity->finalPath) == "regenerated-v3");
    CHECK(generatedSfxOutputStatus(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-test"))
        == GeneratedSfxOutputStatus::Stale);

    // Finalize failure after preserving the old file also compensates it.
    std::filesystem::remove(outputIdentity->stagingPath, cleanupError);
    CHECK(!transaction.commit(
        "sfx-test", recipeV3, *outputIdentity, true, 104).ok);
    CHECK(readFile(outputIdentity->finalPath) == "regenerated-v3");

    // A first-generation race never overwrites an unowned destination and
    // removes its staging file.
    CHECK(outputCoordinator.execute(DuplicateGeneratedSfxCommand{
        "sfx-test", "sfx-race", "Race"}).ok);
    const auto* raceDefinition =
        outputCoordinator.document().findGeneratedSfx("sfx-race");
    CHECK(raceDefinition != nullptr);
    const artcade::sfx::SfxRecipe raceRecipe = raceDefinition
        ? raceDefinition->recipe : artcade::sfx::SfxRecipe{};
    auto raceIdentity = stableGeneratedSfxOutputIdentity(
        outputCoordinator.document(),
        *outputCoordinator.document().findGeneratedSfx("sfx-race"), root);
    CHECK(raceIdentity.has_value());
    if (raceIdentity) {
        writeFile(raceIdentity->finalPath, "external");
        writeFile(raceIdentity->stagingPath, "candidate");
        CHECK(!transaction.commit(
            "sfx-race", raceRecipe, *raceIdentity, false, 105).ok);
        CHECK(readFile(raceIdentity->finalPath) == "external");
        CHECK(!std::filesystem::exists(raceIdentity->stagingPath));
        CHECK(generatedSfxOutputStatus(
            outputCoordinator.document(),
            *outputCoordinator.document().findGeneratedSfx("sfx-race"))
            == GeneratedSfxOutputStatus::NeedsGeneration);
        std::filesystem::remove(raceIdentity->finalPath, cleanupError);
        writeFile(raceIdentity->stagingPath, "retry-success");
        CHECK(transaction.commit(
            "sfx-race", raceRecipe, *raceIdentity, false, 106).ok);
        CHECK(readFile(raceIdentity->finalPath) == "retry-success");
    }

    // Save As copies the project tree, then rebases every retained history
    // side-effect. Undo must affect the new root only; the old project remains
    // a valid independent snapshot.
    {
        const std::filesystem::path oldRoot = root / "relocation-old";
        const std::filesystem::path newRoot = root / "relocation-new";
        std::filesystem::create_directories(oldRoot, cleanupError);
        EditorCoordinator relocated{document.data()};
        GeneratedSfxOutputTransaction relocatedTransaction{relocated, repository};
        auto oldIdentity = stableGeneratedSfxOutputIdentity(
            relocated.document(),
            *relocated.document().findGeneratedSfx("sfx-test"), oldRoot);
        CHECK(oldIdentity.has_value());
        if (oldIdentity) {
            writeFile(oldIdentity->stagingPath, "relocated-v1");
            CHECK(relocatedTransaction.commit(
                "sfx-test", definition->recipe, *oldIdentity, false, 200).ok);
            auto relocatedRecipe = definition->recipe;
            ++relocatedRecipe.randomSeed;
            CHECK(relocated.execute(UpdateGeneratedSfxRecipeCommand{
                "sfx-test", relocatedRecipe}).ok);
            writeFile(oldIdentity->stagingPath, "relocated-v2");
            CHECK(relocatedTransaction.commit(
                "sfx-test", relocatedRecipe, *oldIdentity, true, 201).ok);

            std::filesystem::create_directories(newRoot, cleanupError);
            cleanupError.clear();
            std::filesystem::copy(
                oldRoot / "assets", newRoot / "assets",
                std::filesystem::copy_options::recursive,
                cleanupError);
            CHECK(!cleanupError);
            CHECK(relocated.validateCommandSideEffectRebase(
                oldRoot, newRoot).ok);
            relocated.rebaseCommandSideEffects(oldRoot, newRoot);

            const std::filesystem::path newFinal =
                newRoot / std::filesystem::u8path(oldIdentity->relativePath);
            CHECK(readFile(newFinal) == "relocated-v2");
            CHECK(relocated.undo().ok);
            CHECK(readFile(newFinal) == "relocated-v1");
            CHECK(readFile(oldIdentity->finalPath) == "relocated-v2");
        }
    }

    // Hard delete is one application transaction: recipe, linked catalog
    // asset, structured Logic Board reference and physical WAV move together.
    // Undo restores the exact bytes and references; Redo removes them again.
    {
        ProjectDoc hardDeleteProject = outputCoordinator.document().data();
        const auto* linked = outputCoordinator.document().findGeneratedSfx("sfx-test");
        CHECK(linked != nullptr && !linked->outputAssetId.empty());
        const AssetId linkedAudioId = linked ? linked->outputAssetId : AssetId{};

        EntityDef soundOwner;
        soundOwner.className = "SoundOwner";
        soundOwner.name = "SoundOwner";
        LogicBoardDef board;
        board.id = "sound-owner-board";
        LogicRuleDef rule = Logic::makeDefaultRule("play-generated-sfx");
        LogicBlockDef playSound = Logic::makeDefaultBlock(
            Logic::kAudioPlaySound, Logic::BlockKind::Action);
        for (LogicPropertyDef& property : playSound.properties) {
            if (property.key == "audioAssetId")
                property.value = LogicAssetReference{linkedAudioId};
        }
        rule.actions.push_back(std::move(playSound));
        board.rules.push_back(std::move(rule));
        soundOwner.logicBoard = std::move(board);
        hardDeleteProject.objectTypes.emplace("SoundOwner", std::move(soundOwner));

        const auto referencedAudioId = [](const EditorCoordinator& owner) {
            const LogicBlockDef& action = owner.document().data().objectTypes
                .at("SoundOwner").logicBoard->rules.front().actions.back();
            const LogicPropertyDef* property = Logic::findProperty(
                action, "audioAssetId");
            return property
                ? std::get<LogicAssetReference>(property->value).id
                : AssetId{};
        };

        const std::filesystem::path deleteRoot = root / "hard-delete";
        std::filesystem::create_directories(deleteRoot, cleanupError);
        CHECK(!cleanupError);
        cleanupError.clear();
        EditorCoordinator deleteCoordinator{hardDeleteProject};
        GeneratedSfxOutputTransaction deleteTransaction{
            deleteCoordinator, repository};
        const auto deleteIdentity = stableGeneratedSfxOutputIdentity(
            deleteCoordinator.document(),
            *deleteCoordinator.document().findGeneratedSfx("sfx-test"),
            deleteRoot);
        CHECK(deleteIdentity.has_value());
        if (deleteIdentity) {
            constexpr const char* kDeletedBytes = "RIFF-exact-delete-payload";
            writeFile(deleteIdentity->finalPath, kDeletedBytes);
            CHECK(deleteTransaction.remove(
                RemoveGeneratedSfxIntent{"sfx-test"}, deleteRoot).ok);
            CHECK(!deleteCoordinator.document().hasGeneratedSfx("sfx-test"));
            CHECK(!deleteCoordinator.document().hasAudioAsset(linkedAudioId));
            CHECK(referencedAudioId(deleteCoordinator).empty());
            CHECK(!std::filesystem::exists(deleteIdentity->finalPath));

            bool recoveryFileLeftOnDisk = false;
            for (const auto& item : std::filesystem::directory_iterator(
                     deleteIdentity->finalPath.parent_path())) {
                if (item.path().filename().string().find(".artcade-restore-")
                    != std::string::npos) recoveryFileLeftOnDisk = true;
            }
            CHECK(!recoveryFileLeftOnDisk);

            CHECK(deleteCoordinator.undo().ok);
            CHECK(deleteCoordinator.document().hasGeneratedSfx("sfx-test"));
            CHECK(deleteCoordinator.document().hasAudioAsset(linkedAudioId));
            CHECK(referencedAudioId(deleteCoordinator) == linkedAudioId);
            CHECK(readFile(deleteIdentity->finalPath) == kDeletedBytes);

            CHECK(deleteCoordinator.redo().ok);
            CHECK(!deleteCoordinator.document().hasGeneratedSfx("sfx-test"));
            CHECK(!deleteCoordinator.document().hasAudioAsset(linkedAudioId));
            CHECK(referencedAudioId(deleteCoordinator).empty());
            CHECK(!std::filesystem::exists(deleteIdentity->finalPath));
        }

        const std::filesystem::path playRoot = root / "hard-delete-play-guard";
        std::filesystem::create_directories(playRoot, cleanupError);
        CHECK(!cleanupError);
        cleanupError.clear();
        EditorCoordinator playGuard{hardDeleteProject};
        GeneratedSfxOutputTransaction guardedDelete{playGuard, repository};
        const auto playIdentity = stableGeneratedSfxOutputIdentity(
            playGuard.document(),
            *playGuard.document().findGeneratedSfx("sfx-test"), playRoot);
        CHECK(playIdentity.has_value());
        if (playIdentity) {
            writeFile(playIdentity->finalPath, "play-guard-payload");
            CHECK(playGuard.playCurrentScene().ok);
            CHECK(!guardedDelete.remove(
                RemoveGeneratedSfxIntent{"sfx-test"}, playRoot).ok);
            CHECK(playGuard.document().hasGeneratedSfx("sfx-test"));
            CHECK(playGuard.document().hasAudioAsset(linkedAudioId));
            CHECK(readFile(playIdentity->finalPath) == "play-guard-payload");
            CHECK(playGuard.stopPlaying().ok);
        }
    }

    // Batch is a serial workspace queue over the exact same Generate use case.
    // One run exercises each terminal outcome and proves cardinality is stable:
    // success, structured preflight failure, deleted-item skip and cancellation.
    {
        EditorCoordinator batchCoordinator{makeDocument().data()};
        const std::array<std::string, 4> ids{
            "sfx-batch-ok", "sfx-batch-fail", "sfx-batch-skip",
            "sfx-batch-cancel"};
        for (const std::string& id : ids) {
            artcade::sfx::SfxRecipe original;
            CHECK(batchCoordinator.execute(CreateGeneratedSfxCommand{
                id, id, original}).ok);
            AudioAssetDef audio;
            audio.assetId = generatedAudioAssetId(id);
            audio.sourcePath = generatedAudioRelativePath(id);
            audio.loadMode = AudioLoadMode::StaticSound;
            CHECK(batchCoordinator.execute(RegisterGeneratedSfxOutputCommand{
                id, original, std::move(audio)}).ok);
            ++original.randomSeed;
            CHECK(batchCoordinator.execute(UpdateGeneratedSfxRecipeCommand{
                id, original}).ok);
        }
        const std::size_t audioCardinality =
            batchCoordinator.document().data().audioAssets.size();
        const std::filesystem::path batchRoot = root / "batch";
        std::filesystem::create_directories(
            batchRoot / "assets" / "audio" / "generated", cleanupError);
        const std::filesystem::path batchProject = batchRoot / "batch.artcade";
        auto selective = std::make_shared<SelectiveInspectionRepository>(
            "sfx-batch-fail");
        GeneratedSfxGenerationService batchService{selective};
        const ProjectSessionId batchSession = 700;
        CHECK(batchService.requestRegenerateAllStale(
            batchCoordinator.document(), batchSession, batchProject,
            RegenerateAllStaleSfxIntent{}, false).accepted);
        CHECK(batchService.batchState().items.size() == ids.size());

        std::optional<GeneratedSfxServiceEvent> firstReady;
        for (int attempt = 0; attempt < 5000 && !firstReady; ++attempt) {
            firstReady = batchService.poll(
                batchCoordinator.document(), batchSession, batchProject, false);
            if (!firstReady) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(firstReady.has_value());
        CHECK(firstReady && firstReady->kind
            == GeneratedSfxServiceEventKind::GenerationReady);
        if (firstReady) {
            batchService.resolveGeneration(
                firstReady->stamp.token, SfxBatchItemStatus::Succeeded, "Ready");
        }

        CHECK(batchCoordinator.execute(RemoveGeneratedSfxCommand{
            "sfx-batch-skip"}).ok);
        (void)batchService.poll(
            batchCoordinator.document(), batchSession, batchProject, false);
        batchService.cancelBatch();
        for (int attempt = 0; attempt < 5000
             && batchService.batchState().active; ++attempt) {
            (void)batchService.poll(
                batchCoordinator.document(), batchSession, batchProject, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const SfxBatchState& batch = batchService.batchState();
        CHECK(!batch.active);
        CHECK(batch.succeeded == 1);
        CHECK(batch.failed == 1);
        CHECK(batch.skipped == 1);
        CHECK(batch.cancelled == 1);
        CHECK(batchCoordinator.document().data().audioAssets.size()
            <= audioCardinality); // the queue never registers a duplicate output
        CHECK(batchService.snapshot().diagnostics.count("sfx-batch-fail") == 1);
        CHECK(batchService.dismissDiagnostic("sfx-batch-fail"));
        CHECK(batchService.snapshot().diagnostics.count("sfx-batch-fail") == 0);
        batchService.shutdown();
    }

    std::filesystem::remove_all(root, cleanupError);

    std::cout << "generated-sfx-generation-service-test: "
              << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
