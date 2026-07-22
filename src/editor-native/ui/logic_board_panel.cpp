#include "editor-native/ui/logic_board_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/global_variable_commands.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/ui/logic_property_editor.h"
#include "editor-native/ui/ui_markup.h"
#include "logic-core.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Context.h>

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

std::string actionArg(const LogicRuleId& ruleId, std::size_t index) {
    return ruleId + "|" + std::to_string(index);
}

// A `.drop-list` entry: the current value marks itself and just closes the
// list on click (re-picking itself is a no-op); every other entry dispatches
// `pickAction` with the value carried in data-value (never data-arg, which
// stays the addressing key for the actions that need one — see the
// data-value contract enforced by LogicBoardEditorController). `pickArg`, when
// non-empty, is the addressing key riding
// alongside (e.g. the rule a Key picker belongs to).
std::string dropEntry(const std::string& label, const std::string& value, bool isCurrent,
                      const std::string& closeDropdownId, const char* pickAction,
                      const std::string& pickArg) {
    std::string html = "<div class=\"drop-entry";
    if (isCurrent) html += " selected";
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
        "system", "input", "collision", "entity", "platformer",
        "animation", "audio", "variables", "time", "messages",
    };
    return order;
}

std::string catalogEntries(const EntityDef& owner, const Logic::LogicBlockDescriptor* trigger,
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
            html += "<button class=\"drop-entry logic-catalog-entry";
            if (current) html += " selected";
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
    } else {
        head = descriptorLabel(rule.trigger.typeId);
    }
    if (rule.executionMode == LogicExecutionMode::OncePerActivation)
        head += " \xC2\xB7 once per activation";
    if (!rule.actions.empty()) head += " \xE2\x86\x92 " + descriptorLabel(rule.actions.front().typeId);
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
    const EditorCoordinator& coordinator, bool playing,
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
          + iconMarkup("&#xeb5f;") + "</button></div></div>";

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
            html += dropEntry("Number", "number",
                              variable.type == GameVariableDefinition::Type::Number,
                              typeDropdownId, "set-global-variable-type", variable.key);
            html += dropEntry("Boolean", "boolean",
                              variable.type == GameVariableDefinition::Type::Boolean,
                              typeDropdownId, "set-global-variable-type", variable.key);
            html += dropEntry("String", "string",
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
              + "\">" + iconMarkup("&#xeb41;") + "</button></div>";
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
        clearKeyBindingEditor();
        pendingRevealRuleId_.clear();
    }
    if (!document) return;
    Rml::Element* root = document->GetElementById("logic-board-panel");
    if (!root) return;

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
        if (Rml::Element* scroll = document->GetElementById("logic-scroll"))
            scrollTop_ = scroll->GetScrollTop();
    } else {
        renderedObjectTypeId_ = selectedId;
        scrollTop_ = 0.f;
        openDropdownId_.clear();
        clearKeyBindingEditor();
        pendingRevealRuleId_.clear();
        collapsedRuleIds_.clear();
    }
    if (lastTab_ != view.tab) {
        lastTab_ = view.tab;
        openDropdownId_.clear();
        clearKeyBindingEditor();
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
          + "\">Variables ("
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
    if (variablesDrawerOpen_) html += variablesDrawer(coordinator, playing, openDropdownId_);
    const auto render = [&]() {
        root->SetInnerRML(html);
        if (Rml::Element* scroll = document->GetElementById("logic-scroll"))
            scroll->SetScrollTop(scrollTop_);
        if (!pendingRevealRuleId_.empty()) {
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
        if (!keySearchAddress_.empty()) {
            if (Rml::Element* input = document->GetElementById("logic-key-search-input"))
                input->Focus(true);
        }
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
    const Logic::LogicCompileResult compiled = Logic::compileBoard(
        selectedId, board, selectedType, &coordinator.document().data());
    const LogicKeyBindingEditorState keyBinding{
        keyCaptureAddress_, keySearchAddress_, keySearchQuery_};

    if (view.tab == LogicBoardTab::GeneratedLua) {
        html += "<div id=\"logic-scroll\" class=\"logic-scroll\">";
        for (const Logic::LogicDiagnostic& diagnostic : compiled.diagnostics) {
            html += "<div class=\"logic-diagnostic\">" + escapeRml(diagnostic.code)
                 + " · " + escapeRml(diagnostic.ruleId) + " · "
                 + escapeRml(diagnostic.propertyKey) + " — "
                 + escapeRml(diagnostic.message) + "</div>";
        }
        if (!compiled.programs.empty())
            html += "<div class=\"logic-code\">" + escapeRml(compiled.programs.front().source) + "</div>";
        else
            html += "<div class=\"logic-empty\"><div class=\"logic-muted\">No source is generated while blocking diagnostics exist.</div></div>";
        html += "</div>";
        render();
        return;
    }

    // Collapse All/Expand All/Remove Board now live in the static toolbar and
    // the "..." menu (see EditorUi::refreshToolbar/toggleLogicMoreMenu) — this
    // panel no longer generates a separate tools row for them.
    html += "<div id=\"logic-scroll\" class=\"logic-scroll\">";

    const std::string query = lower(view.search);
    std::size_t renderedRules = 0;
    for (std::size_t ruleIndex = 0; ruleIndex < board.rules.size(); ++ruleIndex) {
        const LogicRuleDef& rule = board.rules[ruleIndex];
        const std::string summary = logicRuleSummary(rule);
        std::string searchable = lower(rule.id + " " + rule.trigger.typeId + " " + summary);
        for (const LogicConditionClause& clause : rule.conditions)
            searchable += " " + lower(clause.block.typeId);
        for (const LogicBlockDef& action : rule.actions) searchable += " " + lower(action.typeId);
        if (!query.empty() && searchable.find(query) == std::string::npos) continue;
        ++renderedRules;

        const bool collapsed = collapsedRuleIds_.count(rule.id) != 0;
        const std::size_t diagnosticCount = static_cast<std::size_t>(std::count_if(
            compiled.diagnostics.begin(), compiled.diagnostics.end(),
            [&](const Logic::LogicDiagnostic& d) {
                return d.ruleId == rule.id
                    && d.severity == Logic::DiagnosticSeverity::Error;
            }));

        html += "<div id=\"logic-rule-" + std::to_string(ruleIndex) + "\" class=\"logic-rule"
                + std::string(rule.enabled ? "" : " off") + "\">"
                "<div class=\"logic-rule-head\">";
        html += "<button class=\"logic-rule-caret\" data-action=\"toggle-logic-rule-collapsed\""
                " data-arg=\"" + escapeRml(rule.id) + "\">"
             + iconMarkup(collapsed ? "&#xeb5f;" : "&#xeb5d;") + "</button>";
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
        iconButton("duplicate-logic-rule", iconMarkup("&#xedef;"), false, "Clone rule");
        iconButton("move-logic-rule-up", "↑", ruleIndex == 0);
        iconButton("move-logic-rule-down", "↓", ruleIndex + 1 == board.rules.size());
        html += "<button class=\"comp-remove";
        if (playing) html += " disabled";
        html += "\" data-action=\"remove-logic-rule\" data-arg=\"" + escapeRml(rule.id)
             + "\" title=\"Delete rule\">" + iconMarkup("&#xeb41;") + "</button>";
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
            html += catalogEntries(objectType, nullptr, Logic::BlockKind::Trigger,
                                   rule.trigger.typeId, triggerDropdownId,
                                   "change-logic-trigger", rule.id, /*eventCatalog=*/true);
        }
        html += renderLogicProperties(
            coordinator.document(), rule.trigger,
            LogicPropertyAddress{rule.id, LogicPropertyTarget::Trigger, 0},
            openDropdownId_, keyBinding, playing);
        html += "</div>"; // .logic-block (WHEN trigger)
        if (rule.executionMode == LogicExecutionMode::OncePerActivation) {
            // Projected into WHEN; never stored in rule.conditions[].
            html += "<div class=\"logic-execution-clause\" title=\"Run Once per Activation\">"
                    "<span class=\"logic-execution-join\">AND</span>"
                    "<span class=\"logic-execution-label\">Once per activation</span>"
                    "</div>";
        }
        html += "</div>"; // .logic-col-content
        const std::string executionDropdownId = "execution|" + rule.id;
        const bool executionOpen = openDropdownId_ == executionDropdownId && !playing;
        html += "<div class=\"logic-col-footer logic-execution-footer\">"
                "<span class=\"logic-block-label\">Execution</span>";
        html += dropdownTriggerMarkup(executionModeLabel(rule.executionMode),
                                      "toggle-logic-dropdown", executionDropdownId,
                                      executionOpen, playing, "logic-execution-trigger");
        if (executionOpen) {
            html += "<div class=\"drop-list logic-key-list\">";
            html += dropEntry("Every occurrence", "every_occurrence",
                              rule.executionMode == LogicExecutionMode::EveryOccurrence,
                              executionDropdownId, "set-logic-execution-mode", rule.id);
            html += dropEntry("Run once per activation", "once_per_activation",
                              rule.executionMode == LogicExecutionMode::OncePerActivation,
                              executionDropdownId, "set-logic-execution-mode", rule.id);
            html += "</div>";
        }
        html += "</div>"; // .logic-col-footer
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
            const std::string arg = actionArg(rule.id, conditionIndex);
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
                  + iconMarkup("&#xeb41;") + "</button></div>";
            html += dropdownTriggerMarkup(
                descriptorLabel(clause.block.typeId), "toggle-logic-dropdown",
                dropdownId, dropdownOpen, playing);
            if (dropdownOpen) {
                html += catalogEntries(
                    objectType, Logic::findDescriptor(rule.trigger.typeId),
                    Logic::BlockKind::Condition, clause.block.typeId, dropdownId,
                    "change-logic-condition", arg);
            }
            html += renderLogicProperties(
                coordinator.document(), clause.block,
                LogicPropertyAddress{
                    rule.id, LogicPropertyTarget::Condition, conditionIndex},
                openDropdownId_, keyBinding, playing);
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
                objectType, Logic::findDescriptor(rule.trigger.typeId),
                Logic::BlockKind::Condition, {}, addConditionDropdownId,
                "add-logic-condition-type", rule.id);
        }
        html += "</div>"; // conditions-col

        // THEN — always at least one action (enforced by RemoveLogicActionCommand).
        html += "<div class=\"logic-rule-col actions-col\">"
                "<div class=\"logic-col-head\">THEN</div>"
                "<div class=\"logic-col-content\">";
        for (std::size_t actionIndex = 0; actionIndex < rule.actions.size(); ++actionIndex) {
            const LogicBlockDef& action = rule.actions[actionIndex];
            const std::string arg = actionArg(rule.id, actionIndex);
            const std::string dropdownId = "action|" + arg;
            const bool dropdownOpen = openDropdownId_ == dropdownId && !playing;
            html += "<div class=\"logic-block\"><div class=\"logic-action-row\"><div class=\"logic-action-main\">"
                 + dropdownTriggerMarkup(descriptorLabel(action.typeId), "toggle-logic-dropdown",
                                       dropdownId, dropdownOpen, playing)
                 + "</div><button class=\"logic-icon-btn";
            if (playing || actionIndex == 0) html += " disabled";
            html += "\" data-action=\"move-logic-action-up\" data-arg=\"" + escapeRml(arg) + "\">↑</button>"
                    "<button class=\"logic-icon-btn";
            if (playing || actionIndex + 1 == rule.actions.size()) html += " disabled";
            html += "\" data-action=\"move-logic-action-down\" data-arg=\"" + escapeRml(arg) + "\">↓</button>"
                    "<button class=\"comp-remove";
            if (playing || rule.actions.size() == 1) html += " disabled";
            html += "\" data-action=\"remove-logic-action\" data-arg=\"" + escapeRml(arg)
                 + "\" title=\"Delete action\">" + iconMarkup("&#xeb41;") + "</button></div>";
            if (dropdownOpen) {
                html += catalogEntries(objectType, Logic::findDescriptor(rule.trigger.typeId),
                                       Logic::BlockKind::Action, action.typeId, dropdownId,
                                       "change-logic-action", arg);
            }
            if (action.typeId == Logic::kSetVisible) {
                bool visible = true;
                if (const LogicPropertyDef* p = property(action, "visible"))
                    if (const auto* v = std::get_if<bool>(&p->value)) visible = *v;
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Self · visible</span>"
                        "<button class=\"logic-btn";
                if (playing) html += " disabled";
                html += "\" data-action=\"toggle-logic-visible\" data-arg=\"" + escapeRml(arg) + "\">"
                     + std::string(visible ? "On" : "Off") + "</button></div>";
            } else if (action.typeId == Logic::kSetPosition || action.typeId == Logic::kTranslateBy) {
                Vec2 value{};
                const char* propertyKey = action.typeId == Logic::kTranslateBy ? "offset" : "position";
                if (const LogicPropertyDef* p = property(action, propertyKey))
                    if (const auto* v = std::get_if<Vec2>(&p->value)) value = *v;
                const char* commitX = action.typeId == Logic::kTranslateBy
                    ? "commit-logic-offset-x" : "commit-logic-position-x";
                const char* commitY = action.typeId == Logic::kTranslateBy
                    ? "commit-logic-offset-y" : "commit-logic-position-y";
                const char* labelX = action.typeId == Logic::kTranslateBy ? "Self · ΔX" : "Self · X";
                const char* labelY = action.typeId == Logic::kTranslateBy ? "ΔY" : "Y";
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">"
                     + std::string(labelX) + "</span>"
                        "<input type=\"text\" data-action=\"" + std::string(commitX) + "\" data-arg=\""
                     + escapeRml(arg) + "\" value=\"" + number(value.x) + "\"";
                if (playing) html += " disabled=\"disabled\"";
                html += "/><span class=\"logic-block-label\">" + std::string(labelY) + "</span>"
                        "<input type=\"text\" data-action=\"" + std::string(commitY) + "\" data-arg=\""
                     + escapeRml(arg) + "\" value=\"" + number(value.y) + "\"";
                if (playing) html += " disabled=\"disabled\"";
                html += "/></div>";
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
                        html += dropEntry(animationAssetLabel(*candidate), candidate->id,
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
                        html += dropEntry(clipLabel(clip), clip.id, clip.id == selectedClip,
                                          clipDropdownId, "set-logic-animation-clip", arg);
                    }
                    html += "</div>";
                }
            } else if (action.typeId == Logic::kAnimationSetPlaybackSpeed) {
                double speed = 1.0;
                if (const LogicPropertyDef* p = property(action, "speed"))
                    if (const auto* v = std::get_if<double>(&p->value)) speed = *v;
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Self - speed</span>"
                        "<input type=\"text\" data-action=\"commit-logic-animation-speed\" data-arg=\""
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
                    if (const auto* v = std::get_if<double>(&p->value)) volume = *v;
                html += "<div class=\"logic-inline\"><span class=\"logic-block-label\">Volume</span>"
                        "<input type=\"text\" data-action=\"commit-logic-audio-volume\" data-arg=\""
                     + escapeRml(arg) + "\" value=\"" + number(volume) + "\"";
                if (playing) html += " disabled=\"disabled\"";
                html += "/></div>";
            } else {
                html += renderLogicProperties(
                    coordinator.document(), action,
                    LogicPropertyAddress{
                        rule.id, LogicPropertyTarget::Action, actionIndex},
                    openDropdownId_, keyBinding, playing);
            }
            html += "</div>";
        }
        html += "</div>"; // .logic-col-content
        const std::string addActionDropdownId = "add-action|" + rule.id;
        const bool addActionOpen = openDropdownId_ == addActionDropdownId && !playing;
        html += "<div class=\"logic-col-footer\"><button class=\"logic-btn logic-add-action";
        if (playing || rule.actions.size() >= Logic::kMaxActionsPerRule) html += " disabled";
        html += "\" data-action=\"toggle-logic-dropdown\" data-arg=\"" + escapeRml(addActionDropdownId)
             + "\">+ Add Action</button></div>";
        if (addActionOpen) {
            html += catalogEntries(objectType, Logic::findDescriptor(rule.trigger.typeId),
                                   Logic::BlockKind::Action, {}, addActionDropdownId,
                                   "add-logic-action-type", rule.id);
        }
        html += "</div>"; // actions-col

        html += "</div>"; // .logic-rule-body
        html += "<div class=\"logic-rule-diagnostics\">";
        for (const Logic::LogicDiagnostic& diagnostic : compiled.diagnostics) {
            if (diagnostic.ruleId != rule.id) continue;
            const bool warning = diagnostic.severity == Logic::DiagnosticSeverity::Warning;
            html += "<div class=\"logic-diagnostic"
                 + std::string(warning ? " warning" : "") + "\">"
                 + escapeRml(diagnostic.code) + " · "
                 + escapeRml(diagnostic.propertyKey) + " — " + escapeRml(diagnostic.message) + "</div>";
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

void LogicBoardPanel::toggleDropdown(Rml::ElementDocument* document,
                                     const EditorCoordinator& coordinator,
                                     const std::string& dropdownId) {
    openDropdownId_ = (openDropdownId_ == dropdownId) ? std::string() : dropdownId;
    refresh(document, coordinator);
}

void LogicBoardPanel::beginKeyCapture(Rml::ElementDocument* document,
                                      const EditorCoordinator& coordinator,
                                      const std::string& propertyAddress) {
    if (coordinator.isPlaying() || propertyAddress.empty()) return;
    keyCaptureAddress_ = propertyAddress;
    keySearchAddress_.clear();
    keySearchQuery_.clear();
    openDropdownId_.clear();
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
    clearKeyBindingEditor();
    if (collapsedRuleIds_.erase(ruleId) == 0) collapsedRuleIds_.insert(ruleId);
    refresh(document, coordinator);
}

void LogicBoardPanel::collapseAllRules(Rml::ElementDocument* document,
                                       const EditorCoordinator& coordinator) {
    if (const LogicBoardDef* board = currentBoard(coordinator)) {
        openDropdownId_.clear();
        clearKeyBindingEditor();
        for (const LogicRuleDef& rule : board->rules) collapsedRuleIds_.insert(rule.id);
    }
    refresh(document, coordinator);
}

void LogicBoardPanel::expandAllRules(Rml::ElementDocument* document,
                                     const EditorCoordinator& coordinator) {
    openDropdownId_.clear();
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
