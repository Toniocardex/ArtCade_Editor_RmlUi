// script-delete-disk-test.cpp — delete Script asset + raw .lua side-effect history.
// Isolated from script-asset-test to keep MSVC /GS stack frames small.

#include "editor_core_test_harness.h"

#include "editor-native/app/editor_command_side_effect.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/project_script_file_service.h"
#include "editor-native/app/script_asset_workflow.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/script_asset_commands.h"
#include "editor-native/commands/script_attachment_commands.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using namespace ArtCade;
using namespace ArtCade::EditorNative;
using namespace ArtCade::EditorNative::CoreTest;

namespace {

std::filesystem::path makeUniqueRoot(const char* tag) {
    const auto stamp = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path root =
        std::filesystem::temp_directory_path()
        / (std::string("artcade-script-delete-") + tag + "-" + stamp);
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "scripts", ec);
    CHECK(!ec);
    return root;
}

void wipeRoot(const std::filesystem::path& root) {
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

const ScriptDirtyBufferQuery kNeverDirty = [](const AssetId&) { return false; };

const std::vector<std::uint8_t> kBomCrlf = {
    0xef, 0xbb, 0xbf,
    'r','e','t','u','r','n',' ','{','\r','\n','}','\r','\n'
};

struct ProbeFlags {
    bool initialRolledBack = false;
};

class ProbeSideEffect final : public EditorCommandSideEffect {
public:
    explicit ProbeSideEffect(std::shared_ptr<ProbeFlags> flags)
        : flags_(std::move(flags)) {}
    EditorCommandSideEffectResult rollbackInitial() override {
        flags_->initialRolledBack = true;
        return EditorCommandSideEffectResult::success();
    }
    EditorCommandSideEffectResult prepareUndo() override {
        return EditorCommandSideEffectResult::success();
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
        return EditorCommandSideEffectResult::success();
    }
    void rebaseProjectRoot(const std::filesystem::path&,
                           const std::filesystem::path&) override {}
private:
    std::shared_ptr<ProbeFlags> flags_;
};

void testBomCrlfRoundTrip() {
    const std::filesystem::path root = makeUniqueRoot("bom");
    ProjectScriptFileService files{root};
    CHECK(files.writeRawScriptNoReplace("scripts/orphan.lua", kBomCrlf).ok);

    ProjectDoc doc = makeDoc();
    doc.scriptAssets = {ScriptAssetDef{"orphan", "Orphan", "scripts/orphan.lua"}};
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));

    CHECK(removeScriptAsset(*coordinator, root, "orphan", kNeverDirty).ok);
    CHECK(!coordinator->document().hasScriptAsset("orphan"));
    CHECK(!files.readRawScriptIfExists("scripts/orphan.lua").value.existed);

    CHECK(coordinator->undo().ok);
    CHECK(coordinator->document().hasScriptAsset("orphan"));
    CHECK(files.readRawScriptIfExists("scripts/orphan.lua").value.bytes == kBomCrlf);

    CHECK(coordinator->redo().ok);
    CHECK(!coordinator->document().hasScriptAsset("orphan"));
    CHECK(!files.readRawScriptIfExists("scripts/orphan.lua").value.existed);

    wipeRoot(root);
}

void testAttachedRejectsIncludingDisabled() {
    const std::filesystem::path root = makeUniqueRoot("attached");
    ProjectScriptFileService files{root};
    CHECK(files.writeScriptAtomically(
        "scripts/attached.lua",
        "artcade.require_api_version(1)\nreturn {}\n").ok);

    ProjectDoc doc = makeDoc();
    EntityDef heroType;
    heroType.className = "Hero";
    heroType.name = "Hero";
    doc.objectTypes.emplace("Hero", std::move(heroType));
    doc.scriptAssets = {
        ScriptAssetDef{"attached", "Attached", "scripts/attached.lua"},
    };
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));
    CHECK(coordinator->execute(AddScriptAttachmentCommand{
        "Hero", ScriptAttachmentDef{"att-1", "attached", false}, 0}).ok);

    CHECK(!removeScriptAsset(*coordinator, root, "attached", kNeverDirty).ok);
    CHECK(coordinator->document().hasScriptAsset("attached"));
    CHECK(files.readRawScriptIfExists("scripts/attached.lua").value.existed);

    wipeRoot(root);
}

void testMissingFileExternalRecreateBlocksUndo() {
    const std::filesystem::path root = makeUniqueRoot("ghost");
    ProjectScriptFileService files{root};

    ProjectDoc doc = makeDoc();
    doc.scriptAssets = {ScriptAssetDef{"ghost", "Ghost", "scripts/ghost.lua"}};
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));

    CHECK(removeScriptAsset(*coordinator, root, "ghost", kNeverDirty).ok);
    CHECK(!files.readRawScriptIfExists("scripts/ghost.lua").value.existed);
    CHECK(files.writeRawScriptNoReplace(
        "scripts/ghost.lua", std::vector<std::uint8_t>{'x'}).ok);
    CHECK(!coordinator->undo().ok);
    CHECK(!coordinator->document().hasScriptAsset("ghost"));
    CHECK(files.readRawScriptIfExists("scripts/ghost.lua").value.bytes
          == std::vector<std::uint8_t>{'x'});

    wipeRoot(root);
}

