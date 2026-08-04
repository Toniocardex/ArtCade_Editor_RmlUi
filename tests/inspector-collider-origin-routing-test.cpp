// ADR-0058 — real Inspector markup and action routing for Entity Origin presets.
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/model/box_collider_view.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core.h>
#include <filesystem>
#include <iostream>

using namespace ArtCade;
using namespace ArtCade::EditorNative;

static int passed = 0, failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

namespace {
class NullRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
                        Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& d, const Rml::String&) override {
        d = {1, 1}; return 1;
    }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};
void frame(Rml::Context& context, EditorUi& ui) {
    ui.processFrame(); context.Update(); ui.restoreAfterRmlLayout();
}
std::string attr(Rml::Element* e, const char* name) {
    return e ? e->GetAttribute<Rml::String>(name, {}) : std::string{};
}
Rml::Element* findAction(Rml::Element* root, const std::string& action,
                         const std::string& arg = {}) {
    if (!root) return nullptr;
    if (attr(root, "data-action") == action
        && (arg.empty() || attr(root, "data-arg") == arg)) return root;
    for (int i = 0; i < root->GetNumChildren(); ++i) {
        if (auto* hit = findAction(root->GetChild(i), action, arg)) return hit;
    }
    return nullptr;
}
ProjectDoc makeProject() {
    ProjectDoc doc;
    doc.formatVersion = 14;
    doc.projectName = "Collider Origin UI";
    doc.activeSceneId = "scene";
    EntityDef type;
    type.className = "Solid"; type.name = "Solid";
    BoxCollider2DComponent collider;
    collider.offset = {3.f, -2.f}; collider.size = {20.f, 10.f};
    type.boxCollider2D = collider;
    doc.objectTypes.emplace("Solid", std::move(type));
    SceneDef scene;
    scene.id = "scene"; scene.name = "Scene";
    scene.layers.push_back({"main", "Main", false});
    scene.defaultLayerId = "main";
    SceneInstanceDef instance;
    instance.id = 1; instance.objectTypeId = "Solid"; instance.layerId = "main";
    instance.transform.position = {100.f, 80.f}; instance.transform.scale = {2.f, 3.f};
    scene.instances.push_back(instance);
    doc.scenes.emplace(scene.id, std::move(scene));
    return doc;
}
} // namespace

int main() {
    NullRenderInterface render;
    Rml::SetRenderInterface(&render);
    CHECK(Rml::Initialise());
    Rml::Context* context = Rml::CreateContext("collider-origin-ui", {1600, 900});
    CHECK(context != nullptr);
    const std::filesystem::path fonts =
        std::filesystem::path(ARTCADE_UI_RESOURCE_DIR).parent_path() / "fonts" / "inter";
    for (const char* face : {"Inter-Regular.ttf", "Inter-Medium.ttf",
                             "Inter-SemiBold.ttf", "Inter-Bold.ttf"}) {
        CHECK(Rml::LoadFontFace((fonts / face).string()));
    }
    Rml::ElementDocument* document = context->LoadDocument(
        (std::filesystem::path(ARTCADE_UI_RESOURCE_DIR) / "editor_shell.rml").string());
    CHECK(document != nullptr);
    document->Show(); context->Update();

    EditorCoordinator coordinator{makeProject()};
    CHECK(coordinator.apply(SelectEntityIntent{1}).ok);
    EditorUi ui{coordinator, document, nullptr, nullptr};
    ui.bind(); frame(*context, ui);

    for (const char* anchor : {"tl", "tc", "tr", "ml", "c", "mr", "bl", "bc", "br"}) {
        Rml::Element* button = findAction(document, "set-entity-origin-anchor", anchor);
        CHECK(button != nullptr);
        CHECK(button && !button->HasAttribute("disabled"));
    }
    CHECK(std::string(document->GetInnerRML()).find("Entity Origin") != std::string::npos);
    CHECK(std::string(document->GetInnerRML()).find("Inherited from Object Type") != std::string::npos);

    const WorldRect before = collectBoxColliderBounds(
        coordinator.document(), "scene", 1).front().worldBounds;
    ui.handleAction("set-entity-origin-anchor", "tl", "");
    frame(*context, ui);
    const WorldRect after = collectBoxColliderBounds(
        coordinator.document(), "scene", 1).front().worldBounds;
    CHECK(before.x == after.x && before.y == after.y
          && before.width == after.width && before.height == after.height);
    CHECK(coordinator.document().findInstanceInScene("scene", 1)->boxCollider2DOverride);
    CHECK(findAction(document, "reset-instance-collider-offset") != nullptr);
    CHECK(std::string(document->GetInnerRML()).find("INSTANCE OVERRIDE") != std::string::npos);

    ui.handleAction("reset-instance-collider-offset", "", "");
    frame(*context, ui);
    CHECK(!coordinator.document().findInstanceInScene("scene", 1)->boxCollider2DOverride);

    CHECK(coordinator.playCurrentScene().ok);
    frame(*context, ui);
    Rml::Element* center = findAction(document, "set-entity-origin-anchor", "c");
    CHECK(center && attr(center, "class").find("disabled") != std::string::npos);
    const uint64_t revision = coordinator.document().revision();
    ui.handleAction("set-entity-origin-anchor", "br", "");
    frame(*context, ui);
    CHECK(coordinator.document().revision() == revision);

    ui.detach();
    Rml::Shutdown();
    if (failed) std::cerr << passed << " passed, " << failed << " failed\n";
    else std::cout << "inspector-collider-origin-routing-test: " << passed
                   << " passed, 0 failed\n";
    return failed ? 1 : 0;
}
