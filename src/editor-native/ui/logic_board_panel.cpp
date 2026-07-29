#include "editor-native/ui/logic_board_panel.h"
#include "editor-native/ui/ui_icons.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/global_variable_commands.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/ui/logic_property_editor.h"
#include "editor-native/ui/ui_markup.h"
#include "logic-core.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace ArtCade::EditorNative {
namespace {

// Initial reasoned value (roughly the middle of an 840-900dp range sized for
// two mode buttons + reorder/remove icons + a Yes/No or X/Y input pair to fit
// the narrowest, 30%-wide EVENT column without wrapping) — not yet tuned
// against real screenshots at DPI scale.
constexpr float kCompactWidthThreshold = 860.f;

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

const LogicPropertyDef* property(const LogicBlockDef& block, const char* key) {
    return Logic::findProperty(block, key);
}

std::string number(float value) {
    std::ostringstream out;
    out.precision(7);
    out << value;
    return out.str();
}

std::string number(double value) {
    std::ostringstream out;
    out.precision(7);
    out << value;
    return out.str();
}

std::string actionArg(const LogicRuleId& ruleId, const LogicActionId& actionId) {
    return ruleId + "|" + actionId;
}

// A `.drop-list` entry: the current value marks itself and just closes the
// list on click (re-picking itself is a no-op); every other entry dispatches
// `pickAction` with the value carried in data-value (never data-arg, which
// stays the addressing key for the actions that need one — see the
// data-value contract enforced by LogicBoardEditorController). `pickArg`, when
// non-empty, is the addressing key riding
// alongside (e.g. the rule a Key picker belongs to).
//
// ADR-0035: `nav` is the same DropdownNavigation the panel uses for every
// other dropdown; every call site pushes one entry here so Up/Down/Enter
// work for free wherever dropEntry() is reused.
std::string dropEntry(DropdownNavigation& nav,
                      const std::string& label, const std::string& value, bool isCurrent,
                      const std::string& closeDropdownId, const char* pickAction,
                      const std::string& pickArg) {
    const std::size_t navIndex = nav.push(isCurrent
        ? DropdownNavEntry{"toggle-logic-dropdown", closeDropdownId, "", true}
        : DropdownNavEntry{pickAction, pickArg, value, false});
    std::string html = "<div class=\"drop-entry";
    if (isCurrent) html += " selected";
    if (nav.isHighlighted(navIndex)) html += " highlighted";
    html += "\"";
    if (isCurrent) {
        html += " data-action=\"toggle-logic-dropdown\" data-arg=\""
              + escapeRml(closeDropdownId) + "\"";
    } else {
        html += " data-action=\"" + std::string(pickAction) + "\"";
        if (!pickArg.empty()) html += " data-arg=\"" + escapeRml(pickArg) + "\"";
        html += " data-value=\"" + escapeRml(value) + "\"";
    }
    html += ">";
    if (isCurrent) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
    html += escapeRml(label) + "</div>";
    return html;
}

// A 2-5 option button row (`.mode-block`/`.mode-options`/`.panel-btn.mode-
// option`) — the same shape used for Box Collider Mode and Tilemap Tool.
// Each option carries the addressing key in data-arg (unchanged from what
// the native <select> it replaces used to receive) and the new value in
// data-value.
std::string modeOption(const char* label, const std::string& value, bool active,
                       const char* action, const std::string& arg, bool disabled) {
    std::string html = "<button class=\"panel-btn mode-option";
    if (active) html += " active";
    if (disabled) html += " disabled";
    html += "\" data-action=\"" + std::string(action) + "\" data-arg=\"" + escapeRml(arg)
         + "\" data-value=\"" + escapeRml(value) + "\">" + label + "</button>";
    return html;
}

std::string categoryLabel(const Logic::LogicCategoryId& categoryId) {
    if (categoryId.empty()) return "Other";
    std::string label = categoryId;
    label.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(label.front())));
    return label;
}

// Fixed display order for the catalog picker's category headers. A category
// not listed here (a future addition not yet slotted in) still renders —
// appended after, in first-seen registry order — rather than being dropped.
const std::vector<Logic::LogicCategoryId>& catalogCategoryOrder() {
    static const std::vector<Logic::LogicCategoryId> order{
        "system", "scene", "camera", "input", "collision", "entity", "platformer",
        "animation", "audio", "variables", "time", "messages",
    };
    return order;
}

std::string catalogEntries(DropdownNavigation& nav,
                           const EntityDef& owner, const Logic::LogicBlockDescriptor* trigger,
                           Logic::BlockKind kind, const std::string& currentTypeId,
                           const std::string& dropdownId, const char* selectAction,
                           const std::string& selectArg, bool eventCatalog = false) {
    // Group by category first, then render — Logic::registry() is ordered by
    // declaration, not by category, so a category whose descriptors aren't
    // contiguous there (e.g. "entity" split by "platformer"/"collision"
    // entries registered in between) would otherwise get a duplicate header
    // if emitted every time categoryId changes during a single linear pass.
    std::map<Logic::LogicCategoryId, std::vector<const Logic::LogicBlockDescriptor*>> byCategory;
    std::vector<Logic::LogicCategoryId> firstSeenOrder;
    for (const Logic::LogicBlockDescriptor& descriptor : Logic::registry()) {
        if (descriptor.catalogHidden) continue;
        const bool include = eventCatalog
            ? Logic::isEventEligible(descriptor)
            : descriptor.kind == kind;
        if (!include) continue;
        auto [it, inserted] = byCategory.try_emplace(descriptor.categoryId);
        if (inserted) firstSeenOrder.push_back(descriptor.categoryId);
        it->second.push_back(&descriptor);
    }

    std::vector<Logic::LogicCategoryId> renderOrder;
    for (const Logic::LogicCategoryId& categoryId : catalogCategoryOrder())
        if (byCategory.count(categoryId)) renderOrder.push_back(categoryId);
    for (const Logic::LogicCategoryId& categoryId : firstSeenOrder)
        if (std::find(renderOrder.begin(), renderOrder.end(), categoryId) == renderOrder.end())
            renderOrder.push_back(categoryId);

    std::string html = "<div class=\"drop-list logic-catalog-list\">";
    for (const Logic::LogicCategoryId& categoryId : renderOrder) {
        html += "<div class=\"logic-catalog-category\">"
             + escapeRml(categoryLabel(categoryId)) + "</div>";
        for (const Logic::LogicBlockDescriptor* descriptorPtr : byCategory.at(categoryId)) {
            const Logic::LogicBlockDescriptor& descriptor = *descriptorPtr;
            const bool current = descriptor.typeId == currentTypeId;
            const Logic::LogicBlockAvailability availability =
                Logic::blockAvailability(owner, descriptor, trigger);
            // Incompatible entries carry no data-action (same as disabled/
            // locked rows elsewhere), so they are excluded from arrow-key
            // navigation the same way: nothing to commit there.
            std::optional<std::size_t> navIndex;
            if (current) {
                navIndex = nav.push({"toggle-logic-dropdown", dropdownId, "", true});
            } else if (availability.compatible) {
                navIndex = nav.push({selectAction, selectArg, descriptor.typeId, false});
            }
            html += "<button class=\"drop-entry logic-catalog-entry";
            if (current) html += " selected";
            if (navIndex && nav.isHighlighted(*navIndex)) html += " highlighted";
            if (!availability.compatible) html += " disabled";
            html += "\"";
            if (current) {
                html += " data-action=\"toggle-logic-dropdown\" data-arg=\""
                     + escapeRml(dropdownId) + "\"";
            } else if (availability.compatible) {
                html += " data-action=\"" + std::string(selectAction) + "\" data-arg=\""
                     + escapeRml(selectArg) + "\" data-value=\""
                     + escapeRml(descriptor.typeId) + "\"";
            } else {
                html += " disabled=\"disabled\" title=\"" + escapeRml(availability.reason) + "\"";
            }
            html += "><span class=\"logic-catalog-name\">" + escapeRml(descriptor.displayName)
                 + "</span><span class=\"logic-catalog-description\">"
                 + escapeRml(availability.compatible ? descriptor.description : availability.reason)
                 + "</span></button>";
        }
    }
    return html + "</div>";
}

// Falls back to the raw typeId (never blank) for a descriptor the registry no
// longer knows — a migrated project, a removed block type, or a future
// schema opened by an older build must still show *something* readable.
std::string descriptorLabel(const std::string& typeId) {
    if (const Logic::LogicBlockDescriptor* descriptor = Logic::findDescriptor(typeId))
        return descriptor->displayName;
    return typeId.empty() ? "Unknown" : typeId;
}

