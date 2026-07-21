#include "editor-native/view/texture_request_catalog.h"

#include "editor-native/model/path_confinement.h"
#include "editor-native/model/project_document.h"

namespace ArtCade::EditorNative {
namespace {

std::filesystem::path resolveImageAssetPath(const std::filesystem::path& assetRoot,
                                             const std::string& sourcePath) {
    if (sourcePath.empty()) return {};
    const PathConfinementResult resolved = resolvePathInsideRoot(
        assetRoot, std::filesystem::u8path(sourcePath));
    return resolved.ok ? resolved.value : std::filesystem::path{};
}

} // namespace

const TextureRequestCatalog::Requests& TextureRequestCatalog::forDocument(
    const ProjectDocument& document, const std::filesystem::path& assetRoot) {
    if (hasRequests_ && documentRevision_ == document.revision()
        && assetRoot_ == assetRoot) {
        return requests_;
    }

    requests_.clear();
    for (const ImageAssetDef& asset : document.data().imageAssets) {
        requests_.emplace(asset.assetId, TextureRequest{
            asset.assetId, resolveImageAssetPath(assetRoot, asset.sourcePath)});
    }
    hasRequests_ = true;
    documentRevision_ = document.revision();
    assetRoot_ = assetRoot;
    return requests_;
}

} // namespace ArtCade::EditorNative
