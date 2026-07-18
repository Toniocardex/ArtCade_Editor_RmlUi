#pragma once

#include "editor-native/app/generated_sfx_status_projection.h"

#include <functional>
#include <string>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Renders the project's image-asset catalog. Each row offers "Use" (assign to the
// selected entity's sprite renderer, reusing the existing command) and "Remove".
// Import is an application-level operation triggered from here.
class AssetsPanel {
public:
    using GeneratedSfxStatusQuery =
        std::function<GeneratedSfxStatusProjection(const std::string&)>;
    void refresh(Rml::ElementDocument* document,
                 const EditorCoordinator& coordinator,
                 const GeneratedSfxStatusQuery& generatedSfxStatus) const;
};

} // namespace ArtCade::EditorNative
