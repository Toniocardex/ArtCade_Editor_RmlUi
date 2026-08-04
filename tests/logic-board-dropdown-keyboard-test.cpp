// ============================================================================
// ADR-0035 — Logic Board dropdown keyboard navigation.
//
// Mirrors tests/inspector-layer-dropdown-keyboard-test.cpp (ADR-0034) exactly,
// but targets the Logic Board's own dropdowns: the WHEN trigger-type catalog
// (catalogEntries(), the same picker the "On Start"/"Set Position"/"Is
// Visible" rows in the Logic Board use) and the hasOpenContextMenu() Escape
// gap fix extended to logicBoardEditor_.hasOpenDropdown().
//
// Starts at the real rendered elements and crosses EditorUi's single
// listener, since that seam (the toggle-logic-dropdown keydown branch,
// pendingLogicDropdownHighlightMove_/Commit_/Close_) is what ADR-0035 added.
// ============================================================================

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/logic_board_commands.h"
#include "editor-native/model/project_document.h"
#include "editor-native/ui/editor_ui.h"
#include "logic-core.h"

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

Rml::Element* findHighlightedCatalogEntry(Rml::Element* root) {
    if (!root) return nullptr;
    if (root->IsClassSet("logic-catalog-entry") && root->IsClassSet("highlighted")) return root;
    for (int i = 0; i < root->GetNumChildren(); ++i) {
        if (Rml::Element* hit = findHighlightedCatalogEntry(root->GetChild(i))) return hit;
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

ProjectDoc makeLogicBoardProject() {
    ProjectDoc doc;
    doc.formatVersion = 14;
    doc.projectName = "Logic Board Dropdown Keyboard";

    EntityDef hero;
    hero.name = "Hero";
    hero.className = "Hero";
    hero.spriteRenderer = SpriteRendererComponent{{}, true};

    LogicBoardDef board;
    board.id = "logic:Hero";
    LogicRuleDef rule = Logic::makeDefaultRule("rule-1");
    // Trigger-only, no actions: changing the trigger type must never be
    // rejected by ReplaceLogicTriggerCommand's board validation over an
    // incompatible default action — this test is about the picker's
    // keyboard-nav plumbing, not about which trigger/action pairs validate.
    rule.actions.clear();
    board.rules.push_back(std::move(rule));
    hero.logicBoard = std::move(board);

    doc.objectTypes.emplace("Hero", std::move(hero));
    return doc;
}

std::string currentTriggerTypeId(const EditorCoordinator& coordinator) {
    const auto& objectType = coordinator.document().data().objectTypes.at("Hero");
    return objectType.logicBoard->rules.at(0).trigger.typeId;
}

} // namespace

int main() {
    NullRenderInterface render;
    Rml::SetRenderInterface(&render);
    if (!Rml::Initialise()) {
        std::cerr << "FAIL Rml::Initialise()\n";
        return 1;
    }

    Rml::Context* context = Rml::CreateContext("logic-board-dropdown-test", Rml::Vector2i(1600, 900));
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

    EditorCoordinator coordinator{makeLogicBoardProject()};
    CHECK(coordinator.apply(OpenLogicBoardIntent{"Hero"}).ok);
    EditorUi ui{coordinator, document, nullptr, nullptr};
    ui.bind();
    frame(*context, ui);

    const std::string originalTrigger = currentTriggerTypeId(coordinator);
    const std::string triggerDropdownId = "trigger|rule-1";

    Rml::Element* trigger = findAction(document, "toggle-logic-dropdown", triggerDropdownId);
    CHECK(trigger != nullptr);
    CHECK(trigger
          && trigger->GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto);
    CHECK(ui.hasOpenContextMenu() == false); // ADR-0035 gap fix baseline: closed.

    click(trigger);
    frame(*context, ui);
    CHECK(ui.hasOpenContextMenu() == true); // gap fix: open Logic Board dropdown counts too.

    const std::uint64_t revisionAtOpen = coordinator.document().revision();

    // -- ArrowDown highlights some other catalog entry without touching the document.
    trigger = findAction(document, "toggle-logic-dropdown", triggerDropdownId);
    pressKey(trigger, Rml::Input::KI_DOWN);
    frame(*context, ui);
    Rml::Element* highlighted = findHighlightedCatalogEntry(document);
    CHECK(highlighted != nullptr);
    CHECK(coordinator.document().revision() == revisionAtOpen);
    CHECK(currentTriggerTypeId(coordinator) == originalTrigger);
    const std::string highlightedValue = attributeOf(highlighted, "data-value");

    // -- Escape closes without committing.
    trigger = findAction(document, "toggle-logic-dropdown", triggerDropdownId);
    pressKey(trigger, Rml::Input::KI_ESCAPE);
    frame(*context, ui);
    CHECK(currentTriggerTypeId(coordinator) == originalTrigger);
    CHECK(coordinator.document().revision() == revisionAtOpen);
    CHECK(ui.hasOpenContextMenu() == false);
    CHECK(findAction(document, "toggle-logic-dropdown", triggerDropdownId) != nullptr);
    CHECK(findHighlightedCatalogEntry(document) == nullptr); // list collapsed

    // -- Reopen, ArrowDown to the same entry, Enter commits it (undoable).
    trigger = findAction(document, "toggle-logic-dropdown", triggerDropdownId);
    click(trigger);
    frame(*context, ui);
    trigger = findAction(document, "toggle-logic-dropdown", triggerDropdownId);
    pressKey(trigger, Rml::Input::KI_DOWN);
    frame(*context, ui);
    highlighted = findHighlightedCatalogEntry(document);
    CHECK(highlighted != nullptr);
    CHECK(highlighted && attributeOf(highlighted, "data-value") == highlightedValue);
    trigger = findAction(document, "toggle-logic-dropdown", triggerDropdownId);
    pressKey(trigger, Rml::Input::KI_RETURN);
    frame(*context, ui);
    CHECK(currentTriggerTypeId(coordinator) == highlightedValue);
    CHECK(coordinator.document().revision() != revisionAtOpen);
    CHECK(coordinator.canUndo());
    CHECK(ui.hasOpenContextMenu() == false); // Enter's commit closes the dropdown too.

    CHECK(coordinator.undo().ok);
    frame(*context, ui);
    CHECK(currentTriggerTypeId(coordinator) == originalTrigger);

    if (failed > 0) {
        std::cerr << "\n" << passed << " passed, " << failed << " failed\n";
    } else {
        std::cout << "logic-board-dropdown-keyboard-test: " << passed << " passed, "
                  << failed << " failed\n";
    }
    ui.detach();
    Rml::Shutdown();
    return failed > 0 ? 1 : 0;
}