std::string animationAssetLabel(const SpriteAnimationAssetDef& asset) {
    return asset.name.empty() ? asset.id : asset.name;
}

std::string clipLabel(const SpriteAnimationClipDef& clip) {
    return clip.name.empty() ? clip.id : clip.name;
}

std::string defaultClipId(const SpriteAnimationAssetDef& asset) {
    return asset.clips.empty() ? std::string{} : asset.clips.front().id;
}

// Derived, read-only projection — never a second source of truth for the
// trigger/action display names, always resolved through the same registry
// the compiler and the mode-option buttons use. The action suffix is
// unconditional — "On Start" alone used to be a bug (the suffix only
// appeared for the Key Pressed trigger), not an intentional distinction.
std::string logicRuleSummary(const LogicRuleDef& rule) {
    std::string head;
    if (rule.trigger.typeId == Logic::kKeyPressed) {
        head = "Key";
        if (const LogicPropertyDef* p = property(rule.trigger, "key"))
            if (const auto* key = std::get_if<LogicKey>(&p->value)) head = Logic::logicKeyName(*key);
    } else if (rule.trigger.typeId == Logic::kPlatformerMotionState) {
        head = "Platformer State";
        if (const LogicPropertyDef* p = property(rule.trigger, "state")) {
            if (const auto* state = std::get_if<LogicStringValue>(&p->value)) {
                if (state->value == "Moving") head = "Platformer Moving";
                else if (state->value == "Stopped") head = "Platformer Stopped";
                else if (state->value == "Jumping") head = "Platformer Jumping";
                else if (state->value == "Falling") head = "Platformer Falling";
            }
        }
    } else {
        head = descriptorLabel(rule.trigger.typeId);
    }
    if (!rule.actions.empty())
        head += " \xE2\x86\x92 " + descriptorLabel(rule.actions[0].block.typeId);
    if (rule.actions.size() > 1)
        head += " \xC2\xB7 " + std::to_string(rule.actions.size()) + " actions";
    return head;
}

const char* executionModeLabel(LogicExecutionMode mode) {
    switch (mode) {
    case LogicExecutionMode::OncePerActivation: return "Once per activation";
    case LogicExecutionMode::EveryOccurrence:
    default: return "Every occurrence";
    }
}

const char* variableTypeName(GameVariableDefinition::Type type) {
    switch (type) {
    case GameVariableDefinition::Type::Boolean: return "Boolean";
    case GameVariableDefinition::Type::String: return "String";
    case GameVariableDefinition::Type::Number:
    default: return "Number";
    }
}

std::string variableInitialValue(const GameVariableDefinition& definition) {
    if (const auto* numberValue = std::get_if<double>(&definition.initialValue))
        return number(*numberValue);
    if (const auto* boolValue = std::get_if<bool>(&definition.initialValue))
        return *boolValue ? "true" : "false";
    if (const auto* stringValue = std::get_if<std::string>(&definition.initialValue))
        return *stringValue;
    return {};
}

std::string variablesDrawer(
    DropdownNavigation& nav, const EditorCoordinator& coordinator, bool playing,
    const std::string& openDropdownId) {
    std::string html =
        "<div class=\"logic-variables-drawer\">"
        "<div class=\"logic-variables-head\"><div>"
        "<span class=\"logic-variables-title\">Project Variables</span>"
        "<span class=\"logic-muted\">Typed globals shared by every Logic Board</span>"
        "</div><div class=\"logic-variables-head-actions\">"
        "<button class=\"logic-btn primary";
    if (playing) html += " disabled";
    html += "\" data-action=\"add-global-variable\">+ Variable</button>"
            "<button class=\"logic-variables-collapse\" data-action=\"toggle-global-variables\" "
            "title=\"Hide Project Variables\">"
          + iconMarkup("" UI_ICON_EXPAND "") + "</button></div></div>";

    const auto& variables = coordinator.document().data().globalVariables;
    if (variables.empty()) {
        html += "<div class=\"logic-variables-empty\">No project variables yet. "
                "Create one to use Compare, Set, Add, Subtract or Toggle blocks.</div>";
    } else {
        // Column eyebrows: identical boxes without labels read as four anonymous
        // values. Description is optional notes, not a fourth typed value.
        html += "<div class=\"logic-variable-columns\">"
                "<span class=\"logic-variable-col logic-variable-col-key\">Name</span>"
                "<span class=\"logic-variable-col logic-variable-col-type\">Type</span>"
                "<span class=\"logic-variable-col logic-variable-col-value\">Value</span>"
                "<span class=\"logic-variable-col logic-variable-col-description\">Description</span>"
                "<span class=\"logic-variable-col logic-variable-col-trail\"></span>"
                "</div>";
    }
    for (const GameVariableDefinition& variable : variables) {
        const std::size_t refs =
            countGlobalVariableReferences(coordinator.document(), variable.key);
        const std::string typeDropdownId = "variable-type|" + variable.key;
        const bool typeOpen = openDropdownId == typeDropdownId && !playing;
        html += "<div class=\"logic-variable-row\">";
        html += "<input class=\"logic-variable-key\" data-action=\"commit-global-variable-key\""
                " data-arg=\"" + escapeRml(variable.key) + "\" value=\""
              + escapeRml(variable.key) + "\"";
        if (playing) html += " disabled=\"disabled\"";
        html += "/>";
        html += "<div class=\"logic-variable-type\">";
        html += dropdownTriggerMarkup(variableTypeName(variable.type), "toggle-logic-dropdown",
                                      typeDropdownId, typeOpen, playing);
        if (typeOpen) {
            html += "<div class=\"drop-list\">";
            html += dropEntry(nav, "Number", "number",
                              variable.type == GameVariableDefinition::Type::Number,
                              typeDropdownId, "set-global-variable-type", variable.key);
            html += dropEntry(nav, "Boolean", "boolean",
                              variable.type == GameVariableDefinition::Type::Boolean,
                              typeDropdownId, "set-global-variable-type", variable.key);
            html += dropEntry(nav, "String", "string",
                              variable.type == GameVariableDefinition::Type::String,
                              typeDropdownId, "set-global-variable-type", variable.key);
            html += "</div>";
        }
        html += "</div>";
        if (variable.type == GameVariableDefinition::Type::Boolean) {
            const bool checked = std::get_if<bool>(&variable.initialValue)
                && std::get<bool>(variable.initialValue);
            html += "<button class=\"logic-btn logic-variable-value";
            if (checked) html += " active";
            if (playing) html += " disabled";
            html += "\" data-action=\"toggle-global-variable-value\" data-arg=\""
                  + escapeRml(variable.key) + "\">" + (checked ? "True" : "False") + "</button>";
        } else {
            html += "<input class=\"logic-variable-value\" data-action=\"commit-global-variable-value\""
                    " data-arg=\"" + escapeRml(variable.key) + "\" value=\""
                  + escapeRml(variableInitialValue(variable)) + "\"";
            if (playing) html += " disabled=\"disabled\"";
            html += "/>";
        }
        html += "<input class=\"logic-variable-description\""
                " data-action=\"commit-global-variable-description\" data-arg=\""
              + escapeRml(variable.key) + "\" placeholder=\"Description\" value=\""
              + escapeRml(variable.description) + "\"";
        if (playing) html += " disabled=\"disabled\"";
        html += "/>";
        html += "<span class=\"logic-variable-refs\">" + std::to_string(refs)
              + (refs == 1 ? " ref" : " refs") + "</span>";
        html += "<button class=\"comp-remove";
        if (playing || refs != 0) html += " disabled";
        html += "\" data-action=\"remove-global-variable\" data-arg=\""
              + escapeRml(variable.key) + "\" title=\""
              + (refs == 0 ? "Delete variable" : "Referenced variables cannot be deleted")
              + "\">" + iconMarkup("" UI_ICON_DELETE "") + "</button></div>";
    }
    return html + "</div>";
}

} // namespace

