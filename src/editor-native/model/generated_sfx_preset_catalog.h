#pragma once

#include "artcade/sfx/types.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace ArtCade::EditorNative {

using GeneratedSfxPresetFactory = artcade::sfx::SfxRecipe (*)();

struct GeneratedSfxPresetDescriptor {
    std::string_view id;
    std::string_view label;
    std::string_view defaultAssetName;
    bool availableForApply = true;
    GeneratedSfxPresetFactory makeRecipe = nullptr;
};

const std::array<GeneratedSfxPresetDescriptor, 6>& generatedSfxPresetCatalog();
const GeneratedSfxPresetDescriptor* findGeneratedSfxPreset(std::string_view id);
std::optional<artcade::sfx::SfxRecipe> generatedSfxRecipeFromPreset(
    std::string_view id);
std::string activeGeneratedSfxPresetId(const artcade::sfx::SfxRecipe& recipe);

} // namespace ArtCade::EditorNative
