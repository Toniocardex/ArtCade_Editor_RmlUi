// ============================================================================
// ADR-0029 — the expression field's completion list opens on focus.
//
// The seam this suite exists for is the one that broke: a real focus event on
// a real RmlUi element, through EditorUi's single event listener, into the
// router, into LogicBoardEditorController, into LogicBoardPanel, and back out
// as rendered markup. Every existing expression test calls
// `controller.handleAction("focus-logic-expression", …)` directly — *below*
// the router — so all of them stayed green while the feature was unreachable:
// an unconditional `if (type == "focus") return;` sat above the expression
// branch and swallowed every focus event before it could be dispatched.
//
// A test that starts at handleAction cannot catch that class of defect. This
// one starts at the element.
// ============================================================================

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/logic_expression_commands.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/ui/logic_property_editor.h"
#include "logic-core.h"
#include "logic-number-expression-format.h"
#include "logic-number-expression-parse.h"

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

// RmlUi needs a render interface to create a context; this suite asserts on the
// element tree, never on pixels, so every call is a no-op returning a handle
// RmlUi treats as valid-but-empty.
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

/**
 * One editor frame in the application's order (editor_app.cpp): input has
 * already been routed, then panels repaint, then RmlUi lays out, then the
 * presentation-only restoration that needs final layout runs. Focus and caret
 * restoration depend on this order, so the test must not shortcut it.
 */
void frame(Rml::Context& context, EditorUi& ui) {
    ui.processFrame();
    context.Update();
    ui.restoreAfterRmlLayout();
}

std::string attributeOf(Rml::Element* element, const char* name) {
    return element ? element->GetAttribute<Rml::String>(name, Rml::String()) : std::string();
}

/** Depth-first search for the first element carrying @p attribute == @p value. */
Rml::Element* findByAttribute(Rml::Element* root, const char* attribute,
                              const std::string& value) {
    if (!root) return nullptr;
    if (attributeOf(root, attribute) == value) return root;
    for (int i = 0; i < root->GetNumChildren(); ++i) {
        if (Rml::Element* hit = findByAttribute(root->GetChild(i), attribute, value))
            return hit;
    }
    return nullptr;
}

/** Collects the visible text of every element matching a class, in tree order. */
void collectByClass(Rml::Element* root, const std::string& className,
                    std::vector<Rml::Element*>& out) {
    if (!root) return;
    if (root->IsClassSet(className)) out.push_back(root);
    for (int i = 0; i < root->GetNumChildren(); ++i)
        collectByClass(root->GetChild(i), className, out);
}

bool hasClass(Rml::Element* root, const std::string& className) {
    std::vector<Rml::Element*> hits;
    collectByClass(root, className, hits);
    return !hits.empty();
}

/** Concatenated inner text of the completion list, for "is `random` offered?". */
std::string completionText(Rml::Element* root) {
    std::vector<Rml::Element*> lists;
    collectByClass(root, "logic-expression-completions", lists);
    std::string text;
    for (Rml::Element* list : lists) text += list->GetInnerRML();
    return text;
}

ProjectDoc makeProjectWithSetPosition() {
    ProjectDoc doc;
    doc.formatVersion = 4;
    doc.projectName = "Expression Focus";

    EntityDef hero;
    hero.name = "Hero";
    hero.className = "Hero";
    hero.spriteRenderer = SpriteRendererComponent{{}, true};

    LogicBoardDef board;
    board.id = "logic:Hero";
    LogicRuleDef rule = Logic::makeDefaultRule("rule-pos");
    rule.trigger = {Logic::kOnStart, {}};
    rule.actions[0] = Logic::makeDefaultBlock(Logic::kSetPosition, Logic::BlockKind::Action);
    board.rules.push_back(std::move(rule));
    hero.logicBoard = std::move(board);
    doc.objectTypes.emplace("Hero", hero);

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
    scene.instances.push_back(instance);
    scene.entityIds.push_back(1);
    doc.scenes.emplace(scene.id, scene);
    doc.activeSceneId = scene.id;
    return doc;
}

