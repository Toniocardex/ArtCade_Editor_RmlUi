#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/pending_edit.h"
#include "editor-native/commands/generated_sfx_commands.h"
#include "editor-native/model/generated_sfx_preset_catalog.h"
#include "editor-native/ui/generated_sfx_editor_controller.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

using namespace ArtCade;
using namespace ArtCade::EditorNative;

namespace {

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::cerr << "FAIL: " #condition " at line " << __LINE__ << '\n'; \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

ProjectDoc makeProject() {
    ProjectDoc project;
    project.projectName = "SFX controller test";
    SceneDef scene;
    scene.id = "scene";
    scene.name = "Scene";
    project.scenes.emplace(scene.id, scene);
    project.activeSceneId = scene.id;
    return project;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

std::set<std::string> markupActionIds(std::string source) {
    std::size_t escaped = 0;
    while ((escaped = source.find("\\\"", escaped)) != std::string::npos)
        source.replace(escaped, 2, "\"");
    std::set<std::string> result;
    constexpr std::string_view prefix = "data-action=\"";
    std::size_t cursor = 0;
    while ((cursor = source.find(prefix, cursor)) != std::string::npos) {
        cursor += prefix.size();
        const std::size_t end = source.find('"', cursor);
        if (end == std::string::npos) break;
        result.emplace(source.substr(cursor, end - cursor));
        cursor = end + 1;
    }
    return result;
}

std::size_t countOccurrences(std::string_view source, std::string_view needle) {
    std::size_t count = 0;
    std::size_t cursor = 0;
    while ((cursor = source.find(needle, cursor)) != std::string_view::npos) {
        ++count;
        cursor += needle.size();
    }
    return count;
}

} // namespace

int main() {
    const auto& actions = generatedSfxEditorActionCatalog();
    CHECK(actions.size()
          == static_cast<std::size_t>(GeneratedSfxEditorAction::Count));
    std::set<std::string> actionIds;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const auto& descriptor = actions[index];
        CHECK(static_cast<std::size_t>(descriptor.action) == index);
        CHECK(!descriptor.id.empty());
        CHECK(actionIds.emplace(descriptor.id).second);
        CHECK(findGeneratedSfxEditorAction(descriptor.id) == &descriptor);
    }
    CHECK(findGeneratedSfxEditorAction("generate-new-sfx-output") == nullptr);
    CHECK(findGeneratedSfxEditorAction("create-generated-sfx-from-preset")
          == nullptr);
    CHECK(findGeneratedSfxEditorAction("generate-sfx-output") != nullptr);
    CHECK(findGeneratedSfxEditorAction("create-generated-sfx") != nullptr);
    CHECK(findGeneratedSfxEditorAction("toggle-sfx-mode")
          ->requiresPendingEditResolution);
    CHECK(findGeneratedSfxEditorAction("commit-sfx-field")
          ->mutatesAuthoring);
    CHECK(findGeneratedSfxEditorAction("generate-sfx-output")
          ->requiresPendingEditResolution);
    CHECK(findGeneratedSfxEditorAction("apply-sfx-preset")
          ->requiresPendingEditResolution);
    CHECK(findGeneratedSfxEditorAction("remove-generated-sfx")
          ->requiresPendingEditResolution);
    CHECK(classifyPendingEdit("commit-sfx-macro", "-").status
          == PendingEditStatus::Incomplete);
    CHECK(classifyPendingEdit("commit-sfx-macro", "not-a-number").status
          == PendingEditStatus::Invalid);
    CHECK(classifyPendingEdit("commit-sfx-macro", "880").resolved());

