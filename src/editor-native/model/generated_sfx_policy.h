#pragma once

#include "artcade/sfx/types.hpp"
#include "core/types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ArtCade::EditorNative {

class ProjectDocument;

/** Single domain policy for Generated SFX names, ownership and canonical output. */
[[nodiscard]] std::string normalizeAudioDisplayName(std::string_view value);

[[nodiscard]] const artcade::sfx::GeneratedSfxDef* findGeneratedSfxOutputOwner(
    const ProjectDoc& document, const AssetId& audioAssetId);

[[nodiscard]] bool audioIsLinkedGeneratedOutput(
    const ProjectDoc& document, const AssetId& audioAssetId);

/** Case-insensitive display-name collision across Generated SFX + Audio assets. */
[[nodiscard]] bool audioDisplayNameExists(
    const ProjectDoc& document,
    std::string_view candidate,
    const std::optional<std::string>& exceptSfxId = std::nullopt,
    const std::optional<AssetId>& exceptAudioAssetId = std::nullopt);

[[nodiscard]] std::string uniqueGeneratedSfxName(const ProjectDocument& document,
                                                 const std::string& baseName);

[[nodiscard]] bool generatedSfxRecipesEqual(const artcade::sfx::SfxRecipe& left,
                                            const artcade::sfx::SfxRecipe& right);

/** Deterministic 1:1 catalog identity: generated-audio-{sfxId}. */
[[nodiscard]] AssetId generatedAudioAssetId(const std::string& generatedSfxId);
[[nodiscard]] std::string generatedAudioRelativePath(const std::string& generatedSfxId);

/** Next free authoring id, including ids blocked by canonical audio collisions. */
[[nodiscard]] std::string nextGeneratedSfxId(const ProjectDocument& document);

/** True when any AudioAssetDef already owns this project-relative path. */
[[nodiscard]] bool generatedSfxOutputPathTaken(
    const ProjectDocument& document,
    const std::string& relativePath,
    const std::optional<AssetId>& exceptAssetId = std::nullopt);

struct GeneratedSfxOutputIdentity {
    AssetId assetId;
    std::string relativePath;
    std::filesystem::path finalPath;
    std::filesystem::path stagingPath;
};

/** Stable canonical output for first Generate and every Regenerate. */
[[nodiscard]] std::optional<GeneratedSfxOutputIdentity>
stableGeneratedSfxOutputIdentity(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot);

/** Portable comparison key for generated output paths (slash + case normalized). */
[[nodiscard]] std::string generatedSfxPathKey(std::string_view path);

/** Derived readiness of a Generated SFX versus its linked WAV (fingerprint model). */
enum class GeneratedSfxOutputStatus {
    NeedsGeneration,
    Stale,
    Ready,
};

[[nodiscard]] GeneratedSfxOutputStatus generatedSfxOutputStatus(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition);

[[nodiscard]] const char* generatedSfxOutputStatusLabel(GeneratedSfxOutputStatus status);

struct GeneratedSfxAuthorityValidation {
    bool ok = true;
    std::string error;

    [[nodiscard]] static GeneratedSfxAuthorityValidation failure(std::string message);
};

/** Validates the one-way GeneratedSfxDef -> AudioAssetDef ownership contract. */
[[nodiscard]] GeneratedSfxAuthorityValidation validateGeneratedSfxAuthority(
    const ProjectDoc& document);

/** v7 -> v8 compatibility: remove linked-name mirrors and derive provenance. */
void migrateGeneratedSfxAuthority(ProjectDoc& document);

} // namespace ArtCade::EditorNative
