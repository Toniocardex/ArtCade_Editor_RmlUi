#pragma once

#include "core/types.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Renders the Scene Inspector or the selected entity properties. Local state is
// transient presentation only: menus, inline text drafts, and Appearance opacity
// slider preview — never document data.
class InspectorPanel {
public:
    struct ObjectVariableDraftCommit {
        std::string action;
        std::string arg;
        std::string value;
    };

    void refresh(Rml::ElementDocument* document, const EditorCoordinator& coordinator);

    // Toggle / close the in-flow Add Component menu (repaints on toggle).
    void toggleAddMenu(Rml::ElementDocument* document, const EditorCoordinator& coordinator);
    void closeAddMenu() { addMenuOpen_ = false; }

    // Toggle the in-flow value dropdown named `dropdownId` (Layer / Source /
    // Tileset / Game View preset); at most one is open at a time. closeDropdowns()
    // runs when an entry commits a pick — the pick invalidates the Inspector,
    // which then re-renders with the list collapsed (same pattern as closeAddMenu).
    void toggleDropdown(Rml::ElementDocument* document, const EditorCoordinator& coordinator,
                        const std::string& dropdownId);
    void closeDropdowns() { openDropdownId_.clear(); dropdownHighlightIndex_.reset(); }
    // Open a value dropdown without toggling (e.g. Binding → Variable after a scope pick).
    void setOpenDropdown(std::string dropdownId) {
        openDropdownId_ = std::move(dropdownId);
        dropdownHighlightIndex_.reset();
    }
    bool hasOpenDropdown() const { return !openDropdownId_.empty(); }
    const std::string& openDropdownId() const { return openDropdownId_; }
    bool hasOpenAddMenu() const { return addMenuOpen_; }
    /** Close Add Component / value dropdowns and repaint when either was open. */
    void dismissTransientMenus(Rml::ElementDocument* document,
                               const EditorCoordinator& coordinator);

    // ADR-0034 spike: arrow-key cursor within whichever dropdown is open,
    // scoped today to the Scene Layer picker. `delta` is +1 (Down) or -1 (Up);
    // wraps at the ends of navigableDropdownEntries_ (same order rendered).
    void moveDropdownHighlight(Rml::ElementDocument* document,
                               const EditorCoordinator& coordinator, int delta);
    // The (action, arg) pair the highlighted entry would dispatch on Enter —
    // the exact pair its .drop-entry click handler already uses — or nullopt
    // when no dropdown is open or nothing is highlighted yet.
    std::optional<std::pair<std::string, std::string>> dropdownHighlightCommit() const;

    // Session-local presentation state. The section id is accepted only from
    // the fixed Inspector catalog; toggling never mutates editor or project
    // state and therefore has no Command/Undo/dirty effect.
    void toggleSection(Rml::ElementDocument* document,
                       const EditorCoordinator& coordinator,
                       const std::string& sectionId);

    void beginSceneLayerRename(Rml::ElementDocument* document,
                               const EditorCoordinator& coordinator,
                               const std::string& layerId);
    void beginActiveSceneLayerRename(Rml::ElementDocument* document,
                                     const EditorCoordinator& coordinator);
    void commitSceneLayerRename(Rml::ElementDocument* document,
                                EditorCoordinator& coordinator,
                                const std::string& requestedName);
    void cancelSceneLayerRename(Rml::ElementDocument* document,
                                const EditorCoordinator& coordinator);
    void showEntityPositionPreview(Rml::ElementDocument* document,
                                   const EditorCoordinator& coordinator,
                                   EntityId entity,
                                   Vec2 position);

    // Consumes a one-shot InspectorRevealRequest from the coordinator after
    // refresh() has rebuilt the target fields.
    void consumeInspectorReveal(Rml::ElementDocument* document,
                                EditorCoordinator& coordinator);

