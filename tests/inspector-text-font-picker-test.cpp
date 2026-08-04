// ============================================================================
// ADR-0036 â€” Text component Font picker.
//
// Same real-element/listener discipline as inspector-layer-dropdown-keyboard-
// test.cpp (ADR-0034): starts at the rendered trigger/entries, crosses
// EditorUi's single listener, and re-fetches every element after each
// frame() (refresh() rebuilds #inspector-body on every pick).
//
// Scope: the picker's data plumbing (set-text-font -> TextComponent.fontPath
// via the existing SetObjectTypeTextComponentCommand, Undo, "Default Font"
// entry) - not a second pass over DropdownNavigation's keyboard mechanics,
// already covered by inspector-layer-dropdown-keyboard-test.cpp and
// logic-board-dropdown-keyboard-test.cpp.
// ============================================================================

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/model/project_document.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace ArtCade;
using namespace ArtCade::EditorNative;

static int passed = 0;
static int failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

namespace {

class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
                        Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String&) override {
        dimensions = Rml::Vector2i(1, 1);
        return 1;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
        return 1;
    }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

void frame(Rml::Context& context, EditorUi& ui) {
    ui.processFrame();
    context.Update();
    ui.restoreAfterRmlLayout();
}

std::string attributeOf(Rml::Element* element, const char* name) {
    return element
        ? element->GetAttribute<Rml::String>(name, Rml::String())
        : std::string();
}

Rml::Element* findAction(Rml::Element* root, const std::string& action,
                         const std::string& arg = {}) {
    if (!root) return nullptr;
    if (attributeOf(root, "data-action") == action
        && (arg.empty() || attributeOf(root, "data-arg") == arg)) {
        return root;
    }
    for (int i = 0; i < root->GetNumChildren(); ++i) {
        if (Rml::Element* hit = findAction(root->GetChild(i), action, arg)) return hit;
    }
    return nullptr;
}

void click(Rml::Element* element) {
    CHECK(element != nullptr);
    if (!element) return;
    Rml::Dictionary parameters;
    element->DispatchEvent(Rml::EventId::Click, parameters);
}

ProjectDoc makeTextFontProject() {
    ProjectDoc doc;
    doc.formatVersion = 14;
    doc.projectName = "Text Font Picker";

    doc.fontAssets.push_back(FontAssetDef{"font-ui", "UI Font", "fonts/ui.ttf", 32, {}});
    doc.fontAssets.push_back(FontAssetDef{"font-title", "Title Font", "fonts/title.ttf", 32, {}});

    EntityDef hero;
    hero.name = "Hero";
    hero.className = "Hero";
    TextComponent text;
    text.text = "Score";
    text.size = 24;
    text.align = "top-left";
    text.format = "text";
    text.bindScope = "global";
    hero.text = text;
    doc.objectTypes.emplace("Hero", std::move(hero));

    SceneDef scene;
    scene.id = "scene-1";
    scene.name = "Scene 1";
    scene.worldSize = {512.f, 320.f};
    scene.defaultLayerId = "layer-1";
    scene.layers.push_back(SceneLayerDef{"layer-1", "Layer 1"});
    SceneInstanceDef instance;
    instance.id = 1;
    instance.objectTypeId = "Hero";
    instance.layerId = "layer-1";
    scene.instances.push_back(instance);
    scene.entityIds.push_back(1);
    doc.scenes.emplace(scene.id, scene);
    doc.activeSceneId = scene.id;
    return doc;
}

std::string currentFontPath(const EditorCoordinator& coordinator) {
    const EntityDef* type = coordinator.document().findObjectType("Hero");
    return (type && type->text) ? type->text->fontPath : std::string();
}

} // namespace