// ----------------------------------------------------------------------------
// The regression: focus on the X axis field must reach the panel.
// ----------------------------------------------------------------------------
void testFocusOpensTheCompletionList(Rml::Context& context, Rml::ElementDocument& document,
                                     EditorCoordinator& coordinator, EditorUi& ui) {
    CHECK(coordinator.apply(OpenLogicBoardIntent{"Hero"}).ok);
    frame(context, ui);

    // The board renders one expression field per axis (ADR-0029), addressed
    // rule|a|0|position|x. Locate X without assuming the rule id.
    Rml::Element* axisX = nullptr;
    {
        std::vector<Rml::Element*> inputs;
        collectByClass(&document, "logic-expression-input", inputs);
        for (Rml::Element* input : inputs) {
            const std::string arg = attributeOf(input, "data-arg");
            if (arg.size() >= 2 && arg.compare(arg.size() - 2, 2, "|x") == 0) axisX = input;
        }
    }
    CHECK(axisX != nullptr);
    if (!axisX) return;
    CHECK(attributeOf(axisX, "data-action") == "edit-logic-expression");
    const std::string axisArg = attributeOf(axisX, "data-arg");

    // Nothing is focused yet, so no list is open and the field shows the Code
    // form of the literal it holds.
    CHECK(!hasClass(&document, "logic-expression-completions"));
    CHECK(attributeOf(axisX, "value") == "0");

    const uint64_t revisionBefore = coordinator.document().revision();
    const bool undoBefore = coordinator.canUndo();

    // The event under test — RmlUi's own focus, not a synthesised action call.
    // The repaint lands in processFrame (the application's frame order is
    // input → processFrame → Context::Update), so `axisX` survives the
    // dispatch and is only invalidated by the rebuild below.
    CHECK(axisX->Focus());
    axisX = nullptr;
    frame(context, ui);

    // (1)(2) The focused field came back, addressed to X.
    Rml::Element* focused = document.GetElementById("logic-expression-input");
    CHECK(focused != nullptr);
    CHECK(attributeOf(focused, "data-arg") == axisArg);

    // (3) The draft shows the current Code value rather than an empty field.
    CHECK(attributeOf(focused, "value") == "0");

    // (4)(5) The whole vocabulary is offered, unfiltered by the "0" already in
    // the field — filtering on it would match nothing, which is the opposite of
    // a discovery surface.
    CHECK(hasClass(&document, "logic-expression-completions"));
    const std::string offered = completionText(&document);
    CHECK(offered.find("random(min, max)") != std::string::npos);
    CHECK(offered.find("clamp(value, min, max)") != std::string::npos);
    CHECK(offered.find("self.x") != std::string::npos);
    CHECK(offered.find("Nothing matches") == std::string::npos);

    // (7) Focus is presentation: no command, no revision, no undo entry.
    CHECK(coordinator.document().revision() == revisionBefore);
    CHECK(coordinator.canUndo() == undoBefore);

    // The caret really is in the field, not on the panel the rebuild left
    // focused — otherwise nothing below would type anywhere.
    CHECK(context.GetFocusElement() == document.GetElementById("logic-expression-input"));

    // (6) Typing narrows the list. Real typing through RmlUi's text widget,
    // which is what emits `change`; routing that is what is under test.
    // Select-all then type is how an author replaces the literal. Emptying the
    // field instead is not equivalent today: an empty draft is indistinguishable
    // from "no draft" in the render, so the field snaps back to the formatted
    // value. That is a draft-model defect, tracked for the draft/caret slice,
    // not something this routing fix touches.
    context.ProcessKeyDown(Rml::Input::KI_A, Rml::Input::KM_CTRL);
    context.ProcessTextInput(Rml::String("ra"));
    frame(context, ui);

    Rml::Element* typed = document.GetElementById("logic-expression-input");
    CHECK(typed != nullptr);
    CHECK(attributeOf(typed, "value") == "ra");

    const std::string narrowed = completionText(&document);
    CHECK(narrowed.find("random(min, max)") != std::string::npos);
    CHECK(narrowed.find("self.x") == std::string::npos);
    CHECK(narrowed.find("Nothing matches") == std::string::npos);

    // (7) Still no authoring mutation: the draft never reaches the document.
    CHECK(coordinator.document().revision() == revisionBefore);
    CHECK(coordinator.canUndo() == undoBefore);

    // Picking `random(` with the mouse. Clicking a list entry moves RmlUi's
    // focus off the field, so the field blurs *before* the click is delivered —
    // and a blur that commits tears down the list the click was aimed at.
    Rml::Element* entry = nullptr;
    for (Rml::Element* candidate :
         [&] {
             std::vector<Rml::Element*> all;
             collectByClass(&document, "logic-expression-completion", all);
             return all;
         }()) {
        if (attributeOf(candidate, "data-value") == "random(") entry = candidate;
    }
    CHECK(entry != nullptr);
    if (!entry) return;

    const Rml::Vector2f centre =
        entry->GetAbsoluteOffset(Rml::BoxArea::Border)
        + Rml::Vector2f(entry->GetClientWidth() * 0.5f, entry->GetClientHeight() * 0.5f);
    context.ProcessMouseMove(static_cast<int>(centre.x), static_cast<int>(centre.y), 0);
    context.ProcessMouseButtonDown(0, 0);
    frame(context, ui);
    context.ProcessMouseButtonUp(0, 0);
    frame(context, ui);

    Rml::Element* picked = document.GetElementById("logic-expression-input");
    CHECK(picked != nullptr);
    CHECK(attributeOf(picked, "value") == "random(");
    // The caret goes back into the field, so the next argument can be typed
    // without clicking back in.
    CHECK(context.GetFocusElement() == picked);
    // The list stays open on the newly inserted token, so the next argument can
    // be picked without clicking back into the field.
    CHECK(hasClass(&document, "logic-expression-completions"));
    // Still a draft: inserting a completion is not a commit.
    CHECK(coordinator.document().revision() == revisionBefore);
    CHECK(coordinator.canUndo() == undoBefore);
}

