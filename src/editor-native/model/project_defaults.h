#pragma once

#include "core/types.h"

namespace ArtCade::EditorNative {

// Default SceneDef::backgroundColor for newly created scenes and Inspector Reset.
// Dark neutral #1E1E24 (opaque) — readable against the dark editor chrome.
inline constexpr Vec4 kDefaultSceneBackground{0.118f, 0.118f, 0.141f, 1.0f};

} // namespace ArtCade::EditorNative
