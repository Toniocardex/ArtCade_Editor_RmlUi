// ============================================================================
// ADR-0031 A2 — Object Variables Inspector rendering and dispatch.
//
// This suite starts at real RmlUi elements, crosses EditorUi's single listener,
// and verifies both the rebuilt Inspector tree and the authoritative document.
// Calling InspectorPanel::refresh() or EditorUi::handleAction() directly would
// sit below the two seams that already regressed:
//   - section-control markers escaped into visible Inspector text;
//   - the per-variable type dropdown closed in the frame that opened it.
//
// Authority / lifecycle contract:
//   - dropdown state and rendered markup are panel-local presentation;
//   - edits dispatch one existing ObjectVariable Command and are undoable;
//   - definition defaults and instance overrides remain separate authorities;
//   - Play renders the authoring controls disabled and dispatch mutates nothing.
// ============================================================================

#include "editor-native/app/editor_coordinator.h"
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

void collectByClass(Rml::Element* root, const std::string& className,
                    std::vector<Rml::Element*>& out) {
    if (!root) return;
    if (root->IsClassSet(className)) out.push_back(root);
    for (int i = 0; i < root->GetNumChildren(); ++i)
        collectByClass(root->GetChild(i), className, out);
}

void click(Rml::Element* element) {
    CHECK(element != nullptr);
    if (!element) return;
    Rml::Dictionary parameters;
    element->DispatchEvent(Rml::EventId::Click, parameters);
}

void commitWithEnter(Rml::Context& context, Rml::Element* element,
                     const std::string& value) {
    CHECK(element != nullptr);
    if (!element) return;
    auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(element);
    CHECK(control != nullptr);
    if (!control) return;
    CHECK(element->Focus());
    context.Update();
    control->SetValue(value);
    Rml::Dictionary parameters;
    parameters["key_identifier"] = static_cast<int>(Rml::Input::KI_RETURN);
    element->DispatchEvent(Rml::EventId::Keydown, parameters);
}

ProjectDoc makeObjectVariablesProject() {
    ProjectDoc doc;
    doc.formatVersion = 11;
    doc.projectName = "Inspector Object Variables";

    EntityDef hero;
    hero.name = "Hero";
    hero.className = "Hero";
    hero.spriteRenderer = SpriteRendererComponent{{}, true};
    hero.localVariables.push_back(GameVariableDefinition{
        "Health", GameVariableDefinition::Type::Number,
        GameVariableValue{100.0}, "Starting health"});
    hero.localVariables.push_back(GameVariableDefinition{
        "Collected", GameVariableDefinition::Type::Boolean,
        GameVariableValue{false}, "Whether this item was collected"});
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
    instance.instanceName = "Hero 1";
    instance.layerId = "layer-1";
    instance.localVariableOverrides.emplace("Collected", GameVariableValue{true});
    scene.instances.push_back(instance);
    scene.entityIds.push_back(1);
    doc.scenes.emplace(scene.id, scene);
    doc.activeSceneId = scene.id;
    return doc;
}

const GameVariableDefinition* variable(const EditorCoordinator& coordinator,
                                       const std::string& key) {
    const EntityDef* type = coordinator.document().findObjectType("Hero");
    if (!type) return nullptr;
    for (const GameVariableDefinition& candidate : type->localVariables)
        if (candidate.key == key) return &candidate;
    return nullptr;
}

const SceneInstanceDef* selectedInstance(const EditorCoordinator& coordinator) {
    return coordinator.document().findInstanceInScene("scene-1", 1);
}

