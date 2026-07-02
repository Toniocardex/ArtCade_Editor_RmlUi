#pragma once

namespace ArtCade::EditorNative {

// =============================================================================
// EditorApp — owns the native window and the frame loop (prompt §11 shape):
//   poll input → route → coordinator pending ops → apply invalidations
//   → viewport render → RmlUi render. No serialization, no fingerprint, no
//   project replace in the normal frame.
// =============================================================================
class EditorApp {
public:
    int run(int argc, char** argv);
};

} // namespace ArtCade::EditorNative