void testDirtyRedoRejected() {
    const std::filesystem::path root = makeUniqueRoot("dirty-redo");
    ProjectScriptFileService files{root};
    CHECK(files.writeRawScriptNoReplace("scripts/orphan.lua", kBomCrlf).ok);

    ProjectDoc doc = makeDoc();
    doc.scriptAssets = {ScriptAssetDef{"orphan", "Orphan", "scripts/orphan.lua"}};
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));

    const ScriptDirtyBufferQuery dirtyQuery =
        [&](const AssetId& id) {
            const ScriptEditorBuffer* buffer =
                coordinator->state().scriptEditor.find(id);
            return buffer && buffer->dirty();
        };

    CHECK(removeScriptAsset(*coordinator, root, "orphan", dirtyQuery).ok);
    CHECK(coordinator->undo().ok);
    CHECK(coordinator->apply(OpenScriptBufferIntent{
        "orphan", "return {\r\n}\r\n"}).ok);
    CHECK(coordinator->apply(EditScriptBufferIntent{
        "orphan", "return { edited = true }\n", 8}).ok);
    CHECK(coordinator->state().scriptEditor.find("orphan")->dirty());

    const auto before = files.readRawScriptIfExists("scripts/orphan.lua");
    CHECK(before.ok && before.value.existed);
    CHECK(!coordinator->redo().ok);
    CHECK(coordinator->document().hasScriptAsset("orphan"));
    CHECK(coordinator->state().scriptEditor.find("orphan") != nullptr);
    CHECK(coordinator->state().scriptEditor.find("orphan")->dirty());
    CHECK(files.readRawScriptIfExists("scripts/orphan.lua").value.bytes
          == before.value.bytes);

    wipeRoot(root);
}

void testExternalDivergeBlocksRedo() {
    const std::filesystem::path root = makeUniqueRoot("diverge");
    ProjectScriptFileService files{root};
    CHECK(files.writeRawScriptNoReplace("scripts/orphan.lua", kBomCrlf).ok);

    ProjectDoc doc = makeDoc();
    doc.scriptAssets = {ScriptAssetDef{"orphan", "Orphan", "scripts/orphan.lua"}};
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));

    CHECK(removeScriptAsset(*coordinator, root, "orphan", kNeverDirty).ok);
    CHECK(coordinator->undo().ok);
    CHECK(files.writeScriptAtomically(
        "scripts/orphan.lua", "return { mutated = true }\n").ok);
    CHECK(!coordinator->redo().ok);
    CHECK(coordinator->document().hasScriptAsset("orphan"));
    CHECK(files.readScript("scripts/orphan.lua").value
          == "return { mutated = true }\n");

    wipeRoot(root);
}

void testDirtySaveThenDeleteRestoresSavedBytes() {
    const std::filesystem::path root = makeUniqueRoot("save");
    ProjectScriptFileService files{root};
    const std::vector<std::uint8_t> oldBytes = {
        'r','e','t','u','r','n',' ','{','}','\n'
    };
    CHECK(files.writeRawScriptNoReplace("scripts/orphan.lua", oldBytes).ok);

    ProjectDoc doc = makeDoc();
    doc.scriptAssets = {ScriptAssetDef{"orphan", "Orphan", "scripts/orphan.lua"}};
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));

    CHECK(coordinator->apply(OpenScriptBufferIntent{"orphan", "return {}\n"}).ok);
    const std::string saved = "return { saved = true }\n";
    CHECK(coordinator->apply(EditScriptBufferIntent{"orphan", saved, 8}).ok);
    CHECK(files.writeScriptAtomically("scripts/orphan.lua", saved).ok);
    CHECK(coordinator->apply(MarkScriptBufferSavedIntent{"orphan", saved}).ok);
    CHECK(coordinator->apply(CloseScriptBufferIntent{"orphan"}).ok);

    CHECK(removeScriptAsset(*coordinator, root, "orphan", kNeverDirty).ok);
    CHECK(coordinator->undo().ok);
    CHECK(files.readScript("scripts/orphan.lua").value == saved);
    CHECK(coordinator->state().scriptEditor.find("orphan") == nullptr);

    wipeRoot(root);
}

void testDirtyDiscardThenDeleteRestoresPersistedOnly() {
    const std::filesystem::path root = makeUniqueRoot("discard");
    ProjectScriptFileService files{root};
    CHECK(files.writeRawScriptNoReplace("scripts/orphan.lua", kBomCrlf).ok);

    ProjectDoc doc = makeDoc();
    doc.scriptAssets = {ScriptAssetDef{"orphan", "Orphan", "scripts/orphan.lua"}};
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));

    CHECK(coordinator->apply(OpenScriptBufferIntent{
        "orphan", "return {\r\n}\r\n"}).ok);
    CHECK(coordinator->apply(EditScriptBufferIntent{
        "orphan", "return { draft = true }\n", 8}).ok);
    CHECK(coordinator->apply(CloseScriptBufferIntent{"orphan"}).ok);

    CHECK(removeScriptAsset(*coordinator, root, "orphan", kNeverDirty).ok);
    CHECK(coordinator->undo().ok);
    CHECK(files.readRawScriptIfExists("scripts/orphan.lua").value.bytes == kBomCrlf);
    CHECK(coordinator->state().scriptEditor.find("orphan") == nullptr);

    wipeRoot(root);
}

