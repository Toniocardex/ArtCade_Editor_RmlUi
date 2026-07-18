#include "editor-native/model/generated_sfx_preset_catalog.h"

#include "editor-native/model/generated_sfx_policy.h"

#include "artcade/sfx/presets.hpp"

namespace ArtCade::EditorNative {
namespace {

artcade::sfx::SfxRecipe blankPreset() { return {}; }

const std::array<GeneratedSfxPresetDescriptor, 6> kCatalog{{
    {"blank", "Blank", "SFX", false, &blankPreset},
    {"coin", "Coin", "Coin", true, &artcade::sfx::presets::coin},
    {"jump", "Jump", "Jump", true, &artcade::sfx::presets::jump},
    {"laser", "Laser", "Laser", true, &artcade::sfx::presets::laser},
    {"explosion", "Explosion", "Explosion", true,
     &artcade::sfx::presets::explosion},
    {"hit", "Hit", "Hit", true, &artcade::sfx::presets::hit},
}};

} // namespace

const std::array<GeneratedSfxPresetDescriptor, 6>& generatedSfxPresetCatalog() {
    return kCatalog;
}

const GeneratedSfxPresetDescriptor* findGeneratedSfxPreset(std::string_view id) {
    for (const auto& preset : kCatalog)
        if (preset.id == id) return &preset;
    return nullptr;
}

std::optional<artcade::sfx::SfxRecipe> generatedSfxRecipeFromPreset(
    std::string_view id) {
    const auto* preset = findGeneratedSfxPreset(id);
    if (!preset || !preset->makeRecipe) return std::nullopt;
    return preset->makeRecipe();
}

std::string activeGeneratedSfxPresetId(const artcade::sfx::SfxRecipe& recipe) {
    for (const auto& preset : kCatalog) {
        if (!preset.availableForApply || !preset.makeRecipe) continue;
        if (generatedSfxRecipesEqual(recipe, preset.makeRecipe()))
            return std::string(preset.id);
    }
    return {};
}

} // namespace ArtCade::EditorNative
