#pragma once

#include <cstdint>

namespace ArtCade::EditorNative {

// Phase 2 bridge: explicit exclusive keyboard capture result (ADR-0024).
// Full LogicKeyCaptureSession deferred to Phase 3.
enum class ExclusiveCaptureKind : std::uint8_t {
    None = 0,
    LogicKeyCapture,
};

struct ExclusiveCaptureResult {
    ExclusiveCaptureKind kind = ExclusiveCaptureKind::None;

    bool active() const { return kind != ExclusiveCaptureKind::None; }
};

} // namespace ArtCade::EditorNative