void testRenderedSectionHasNoControlMarkers(Rml::ElementDocument& document) {
    Rml::Element* body = document.GetElementById("inspector-body");
    CHECK(body != nullptr);
    if (!body) return;

    CHECK(document.GetElementById("inspector-section-object-variables-toggle") != nullptr);
    CHECK(findAction(body, "commit-object-variable-key", "Health") != nullptr);
    CHECK(findAction(body, "commit-object-variable-default", "Health") != nullptr);
    CHECK(findAction(body, "override-instance-variable", "Health") != nullptr);
    CHECK(findAction(body, "toggle-instance-variable-override", "Collected") != nullptr);
    CHECK(findAction(body, "add-object-variable") != nullptr);

    // These strings are an internal control language for collapsing sections.
    // If either survives finalization, RmlUi creates a visible text node.
    const std::string rendered = body->GetInnerRML();
    CHECK(rendered.find("[[inspector-section:") == std::string::npos);
    CHECK(rendered.find("[[inspector-body:") == std::string::npos);
    CHECK(rendered.find("[[inspector-sections-end]]") == std::string::npos);
    CHECK(rendered.find("Defaults belong to the Object Type") != std::string::npos);
    CHECK(rendered.find("Uses the shared value") != std::string::npos);
}

void testTypeDropdownSurvivesItsOpeningFrame(
    Rml::Context& context, Rml::ElementDocument& document,
    EditorCoordinator& coordinator, EditorUi& ui) {
    const uint64_t revisionBefore = coordinator.document().revision();
    const bool undoBefore = coordinator.canUndo();

    Rml::Element* trigger = findAction(
        &document, "toggle-inspector-dropdown", "object-variable-type|Health");
    click(trigger);
    trigger = nullptr; // opening rebuilds the Inspector
    frame(context, ui);

    // Opening is presentation-only, and the list must still exist after the
    // complete application frame rather than being dismissed by the same click.
    CHECK(coordinator.document().revision() == revisionBefore);
    CHECK(coordinator.canUndo() == undoBefore);
    Rml::Element* stringEntry =
        findAction(&document, "set-object-variable-type", "string|Health");
    CHECK(stringEntry != nullptr);
    std::vector<Rml::Element*> lists;
    collectByClass(&document, "drop-list", lists);
    CHECK(!lists.empty());

    click(stringEntry);
    stringEntry = nullptr;
    frame(context, ui);

    const GameVariableDefinition* health = variable(coordinator, "Health");
    CHECK(health != nullptr);
    CHECK(health && health->type == GameVariableDefinition::Type::String);
    CHECK(health && std::get_if<std::string>(&health->initialValue) != nullptr);
    CHECK(findAction(&document, "set-object-variable-type", "number|Health") == nullptr);

    // One pick is one undo entry. Undo restores both the type and its value.
    CHECK(coordinator.undo().ok);
    frame(context, ui);
    health = variable(coordinator, "Health");
    CHECK(health != nullptr);
    CHECK(health && health->type == GameVariableDefinition::Type::Number);
    CHECK(health && std::get<double>(health->initialValue) == 100.0);
}

void testDefaultAndOverrideDispatchStaySeparate(
    Rml::Context& context, Rml::ElementDocument& document,
    EditorCoordinator& coordinator, EditorUi& ui) {
    Rml::Element* defaultField =
        findAction(&document, "commit-object-variable-default", "Health");
    commitWithEnter(context, defaultField, "125.5");
    defaultField = nullptr;
    frame(context, ui);

    const GameVariableDefinition* health = variable(coordinator, "Health");
    const SceneInstanceDef* instance = selectedInstance(coordinator);
    CHECK(health && std::get<double>(health->initialValue) == 125.5);
    CHECK(instance && instance->localVariableOverrides.count("Health") == 0);

    // Creating the override seeds it from the shared value without changing
    // the Object Type default.
    click(findAction(&document, "override-instance-variable", "Health"));
    frame(context, ui);
    health = variable(coordinator, "Health");
    instance = selectedInstance(coordinator);
    CHECK(health && std::get<double>(health->initialValue) == 125.5);
    CHECK(instance && std::get<double>(
        instance->localVariableOverrides.at("Health")) == 125.5);

    Rml::Element* overrideField =
        findAction(&document, "commit-instance-variable-override", "Health");
    commitWithEnter(context, overrideField, "80");
    overrideField = nullptr;
    frame(context, ui);
    health = variable(coordinator, "Health");
    instance = selectedInstance(coordinator);
    CHECK(health && std::get<double>(health->initialValue) == 125.5);
    CHECK(instance && std::get<double>(
        instance->localVariableOverrides.at("Health")) == 80.0);

    // One undo reverts only the last dispatch: the override returns to its
    // seeded value and the shared default is still untouched.
    CHECK(coordinator.undo().ok);
    frame(context, ui);
    health = variable(coordinator, "Health");
    instance = selectedInstance(coordinator);
    CHECK(health && std::get<double>(health->initialValue) == 125.5);
    CHECK(instance && std::get<double>(
        instance->localVariableOverrides.at("Health")) == 125.5);

    click(findAction(&document, "reset-instance-variable-override", "Health"));
    frame(context, ui);
    instance = selectedInstance(coordinator);
    CHECK(instance && instance->localVariableOverrides.count("Health") == 0);
    CHECK(findAction(&document, "override-instance-variable", "Health") != nullptr);
}

