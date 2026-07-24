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
    void closeDropdowns() { openDropdownId_.clear(); }

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

    bool reconcileSceneLayerRenameUiState(const EditorCoordinator& coordinator);
    void focusSceneLayerRenameInput(Rml::ElementDocument* document);
    void revealTilemapCellSize(Rml::ElementDocument* document,
                               const EditorCoordinator& coordinator);
    bool isSectionCollapsed(const std::string& sectionId) const;
    void reconcileOpenDropdownForScene();
    void reconcileOpenDropdownForEntity();
    void reconcileBackgroundDraft(const EditorCoordinator& coordinator, bool sceneMode);
    void applyBackgroundOpacityPreview(Rml::ElementDocument* document, const Vec4& color);

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
};

} // namespace ArtCade::EditorNative
