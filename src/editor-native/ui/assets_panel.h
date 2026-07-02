#pragma once

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Renders the project's image-asset catalog. Each row offers "Use" (assign to the
// selected entity's sprite renderer, reusing the existing command) and "Remove".
// Import is an application-level operation triggered from here.
class AssetsPanel {
public:
    void refresh(Rml::ElementDocument* document, const EditorCoordinator& coordinator) const;
};

} // namespace ArtCade::EditorNative