void testAddRenameAndDescriptionDispatch(
    Rml::Context& context, Rml::ElementDocument& document,
    EditorCoordinator& coordinator, EditorUi& ui) {
    click(findAction(&document, "add-object-variable"));
    frame(context, ui);
    CHECK(variable(coordinator, "Variable") != nullptr);

    Rml::Element* name = findAction(&document, "commit-object-variable-key", "Variable");
    commitWithEnter(context, name, "Speed");
    name = nullptr;
    frame(context, ui);
    CHECK(variable(coordinator, "Variable") == nullptr);
    CHECK(variable(coordinator, "Speed") != nullptr);

    Rml::Element* description =
        findAction(&document, "commit-object-variable-description", "Speed");
    commitWithEnter(context, description, "Movement speed");
    description = nullptr;
    frame(context, ui);
    CHECK(variable(coordinator, "Speed")
          && variable(coordinator, "Speed")->description == "Movement speed");
}

void testPlayDisablesAndGuardsAuthoring(
    Rml::Context& context, Rml::ElementDocument& document,
    EditorCoordinator& coordinator, EditorUi& ui) {
    const uint64_t revisionBefore = coordinator.document().revision();
    CHECK(coordinator.playCurrentScene().ok);
    frame(context, ui);
    CHECK(coordinator.isPlaying());

    Rml::Element* add = findAction(&document, "add-object-variable");
    CHECK(add != nullptr);
    CHECK(add && add->IsClassSet("disabled"));
    const std::size_t countBefore =
        coordinator.document().findObjectType("Hero")->localVariables.size();
    click(add);
    frame(context, ui);
    CHECK(coordinator.document().revision() == revisionBefore);
    CHECK(coordinator.document().findObjectType("Hero")->localVariables.size() == countBefore);

    Rml::Element* healthName =
        findAction(&document, "commit-object-variable-key", "Health");
    CHECK(healthName != nullptr);
    CHECK(healthName && healthName->HasAttribute("disabled"));

    CHECK(coordinator.stopPlaying().ok);
    frame(context, ui);
}

} // namespace

int main() {
    NullRenderInterface render;
    Rml::SetRenderInterface(&render);
    if (!Rml::Initialise()) {
        std::cerr << "FAIL Rml::Initialise()\n";
        return 1;
    }

    Rml::Context* context = Rml::CreateContext("inspector-test", Rml::Vector2i(1600, 900));
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

    EditorCoordinator coordinator{makeObjectVariablesProject()};
    CHECK(coordinator.apply(SelectEntityIntent{1}).ok);
    EditorUi ui{coordinator, document, nullptr, nullptr};
    ui.bind();
    frame(*context, ui);

    testRenderedSectionHasNoControlMarkers(*document);
    testTypeDropdownSurvivesItsOpeningFrame(*context, *document, coordinator, ui);
    testDefaultAndOverrideDispatchStaySeparate(*context, *document, coordinator, ui);
    testAddRenameAndDescriptionDispatch(*context, *document, coordinator, ui);
    testPlayDisablesAndGuardsAuthoring(*context, *document, coordinator, ui);

    ui.detach();
    Rml::Shutdown();

    std::cout << "passed " << passed << " failed " << failed << "\n";
    return failed == 0 ? 0 : 1;
}
