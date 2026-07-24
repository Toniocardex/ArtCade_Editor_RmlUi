#pragma once

#include "presentation_mode.h"
#include "presentation_snapshot.h"

#include <cstdint>

namespace ArtCade::Presentation {

constexpr uint32_t PRESENTATION_SNAPSHOT_ABI_VERSION = 2;
constexpr uint32_t PRESENTATION_SNAPSHOT_WASM_SIZE_V1 = 64;
constexpr uint32_t PRESENTATION_SNAPSHOT_WASM_SIZE = 96;

/**
 * Flat WASM/JS ABI for a committed PresentationSnapshot.
 * Layout is versioned; update the TS parser when changing field order.
 */
#pragma pack(push, 1)
struct PresentationSnapshotWasm {
    uint32_t abiVersion = PRESENTATION_SNAPSHOT_ABI_VERSION;
    uint32_t byteSize = PRESENTATION_SNAPSHOT_WASM_SIZE;
    uint64_t revision = 0;
    uint32_t effectiveMode = 0;
    uint32_t flags = 0; /**< bit0 letterboxActive, bit1 useIdentityPlacement */
    float surfaceFbW = 0.f;
    float surfaceFbH = 0.f;
    float logicalW = 0.f;
    float logicalH = 0.f;
    float destX = 0.f;
    float destY = 0.f;
    float destW = 0.f;
    float destH = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    float editorViewOriginX = 0.f;
    float editorViewOriginY = 0.f;
    float surfacePixelsPerWorldUnit = 1.f;
    float visibleWorldMinX = 0.f;
    float visibleWorldMinY = 0.f;
    float visibleWorldMaxX = 0.f;
    float visibleWorldMaxY = 0.f;
    uint32_t reserved = 0;
};
#pragma pack(pop)

static_assert(sizeof(PresentationSnapshotWasm) == PRESENTATION_SNAPSHOT_WASM_SIZE,
              "PresentationSnapshotWasm ABI size changed; update byteSize and TS parser");
static_assert(static_cast<uint32_t>(PresentationMode::SceneEdit) == 0);
static_assert(static_cast<uint32_t>(PresentationMode::CameraPreview) == 1);
static_assert(static_cast<uint32_t>(PresentationMode::PlayEmbedded) == 2);
static_assert(static_cast<uint32_t>(PresentationMode::PlayExternal) == 3);
static_assert(static_cast<uint32_t>(PresentationMode::PlayFullscreen) == 4);

/** Packs a committed snapshot into the flat WASM struct. */
PresentationSnapshotWasm snapshot_to_wasm(const PresentationSnapshot& snapshot);

} // namespace ArtCade::Presentation
