#pragma once

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Dedicated projection for the complex Object-Type-owned Logic Board editor.
// It owns no authoring state and rebuilds only on LogicBoard invalidation.
class LogicBoardPanel {
public:
    void refresh(Rml::ElementDocument* document,
                 const EditorCoordinator& coordinator) const;
};

} // namespace ArtCade::EditorNative