    const std::filesystem::path sourceRoot{ARTCADE_SOURCE_DIR};
    const auto sourceActions = markupActionIds(readText(
        sourceRoot / "src/editor-native/ui/editor_ui.cpp"));
    const auto shellActions = markupActionIds(readText(
        sourceRoot / "src/editor-native/resources/ui/editor_shell.rml"));
    const auto assetsPanelActions = markupActionIds(readText(
        sourceRoot / "src/editor-native/ui/assets_panel.cpp"));
    const std::string controllerSource = readText(
        sourceRoot / "src/editor-native/ui/generated_sfx_editor_controller.cpp");
    const std::string applicationSource = readText(
        sourceRoot / "src/editor-native/app/editor_app.cpp");
    const std::string assetsPanelSource = readText(
        sourceRoot / "src/editor-native/ui/assets_panel.cpp");
    CHECK(controllerSource.find("Command{") == std::string::npos);
    CHECK(controllerSource.find("coordinator_.execute(") == std::string::npos);
    CHECK(controllerSource.find("GeneratedSfxIntent{") != std::string::npos);
    CHECK(countOccurrences(applicationSource, "sfxGeneration.requestGenerate(") == 1);
    CHECK(assetsPanelSource.find(
        "entry(\"open-generated-sfx\", \"Generated SFX Editor\")")
        != std::string::npos);
    CHECK(assetsPanelSource.find(
        "entry(\"create-generated-sfx\", \"Generated SFX\", \"coin\")")
        == std::string::npos);
    for (const auto& id : sourceActions) {
        if (id.find("sfx") == std::string::npos
            && id != "copy-primary-to-secondary") continue;
        if (id.find_first_of("\r\n/ ") != std::string::npos) continue;
        CHECK(findGeneratedSfxEditorAction(id) != nullptr);
    }
    for (const auto& id : shellActions) {
        if (id.find("sfx") == std::string::npos) continue;
        CHECK(findGeneratedSfxEditorAction(id) != nullptr);
    }
    for (const auto& id : assetsPanelActions) {
        if (id.find("sfx") == std::string::npos) continue;
        CHECK(findGeneratedSfxEditorAction(id) != nullptr);
    }

    std::set<std::string> presetIds;
    for (const auto& preset : generatedSfxPresetCatalog()) {
        CHECK(!preset.id.empty());
        CHECK(!preset.label.empty());
        CHECK(presetIds.emplace(preset.id).second);
        CHECK(findGeneratedSfxPreset(preset.id) == &preset);
        CHECK(generatedSfxRecipeFromPreset(preset.id).has_value());
    }
    CHECK(!generatedSfxRecipeFromPreset("unknown").has_value());

    EditorCoordinator coordinator{makeProject()};
    GeneratedSfxEditorController controller{coordinator};
    const auto openedEmpty = controller.dispatch("open-generated-sfx", {}, {});
    CHECK(openedEmpty.handled && openedEmpty.refresh);
    CHECK(controller.viewModel().workspaceOpen);
    CHECK(!controller.viewModel().selectedId.has_value());
    CHECK(coordinator.document().data().generatedSfx.empty());
    CHECK(coordinator.document().data().audioAssets.empty());
    int previewRequests = 0;
    int stopRequests = 0;
    int generateRequests = 0;
    int dismissRequests = 0;
    std::string generatedId;
    controller.setGenerationHandlers(
        [&](const std::string&) { ++previewRequests; },
        [&] { ++stopRequests; },
        [&](const std::string& id) {
            ++generateRequests;
            generatedId = id;
        });
    // Empty open with a stale selection must stop preview audio.
    {
        EditorCoordinator withRecipe{makeProject()};
        CHECK(withRecipe.apply(
            CreateGeneratedSfxIntent{
                "generated-sfx-1", "Coin",
                *generatedSfxRecipeFromPreset("coin")}).ok);
        GeneratedSfxEditorController staleOpen{withRecipe};
        int staleStops = 0;
        staleOpen.setGenerationHandlers(
            [](const std::string&) {},
            [&] { ++staleStops; },
            [](const std::string&) {});
        CHECK(staleOpen.dispatch("open-generated-sfx", "generated-sfx-1", {}).refresh);
        CHECK(staleOpen.viewModel().selectedId == "generated-sfx-1");
        CHECK(withRecipe.execute(RemoveGeneratedSfxCommand{"generated-sfx-1"}).ok);
        const auto reopened = staleOpen.dispatch("open-generated-sfx", {}, {});
        CHECK(reopened.handled && reopened.refresh);
        CHECK(!staleOpen.viewModel().selectedId.has_value());
        CHECK(staleStops == 1);
    }
    controller.setDiagnosticHandler(
        [&](const std::string&) { ++dismissRequests; });
    controller.setCreateFromCurrentHandler(
        [&](const std::string& sourceId, const std::string& newId,
            const std::string& name) {
            const auto result = coordinator.apply(
                DuplicateGeneratedSfxIntent{sourceId, newId, name});
            if (result.ok) {
                ++generateRequests;
                generatedId = newId;
            }
            return result;
        });
    controller.setDeleteHandler(
        [&](const RemoveGeneratedSfxIntent& intent) {
            return coordinator.execute(RemoveGeneratedSfxCommand{intent.id});
        });
    controller.setProjectSavedQuery([] { return true; });
    controller.setGenerationAvailabilityQuery(
        [](const std::string&) { return GeneratedSfxGenerationAvailability{}; });

