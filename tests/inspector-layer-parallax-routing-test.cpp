// ============================================================================
// ADR-0056 — Layer Manager parallax fields: render + hierarchy-action routing.
//
// Starts at real RmlUi Inspector elements (same discipline as
// inspector-layer-dropdown-keyboard-test): fields and Reset are found by
// data-action, commits go through EditorUi::handleAction, and Play must
// disable the affordance without mutating ProjectDocument.
// ============================================================================

#include "editor-native/app/editor_coordinator.h"
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

bool isDisabled(Rml::Element* element) {
    return element && element->HasAttribute("disabled");
}

std::string fieldValue(Rml::Element* element) {
    if (!element) return {};
    auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element);
    return control ? std::string(control->GetValue()) : std::string();
}

ProjectDoc makeParallaxProject() {
    // Mirror makeDoc() shape so Play's canonical AssetLoader accepts the
    // package; layer-bg is locked to cover ADR-0056 "locked still editable".
    ProjectDoc doc;
    doc.formatVersion = 12;
    doc.projectName = "Layer Parallax Routing";
    doc.activeSceneId = "scene-1";

    EntityDef hero;
    hero.className = "Hero";
    hero.name = "Hero";
    doc.objectTypes.emplace("Hero", std::move(hero));

    SceneDef scene;
    scene.id = "scene-1";
    scene.name = "Scene 1";
    scene.backgroundColor = {0.1f, 0.1f, 0.1f, 1.f};
    scene.worldSize = {512.f, 320.f};
    scene.viewportSize = {512.f, 320.f};
    scene.defaultLayerId = "layer-1";
    scene.layers.push_back(SceneLayerDef{"layer-bg", "Background", true});
    scene.layers.push_back(SceneLayerDef{"layer-1", "Main", false});
    SceneLayerSettings bg;
    bg.parallax = {0.35f, 0.6f};
    scene.layerSettings.emplace("layer-bg", std::move(bg));
    SceneInstanceDef instance;
    instance.id = 1;
    instance.objectTypeId = "Hero";
    instance.layerId = "layer-1";
    instance.transform.position = {10.f, 20.f};
    scene.instances.push_back(instance);

    doc.scenes.emplace(scene.id, std::move(scene));
    return doc;
}

} // namespace

int main() {
    NullRenderInterface render;
    Rml::SetRenderInterface(&render);
    if (!Rml::Initialise()) {
        std::cerr << "FAIL Rml::Initialise()\n";
        return 1;
    }

    Rml::Context* context =
        Rml::CreateContext("inspector-layer-parallax-test", Rml::Vector2i(1600, 900));
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

    EditorCoordinator coordinator{makeParallaxProject()};
    CHECK(coordinator.apply(SetActiveLayerIntent{"scene-1", "layer-bg"}).ok);
    EditorUi ui{coordinator, document, nullptr, nullptr};
    ui.bind();
    frame(*context, ui);

    Rml::Element* xField = findAction(document, "commit-layer-parallax-x");
    Rml::Element* yField = findAction(document, "commit-layer-parallax-y");
    Rml::Element* reset = findAction(document, "reset-layer-parallax");
    CHECK(xField != nullptr);
    CHECK(yField != nullptr);
    CHECK(reset != nullptr);
    CHECK(!isDisabled(xField));
    CHECK(!isDisabled(yField));
    // Locked Background layer remains authorable for parallax.
    CHECK(coordinator.document().isLayerLocked("scene-1", "layer-bg"));
    CHECK(fieldValue(xField).find("0.35") != std::string::npos
          || fieldValue(xField).find("0.3") != std::string::npos);

    // Invalid input: no command / no revision change.
    const uint64_t revBeforeInvalid = coordinator.document().revision();
    const std::size_t undoBeforeInvalid = coordinator.undoSize();
    ui.handleAction("commit-layer-parallax-x", "", "NaN");
    frame(*context, ui);
    CHECK(coordinator.document().revision() == revBeforeInvalid);
    CHECK(coordinator.undoSize() == undoBeforeInvalid);
    CHECK(std::abs(coordinator.document().effectiveLayerSettings("scene-1", "layer-bg")
                       .parallax.x
                   - 0.35f)
          < 1e-5f);

    // Valid X commit is one undoable command (Y preserved).
    ui.handleAction("commit-layer-parallax-x", "", "0.5");
    frame(*context, ui);
    CHECK(std::abs(coordinator.document().effectiveLayerSettings("scene-1", "layer-bg")
                       .parallax.x
                   - 0.5f)
          < 1e-5f);
    CHECK(std::abs(coordinator.document().effectiveLayerSettings("scene-1", "layer-bg")
                       .parallax.y
                   - 0.6f)
          < 1e-5f);
    CHECK(coordinator.canUndo());

    // Reset emits one command back to {1,1} and erases the sparse entry.
    const std::size_t undoBeforeReset = coordinator.undoSize();
    ui.handleAction("reset-layer-parallax", "", "");
    frame(*context, ui);
    CHECK(coordinator.undoSize() == undoBeforeReset + 1);
    CHECK(coordinator.document().findScene("scene-1")->layerSettings.count("layer-bg")
          == 0);
    CHECK(coordinator.document().effectiveLayerSettings("scene-1", "layer-bg").parallax.x
          == 1.f);

    // Default layer is editable.
    ui.handleAction("select-layer", "layer-1", "");
    frame(*context, ui);
    xField = findAction(document, "commit-layer-parallax-x");
    CHECK(xField != nullptr);
    ui.handleAction("commit-layer-parallax-y", "", "0.75");
    frame(*context, ui);
    CHECK(std::abs(coordinator.document().effectiveLayerSettings("scene-1", "layer-1")
                       .parallax.y
                   - 0.75f)
          < 1e-5f);

    // Play: fields disabled; hierarchy action must not mutate (coordinator gate).
    CHECK(coordinator.playProject().ok);
    frame(*context, ui);
    xField = findAction(document, "commit-layer-parallax-x");
    yField = findAction(document, "commit-layer-parallax-y");
    reset = findAction(document, "reset-layer-parallax");
    CHECK(isDisabled(xField));
    CHECK(isDisabled(yField));
    CHECK(reset == nullptr); // Reset button omitted while playing.
    const uint64_t revPlay = coordinator.document().revision();
    ui.handleAction("commit-layer-parallax-x", "", "0.11");
    frame(*context, ui);
    CHECK(coordinator.document().revision() == revPlay);
    CHECK(coordinator.stopPlaying().ok);

    if (failed > 0) {
        std::cerr << "\n" << passed << " passed, " << failed << " failed\n";
    } else {
        std::cout << "inspector-layer-parallax-routing-test: " << passed << " passed, "
                  << failed << " failed\n";
    }
    ui.detach();
    Rml::Shutdown();
    return failed > 0 ? 1 : 0;
}
