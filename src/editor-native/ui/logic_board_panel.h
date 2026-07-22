#pragma once

#include "core/types.h"

#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

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

    // Applies scroll/reveal presentation state after RmlUi has completed the
    // layout caused by refresh(). This is deliberately separate from refresh:
    // a newly rebuilt scroll container has no final scroll range until
    // Context::Update(), so restoring sooner can be clamped back to the top.
    void restoreAfterLayout(Rml::ElementDocument* document,
                            const EditorCoordinator& coordinator) const;

    // Toggle an in-flow value dropdown named `dropdownId`; at most one is
    // open at a time. Mirrors InspectorPanel's
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
    void beginKeyCapture(Rml::ElementDocument* document,
                         const EditorCoordinator& coordinator,
                         const std::string& propertyAddress);
    void toggleKeySearch(Rml::ElementDocument* document,
                         const EditorCoordinator& coordinator,
                         const std::string& propertyAddress);
    void setKeySearchQuery(Rml::ElementDocument* document,
                           const EditorCoordinator& coordinator,
                           const std::string& propertyAddress,
                           std::string query);
    void cancelKeyCapture(Rml::ElementDocument* document,
                          const EditorCoordinator& coordinator);
    bool hasKeyCapture() const { return !keyCaptureAddress_.empty(); }
    const std::string& keyCaptureAddress() const { return keyCaptureAddress_; }
    void clearKeyBindingEditor() const {
        keyCaptureAddress_.clear();
        keySearchAddress_.clear();
        keySearchQuery_.clear();
    }
    void toggleVariablesDrawer(
        Rml::ElementDocument* document, const EditorCoordinator& coordinator);

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

    // UI-only acknowledgement for a successful clone. The controller resolves
    // the new stable id after the Command and the panel reveals it once after
    // its next reconciliation; neither state is persisted or undoable.
    void revealRuleAfterRefresh(LogicRuleId ruleId) const {
        pendingRevealRuleId_ = std::move(ruleId);
    }

    // True when at least one of the current board's rules is not collapsed
    // (so Collapse All would have an effect) / is collapsed (so Expand All
    // would). Positive semantics deliberately, not collapsedRuleIds_.empty()
    // — that set can outlive a rule that gets deleted while collapsed, so an
    // emptiness check alone would misreport "nothing collapsed" as false.
    bool canCollapseAllRules(const EditorCoordinator& coordinator) const;
    bool canExpandAllRules(const EditorCoordinator& coordinator) const;

    // Re-measures #center-workspace (always visible, unlike this panel's own
    // root while Scene mode is active) and toggles the "compact" class on
    // #logic-board-panel accordingly. Cheap: no rebuild, just a SetClass.
    // Called at the end of refresh() and directly from splitter drag.
    void syncResponsiveClass(Rml::ElementDocument* document) const;

private:
    // The LogicBoardDef belonging to renderedObjectTypeId_ (the board this
    // panel's collapse/scroll/dropdown state is currently scoped to), or
    // nullptr if that type no longer exists or has no board. Shared by every
    // method that needs to validate a rule id or enumerate rules without
    // duplicating the same "resolve current board" lookup.
    const LogicBoardDef* currentBoard(const EditorCoordinator& coordinator) const;

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
    // A rebuild replaces #logic-scroll. Keep the last measured offset intact
    // until the replacement has a final layout; otherwise a second refresh in
    // the same frame would read its transient zero and lose the user's place.
    mutable bool scrollRestorePending_ = false;
    // Open value dropdown ("" = none). Cleared whenever the rendered context
    // it belonged to changes from under it: Object Type switch, Rules <->
    // Generated Lua tab switch, or Play starting (kPlayToggleInvalidation
    // already re-triggers refresh() on Play/Stop, so this needs no separate
    // hook). A stale rule-scoped id (e.g. "key|<removed-rule>") is inert by
    // construction: it simply never matches a dropdown id rendered again.
    mutable std::string openDropdownId_;
    // ADR-0004 transient key-binding interaction state. Never serialised and
    // cleared with the rendered Logic Board context or Play.
    mutable std::string keyCaptureAddress_;
    mutable std::string keySearchAddress_;
    mutable std::string keySearchQuery_;
    mutable bool variablesDrawerOpen_ = false;
    mutable std::optional<LogicBoardTab> lastTab_;
    mutable LogicRuleId pendingRevealRuleId_;
    // Presentation-only per-rule collapse state, scoped to renderedObjectTypeId_.
    mutable std::unordered_set<LogicRuleId> collapsedRuleIds_;
};

} // namespace ArtCade::EditorNative