    const auto created = controller.dispatch(
        "create-generated-sfx", "coin", {});
    CHECK(created.handled && created.refresh);
    CHECK(coordinator.document().data().generatedSfx.size() == 1);
    const std::string sourceId =
        coordinator.document().data().generatedSfx.front().id;
    CHECK(controller.viewModel().selectedId == sourceId);
    CHECK(controller.viewModel().workspaceOpen);
    CHECK(controller.viewModel().visible);
    CHECK(controller.consumeFocusNameField());
    CHECK(!controller.consumeFocusNameField());

    CHECK(controller.dispatch("preview-generated-sfx", {}, {}).handled);
    CHECK(previewRequests == 1);
    CHECK(controller.dispatch("stop-generated-sfx-preview", {}, {}).handled);
    CHECK(stopRequests == 1);

    CHECK(controller.dispatch(
        "open-sfx-create-from-current", sourceId, {}).refresh);
    CHECK(controller.viewModel().createFromCurrentOpen);
    const auto duplicated = controller.confirmCreateFromCurrent("Coin Variant");
    CHECK(duplicated.handled && duplicated.deferRefresh);
    CHECK(coordinator.document().data().generatedSfx.size() == 2);
    CHECK(generateRequests == 1);
    CHECK(!generatedId.empty() && generatedId != sourceId);
    CHECK(controller.viewModel().selectedId == generatedId);
    CHECK(!controller.viewModel().createFromCurrentOpen);

    CHECK(coordinator.undo().ok);
    controller.reconcileDocument();
    CHECK(controller.viewModel().selectedId == sourceId);
    CHECK(controller.viewModel().workspaceOpen);
    CHECK(coordinator.redo().ok);
    controller.reconcileDocument();
    CHECK(controller.viewModel().selectedId == sourceId);
    CHECK(controller.dispatch("open-generated-sfx", generatedId, {}).refresh);
    CHECK(controller.viewModel().selectedId == generatedId);
    CHECK(controller.dispatch(
        "dismiss-sfx-generation-error", {}, {}).refresh);
    CHECK(dismissRequests == 1);

    const std::size_t countBeforePlay =
        coordinator.document().data().generatedSfx.size();
    CHECK(coordinator.playProject().ok);
    const auto playView = controller.viewModel();
    CHECK(!playView.visible);
    CHECK(!playView.allows(GeneratedSfxEditorAction::CreateFromPreset));
    CHECK(playView.allows(GeneratedSfxEditorAction::StopPreview));
    const std::uint64_t playRevision = coordinator.document().revision();
    CHECK(!controller.changeMacro("duration", 0.5f).handled);
    CHECK(coordinator.document().revision() == playRevision);
    const auto blocked = controller.dispatch(
        "create-generated-sfx", "jump", {});
    CHECK(blocked.handled && !blocked.refresh);
    CHECK(coordinator.document().data().generatedSfx.size() == countBeforePlay);
    CHECK(coordinator.stopPlaying().ok);

    CHECK(controller.dispatch(
        "remove-generated-sfx", generatedId, {}).refresh);
    CHECK(controller.viewModel().selectedId == sourceId);
    CHECK(controller.dispatch(
        "remove-generated-sfx", sourceId, {}).refresh);
    CHECK(coordinator.document().data().generatedSfx.empty());
    CHECK(!controller.viewModel().selectedId.has_value());
    CHECK(controller.viewModel().workspaceOpen);
    CHECK(controller.viewModel().visible);
    CHECK(!controller.dispatch("toggle-sfx-more-menu", {}, {}).refresh);

    CHECK(controller.dispatch("close-generated-sfx", {}, {}).refresh);
    CHECK(!controller.viewModel().workspaceOpen);
    CHECK(!controller.viewModel().visible);

    controller.detach();
    std::cout << "generated-sfx-editor-controller-test: "
              << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
