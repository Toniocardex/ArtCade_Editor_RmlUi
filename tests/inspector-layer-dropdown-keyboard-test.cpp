// ============================================================================
// ADR-0034 spike — Inspector Layer dropdown keyboard navigation.
//
// This suite starts at the real rendered `.drop-trigger`/`.drop-entry`
// elements and crosses EditorUi's single listener, the same discipline as
// inspector-object-variables-routing-test.cpp: Up/Down/Enter/Escape are
// dispatched as real RmlUi keydown events, not calls into InspectorPanel or
// EditorUi::handleAction() directly, since the seam under test IS that
// listener (deferred pending state, StopPropagation, processFrame consumption).
//
// Authority / lifecycle contract:
//   - the highlight index and navigable-entry list are panel-local presentation
//     (InspectorPanel), never the document;
//   - Up/Down/Escape never touch the ProjectDocument (no revision change);
//   - Enter dispatches the exact same (action, arg) pair a click on that
//     .drop-entry already uses (SetEntityLayerCommand), so it is undoable
//     exactly like a mouse pick.
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

void pressKey(Rml::Element* element, Rml::Input::KeyIdentifier key) {
    CHECK(element != nullptr);
    if (!element) return;
    Rml::Dictionary parameters;
    parameters["key_identifier"] = static_cast<int>(key);
    element->DispatchEvent(Rml::EventId::Keydown, parameters);
}

ProjectDoc makeTwoLayerProject() {
    ProjectDoc doc;
    doc.formatVersion = 11;
    doc.projectName = "Layer Dropdown Keyboard";

    EntityDef hero;
    hero.name = "Hero";
    hero.className = "Hero";
    hero.spriteRenderer = SpriteRendererComponent{{}, true};
    doc.objectTypes.emplace("Hero", std::move(hero));

    SceneDef scene;
    scene.id = "scene-1";
    scene.name = "Scene 1";
    scene.worldSize = {512.f, 320.f};
    scene.defaultLayerId = "layer-1";
    scene.layers.push_back(SceneLayerDef{"layer-1", "Layer 1"});
    scene.layers.push_back(SceneLayerDef{"layer-2", "Layer 2"});

    SceneInstanceDef instance;
    instance.id = 1;
    instance.objectTypeId = "Hero";
    instance.instanceName = "Hero 1";
    instance.layerId = "layer-2";
    scene.instances.push_back(instance);
    scene.entityIds.push_back(1);

    doc.scenes.emplace(scene.id, scene);
    doc.activeSceneId = scene.id;
    return doc;
}

std::string currentLayerId(const EditorCoordinator& coordinator) {
    const SceneInstanceDef* inst =
        coordinator.document().findInstanceInScene("scene-1", 1);
    return inst ? inst->layerId : std::string();
}

} // namespace