void LogicBoardPanel::refresh(Rml::ElementDocument* document,
                              const EditorCoordinator& coordinator) const {
    // Play may begin while this projection is hidden. Tear down transient
    // authoring interaction even when there is no Rml document to repaint.
    if (coordinator.isPlaying()) {
        openDropdownId_.clear();
        dropdownNav_.resetSession();
        clearKeyBindingEditor();
        discardContextualGlobalVariable();
        pendingRevealRuleId_.clear();
        scrollRestorePending_ = false;
    }
    if (!document) return;
    Rml::Element* root = document->GetElementById("logic-board-panel");
    if (!root) return;
    // ADR-0035: rebuilt fresh every paint by whichever dropdown block is open.
    dropdownNav_.clearEntries();

    const LogicBoardEditorState& view = coordinator.state().logicBoardEditor;
    const bool playing = coordinator.isPlaying();

    std::vector<ObjectTypeId> typeIds;
    typeIds.reserve(coordinator.document().data().objectTypes.size());
    for (const auto& [id, unused] : coordinator.document().data().objectTypes)
        typeIds.push_back(id);
    std::sort(typeIds.begin(), typeIds.end());

    // The board actually rendered — resolved with the same first-sorted-type
    // fallback as below — not the raw (possibly-nullopt) workspace value.
    // Gating scroll/dropdown/collapse clearing on the raw value would let
    // state leak across two different boards that both happen to have a
    // "rule-1" whenever view.objectTypeId sits at nullopt and the fallback
    // silently resolves to a different type each time.
    const ObjectTypeId selectedId = view.objectTypeId
        && coordinator.document().hasObjectType(*view.objectTypeId)
        ? *view.objectTypeId : (typeIds.empty() ? ObjectTypeId{} : typeIds.front());

    if (renderedObjectTypeId_ == selectedId) {
        // Do not sample a replacement scroll container before its deferred
        // layout has run: RmlUi reports zero there, which would overwrite the
        // offset captured from the previous completed frame.
        if (Rml::Element* scroll = document->GetElementById("logic-scroll"))
            if (!scrollRestorePending_) scrollTop_ = scroll->GetScrollTop();
    } else {
        renderedObjectTypeId_ = selectedId;
        scrollTop_ = 0.f;
        scrollRestorePending_ = false;
        openDropdownId_.clear();
        dropdownNav_.resetSession();
        clearKeyBindingEditor();
        discardContextualGlobalVariable();
        pendingRevealRuleId_.clear();
        collapsedRuleIds_.clear();
    }
    if (hasContextualGlobalVariableDraft()
        && (contextualVariableObjectTypeId_ != selectedId
            || contextualVariableSourceRevision_
                != coordinator.document().revision())) {
        discardContextualGlobalVariable();
    }
    if (lastTab_ != view.tab) {
        lastTab_ = view.tab;
        openDropdownId_.clear();
        dropdownNav_.resetSession();
        clearKeyBindingEditor();
        discardContextualGlobalVariable();
    }

    const auto instanceCountFor = [&](const ObjectTypeId& objectTypeId) {
        std::size_t count = 0;
        for (const auto& [sceneId, scene] : coordinator.document().data().scenes)
            for (const SceneInstanceDef& instance : scene.instances)
                if (instance.objectTypeId == objectTypeId) ++count;
        return count;
    };

    const EntityDef* selectedType = selectedId.empty()
        ? nullptr : &coordinator.document().data().objectTypes.at(selectedId);
    const std::string selectedName = selectedType && !selectedType->name.empty()
        ? selectedType->name : selectedId;
    const std::size_t sharedCount = selectedId.empty() ? 0 : instanceCountFor(selectedId);
    const SceneDef* activeScene =
        coordinator.document().findScene(coordinator.state().activeSceneId);
    const bool hasInstanceInActiveScene = selectedType && activeScene
        && std::any_of(activeScene->instances.begin(), activeScene->instances.end(),
                       [&](const SceneInstanceDef& instance) {
                           return instance.objectTypeId == selectedId;
                       });

    const bool selectedHasBoard = selectedType && selectedType->logicBoard.has_value();
    std::string html = "<div class=\"logic-head\"><div class=\"logic-heading\">"
                       "<div class=\"logic-title-row\"><span class=\"logic-title-prefix\">Logic Board"
                       + std::string(selectedName.empty() ? "" : " ·") + "</span>";
    if (!typeIds.empty()) {
        // `open` is always false here: EditorUi owns this picker's floating
        // menu (see objectTypeMenuEntries) and toggles the "open" class on
        // this element directly, since opening/closing it never invalidates
        // the Logic Board (no repaint would exist to reflect it otherwise).
        html += dropdownTriggerMarkup(selectedName, "toggle-logic-dropdown", "object-type",
                                      /*open=*/false, playing, "logic-type-trigger",
                                      "logic-type-trigger");
    }
    if (selectedHasBoard) {
        // "..." menu (Remove Logic Board), a small step away from the picker
        // so it doesn't read as "delete this Object Type" — see
        // EditorUi::toggleLogicMoreMenu, which mirrors the Object Type
        // picker's own floating-menu mechanism.
        html += "<button id=\"logic-more-trigger\" class=\"logic-more-trigger";
        if (playing) html += " disabled";
        html += "\" data-action=\"toggle-logic-more-menu\" title=\"More\">"
                // Three ASCII periods, not a Unicode ellipsis or tabler-icons
                // glyph: U+22EF rendered as a missing-glyph tofu box in this
                // app's font (confirmed by screenshot), and a codepoint
                // outside the embedded tabler-icons subset risks the same —
                // plain periods can't fail to render in any font.
                "...</button>";
    }
    html += "<button class=\"logic-variables-toggle";
    if (variablesDrawerOpen_) html += " active";
    html += "\" data-action=\"toggle-global-variables\" title=\""
          + std::string(variablesDrawerOpen_ ? "Hide Project Variables" : "Show Project Variables")
          + "\">Project Variables ("
          + std::to_string(coordinator.document().data().globalVariables.size()) + ")</button>";
    html += "</div><span class=\"logic-owner\">OBJECT TYPE · ";
    if (selectedName.empty()) {
        html += "No target";
    } else {
        html += "Shared by " + std::to_string(sharedCount)
             + (sharedCount == 1 ? " instance" : " instances");
    }
    html += "</span></div>"; // .logic-heading
    html += "</div>"; // .logic-head
    if (selectedType && !hasInstanceInActiveScene) {
        // ADR-0031 A2.3: Slice A can author Object Variables only through a
        // selected instance. State that limitation at the Logic Board instead
        // of offering navigation that cannot reach this type in this scene.
        html += "<div id=\"logic-object-variables-reachability\""
                " class=\"logic-object-variables-reachability logic-muted\">"
                "Object variables for " + escapeRml(selectedName)
              + " are edited by selecting a " + escapeRml(selectedName)
              + " instance</div>";
    }
    if (variablesDrawerOpen_) html += variablesDrawer(dropdownNav_, coordinator, playing, openDropdownId_);
    const auto render = [&]() {
        // Where the author left the caret, captured before the markup that
        // holds it is thrown away.
        expressionCaretStart_ = -1;
        expressionCaretEnd_ = -1;
        if (Rml::Element* previous = document->GetElementById("logic-expression-input")) {
            if (auto* input =
                    rmlui_dynamic_cast<Rml::ElementFormControlInput*>(previous)) {
                Rml::String selected;
                input->GetSelection(&expressionCaretStart_, &expressionCaretEnd_,
                                    &selected);
            }
        }
        // Destroying the focused field makes RmlUi blur it synchronously; the
        // flag is what tells the blur handler this is our own rebuild and not
        // the author leaving the field (see isRebuilding()).
        rebuilding_ = true;
        root->SetInnerRML(html);
        rebuilding_ = false;
        // See restoreAfterLayout(): the new scroll container only has a valid
        // range after the application's single Context::Update() this frame.
        scrollRestorePending_ = true;
        if (!keySearchAddress_.empty()) {
            if (Rml::Element* input = document->GetElementById("logic-key-search-input"))
                input->Focus(true);
        }
        // ADR-0029: focusing an expression field rebuilds the panel, and
        // SetInnerRML destroys the very element that holds the caret. Restoring
        // it here does not stick — the Context::Update() that follows processes
        // the old element's removal and moves focus to the surviving ancestor,
        // so the field would come back visible but dead to typing. The restore
        // is deferred to restoreAfterLayout(), which runs after that update.
        expressionFocusRestorePending_ = !expressionFocusAddress_.empty();
        contextualVariableFocusRestorePending_ =
            hasContextualGlobalVariableDraft();
        syncResponsiveClass(document);
    };

    if (typeIds.empty()) {
        html += "<div class=\"logic-empty\"><div class=\"logic-empty-title\">No Object Types</div>"
                "<div class=\"logic-muted\">Create an Object Type before adding gameplay logic.</div></div>";
        render();
        return;
    }

    const EntityDef& objectType = coordinator.document().data().objectTypes.at(selectedId);
    if (!objectType.logicBoard) {
        html += "<div class=\"logic-empty\"><div class=\"logic-empty-title\">No Logic Board</div>"
                "<div class=\"logic-muted\">This board belongs to the Object Type and applies to every instance.</div>"
                "<button class=\"logic-btn primary";
        if (playing) html += " disabled";
        html += "\" data-action=\"create-logic-board\">Create Logic Board</button></div>";
        render();
        return;
    }

    const LogicBoardDef& board = *objectType.logicBoard;
    const LogicKeyBindingEditorState keyBinding{
        keyCaptureAddress_, keySearchAddress_, keySearchQuery_};
    const LogicExpressionFieldState expressionField{
        expressionFocusAddress_, expressionDraftText_, expressionErrorMessage_,
        expressionEdited_};
    LogicVariableCreationFieldState variableCreation;
    if (hasContextualGlobalVariableDraft()) {
        variableCreation.propertyAddress = contextualVariableAddress_;
        variableCreation.key = contextualVariableKey_;
        variableCreation.errorMessage = contextualVariableError_;
        variableCreation.requiredType = contextualVariableRequiredType_;
    }

    if (view.tab == LogicBoardTab::GeneratedLua) {
        const Logic::LogicCompileResult compiled = Logic::compileBoard(
            selectedId, board, selectedType, &coordinator.document().data());
        html += "<div id=\"logic-scroll\" class=\"logic-scroll\">";
        for (const Logic::LogicDiagnostic& diagnostic : compiled.diagnostics) {
            if (diagnostic.severity != Logic::DiagnosticSeverity::Error) continue;
            html += "<div class=\"logic-diagnostic\">" + escapeRml(diagnostic.code)
                 + " · " + escapeRml(diagnostic.ruleId) + " · "
                 + escapeRml(diagnostic.propertyKey) + " — "
                 + escapeRml(diagnostic.message) + "</div>";
        }
        if (compiled.ok() && !compiled.programs.empty())
            html += "<div class=\"logic-code\">" + escapeRml(compiled.programs.front().source) + "</div>";
        else
            html += "<div class=\"logic-empty\"><div class=\"logic-muted\">No source is generated while blocking diagnostics exist.</div></div>";
        html += "</div>";
        render();
        return;
    }

    const auto authoringDiagnostics = Logic::validateBoard(
        selectedId, board, selectedType, &coordinator.document().data(),
        Logic::LogicValidationPurpose::AuthoringDiagnostics);
    if (view.focusRuleId) {
        collapsedRuleIds_.erase(*view.focusRuleId);
        pendingRevealRuleId_ = *view.focusRuleId;
    }

    const std::size_t incompatibleCount = static_cast<std::size_t>(std::count_if(
        authoringDiagnostics.begin(), authoringDiagnostics.end(),
        [](const Logic::LogicDiagnostic& d) {
            return d.code == "LB_INCOMPATIBLE_BLOCK"
                && d.severity == Logic::DiagnosticSeverity::Error;
        }));

    // Collapse All/Expand All/Remove Board now live in the static toolbar and
    // the "..." menu (see EditorUi::refreshToolbar/toggleLogicMoreMenu) — this
    // panel no longer generates a separate tools row for them.
    html += "<div id=\"logic-scroll\" class=\"logic-scroll\">";
    if (incompatibleCount > 0 && !playing) {
        html += "<div class=\"logic-repair-banner\">"
                "<div class=\"logic-repair-title\">"
              + std::to_string(incompatibleCount)
              + (incompatibleCount == 1 ? " incompatible block" : " incompatible blocks")
              + "</div>"
                "<div class=\"logic-muted\">Blocks stay editable. Repair does not restore "
                "removed components.</div>"
                "<div class=\"logic-repair-actions\">"
                "<button class=\"panel-btn\" data-action=\"repair-logic-disable-rules\">"
                "Disable Affected Rules</button>"
                "<button class=\"panel-btn\" data-action=\"repair-logic-remove-actions\">"
                "Remove Affected Actions</button>"
                "<button class=\"panel-btn\" data-action=\"repair-logic-remove-rules\">"
                "Remove Affected Rules</button>"
                "</div></div>";
    }

    const std::string query = lower(view.search);
    std::size_t renderedRules = 0;
    for (std::size_t ruleIndex = 0; ruleIndex < board.rules.size(); ++ruleIndex) {
        const LogicRuleDef& rule = board.rules[ruleIndex];
        const std::string summary = logicRuleSummary(rule);
        std::string searchable = lower(rule.id + " " + rule.trigger.typeId + " " + summary);
        for (const LogicConditionClause& clause : rule.conditions)
            searchable += " " + lower(clause.block.typeId);
        for (const LogicActionDef& action : rule.actions)
            searchable += " " + lower(action.block.typeId);
        if (!query.empty() && searchable.find(query) == std::string::npos) continue;
        ++renderedRules;

        const bool collapsed = collapsedRuleIds_.count(rule.id) != 0;
        const std::size_t diagnosticCount = static_cast<std::size_t>(std::count_if(
            authoringDiagnostics.begin(), authoringDiagnostics.end(),
            [&](const Logic::LogicDiagnostic& d) {
                return d.ruleId == rule.id
                    && d.severity == Logic::DiagnosticSeverity::Error;
            }));

        html += "<div id=\"logic-rule-" + std::to_string(ruleIndex) + "\" class=\"logic-rule"
                + std::string(rule.enabled ? "" : " off")
                + std::string(view.focusRuleId && *view.focusRuleId == rule.id
                                  ? " logic-rule-focus" : "")
                + "\">"
                "<div class=\"logic-rule-head\""
                " data-action=\"toggle-logic-rule-collapsed\""
                " data-arg=\"" + escapeRml(rule.id) + "\">";
        // Caret is visual only — the whole .logic-rule-head carries the
        // toggle action (right-side On/clone/move/delete buttons keep their
        // own data-action and win the click walk-up first).
        html += "<span class=\"logic-rule-caret\">"
             + iconMarkup(collapsed ? "" UI_ICON_COLLAPSE "" : "" UI_ICON_EXPAND "") + "</span>";
        html += "<span class=\"logic-rule-index\">" + std::to_string(ruleIndex + 1) + "</span>";
        html += "<span class=\"logic-rule-title\">" + escapeRml(summary) + "</span>";
        if (diagnosticCount > 0) {
            html += "<span class=\"logic-rule-diagnostic-count\">" + std::to_string(diagnosticCount)
                 + (diagnosticCount == 1 ? " error" : " errors") + "</span>";
        }
        html += "<button class=\"panel-btn\"";
        if (playing) html += " disabled=\"disabled\"";
        html += " data-action=\"toggle-logic-rule\" data-arg=\"" + escapeRml(rule.id) + "\">"
             + std::string(rule.enabled ? "On" : "Off") + "</button>";
        auto iconButton = [&](const char* action, const std::string& label, bool disabled = false,
                              const char* title = nullptr) {
            html += "<button class=\"logic-icon-btn";
            if (playing || disabled) html += " disabled";
            html += "\"";
            if (title) html += " title=\"" + std::string(title) + "\" aria-label=\""
                                + std::string(title) + "\"";
            html += " data-action=\"" + std::string(action) + "\" data-arg=\""
                 + escapeRml(rule.id) + "\">" + label + "</button>";
        };
        iconButton("duplicate-logic-rule", iconMarkup("" UI_ICON_DUPLICATE ""), false, "Clone rule");
        iconButton("move-logic-rule-up", "↑", ruleIndex == 0);
        iconButton("move-logic-rule-down", "↓", ruleIndex + 1 == board.rules.size());
        html += "<button class=\"comp-remove";
        if (playing) html += " disabled";
        html += "\" data-action=\"remove-logic-rule\" data-arg=\"" + escapeRml(rule.id)
             + "\" title=\"Delete rule\">" + iconMarkup("" UI_ICON_DELETE "") + "</button>";
        html += "</div>"; // .logic-rule-head

        if (collapsed) {
            html += "</div>"; // .logic-rule
            continue;
        }

        html += "<div class=\"logic-rule-body\">";

        // WHEN — trigger/predicate plus projected execution mode (not a Condition).
        html += "<div class=\"logic-rule-col event-col\">"
                "<div class=\"logic-col-head\">WHEN</div>"
                "<div class=\"logic-col-content\">"
                "<div class=\"logic-block\">";
        const std::string triggerDropdownId = "trigger|" + rule.id;
        const bool triggerOpen = openDropdownId_ == triggerDropdownId && !playing;
        html += dropdownTriggerMarkup(descriptorLabel(rule.trigger.typeId), "toggle-logic-dropdown",
                                      triggerDropdownId, triggerOpen, playing);
        if (triggerOpen) {
            html += catalogEntries(dropdownNav_, objectType, nullptr, Logic::BlockKind::Trigger,
                                   rule.trigger.typeId, triggerDropdownId,
                                   "change-logic-trigger", rule.id, /*eventCatalog=*/true);
        }
        html += renderLogicProperties(
            coordinator.document(), selectedType, rule.trigger,
            LogicPropertyAddress{rule.id, {}, LogicPropertyTarget::Trigger, 0},
            openDropdownId_, keyBinding, expressionField, playing,
            variableCreation);
        html += "</div>"; // .logic-block (WHEN trigger)
        html += "</div>"; // .logic-col-content
        html += "</div>"; // event-col

        // IF — zero or more authored clauses. The connector belongs to the
        // clause (not to its block); all block properties use the same
        // descriptor-driven projection as WHEN/THEN.
        html += "<div class=\"logic-rule-col conditions-col\">"
                "<div class=\"logic-col-head\">IF</div>"
                "<div class=\"logic-col-content\">";
        if (rule.conditions.empty())
            html += "<div class=\"logic-col-empty\">No additional conditions</div>";
        for (std::size_t conditionIndex = 0;
             conditionIndex < rule.conditions.size(); ++conditionIndex) {
            const LogicConditionClause& clause = rule.conditions[conditionIndex];
            const std::string arg = rule.id + "|" + std::to_string(conditionIndex);
            const std::string dropdownId = "condition|" + arg;
            const bool dropdownOpen = openDropdownId_ == dropdownId && !playing;
            html += "<div class=\"logic-block logic-condition-block\">"
                    "<div class=\"logic-condition-toolbar\">";
            if (conditionIndex == 0) {
                html += "<span class=\"logic-condition-join fixed\">AND</span>";
            } else {
                html += modeOption("AND", "and",
                    clause.joinBefore == LogicConditionJoin::And,
                    "set-logic-condition-join", arg, playing);
                html += modeOption("OR", "or",
                    clause.joinBefore == LogicConditionJoin::Or,
                    "set-logic-condition-join", arg, playing);
            }
            html += "<button class=\"logic-btn";
            if (clause.negated) html += " active";
            if (playing) html += " disabled";
            html += "\" data-action=\"toggle-logic-condition-negated\" data-arg=\""
                  + escapeRml(arg) + "\">NOT</button>";
            html += "<button class=\"logic-icon-btn";
            if (playing || conditionIndex == 0) html += " disabled";
            html += "\" data-action=\"move-logic-condition-up\" data-arg=\""
                  + escapeRml(arg) + "\">↑</button><button class=\"logic-icon-btn";
            if (playing || conditionIndex + 1 == rule.conditions.size()) html += " disabled";
            html += "\" data-action=\"move-logic-condition-down\" data-arg=\""
                  + escapeRml(arg) + "\">↓</button><button class=\"comp-remove";
            if (playing) html += " disabled";
            html += "\" data-action=\"remove-logic-condition\" data-arg=\""
                  + escapeRml(arg) + "\" title=\"Delete condition\">"
                  + iconMarkup("" UI_ICON_DELETE "") + "</button></div>";
            html += dropdownTriggerMarkup(
                descriptorLabel(clause.block.typeId), "toggle-logic-dropdown",
                dropdownId, dropdownOpen, playing);
            if (dropdownOpen) {
                html += catalogEntries(
                    dropdownNav_, objectType, Logic::findDescriptor(rule.trigger.typeId),
                    Logic::BlockKind::Condition, clause.block.typeId, dropdownId,
                    "change-logic-condition", arg);
            }
            html += renderLogicProperties(
                coordinator.document(), selectedType, clause.block,
                LogicPropertyAddress{
                    rule.id, {}, LogicPropertyTarget::Condition, conditionIndex},
                openDropdownId_, keyBinding, expressionField, playing,
                variableCreation);
            html += "</div>";
        }
        html += "</div>";
        const std::string addConditionDropdownId = "add-condition|" + rule.id;
        const bool addConditionOpen =
            openDropdownId_ == addConditionDropdownId && !playing;
        html += "<div class=\"logic-col-footer\"><button class=\"logic-btn";
        if (playing) html += " disabled";
        html += "\" data-action=\"toggle-logic-dropdown\" data-arg=\""
              + escapeRml(addConditionDropdownId) + "\">+ Add Condition</button></div>";
        if (addConditionOpen) {
            html += catalogEntries(
                dropdownNav_, objectType, Logic::findDescriptor(rule.trigger.typeId),
                Logic::BlockKind::Condition, {}, addConditionDropdownId,
                "add-logic-condition-type", rule.id);
        }
        html += "</div>"; // conditions-col

        // THEN — empty actions are authoring-valid (ADR-0013 Slice 2).
        html += "<div class=\"logic-rule-col actions-col\">"
                "<div class=\"logic-col-head\">THEN</div>"
                "<div class=\"logic-col-content\">";
        const std::vector<LogicActionDef>& actions = rule.actions;
        if (actions.empty()) html += "<div class=\"logic-muted\">No actions yet.</div>";
        for (std::size_t actionIndex = 0; actionIndex < actions.size(); ++actionIndex) {
            const LogicActionDef& actionDef = actions[actionIndex];
            const LogicBlockDef& action = actionDef.block;
            const std::string arg = actionArg(rule.id, actionDef.id);
            const std::string dropdownId = "action|" + arg;
            const bool dropdownOpen = openDropdownId_ == dropdownId && !playing;
            html += "<div class=\"logic-block\"><div class=\"logic-action-row\"><div class=\"logic-action-main\">"
                 + dropdownTriggerMarkup(descriptorLabel(action.typeId), "toggle-logic-dropdown",
                                       dropdownId, dropdownOpen, playing)
                 + "</div><button class=\"logic-icon-btn";
            if (playing || actionIndex == 0) html += " disabled";
            html += "\" data-action=\"move-logic-action-up\" data-arg=\"" + escapeRml(arg) + "\">↑</button>"
                    "<button class=\"logic-icon-btn";
            if (playing || actionIndex + 1 == actions.size()) html += " disabled";
            html += "\" data-action=\"move-logic-action-down\" data-arg=\"" + escapeRml(arg) + "\">↓</button>"
                    "<button class=\"comp-remove";
            if (playing) html += " disabled";
            html += "\" data-action=\"remove-logic-action\" data-arg=\"" + escapeRml(arg)
                 + "\" title=\"Delete action\">" + iconMarkup("" UI_ICON_DELETE "") + "</button></div>";
            if (dropdownOpen) {
                html += catalogEntries(dropdownNav_, objectType,
                                       Logic::findDescriptor(rule.trigger.typeId),
                                       Logic::BlockKind::Action, action.typeId, dropdownId,
                                       "change-logic-action", arg);
            }
            if (action.typeId == Logic::kSetVisible) {
                bool visible = true;
                if (const LogicPropertyDef* p = property(action, "visible"))
                    if (const auto* v = std::get_if<bool>(&p->value)) visible = *v;
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Self · visible</span>"
                     + renderLogicBooleanChoice("set-logic-visible", arg, visible, playing)
                     + "</div>";
            } else if (action.typeId == Logic::kAnimationPlayClip) {
                AssetId selectedAsset;
                std::string selectedClip;
                if (const LogicPropertyDef* p = property(action, "animationAssetId"))
                    if (const auto* v = std::get_if<LogicAssetReference>(&p->value)) selectedAsset = v->id;
                if (const LogicPropertyDef* p = property(action, "clipId"))
                    if (const auto* v = std::get_if<LogicStringValue>(&p->value)) selectedClip = v->value;
                const SpriteAnimationAssetDef* asset = selectedAsset.empty()
                    ? nullptr : coordinator.document().findSpriteAnimationAsset(selectedAsset);

                const std::string assetDropdownId = "animation-asset|" + arg;
                const bool assetOpen = openDropdownId_ == assetDropdownId && !playing;
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Animation</span>"
                     + dropdownTriggerMarkup(asset ? animationAssetLabel(*asset) : "Choose Animation",
                                             "toggle-logic-dropdown", assetDropdownId,
                                             assetOpen, playing) + "</div>";
                if (assetOpen) {
                    html += "<div class=\"drop-list logic-key-list\">";
                    std::vector<const SpriteAnimationAssetDef*> assets;
                    for (const SpriteAnimationAssetDef& candidate :
                         coordinator.document().data().spriteAnimationAssets) {
                        if (!candidate.clips.empty()) assets.push_back(&candidate);
                    }
                    std::sort(assets.begin(), assets.end(),
                        [](const SpriteAnimationAssetDef* a, const SpriteAnimationAssetDef* b) {
                            return a->id < b->id;
                        });
                    for (const SpriteAnimationAssetDef* candidate : assets) {
                        html += dropEntry(dropdownNav_, animationAssetLabel(*candidate), candidate->id,
                                          candidate->id == selectedAsset, assetDropdownId,
                                          "set-logic-animation-asset", arg);
                    }
                    html += "</div>";
                }

                const std::string clipDropdownId = "animation-clip|" + arg;
                const bool clipOpen = openDropdownId_ == clipDropdownId && !playing;
                std::string selectedClipLabel = selectedClip.empty() ? "Choose Clip" : selectedClip;
                if (asset) {
                    for (const SpriteAnimationClipDef& clip : asset->clips) {
                        if (clip.id == selectedClip) selectedClipLabel = clipLabel(clip);
                    }
                }
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Clip</span>"
                     + dropdownTriggerMarkup(selectedClipLabel, "toggle-logic-dropdown",
                                             clipDropdownId, clipOpen, playing || !asset)
                     + "</div>";
                if (clipOpen && asset) {
                    html += "<div class=\"drop-list logic-key-list\">";
                    for (const SpriteAnimationClipDef& clip : asset->clips) {
                        html += dropEntry(dropdownNav_, clipLabel(clip), clip.id, clip.id == selectedClip,
                                          clipDropdownId, "set-logic-animation-clip", arg);
                    }
                    html += "</div>";
                }
            } else if (action.typeId == Logic::kAnimationSetPlaybackSpeed) {
                double speed = 1.0;
                if (const LogicPropertyDef* p = property(action, "speed"))
                    if (const auto* v = std::get_if<NumberExpression>(&p->value))
                        speed = literalNumberValue(*v).value_or(1.0);
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Self - speed</span>"
                        "<input type=\"text\" class=\"logic-value-input\""
                        " data-action=\"commit-logic-animation-speed\" data-arg=\""
                     + escapeRml(arg) + "\" value=\"" + number(speed) + "\"";
                if (playing) html += " disabled=\"disabled\"";
                html += "/></div>";
            } else if (action.typeId == Logic::kAnimationStop) {
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Self - stop playback</span></div>";
            } else if (action.typeId == Logic::kAudioPlaySound) {
                AssetId selectedAudio;
                if (const LogicPropertyDef* p = property(action, "audioAssetId"))
                    if (const auto* v = std::get_if<LogicAssetReference>(&p->value)) selectedAudio = v->id;
                const AudioAssetDef* audio = selectedAudio.empty()
                    ? nullptr : coordinator.document().findAudioAsset(selectedAudio);

                const std::string audioDropdownId = "audio-asset|" + arg;
                const bool audioOpen = openDropdownId_ == audioDropdownId && !playing;
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Sound</span>"
                     + dropdownTriggerMarkup(
                           audio ? resolveAudioAssetDisplayName(coordinator.document(), *audio)
                                 : "Choose Sound",
                           "toggle-logic-dropdown", audioDropdownId,
                           audioOpen, playing) + "</div>";
                if (audioOpen) {
                    html += "<div class=\"drop-list logic-key-list\">";
                    // Only StaticSound assets are playable here (LB_AUDIO_REQUIRES_STATIC) —
                    // Stream (music) gets its own action family later.
                    std::vector<const AudioAssetDef*> assets;
                    for (const AudioAssetDef& candidate : coordinator.document().data().audioAssets) {
                        if (candidate.loadMode == AudioLoadMode::StaticSound) assets.push_back(&candidate);
                    }
                    std::sort(assets.begin(), assets.end(),
                        [](const AudioAssetDef* a, const AudioAssetDef* b) {
                            return a->assetId < b->assetId;
                        });
                    if (assets.empty()) {
                        html += "<div class=\"logic-col-empty\">No audio assets available"
                                "<div class=\"logic-col-empty-sub\">"
                                "Import audio or generate an SFX first.</div></div>";
                    } else {
                        for (const AudioAssetDef* candidate : assets) {
                            html += dropEntry(
                                dropdownNav_,
                                resolveAudioAssetDisplayName(coordinator.document(), *candidate),
                                candidate->assetId,
                                candidate->assetId == selectedAudio, audioDropdownId,
                                "set-logic-audio-asset", arg);
                        }
                    }
                    html += "</div>";
                }

                double volume = 1.0;
                if (const LogicPropertyDef* p = property(action, "volume"))
                    if (const auto* v = std::get_if<NumberExpression>(&p->value))
                        volume = literalNumberValue(*v).value_or(1.0);
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Volume</span>"
                        "<input type=\"text\" class=\"logic-value-input\""
                        " data-action=\"commit-logic-audio-volume\" data-arg=\""
                     + escapeRml(arg) + "\" value=\"" + number(volume) + "\"";
                if (playing) html += " disabled=\"disabled\"";
                html += "/></div>";
            } else {
                html += renderLogicProperties(
                    coordinator.document(), selectedType, action,
                    LogicPropertyAddress{
                        rule.id, actionDef.id, LogicPropertyTarget::Action, actionIndex},
                    openDropdownId_, keyBinding, expressionField, playing,
                    variableCreation);
            }
            const std::string runDropdownId = "action-run|" + arg;
            const bool runDropdownOpen = openDropdownId_ == runDropdownId && !playing;
            html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Run</span>"
                 + dropdownTriggerMarkup(
                       executionModeLabel(actionDef.executionMode),
                       "toggle-logic-dropdown", runDropdownId,
                       runDropdownOpen, playing)
                 + "</div>";
            if (runDropdownOpen) {
                html += "<div class=\"drop-list logic-key-list\">"
                     + dropEntry(
                           dropdownNav_, "Every occurrence", "every_occurrence",
                           actionDef.executionMode == LogicExecutionMode::EveryOccurrence,
                           runDropdownId, "set-logic-action-execution-mode", arg)
                     + dropEntry(
                           dropdownNav_, "Once per activation", "once_per_activation",
                           actionDef.executionMode == LogicExecutionMode::OncePerActivation,
                           runDropdownId, "set-logic-action-execution-mode", arg)
                     + "</div>";
            }
            html += "</div>";
        }
        const std::string addActionDropdownId = "add-action|" + rule.id;
        const bool addActionOpen = openDropdownId_ == addActionDropdownId && !playing;
        html += "<div class=\"logic-col-footer\"><button class=\"logic-btn logic-add-action";
        const std::size_t actionCount = actions.size();
        if (playing || actionCount >= Logic::kMaxActionsPerRule) html += " disabled";
        html += "\" data-action=\"toggle-logic-dropdown\" data-arg=\"" + escapeRml(addActionDropdownId)
             + "\">+ Add Action</button></div>";
        if (addActionOpen) {
            html += catalogEntries(dropdownNav_, objectType,
                                   Logic::findDescriptor(rule.trigger.typeId),
                                   Logic::BlockKind::Action, {}, addActionDropdownId,
                                   "add-logic-action-type", rule.id);
        }
        html += "</div>"; // .logic-col-content
        html += "</div>"; // actions-col

        html += "</div>"; // .logic-rule-body
        html += "<div class=\"logic-rule-diagnostics\">";
        for (const Logic::LogicDiagnostic& diagnostic : authoringDiagnostics) {
            if (diagnostic.ruleId != rule.id) continue;
            const bool warning = diagnostic.severity == Logic::DiagnosticSeverity::Warning;
            const bool focused = view.focusRuleId && *view.focusRuleId == rule.id
                && (view.highlightBlockTypeId.empty()
                    || view.highlightBlockTypeId == diagnostic.blockTypeId)
                && (view.highlightPropertyKey.empty()
                    || view.highlightPropertyKey == diagnostic.propertyKey);
            const std::string navArg = selectedId + "|" + diagnostic.ruleId + "|"
                + diagnostic.blockTypeId + "|" + diagnostic.propertyKey;
            html += "<button class=\"logic-diagnostic"
                 + std::string(warning ? " warning" : "")
                 + std::string(focused ? " logic-diagnostic-focus" : "")
                 + "\" data-action=\"focus-logic-diagnostic\" data-arg=\""
                 + escapeRml(navArg) + "\">"
                 + escapeRml(diagnostic.code) + " · "
                 + escapeRml(diagnostic.propertyKey) + " — " + escapeRml(diagnostic.message)
                 + "</button>";
        }
        html += "</div>"; // .logic-rule-diagnostics
        html += "</div>"; // .logic-rule
    }
    if (renderedRules == 0 && !query.empty()) {
        html += "<div class=\"logic-empty\"><div class=\"logic-muted\">No rules match &quot;"
             + escapeRml(view.search) + "&quot;</div></div>";
    }
    // A second, always-visible entry point for the same add-logic-rule action
    // the toolbar's "+ Rule" button already dispatches — the natural
    // continuation of the list, not a new action or Command path.
    html += "<button class=\"logic-add-rule-footer";
    if (playing) html += " disabled";
    html += "\" data-action=\"add-logic-rule\">+ Add Logic</button>";
    html += "</div>";
    render();
}

