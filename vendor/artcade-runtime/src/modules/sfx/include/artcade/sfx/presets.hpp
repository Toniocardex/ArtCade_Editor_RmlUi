#pragma once

#include "artcade/sfx/types.hpp"

namespace artcade::sfx::presets {

[[nodiscard]] SfxRecipe coin();
[[nodiscard]] SfxRecipe jump();
[[nodiscard]] SfxRecipe laser();
[[nodiscard]] SfxRecipe explosion();
[[nodiscard]] SfxRecipe hit();

} // namespace artcade::sfx::presets