int main() {
    NullRenderInterface render;
    Rml::SetRenderInterface(&render);
    if (!Rml::Initialise()) {
        std::cerr << "FAIL Rml::Initialise()\n";
        return 1;
    }

    Rml::Context* context = Rml::CreateContext("inspector-layer-dropdown-test", Rml::Vector2i(1600, 900));
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

    EditorCoordinator coordinator{makeTwoLayerProject()};
    CHECK(coordinator.apply(SelectEntityIntent{1}).ok);
    EditorUi ui{coordinator, document, nullptr, nullptr};
    ui.bind();
    frame(*context, ui);

    // -- Opening: trigger is tab-focusable, entries render, nothing highlighted yet.
    // refresh() rebuilds #inspector-body's markup on every dropdown/highlight
    // change (toggleDropdown, moveDropdownHighlight, closeDropdowns all call
    // it), which destroys and recreates the trigger/entry elements — so every
    // element pointer must be re-fetched after each frame(), never reused
    // across one, exactly as real RmlUi usage would require.
    auto findSelectedEntry = [&]() -> Rml::Element* {
        std::vector<Rml::Element*> stack{document};
        while (!stack.empty()) {
            Rml::Element* e = stack.back();
            stack.pop_back();
            if (e->IsClassSet("drop-entry") && e->IsClassSet("selected")) return e;
            for (int i = 0; i < e->GetNumChildren(); ++i) stack.push_back(e->GetChild(i));
        }
        return nullptr;
    };

    Rml::Element* trigger = findAction(document, "toggle-inspector-dropdown", "layer");
    CHECK(trigger != nullptr);
    CHECK(trigger && trigger->IsClassSet("kbd-nav"));
    CHECK(ui.hasOpenContextMenu() == false); // ADR-0034 gap fix baseline: closed, so false.
    click(trigger);
    frame(*context, ui);
    CHECK(ui.hasOpenContextMenu() == true); // gap fix: an open dropdown counts as an open popup.

    Rml::Element* toLayer1 = findAction(document, "set-entity-layer", "layer-1");
    Rml::Element* toCurrent = findSelectedEntry();
    CHECK(toLayer1 != nullptr);
    CHECK(toCurrent != nullptr);
    CHECK(toLayer1 && !toLayer1->IsClassSet("highlighted"));
    CHECK(toCurrent && !toCurrent->IsClassSet("highlighted"));

    const std::uint64_t revisionAtOpen = coordinator.document().revision();

    // -- ArrowDown from unset starts adjacent to the current entry (layer-2)
    //    and highlights layer-1, without touching the document.
    trigger = findAction(document, "toggle-inspector-dropdown", "layer");
    pressKey(trigger, Rml::Input::KI_DOWN);
    frame(*context, ui);
    toLayer1 = findAction(document, "set-entity-layer", "layer-1");
    CHECK(toLayer1 && toLayer1->IsClassSet("highlighted"));
    CHECK(coordinator.document().revision() == revisionAtOpen);
    CHECK(currentLayerId(coordinator) == "layer-2");

    // -- ArrowUp moves the highlight back onto the current entry (wraps).
    trigger = findAction(document, "toggle-inspector-dropdown", "layer");
    pressKey(trigger, Rml::Input::KI_UP);
    frame(*context, ui);
    toLayer1 = findAction(document, "set-entity-layer", "layer-1");
    CHECK(toLayer1 && !toLayer1->IsClassSet("highlighted"));
    toCurrent = findSelectedEntry();
    CHECK(toCurrent && toCurrent->IsClassSet("highlighted"));
    CHECK(coordinator.document().revision() == revisionAtOpen);

    // -- Escape closes without committing: still on layer-2, dropdown closed.
    trigger = findAction(document, "toggle-inspector-dropdown", "layer");
    pressKey(trigger, Rml::Input::KI_ESCAPE);
    frame(*context, ui);
    CHECK(currentLayerId(coordinator) == "layer-2");
    CHECK(coordinator.document().revision() == revisionAtOpen);
    CHECK(ui.hasOpenContextMenu() == false);
    CHECK(findAction(document, "set-entity-layer", "layer-1") == nullptr); // list collapsed

    // -- Reopen, ArrowDown to layer-1, Enter commits it (undoable, like a click).
    trigger = findAction(document, "toggle-inspector-dropdown", "layer");
    click(trigger);
    frame(*context, ui);
    trigger = findAction(document, "toggle-inspector-dropdown", "layer");
    pressKey(trigger, Rml::Input::KI_DOWN);
    frame(*context, ui);
    trigger = findAction(document, "toggle-inspector-dropdown", "layer");
    pressKey(trigger, Rml::Input::KI_RETURN);
    frame(*context, ui);
    CHECK(currentLayerId(coordinator) == "layer-1");
    CHECK(coordinator.document().revision() != revisionAtOpen);
    CHECK(coordinator.canUndo());
    CHECK(ui.hasOpenContextMenu() == false); // Enter's commit closes the dropdown too.

    CHECK(coordinator.undo().ok);
    frame(*context, ui);
    CHECK(currentLayerId(coordinator) == "layer-2");

    if (failed > 0) {
        std::cerr << "\n" << passed << " passed, " << failed << " failed\n";
    } else {
        std::cout << "inspector-layer-dropdown-keyboard-test: " << passed << " passed, "
                  << failed << " failed\n";
    }
    ui.detach();
    Rml::Shutdown();
    return failed > 0 ? 1 : 0;
}
