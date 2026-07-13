#pragma once

#include "core/types.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Controller for the Sprite Animation Editor document. It owns only
// presentation state and application callbacks; authoring remains in the
// ProjectDocument and is mutated exclusively through the Coordinator.
class SpriteAnimationEditorController {
public:
    using ImportImageRequest = std::function<std::optional<AssetId>()>;
    using SliceRequest = std::function<void()>;

    SpriteAnimationEditorController(EditorCoordinator& coordinator,
                                    Rml::ElementDocument* document);

    void detach();
    void refresh();
    void updatePlayhead();
    bool handleAction(const std::string& action, const std::string& arg,
                      const std::string& value);

    void setImportImageRequest(ImportImageRequest request);
    void setSliceRequest(SliceRequest request);

private:
    EditorCoordinator&    coordinator_;
    Rml::ElementDocument* document_ = nullptr;
    ImportImageRequest    importImageRequest_;
    SliceRequest          sliceRequest_;
    std::string           markup_;
    std::size_t           timelineCount_ = 0;
};

} // namespace ArtCade::EditorNative
