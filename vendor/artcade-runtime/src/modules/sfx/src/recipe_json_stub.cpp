#include "artcade/sfx/recipe_json.hpp"

namespace artcade::sfx {

Result<std::string> serializeRecipeJson(const GeneratedSfxDef&, int) {
    return Result<std::string>::failure(
        ErrorCode::EncoderUnavailable,
        "Serializzazione JSON non compilata. Abilita ARTCADE_SFX_ENABLE_JSON."
    );
}

Result<GeneratedSfxDef> deserializeRecipeJson(std::string_view) {
    return Result<GeneratedSfxDef>::failure(
        ErrorCode::EncoderUnavailable,
        "Serializzazione JSON non compilata. Abilita ARTCADE_SFX_ENABLE_JSON."
    );
}

std::string recipeFingerprint(const SfxRecipe&) {
    return {};
}

} // namespace artcade::sfx
