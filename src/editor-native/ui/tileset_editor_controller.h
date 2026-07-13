#pragma once

#include "core/types.h"

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

class EditorCoordinator;

// Controller for the Tileset Editor document. Texture-backed operations are
// supplied by the composition root; the controller never owns filesystem or
// graphics resources.
class TilesetEditorController {
public:
    using ApplySlicingRequest = std::function<void()>;
    using CloseRequest = std::function<void()>;
    using CreateFromImageRequest = std::function<void(const AssetId&)>;
    using ImageSizeProvider =
        std::function<std::optional<std::pair<int, int>>(const AssetId&)>;

    TilesetEditorController(EditorCoordinator& coordinator,
                            Rml::ElementDocument* document);

    void detach();
    void refresh();
    void updateZoomReadout();
    bool handleAction(const std::string& action, const std::string& arg,
                      const std::string& value);

    void setApplySlicingRequest(ApplySlicingRequest request);
    void setCloseRequest(CloseRequest request);
    void setCreateFromImageRequest(CreateFromImageRequest request);
    void setImageSizeProvider(ImageSizeProvider provider);

private:
    EditorCoordinator&      coordinator_;
    Rml::ElementDocument*   document_ = nullptr;
    ApplySlicingRequest     applySlicingRequest_;
    CloseRequest            closeRequest_;
    CreateFromImageRequest  createFromImageRequest_;
    ImageSizeProvider       imageSizeProvider_;
    std::string             markup_;
};

} // namespace ArtCade::EditorNative