void LogicBoardPanel::restoreAfterLayout(Rml::ElementDocument* document,
                                         const EditorCoordinator& coordinator) const {
    if (!document) return;

    if (scrollRestorePending_) {
        if (Rml::Element* scroll = document->GetElementById("logic-scroll"))
            scroll->SetScrollTop(scrollTop_);
        scrollRestorePending_ = false;
    }

    // ADR-0029: put the caret back in the expression field the author is in.
    // The id exists on the focused field only, so this can only ever target it.
    if (expressionFocusRestorePending_) {
        expressionFocusRestorePending_ = false;
        if (Rml::Element* input = document->GetElementById("logic-expression-input")) {
            input->Focus(true);
            if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControlInput*>(input)) {
                // A field opened for the first time has no captured caret; the
                // end is where an author continues from, and it is the one
                // position from which Backspace always does something.
                const int end = static_cast<int>(control->GetValue().size());
                const int start = expressionCaretStart_ < 0
                    ? end : std::min(expressionCaretStart_, end);
                const int stop = expressionCaretEnd_ < 0
                    ? end : std::min(expressionCaretEnd_, end);
                control->SetSelectionRange(start, stop);
            }
        }
    }

    if (contextualVariableFocusRestorePending_) {
        contextualVariableFocusRestorePending_ = false;
        if (Rml::Element* input =
                document->GetElementById("logic-context-variable-name-input")) {
            input->Focus(true);
            if (auto* control =
                    rmlui_dynamic_cast<Rml::ElementFormControlInput*>(input)) {
                const int end = static_cast<int>(control->GetValue().size());
                control->SetSelectionRange(end, end);
            }
        }
    }

    if (pendingRevealRuleId_.empty()) return;
    if (const LogicBoardDef* board = currentBoard(coordinator)) {
        const auto it = std::find_if(board->rules.begin(), board->rules.end(),
            [&](const LogicRuleDef& rule) { return rule.id == pendingRevealRuleId_; });
        if (it != board->rules.end()) {
            const std::string cardId = "logic-rule-" + std::to_string(
                static_cast<std::size_t>(it - board->rules.begin()));
            if (Rml::Element* card = document->GetElementById(cardId)) {
                card->ScrollIntoView(Rml::ScrollIntoViewOptions{
                    Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
                    Rml::ScrollBehavior::Smooth});
            }
        }
    }
    pendingRevealRuleId_.clear();
}

