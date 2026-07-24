#include "../include/presentation_snapshot_wasm.h"

namespace ArtCade::Presentation {

PresentationSnapshotWasm snapshot_to_wasm(const PresentationSnapshot& snapshot) {
    PresentationSnapshotWasm abi{};
    abi.abiVersion = PRESENTATION_SNAPSHOT_ABI_VERSION;
    abi.byteSize = PRESENTATION_SNAPSHOT_WASM_SIZE;
    abi.revision = snapshot.revision;
    abi.effectiveMode = static_cast<uint32_t>(snapshot.effectiveMode);
    if (snapshot.letterboxActive) abi.flags |= 1u;
    if (snapshot.useIdentityPlacement) abi.flags |= 2u;
    abi.surfaceFbW = static_cast<float>(snapshot.surface.framebufferWidth);
    abi.surfaceFbH = static_cast<float>(snapshot.surface.framebufferHeight);
    abi.logicalW = static_cast<float>(snapshot.logicalWidth);
    abi.logicalH = static_cast<float>(snapshot.logicalHeight);
    abi.destX = static_cast<float>(snapshot.placement.destX);
    abi.destY = static_cast<float>(snapshot.placement.destY);
    abi.destW = static_cast<float>(snapshot.placement.destW);
    abi.destH = static_cast<float>(snapshot.placement.destH);
    abi.scaleX = static_cast<float>(snapshot.placement.scaleX);
    abi.scaleY = static_cast<float>(snapshot.placement.scaleY);
    abi.editorViewOriginX = static_cast<float>(snapshot.editorViewOriginX);
    abi.editorViewOriginY = static_cast<float>(snapshot.editorViewOriginY);
    abi.surfacePixelsPerWorldUnit =
        static_cast<float>(snapshot.surfacePixelsPerWorldUnit);
    abi.visibleWorldMinX = static_cast<float>(snapshot.visibleWorldMinX);
    abi.visibleWorldMinY = static_cast<float>(snapshot.visibleWorldMinY);
    abi.visibleWorldMaxX = static_cast<float>(snapshot.visibleWorldMaxX);
    abi.visibleWorldMaxY = static_cast<float>(snapshot.visibleWorldMaxY);
    return abi;
}

} // namespace ArtCade::Presentation
