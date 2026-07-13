#pragma once

#include "core/types.h"

#include <optional>
#include <string>

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

private:
    // Presentation-only scroll restoration across hidden/show and projection
    // rebuilds. A different explicit Object Type target starts at the top.
    mutable std::optional<ObjectTypeId> scrollObjectTypeId_;
    mutable float scrollTop_ = 0.f;
    // Open value dropdown ("" = none). Cleared whenever the rendered context
    // it belonged to changes from under it: Object Type switch, Rules <->
    // Generated Lua tab switch, or Play starting (kPlayToggleInvalidation
    // already re-triggers refresh() on Play/Stop, so this needs no separate
    // hook). A stale rule-scoped id (e.g. "key|<removed-rule>") is inert by
    // construction: it simply never matches a dropdown id rendered again.
    mutable std::string openDropdownId_;
    mutable std::optional<LogicBoardTab> lastTab_;
};

} // namespace ArtCade::EditorNative
