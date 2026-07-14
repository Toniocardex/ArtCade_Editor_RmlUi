#pragma once

#include "core/types.h"

#include <optional>
#include <string>
#include <unordered_set>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;
enum class LogicBoardTab;

// Dedicated projection for the complex Object-Type-owned Logic Board editor.
// It owns no authoring state and rebuilds only on LogicBoard invalidation.
class LogicBoardPanel {
public:
    void refresh(Rml::ElementDocument* document,
                 const EditorCoordinator& coordinator) const;

    // Toggle the in-flow value dropdown named `dropdownId` (per-rule Key
    // picker); at most one is open at a time. Mirrors InspectorPanel's
    // toggleDropdown/closeDropdowns pattern exactly. The Object Type picker
    // itself is NOT one of these dropdownIds — it lives in the header's
    // always-visible .logic-head, not inside the scrollable rules list, so
    // EditorUi renders it as a floating menu instead (see
    // objectTypeMenuEntries below) to avoid pushing the board down every
    // time it opens.
    void toggleDropdown(Rml::ElementDocument* document,
                        const EditorCoordinator& coordinator,
                        const std::string& dropdownId);
    void closeDropdown() { openDropdownId_.clear(); }

    // Entries for the floating Object Type menu (one `.drop-entry` per
    // existing type, the current selection marked), for EditorUi to place
    // inside its own positioned `.context-menu` element.
    std::string objectTypeMenuEntries(const EditorCoordinator& coordinator) const;

    // Collapse/expand one rule's body (presentation-only, no Command/Undo/
    // dirty effect — mirrors InspectorPanel's toggleSection). Also clears
    // openDropdownId_: a dropdown open inside a rule that collapses must not
    // silently reappear open when the rule re-expands.
    void toggleRuleCollapsed(Rml::ElementDocument* document,
                             const EditorCoordinator& coordinator,
                             const LogicRuleId& ruleId);
    void collapseAllRules(Rml::ElementDocument* document,
                          const EditorCoordinator& coordinator);
    void expandAllRules(Rml::ElementDocument* document,
                        const EditorCoordinator& coordinator);

    // Re-measures #center-workspace (always visible, unlike this panel's own
    // root while Scene mode is active) and toggles the "compact" class on
    // #logic-board-panel accordingly. Cheap: no rebuild, just a SetClass.
    // Called at the end of refresh() and directly from splitter drag.
    void syncResponsiveClass(Rml::ElementDocument* document) const;

private:
    // Presentation-only scroll restoration across hidden/show and projection
    // rebuilds, and the board that collapsedRuleIds_/openDropdownId_ are
    // currently scoped to. Tracks the resolved selectedId actually rendered
    // (which may differ from EditorState's raw, possibly-empty
    // logicBoardEditor.objectTypeId via the same-first-sorted-type fallback
    // refresh() applies) — comparing against the raw workspace value here
    // would let state leak across boards that both happen to have "rule-1".
    // A different rendered Object Type starts at the top, uncollapsed.
    mutable ObjectTypeId renderedObjectTypeId_;
    mutable float scrollTop_ = 0.f;
    // Open value dropdown ("" = none). Cleared whenever the rendered context
    // it belonged to changes from under it: Object Type switch, Rules <->
    // Generated Lua tab switch, or Play starting (kPlayToggleInvalidation
    // already re-triggers refresh() on Play/Stop, so this needs no separate
    // hook). A stale rule-scoped id (e.g. "key|<removed-rule>") is inert by
    // construction: it simply never matches a dropdown id rendered again.
    mutable std::string openDropdownId_;
    mutable std::optional<LogicBoardTab> lastTab_;
    // Presentation-only per-rule collapse state, scoped to renderedObjectTypeId_.
    mutable std::unordered_set<LogicRuleId> collapsedRuleIds_;
};

} // namespace ArtCade::EditorNative