void LogicBoardPanel::toggleDropdown(Rml::ElementDocument* document,
                                     const EditorCoordinator& coordinator,
                                     const std::string& dropdownId) {
    openDropdownId_ = (openDropdownId_ == dropdownId) ? std::string() : dropdownId;
    dropdownNav_.resetSession();
    refresh(document, coordinator);
}

void LogicBoardPanel::moveDropdownHighlight(Rml::ElementDocument* document,
                                            const EditorCoordinator& coordinator,
                                            int delta) const {
    if (openDropdownId_.empty()) return;
    dropdownNav_.move(delta);
    refresh(document, coordinator);
}

std::optional<DropdownNavEntry> LogicBoardPanel::dropdownHighlightCommit() const {
    return dropdownNav_.commit();
}

void LogicBoardPanel::beginKeyCapture(Rml::ElementDocument* document,
                                      const EditorCoordinator& coordinator,
                                      const std::string& propertyAddress) {
    if (coordinator.isPlaying() || propertyAddress.empty()) return;
    keyCaptureAddress_ = propertyAddress;
    keySearchAddress_.clear();
    keySearchQuery_.clear();
    openDropdownId_.clear();
    dropdownNav_.resetSession();
    refresh(document, coordinator);
    // The clicked button must not retain RmlUi focus: otherwise Space/Enter
    // can activate that old button before the native capture router sees the
    // key. Capture has no text caret, so it intentionally owns focus-free
    // keyboard input for its short lifetime.
    if (document && document->GetContext()) {
        if (Rml::Element* focus = document->GetContext()->GetFocusElement()) focus->Blur();
    }
}

