#pragma once

#include "artcade/sfx/result.hpp"
#include "artcade/sfx/types.hpp"

#include <string>
#include <string_view>

namespace artcade::sfx {

// Available when ARTCADE_SFX_ENABLE_JSON is enabled and nlohmann_json is linked.
[[nodiscard]] Result<std::string> serializeRecipeJson(
    const GeneratedSfxDef& definition,
    int indentation = 2
);

[[nodiscard]] Result<GeneratedSfxDef> deserializeRecipeJson(
    std::string_view jsonText
);

/** Deterministic fingerprint of recipe content only (id/name/output ignored). */
[[nodiscard]] std::string recipeFingerprint(const SfxRecipe& recipe);

} // namespace artcade::sfx
