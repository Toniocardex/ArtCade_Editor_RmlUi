#pragma once

#include "core/types.h"

#include <optional>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Dedicated projection for the complex Object-Type-owned Logic Board editor.
// It owns no authoring state and rebuilds only on LogicBoard invalidation.
class LogicBoardPanel {
public:
    void refresh(Rml::ElementDocument* document,
                 const EditorCoordinator& coordinator) const;

private:
    // Presentation-only scroll restoration across hidden/show and projection
    // rebuilds. A different explicit Object Type target starts at the top.
    mutable std::optional<ObjectTypeId> scrollObjectTypeId_;
    mutable float scrollTop_ = 0.f;
};

} // namespace ArtCade::EditorNative