void LogicBoardPanel::toggleKeySearch(Rml::ElementDocument* document,
                                      const EditorCoordinator& coordinator,
                                      const std::string& propertyAddress) {
    if (coordinator.isPlaying() || propertyAddress.empty()) return;
    keyCaptureAddress_.clear();
    openDropdownId_.clear();
    dropdownNav_.resetSession();
    if (keySearchAddress_ == propertyAddress) {
        keySearchAddress_.clear();
        keySearchQuery_.clear();
    } else {
        keySearchAddress_ = propertyAddress;
        keySearchQuery_.clear();
    }
    refresh(document, coordinator);
}

void LogicBoardPanel::setKeySearchQuery(Rml::ElementDocument* document,
                                        const EditorCoordinator& coordinator,
                                        const std::string& propertyAddress,
                                        std::string query) {
    if (keySearchAddress_ != propertyAddress) return;
    keySearchQuery_ = std::move(query);
    refresh(document, coordinator);
}

void LogicBoardPanel::focusExpressionField(Rml::ElementDocument* document,
                                           const EditorCoordinator& coordinator,
                                           const std::string& address) {
    if (expressionFocusAddress_ == address) return;
    // Moving to another field abandons the previous draft: it never parsed, so
    // there is nothing to keep, and carrying it over would show one field's
    // half-written text under another.
    expressionFocusAddress_ = address;
    expressionDraftText_.clear();
    expressionErrorMessage_.clear();
    expressionEdited_ = false;
    refresh(document, coordinator);
}

