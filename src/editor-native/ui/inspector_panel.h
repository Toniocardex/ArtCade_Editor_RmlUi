#pragma once

#include "core/types.h"

#include <optional>
#include <string>

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

private:
    struct SceneLayerRenameUiState {
        SceneId     sceneId;
        std::string layerId;
        std::string draftName;
        std::string validationError;
    };

    bool reconcileSceneLayerRenameUiState(const EditorCoordinator& coordinator);
    void focusSceneLayerRenameInput(Rml::ElementDocument* document);

    bool     addMenuOpen_ = false;
    EntityId lastEntity_ = INVALID_ENTITY;   // detect a selection change to reset the menu
    std::optional<SceneLayerRenameUiState> layerRename_;
};

} // namespace ArtCade::EditorNative
