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
// transient presentation only: menus and inline text drafts, never document data.
class InspectorPanel {
public:
    void refresh(Rml::ElementDocument* document, const EditorCoordinator& coordinator);

    // Toggle / close the in-flow Add Component menu (repaints on toggle).
    void toggleAddMenu(Rml::ElementDocument* document, const EditorCoordinator& coordinator);
    void closeAddMenu() { addMenuOpen_ = false; }

    // Toggle the in-flow value dropdown named `dropdownId` (Layer / Source /
    // Tileset pickers); at most one is open at a time. closeDropdowns() runs
    // when an entry commits a pick — the pick invalidates the Inspector, which
    // then re-renders with the list collapsed (same pattern as closeAddMenu).
    void toggleDropdown(Rml::ElementDocument* document, const EditorCoordinator& coordinator,
                        const std::string& dropdownId);
    void closeDropdowns() { openDropdownId_.clear(); }

    // Session-local presentation state. The section id is accepted only from
    // the fixed Inspector catalog; toggling never mutates editor or project
    // state and therefore has no Command/Undo/dirty effect.
    void toggleSection(Rml::ElementDocument* document,
                       const EditorCoordinator& coordinator,
                       const std::string& sectionId);

    // Read-only projection of which tiles of a tileset are fully transparent
    // (application-owned derived cache, same pattern as EditorUi's
    // ImageSizeProvider). nullptr / unset provider = unknown; the palette then
    // shows every tile. The returned pointer is only read within one refresh.
    using EmptyTilesProvider =
        std::function<const std::vector<bool>*(const AssetId& tilesetAssetId)>;
    void setEmptyTilesProvider(EmptyTilesProvider provider) {
        emptyTilesProvider_ = std::move(provider);
    }
    // Session-local "Show empty" filter of the tile palette; presentation only,
    // like toggleSection.
    void togglePaletteEmptyTiles(Rml::ElementDocument* document,
                                 const EditorCoordinator& coordinator);

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

private:
    struct SceneLayerRenameUiState {
        SceneId     sceneId;
        std::string layerId;
        std::string draftName;
        std::string validationError;
    };

    bool reconcileSceneLayerRenameUiState(const EditorCoordinator& coordinator);
    void focusSceneLayerRenameInput(Rml::ElementDocument* document);
    void revealTilemapCellSize(Rml::ElementDocument* document,
                               const EditorCoordinator& coordinator);
    bool isSectionCollapsed(const std::string& sectionId) const;

    bool     addMenuOpen_ = false;
    std::string openDropdownId_;             // open value dropdown ("" = none)
    std::unordered_set<std::string> collapsedSections_{
        "diagnostics", "sprite-renderer", "sprite-animator", "tilemap",
        "box-collider", "linear-mover", "top-down-controller",
        "platformer-controller",
    };
    EntityId lastEntity_ = INVALID_ENTITY;   // detect a selection change to reset the menu
    std::optional<SceneLayerRenameUiState> layerRename_;
    EmptyTilesProvider emptyTilesProvider_;
    bool showEmptyPaletteTiles_ = false;     // "Show empty" palette filter
};

} // namespace ArtCade::EditorNative