// ----------------------------------------------------------------------------
// Editing an expression that is already there: the caret must survive the
// rebuild each keystroke triggers, or Backspace lands at offset 0 and does
// nothing, and Delete eats the wrong end.
// ----------------------------------------------------------------------------
void testEditingAnExistingExpression(Rml::Context& context, Rml::ElementDocument& document,
                                     EditorCoordinator& coordinator, EditorUi& ui) {
    const LogicBoardDef& board =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicPropertyAddress address{
        board.rules[0].id, LogicPropertyTarget::Action, 0};
    const std::string axis = encodeLogicPropertyAddress(address, "position") + "|x";

    // Abandon whatever draft the previous case left behind, so this one starts
    // from the document rather than from a half-typed field.
    context.ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
    frame(context, ui);

    // Put a real expression in the field, the way an author would have.
    LogicNumberExpressionAddress target;
    target.objectTypeId = "Hero";
    target.ruleId = board.rules[0].id;
    target.actionIndex = 0;
    target.parameterId = "position";
    target.component = LogicNumericComponent::X;
    CHECK(coordinator.execute(SetLogicNumberExpressionCommand{
        target, Logic::parseNumberExpression("random(0, 100)").value}).ok);
    frame(context, ui);

    Rml::Element* field = nullptr;
    {
        std::vector<Rml::Element*> inputs;
        collectByClass(&document, "logic-expression-input", inputs);
        for (Rml::Element* input : inputs)
            if (attributeOf(input, "data-arg") == axis) field = input;
    }
    CHECK(field != nullptr);
    if (!field) return;
    CHECK(attributeOf(field, "value") == "random(0, 100)");

    CHECK(field->Focus());
    frame(context, ui);

    // The caret starts where RmlUi puts it on focus; move it to the end so the
    // gesture is unambiguous, then delete backwards.
    Rml::Element* live = document.GetElementById("logic-expression-input");
    CHECK(live != nullptr);
    if (!live) return;
    for (int i = 0; i < 6; ++i) {
        context.ProcessKeyDown(Rml::Input::KI_BACK, 0);
        frame(context, ui);
    }

    live = document.GetElementById("logic-expression-input");
    CHECK(live != nullptr);
    // Six backspaces from the end of `random(0, 100)`.
    CHECK(attributeOf(live, "value") == "random(0");

    // And clearing the field really clears it: an empty draft is the author
    // starting over, not the absence of a draft.
    context.ProcessKeyDown(Rml::Input::KI_A, Rml::Input::KM_CTRL);
    context.ProcessKeyDown(Rml::Input::KI_BACK, 0);
    frame(context, ui);
    live = document.GetElementById("logic-expression-input");
    CHECK(live != nullptr);
    CHECK(attributeOf(live, "value").empty());
    // Emptying the field is still only a draft — nothing reaches the document.
    CHECK(Logic::formatNumberExpression(
              std::get<LogicVec2Value>(
                  Logic::findProperty(
                      coordinator.document().data().objectTypes.at("Hero")
                          .logicBoard->rules[0].actions[0], "position")->value).x,
              Logic::NumberExpressionFormatStyle::Code) == "random(0, 100)");
}