    // Appearance Opacity slider draft (ADR-0020). Panel-local only.
    void beginBackgroundOpacityDrag(const EditorCoordinator& coordinator);
    void previewBackgroundOpacity(Rml::ElementDocument* document,
                                  const EditorCoordinator& coordinator,
                                  float opacityPercent);
    void commitBackgroundOpacityDrag(Rml::ElementDocument* document,
                                     EditorCoordinator& coordinator);
    void cancelBackgroundOpacityDrag(Rml::ElementDocument* document,
                                     const EditorCoordinator& coordinator);
    bool backgroundOpacityDragActive() const;
    void cancelBackgroundOpacityDraft();

    // ADR-0031 A2.2: text typed into an Object Variable field is panel-local
    // until Enter/blur submits its existing Command. The address includes the
    // selected instance so a selection/replacement can discard it rather than
    // committing text against a different subject.
    void beginObjectVariableDraft(const EditorCoordinator& coordinator,
                                  const std::string& action,
                                  const std::string& arg,
                                  const std::string& renderedValue);
    void updateObjectVariableDraft(const std::string& action,
                                   const std::string& arg,
                                   const std::string& value);
    std::optional<ObjectVariableDraftCommit> objectVariableDraftCommit() const;
    void setObjectVariableDraftError(std::string error);
    void discardObjectVariableDraft();
    bool hasObjectVariableDraft() const { return objectVariableDraft_.has_value(); }
    bool objectVariableDraftRebuilding() const { return objectVariableDraftRebuilding_; }
    void focusObjectVariableDraft(Rml::ElementDocument* document);

private:
    struct SceneLayerRenameUiState {
        SceneId     sceneId;
        std::string layerId;
        std::string draftName;
        std::string validationError;
    };

    struct SceneBackgroundOpacityDraft {
        SceneId sceneId;
        Vec4 original{};
        Vec4 preview{};
        bool dragActive = false;
    };

    struct ObjectVariableDraft {
        SceneId sceneId;
        EntityId entityId = INVALID_ENTITY;
        ObjectTypeId objectTypeId;
        std::uint64_t sourceRevision = 0;
        std::string action;
        std::string key;
        std::string value;
        std::string error;
    };

    bool reconcileSceneLayerRenameUiState(const EditorCoordinator& coordinator);
    void focusSceneLayerRenameInput(Rml::ElementDocument* document);
    void revealTilemapCellSize(Rml::ElementDocument* document,
                               const EditorCoordinator& coordinator);
    bool isSectionCollapsed(const std::string& sectionId) const;
    void reconcileOpenDropdownForScene();
    void reconcileOpenDropdownForEntity();
    void reconcileBackgroundDraft(const EditorCoordinator& coordinator, bool sceneMode);
    void reconcileObjectVariableDraft(const EditorCoordinator& coordinator);
    void applyBackgroundOpacityPreview(Rml::ElementDocument* document, const Vec4& color);

    // ADR-0034 spike: one navigable entry per pickable .drop-entry, rebuilt by
    // refresh() in the same order the Layer dropdown renders them (targetLocked
    // entries excluded, matching Enter/click parity). dropdownHighlightIndex_
    // indexes into this list and is meaningless once openDropdownId_ changes.
    struct NavigableDropdownEntry {
        std::string action;
        std::string arg;
        bool current = false;
    };
    std::vector<NavigableDropdownEntry> navigableDropdownEntries_;
    std::optional<int> dropdownHighlightIndex_;

    bool     addMenuOpen_ = false;
    std::string openDropdownId_;             // open value dropdown ("" = none)
    std::unordered_set<std::string> collapsedSections_{
        "project",
        "diagnostics", "sprite-renderer", "sprite-animator", "tilemap",
        "box-collider", "linear-mover", "top-down-controller",
        "platformer-controller",
    };
    EntityId lastEntity_ = INVALID_ENTITY;   // detect a selection change to reset the menu
    std::optional<SceneLayerRenameUiState> layerRename_;
    std::optional<SceneBackgroundOpacityDraft> backgroundDraft_;
    std::optional<ObjectVariableDraft> objectVariableDraft_;
    bool objectVariableDraftRebuilding_ = false;
};

} // namespace ArtCade::EditorNative
