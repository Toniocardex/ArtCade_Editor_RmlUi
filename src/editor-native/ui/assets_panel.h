#pragma once

#include "editor-native/app/generated_sfx_status_projection.h"

#include <functional>
#include <string>
#include <unordered_set>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Renders the project's asset catalog. Each category (Images, Animations, …)
// is a local-presentation accordion: collapsed ids live on this panel only
// (EditorUiState stays filter/layout, not section open/close).
class AssetsPanel {
public:
    using GeneratedSfxStatusQuery =
        std::function<GeneratedSfxStatusProjection(const std::string&)>;
    void refresh(Rml::ElementDocument* document,
                 const EditorCoordinator& coordinator,
                 const GeneratedSfxStatusQuery& generatedSfxStatus) const;
    void toggleSection(Rml::ElementDocument* document,
                       const EditorCoordinator& coordinator,
                       const GeneratedSfxStatusQuery& generatedSfxStatus,
                       const std::string& sectionId);

private:
    bool isSectionCollapsed(const std::string& sectionId) const;

    // Empty / secondary catalogs start collapsed so the sidebar stays short;
    // Images and Animations stay open (most common browse targets).
    std::unordered_set<std::string> collapsedSections_{
        "tilesets", "generated-sfx", "audio", "fonts", "scripts",
    };
};

} // namespace ArtCade::EditorNative