// ----------------------------------------------------------------------------
// How an edit ends (Engineering Gates §23): Escape rolls back, blur commits a
// valid draft, and an invalid one is neither committed nor thrown away.
// ----------------------------------------------------------------------------

/** The Code form the document currently holds for Set Position X. */
std::string documentExpression(const EditorCoordinator& coordinator) {
    return Logic::formatNumberExpression(
        std::get<LogicVec2Value>(
            Logic::findProperty(
                coordinator.document().data().objectTypes.at("Hero")
                    .logicBoard->rules[0].actions[0], "position")->value).x,
        Logic::NumberExpressionFormatStyle::Code);
}

/** Focuses the X field and replaces its text with @p text, leaving it focused. */
void typeIntoExpressionField(Rml::Context& context, Rml::ElementDocument& document,
                             EditorUi& ui, const std::string& axis,
                             const std::string& text) {
    std::vector<Rml::Element*> inputs;
    collectByClass(&document, "logic-expression-input", inputs);
    for (Rml::Element* input : inputs) {
        if (attributeOf(input, "data-arg") != axis) continue;
        CHECK(input->Focus());
        break;
    }
    frame(context, ui);
    context.ProcessKeyDown(Rml::Input::KI_A, Rml::Input::KM_CTRL);
    context.ProcessTextInput(Rml::String(text));
    frame(context, ui);
}

void testEscapeRollsBackAndBlurCommits(Rml::Context& context, Rml::ElementDocument& document,
                                       EditorCoordinator& coordinator, EditorUi& ui) {
    const LogicBoardDef& board =
        *coordinator.document().data().objectTypes.at("Hero").logicBoard;
    const LogicPropertyAddress address{
        board.rules[0].id, LogicPropertyTarget::Action, 0};
    const std::string axis = encodeLogicPropertyAddress(address, "position") + "|x";

    // Start from a known document value, with nothing pending.
    LogicNumberExpressionAddress target;
    target.objectTypeId = "Hero";
    target.ruleId = board.rules[0].id;
    target.actionIndex = 0;
    target.parameterId = "position";
    target.component = LogicNumericComponent::X;
    CHECK(coordinator.execute(SetLogicNumberExpressionCommand{
        target, Logic::parseNumberExpression("random(0, 100)").value}).ok);
    frame(context, ui);
    CHECK(documentExpression(coordinator) == "random(0, 100)");

    // ---- Escape abandons the draft -----------------------------------------
    typeIntoExpressionField(context, document, ui, axis, "self.x");
    CHECK(attributeOf(document.GetElementById("logic-expression-input"), "value")
          == "self.x");
    const uint64_t beforeEscape = coordinator.document().revision();

    context.ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
    frame(context, ui);

    CHECK(documentExpression(coordinator) == "random(0, 100)");
    CHECK(coordinator.document().revision() == beforeEscape);
    // The field redraws from the document and the list closes with it.
    CHECK(!hasClass(&document, "logic-expression-completions"));
    {
        std::vector<Rml::Element*> inputs;
        collectByClass(&document, "logic-expression-input", inputs);
        for (Rml::Element* input : inputs)
            if (attributeOf(input, "data-arg") == axis)
                CHECK(attributeOf(input, "value") == "random(0, 100)");
    }

    // ---- Blur commits a valid draft ----------------------------------------
    // The Code form carries only the parentheses the tree needs, so this comes
    // back exactly as typed.
    typeIntoExpressionField(context, document, ui, axis, "self.x + 10");
    const uint64_t beforeBlur = coordinator.document().revision();

    // Clicking elsewhere moves the pointer with it. Leaving it parked over the
    // completion list would look, to the blur handler, exactly like pressing an
    // entry — which is the one case that must not commit.
    context.ProcessMouseMove(5, 5, 0);
    Rml::Element* elsewhere = document.GetElementById("logic-toolbar-search");
    CHECK(elsewhere != nullptr);
    if (!elsewhere) return;
    CHECK(elsewhere->Focus());
    frame(context, ui);

    CHECK(documentExpression(coordinator) == "self.x + 10");
    CHECK(coordinator.document().revision() != beforeBlur);
    CHECK(coordinator.canUndo());
    CHECK(!hasClass(&document, "logic-expression-completions"));

    // ---- Blur on an invalid draft keeps the text and explains --------------
    typeIntoExpressionField(context, document, ui, axis, "random(0,");
    const uint64_t beforeBadBlur = coordinator.document().revision();

    context.ProcessMouseMove(5, 5, 0);
    elsewhere = document.GetElementById("logic-toolbar-search");
    CHECK(elsewhere != nullptr);
    if (!elsewhere) return;
    CHECK(elsewhere->Focus());
    frame(context, ui);

    // ADR-0029: "a parse failure shows an inline diagnostic under the field and
    // never discards what the author typed".
    CHECK(documentExpression(coordinator) == "self.x + 10");
    CHECK(coordinator.document().revision() == beforeBadBlur);
    CHECK(hasClass(&document, "logic-expression-error"));
    {
        bool foundTypedText = false;
        std::vector<Rml::Element*> inputs;
        collectByClass(&document, "logic-expression-input", inputs);
        for (Rml::Element* input : inputs)
            if (attributeOf(input, "data-arg") == axis
                && attributeOf(input, "value") == "random(0,") foundTypedText = true;
        CHECK(foundTypedText);
    }

    // Leave the board clean for whatever runs next.
    context.ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
    frame(context, ui);
}