void LogicBoardPanel::setExpressionDraft(Rml::ElementDocument* document,
                                         const EditorCoordinator& coordinator,
                                         const std::string& address,
                                         std::string text) {
    if (expressionFocusAddress_ != address) return;
    expressionDraftText_ = std::move(text);
    expressionEdited_ = true;
    // Typing clears the previous complaint; it is about text that no longer
    // exists. The next commit decides whether there is a new one.
    expressionErrorMessage_.clear();

    // Narrow the list in place. A full refresh here would destroy the field
    // being typed into on every keystroke: the caret goes with it, so
    // Backspace lands at offset 0 and deletes nothing, Delete eats the wrong
    // end, and RmlUi is left mid-dispatch inside a freed element.
    Rml::Element* list =
        document ? document->GetElementById(kLogicExpressionCompletionsId) : nullptr;
    if (!list) {
        // No list on screen yet (first draft after a full paint) — fall back.
        refresh(document, coordinator);
        return;
    }
    const EntityDef* owner = nullptr;
    if (const auto& objectTypeId = coordinator.state().logicBoardEditor.objectTypeId)
        owner = coordinator.document().findObjectType(*objectTypeId);
    list->SetInnerRML(renderLogicExpressionCompletionEntries(
        coordinator.document(), owner, address, expressionDraftText_));

    // The field already holds what the author typed; only a completion picked
    // from the list puts text there that the field does not have yet.
    if (Rml::Element* input = document->GetElementById("logic-expression-input")) {
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input)) {
            if (control->GetValue() != expressionDraftText_) {
                control->SetValue(expressionDraftText_);
                if (auto* text_input =
                        rmlui_dynamic_cast<Rml::ElementFormControlInput*>(input)) {
                    const int end = static_cast<int>(expressionDraftText_.size());
                    text_input->SetSelectionRange(end, end);
                }
                input->Focus(true);
            }
        }
    }
}

