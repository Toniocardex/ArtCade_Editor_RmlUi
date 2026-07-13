#include "editor-native/view/texture_request_catalog.h"

#include "editor-native/model/path_confinement.h"
#include "editor-native/model/play_session.h"
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
    if (source_ == Source::Document && documentRevision_ == document.revision()
        && assetRoot_ == assetRoot) {
        return requests_;
    }

    requests_.clear();
    for (const ImageAssetDef& asset : document.data().imageAssets) {
        requests_.emplace(asset.assetId, TextureRequest{
            asset.assetId, resolveImageAssetPath(assetRoot, asset.sourcePath)});
    }
    source_ = Source::Document;
    documentRevision_ = document.revision();
    playSnapshot_ = nullptr;
    assetRoot_ = assetRoot;
    return requests_;
}

const TextureRequestCatalog::Requests& TextureRequestCatalog::forPlay(
    const PlayAssetCatalogSnapshot& snapshot, const std::filesystem::path& assetRoot) {
    if (source_ == Source::Play && playSnapshot_ == &snapshot && assetRoot_ == assetRoot) {
        return requests_;
    }

    requests_.clear();
    for (const auto& [assetId, asset] : snapshot.imageAssets) {
        requests_.emplace(assetId, TextureRequest{
            assetId, resolveImageAssetPath(assetRoot, asset.sourcePath)});
    }
    source_ = Source::Play;
    playSnapshot_ = &snapshot;
    assetRoot_ = assetRoot;
    return requests_;
}

} // namespace ArtCade::EditorNative