int main() {
    NullRenderInterface render;
    Rml::SetRenderInterface(&render);
    if (!Rml::Initialise()) {
        std::cerr << "FAIL Rml::Initialise()\n";
        return 1;
    }

    Rml::Context* context = Rml::CreateContext("text-font-picker-test", Rml::Vector2i(1600, 900));
    if (!context) {
        std::cerr << "FAIL Rml::CreateContext()\n";
        Rml::Shutdown();
        return 1;
    }

    const std::filesystem::path fonts =
        std::filesystem::path(ARTCADE_UI_RESOURCE_DIR).parent_path() / "fonts" / "inter";
    for (const char* face : {"Inter-Regular.ttf", "Inter-Medium.ttf",
                             "Inter-SemiBold.ttf", "Inter-Bold.ttf"}) {
        if (!Rml::LoadFontFace((fonts / face).string())) {
            std::cerr << "FAIL LoadFontFace " << face << "\n";
            ++failed;
        }
    }

    const std::filesystem::path shell =
        std::filesystem::path(ARTCADE_UI_RESOURCE_DIR) / "editor_shell.rml";
    Rml::ElementDocument* document = context->LoadDocument(shell.string());
    if (!document) {
        std::cerr << "FAIL LoadDocument " << shell.string() << "\n";
        Rml::Shutdown();
        return 1;
    }
    document->Show();
    context->Update();

    EditorCoordinator coordinator{makeTextFontProject()};
    CHECK(coordinator.apply(SelectEntityIntent{1}).ok);
    EditorUi ui{coordinator, document, nullptr, nullptr};
    ui.bind();
    frame(*context, ui);

    CHECK(currentFontPath(coordinator).empty()); // starts on Default Font

    // -- Open the Font picker: lists Default Font + both font assets.
    Rml::Element* trigger = findAction(document, "toggle-inspector-dropdown", "text-font");
    CHECK(trigger != nullptr);
    click(trigger);
    frame(*context, ui);
    CHECK(findAction(document, "set-text-font", "fonts/ui.ttf") != nullptr);
    CHECK(findAction(document, "set-text-font", "fonts/title.ttf") != nullptr);

    // -- Pick "UI Font": commits via the existing SetObjectTypeTextComponentCommand.
    const std::uint64_t revisionBefore = coordinator.document().revision();
    Rml::Element* uiFontEntry = findAction(document, "set-text-font", "fonts/ui.ttf");
    click(uiFontEntry);
    frame(*context, ui);
    CHECK(currentFontPath(coordinator) == "fonts/ui.ttf");
    CHECK(coordinator.document().revision() != revisionBefore);
    CHECK(coordinator.canUndo());
    // Pick closes the dropdown, matching every other Inspector picker.
    CHECK(findAction(document, "set-text-font", "fonts/title.ttf") == nullptr);

    CHECK(coordinator.undo().ok);
    frame(*context, ui);
    CHECK(currentFontPath(coordinator).empty());

    // -- Reopen, pick "Title Font", then pick "Default Font" to clear it.
    trigger = findAction(document, "toggle-inspector-dropdown", "text-font");
    click(trigger);
    frame(*context, ui);
    Rml::Element* titleFontEntry = findAction(document, "set-text-font", "fonts/title.ttf");
    click(titleFontEntry);
    frame(*context, ui);
    CHECK(currentFontPath(coordinator) == "fonts/title.ttf");

    trigger = findAction(document, "toggle-inspector-dropdown", "text-font");
    click(trigger);
    frame(*context, ui);
    Rml::Element* defaultFontEntry = findAction(document, "set-text-font", "");
    CHECK(defaultFontEntry != nullptr);
    click(defaultFontEntry);
    frame(*context, ui);
    CHECK(currentFontPath(coordinator).empty());

    // -- Play disables every Text field, this row included: a disabled
    //    dropdownTrigger() carries no data-action at all (dropdownTriggerMarkup's
    //    established convention - same as every other disabled Inspector
    //    picker in this codebase), so it is unreachable by data-action the
    //    same way "set-text-align"/"set-text-format" already are.
    CHECK(coordinator.playCurrentScene().ok);
    frame(*context, ui);
    trigger = findAction(document, "toggle-inspector-dropdown", "text-font");
    CHECK(trigger == nullptr);
    CHECK(findAction(document, "toggle-inspector-dropdown", "text-align") == nullptr);
    CHECK(coordinator.stopPlaying().ok);
    frame(*context, ui);

    if (failed > 0) {
        std::cerr << "\n" << passed << " passed, " << failed << " failed\n";
    } else {
        std::cout << "inspector-text-font-picker-test: " << passed << " passed, "
                  << failed << " failed\n";
    }
    ui.detach();
    Rml::Shutdown();
    return failed > 0 ? 1 : 0;
}