void LogicBoardPanel::setExpressionFailure(Rml::ElementDocument* document,
                                           const EditorCoordinator& coordinator,
                                           const std::string& address,
                                           std::string text, std::string message) {
    // One step, not setExpressionDraft + setExpressionError: the draft setter
    // is guarded by focus while the error setter assigns it, so as two calls
    // the author's text survived only because of the order they happened to be
    // in — and was dropped outright if the commit arrived without focus. The
    // ADR requires the typed text to survive a parse failure, so focus, draft
    // and message move together.
    expressionFocusAddress_ = address;
    expressionDraftText_ = std::move(text);
    expressionErrorMessage_ = std::move(message);
    expressionEdited_ = true;
    refresh(document, coordinator);
}

void LogicBoardPanel::beginContextualGlobalVariable(
    Rml::ElementDocument* document, const EditorCoordinator& coordinator,
    const ObjectTypeId& objectTypeId, const std::string& propertyAddress,
    GameVariableDefinition::Type requiredType, std::string suggestedKey) {
    if (coordinator.isPlaying() || objectTypeId.empty()
        || propertyAddress.empty()) return;
    contextualVariableObjectTypeId_ = objectTypeId;
    contextualVariableAddress_ = propertyAddress;
    contextualVariableRequiredType_ = requiredType;
    contextualVariableKey_ = std::move(suggestedKey);
    contextualVariableError_.clear();
    contextualVariableSourceRevision_ = coordinator.document().revision();
    openDropdownId_.clear();
    dropdownNav_.resetSession();
    clearKeyBindingEditor();
    clearExpressionField();
    refresh(document, coordinator);
}

void LogicBoardPanel::setContextualGlobalVariableKey(
    Rml::ElementDocument* document, const std::string& propertyAddress,
    std::string key) {
    if (contextualVariableAddress_ != propertyAddress) return;
    contextualVariableKey_ = std::move(key);
    contextualVariableError_.clear();
    // Typing is presentation-only and must not rebuild the input mid-dispatch.
    if (!document) return;
    if (Rml::Element* input =
            document->GetElementById("logic-context-variable-name-input")) {
        input->SetClass("invalid", false);
    }
    if (Rml::Element* error =
            document->GetElementById("logic-context-variable-error")) {
        error->SetInnerRML("");
    }
}

void LogicBoardPanel::setContextualGlobalVariableError(
    Rml::ElementDocument* document, const EditorCoordinator& coordinator,
    const std::string& propertyAddress, std::string message) {
    if (contextualVariableAddress_ != propertyAddress) return;
    contextualVariableError_ = std::move(message);
    refresh(document, coordinator);
}

std::optional<ContextualGlobalVariableDraft>
LogicBoardPanel::contextualGlobalVariableDraft() const {
    if (!hasContextualGlobalVariableDraft()) return std::nullopt;
    return ContextualGlobalVariableDraft{
        contextualVariableObjectTypeId_, contextualVariableAddress_,
        contextualVariableRequiredType_, contextualVariableKey_};
}

void LogicBoardPanel::cancelKeyCapture(Rml::ElementDocument* document,
                                       const EditorCoordinator& coordinator) {
    if (keyCaptureAddress_.empty()) return;
    keyCaptureAddress_.clear();
    refresh(document, coordinator);
}

void LogicBoardPanel::toggleVariablesDrawer(
    Rml::ElementDocument* document, const EditorCoordinator& coordinator) {
    variablesDrawerOpen_ = !variablesDrawerOpen_;
    openDropdownId_.clear();
    dropdownNav_.resetSession();
    refresh(document, coordinator);
}

const LogicBoardDef* LogicBoardPanel::currentBoard(const EditorCoordinator& coordinator) const {
    if (renderedObjectTypeId_.empty()
        || !coordinator.document().hasObjectType(renderedObjectTypeId_)) return nullptr;
    const EntityDef& objectType = coordinator.document().data().objectTypes.at(renderedObjectTypeId_);
    return objectType.logicBoard ? &*objectType.logicBoard : nullptr;
}

void LogicBoardPanel::toggleRuleCollapsed(Rml::ElementDocument* document,
                                          const EditorCoordinator& coordinator,
                                          const LogicRuleId& ruleId) {
    const LogicBoardDef* board = currentBoard(coordinator);
    if (!board) return;
    const bool exists = std::any_of(board->rules.begin(), board->rules.end(),
        [&](const LogicRuleDef& rule) { return rule.id == ruleId; });
    if (!exists) return;
    // A dropdown open inside a rule about to collapse must not silently
    // reappear open when the rule is later re-expanded.
    openDropdownId_.clear();
    dropdownNav_.resetSession();
    clearKeyBindingEditor();
    discardContextualGlobalVariable();
    if (collapsedRuleIds_.erase(ruleId) == 0) collapsedRuleIds_.insert(ruleId);
    refresh(document, coordinator);
}

void LogicBoardPanel::collapseAllRules(Rml::ElementDocument* document,
                                       const EditorCoordinator& coordinator) {
    if (const LogicBoardDef* board = currentBoard(coordinator)) {
        openDropdownId_.clear();
        dropdownNav_.resetSession();
        clearKeyBindingEditor();
        discardContextualGlobalVariable();
        for (const LogicRuleDef& rule : board->rules) collapsedRuleIds_.insert(rule.id);
    }
    refresh(document, coordinator);
}

void LogicBoardPanel::expandAllRules(Rml::ElementDocument* document,
                                     const EditorCoordinator& coordinator) {
    openDropdownId_.clear();
    dropdownNav_.resetSession();
    collapsedRuleIds_.clear();
    refresh(document, coordinator);
}

bool LogicBoardPanel::canCollapseAllRules(const EditorCoordinator& coordinator) const {
    const LogicBoardDef* board = currentBoard(coordinator);
    if (!board || board->rules.empty()) return false;
    return std::any_of(board->rules.begin(), board->rules.end(),
        [&](const LogicRuleDef& rule) { return collapsedRuleIds_.count(rule.id) == 0; });
}

bool LogicBoardPanel::canExpandAllRules(const EditorCoordinator& coordinator) const {
    const LogicBoardDef* board = currentBoard(coordinator);
    if (!board || board->rules.empty()) return false;
    return std::any_of(board->rules.begin(), board->rules.end(),
        [&](const LogicRuleDef& rule) { return collapsedRuleIds_.count(rule.id) != 0; });
}

void LogicBoardPanel::syncResponsiveClass(Rml::ElementDocument* document) const {
    if (!document) return;
    Rml::Element* root = document->GetElementById("logic-board-panel");
    if (!root) return;
    // #center-workspace is always visible (unlike this panel's own root,
    // which carries display:none while Scene mode is active — measuring it
    // directly during that window would read 0 and wrongly force compact).
    Rml::Element* widthSource = document->GetElementById("center-workspace");
    const float width = widthSource ? widthSource->GetClientWidth() : root->GetClientWidth();
    root->SetClass("compact", width > 0.f && width < kCompactWidthThreshold);
}

std::string LogicBoardPanel::objectTypeMenuEntries(const EditorCoordinator& coordinator) const {
    std::vector<ObjectTypeId> typeIds;
    typeIds.reserve(coordinator.document().data().objectTypes.size());
    for (const auto& [id, unused] : coordinator.document().data().objectTypes)
        typeIds.push_back(id);
    std::sort(typeIds.begin(), typeIds.end());

    const auto& view = coordinator.state().logicBoardEditor;
    const ObjectTypeId selectedId = view.objectTypeId
        && coordinator.document().hasObjectType(*view.objectTypeId)
        ? *view.objectTypeId : (typeIds.empty() ? ObjectTypeId{} : typeIds.front());

    std::string html;
    for (const ObjectTypeId& id : typeIds) {
        const EntityDef& type = coordinator.document().data().objectTypes.at(id);
        const std::string label = type.name.empty() ? id : type.name;
        const bool isCurrent = id == selectedId;
        html += "<div class=\"drop-entry";
        if (isCurrent) html += " selected";
        html += "\" data-action=\"select-logic-object-type\" data-value=\""
             + escapeRml(id) + "\">";
        if (isCurrent) html += "<span class=\"drop-mark\">&#x25cf;</span> ";
        html += escapeRml(label) + "</div>";
    }
    return html;
}

} // namespace ArtCade::EditorNative