// ----------------------------------------------------------------------------
// Non-regression: the generic focus baseline still works for `commit-` fields.
// Narrowing that block must not cost the Escape-restore it exists for.
// ----------------------------------------------------------------------------
void testCommitFieldKeepsItsFocusBaseline(Rml::Context& context,
                                          Rml::ElementDocument& document) {
    Rml::Element* grid = document.GetElementById("grid-cell-size-input");
    CHECK(grid != nullptr);
    if (!grid) return;
    CHECK(attributeOf(grid, "data-action") == "commit-grid-cell-size");

    auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(grid);
    CHECK(control != nullptr);
    if (!control) return;

    const std::string baseline = control->GetValue();
    CHECK(grid->Focus());
    context.Update();

    control->SetValue("999");
    context.Update();

    Rml::Dictionary escape;
    escape["key_identifier"] = static_cast<int>(Rml::Input::KI_ESCAPE);
    grid->DispatchEvent(Rml::EventId::Keydown, escape);
    context.Update();

    // The baseline was captured at focus time, so Escape restores it — the
    // behaviour the unconditional early return was protecting.
    CHECK(control->GetValue() == baseline);
}

} // namespace

int main() {
    NullRenderInterface render;
    Rml::SetRenderInterface(&render);
    if (!Rml::Initialise()) {
        std::cerr << "FAIL Rml::Initialise()\n";
        return 1;
    }

    Rml::Context* context = Rml::CreateContext("test", Rml::Vector2i(1600, 900));
    if (!context) {
        std::cerr << "FAIL Rml::CreateContext()\n";
        Rml::Shutdown();
        return 1;
    }

    // Real fonts, not a fontless context: caret movement (End, Home, arrows)
    // asks the text widget for line metrics, and a widget with no font has no
    // lines to measure. Without these the harness would be measuring a
    // different program than the one that ships.
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

    EditorCoordinator coordinator{makeProjectWithSetPosition()};
    EditorUi ui{coordinator, document, nullptr, nullptr};
    ui.bind();
    context->Update();

    // Scene workspace first: the grid field lives in the Scene toolbar.
    testCommitFieldKeepsItsFocusBaseline(*context, *document);
    testFocusOpensTheCompletionList(*context, *document, coordinator, ui);
    testEditingAnExistingExpression(*context, *document, coordinator, ui);
    testEscapeRollsBackAndBlurCommits(*context, *document, coordinator, ui);

    // Controllers are detached before the documents they observe, and the
    // documents before the context (Constitution AC-LIFE-001).
    ui.detach();
    Rml::Shutdown();

    std::cout << "passed " << passed << " failed " << failed << "\n";
    return failed == 0 ? 0 : 1;
}
