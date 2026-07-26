#pragma once

#include "types.h"

#include <optional>
#include <string>
#include <string_view>

namespace ArtCade {

/** Parse `#RGB` or `#RRGGBB` (optional leading `#`, case-insensitive) into RGB in [0,1]. */
std::optional<Vec4> parseColorHexRgb(std::string_view text);

/** Format RGB of @p color as uppercase `#RRGGBB` (alpha ignored). */
std::string formatColorHexRgb(Vec4 color);

} // namespace ArtCade