void testCommandNoOpRollsBackSideEffect() {
    ProjectDoc doc = makeDoc();
    doc.scriptAssets = {ScriptAssetDef{"orphan", "Orphan", "scripts/orphan.lua"}};
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));
    auto flags = std::make_shared<ProbeFlags>();

    CHECK(coordinator->executeWithSideEffect(
        RenameScriptAssetCommand{"orphan", "Orphan"},
        std::make_unique<ProbeSideEffect>(flags)).ok);
    CHECK(flags->initialRolledBack);
    CHECK(!coordinator->canUndo());
}

void testStagingOccupiedAbortsWithoutMutation() {
    const std::filesystem::path root = makeUniqueRoot("staging");
    ProjectScriptFileService files{root};
    CHECK(files.writeRawScriptNoReplace("scripts/orphan.lua", kBomCrlf).ok);

    // Token counter starts at 1 for the process; occupy the first staging path.
    {
        std::ofstream occupied(
            root / "scripts" / "orphan.lua.artcade-delete-1.tmp",
            std::ios::binary | std::ios::trunc);
        occupied << "busy";
        CHECK(static_cast<bool>(occupied));
    }

    ProjectDoc doc = makeDoc();
    doc.scriptAssets = {ScriptAssetDef{"orphan", "Orphan", "scripts/orphan.lua"}};
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));

    CHECK(!removeScriptAsset(*coordinator, root, "orphan", kNeverDirty).ok);
    CHECK(coordinator->document().hasScriptAsset("orphan"));
    CHECK(files.readRawScriptIfExists("scripts/orphan.lua").value.bytes == kBomCrlf);

    wipeRoot(root);
}

void testSaveAsRebaseAppliedAndUndone() {
    const std::filesystem::path root = makeUniqueRoot("rebase-a");
    const std::filesystem::path nextApplied = makeUniqueRoot("rebase-b");
    const std::filesystem::path nextUndone = makeUniqueRoot("rebase-c");
    ProjectScriptFileService files{root};
    CHECK(files.writeRawScriptNoReplace("scripts/orphan.lua", kBomCrlf).ok);

    ProjectDoc doc = makeDoc();
    doc.scriptAssets = {ScriptAssetDef{"orphan", "Orphan", "scripts/orphan.lua"}};
    auto coordinator = std::make_unique<EditorCoordinator>(std::move(doc));

    CHECK(removeScriptAsset(*coordinator, root, "orphan", kNeverDirty).ok);

    // Applied: destination must not contain the deleted .lua.
    CHECK(coordinator->validateCommandSideEffectRebase(root, nextApplied).ok);
    {
        ProjectScriptFileService nextFiles{nextApplied};
        CHECK(nextFiles.writeRawScriptNoReplace("scripts/orphan.lua", kBomCrlf).ok);
    }
    CHECK(!coordinator->validateCommandSideEffectRebase(root, nextApplied).ok);

    CHECK(coordinator->undo().ok);
    // Undone: destination must contain exact restored bytes.
    CHECK(!coordinator->validateCommandSideEffectRebase(root, nextUndone).ok);
    {
        ProjectScriptFileService nextFiles{nextUndone};
        CHECK(nextFiles.writeRawScriptNoReplace("scripts/orphan.lua", kBomCrlf).ok);
    }
    CHECK(coordinator->validateCommandSideEffectRebase(root, nextUndone).ok);
    coordinator->rebaseCommandSideEffects(root, nextUndone);

    CHECK(coordinator->redo().ok);
    ProjectScriptFileService undoneFiles{nextUndone};
    CHECK(!undoneFiles.readRawScriptIfExists("scripts/orphan.lua").value.existed);

    wipeRoot(root);
    wipeRoot(nextApplied);
    wipeRoot(nextUndone);
}

} // namespace

int main() {
    // Staging token probe must run before any other delete consumes token 1.
    testStagingOccupiedAbortsWithoutMutation();
    testBomCrlfRoundTrip();
    testAttachedRejectsIncludingDisabled();
    testMissingFileExternalRecreateBlocksUndo();
    testDirtyRedoRejected();
    testExternalDivergeBlocksRedo();
    testDirtySaveThenDeleteRestoresSavedBytes();
    testDirtyDiscardThenDeleteRestoresPersistedOnly();
    testCommandNoOpRollsBackSideEffect();
    testSaveAsRebaseAppliedAndUndone();
    return reportAndExit("script-delete-disk-test");
}
