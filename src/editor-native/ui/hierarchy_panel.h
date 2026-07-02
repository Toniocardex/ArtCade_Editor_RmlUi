#pragma once

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Refreshes the scene tab bar and the entity tree from the read model. Holds no
// authoring state; it only renders what the coordinator exposes (prompt §11).
class HierarchyPanel {
public:
    void refresh(Rml::ElementDocument* document, const EditorCoordinator& coordinator) const;
};

} // namespace ArtCade::EditorNative
