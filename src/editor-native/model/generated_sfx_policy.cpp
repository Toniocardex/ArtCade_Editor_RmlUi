#include "editor-native/model/generated_sfx_policy.h"

#include "artcade/sfx/recipe_json.hpp"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/project_document.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

std::string folded(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string uniqueNameStem(std::string name) {
    name = normalizeAudioDisplayName(name);
    if (name.empty()) return {};
    const auto space = name.rfind(' ');
    if (space == std::string::npos || space + 1 >= name.size()) return name;
    const std::string suffix = name.substr(space + 1);
    if (suffix.empty()
        || !std::all_of(suffix.begin(), suffix.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return name;
    }
    return normalizeAudioDisplayName(name.substr(0, space));
}

const AudioAssetDef* findAudioAsset(const ProjectDoc& document, const AssetId& id) {
    const auto it = std::find_if(document.audioAssets.begin(), document.audioAssets.end(),
        [&](const AudioAssetDef& asset) { return asset.assetId == id; });
    return it == document.audioAssets.end() ? nullptr : &*it;
}

AudioAssetDef* findAudioAsset(ProjectDoc& document, const AssetId& id) {
    const auto it = std::find_if(document.audioAssets.begin(), document.audioAssets.end(),
        [&](const AudioAssetDef& asset) { return asset.assetId == id; });
    return it == document.audioAssets.end() ? nullptr : &*it;
}

} // namespace

std::string normalizeAudioDisplayName(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size()
           && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin
           && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

const artcade::sfx::GeneratedSfxDef* findGeneratedSfxOutputOwner(
    const ProjectDoc& document, const AssetId& audioAssetId) {
    if (audioAssetId.empty()) return nullptr;
    const auto it = std::find_if(document.generatedSfx.begin(), document.generatedSfx.end(),
        [&](const artcade::sfx::GeneratedSfxDef& definition) {
            return !definition.outputAssetId.empty()
                && definition.outputAssetId == audioAssetId;
        });
    return it == document.generatedSfx.end() ? nullptr : &*it;
}

bool audioIsLinkedGeneratedOutput(const ProjectDoc& document,
                                  const AssetId& audioAssetId) {
    return findGeneratedSfxOutputOwner(document, audioAssetId) != nullptr;
}

bool audioDisplayNameExists(
    const ProjectDoc& document,
    std::string_view candidate,
    const std::optional<std::string>& exceptSfxId,
    const std::optional<AssetId>& exceptAudioAssetId) {
    const std::string needle = folded(normalizeAudioDisplayName(candidate));
    if (needle.empty()) return false;
    if (std::any_of(document.generatedSfx.begin(), document.generatedSfx.end(),
            [&](const artcade::sfx::GeneratedSfxDef& definition) {
                if (exceptSfxId && definition.id == *exceptSfxId) return false;
                return folded(normalizeAudioDisplayName(definition.name)) == needle;
            })) {
        return true;
    }
    return std::any_of(document.audioAssets.begin(), document.audioAssets.end(),
        [&](const AudioAssetDef& asset) {
            if (exceptAudioAssetId && asset.assetId == *exceptAudioAssetId) return false;
            if (audioIsLinkedGeneratedOutput(document, asset.assetId)) return false;
            if (asset.name.empty()) return false;
            return folded(normalizeAudioDisplayName(asset.name)) == needle;
        });
}

std::string uniqueGeneratedSfxName(const ProjectDocument& document,
                                   const std::string& baseName) {
    std::string stem = uniqueNameStem(baseName);
    if (stem.empty()) stem = "Generated SFX";
    if (!audioDisplayNameExists(document.data(), stem)) return stem;
    for (int index = 2; index < 10000; ++index) {
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), " %02d", index);
        const std::string candidate = stem + suffix;
        if (!audioDisplayNameExists(document.data(), candidate)) return candidate;
    }
    return stem + " 9999";
}

bool generatedSfxRecipesEqual(const artcade::sfx::SfxRecipe& left,
                              const artcade::sfx::SfxRecipe& right) {
    return artcade::sfx::recipeFingerprint(left) == artcade::sfx::recipeFingerprint(right);
}

AssetId generatedAudioAssetId(const std::string& generatedSfxId) {
    return "generated-audio-" + generatedSfxId;
}

std::string generatedAudioRelativePath(const std::string& generatedSfxId) {
    return "assets/audio/generated/" + generatedAudioAssetId(generatedSfxId) + ".wav";
}

std::string nextGeneratedSfxId(const ProjectDocument& document) {
    int index = 1;
    std::string id;
    do {
        id = "generated-sfx-" + std::to_string(index++);
    } while (document.hasGeneratedSfx(id)
             || document.hasAudioAsset(generatedAudioAssetId(id)));
    return id;
}

bool generatedSfxOutputPathTaken(const ProjectDocument& document,
                                 const std::string& relativePath,
                                 const std::optional<AssetId>& exceptAssetId) {
    if (relativePath.empty()) return false;
    const std::string needle = generatedSfxPathKey(relativePath);
    return std::any_of(document.data().audioAssets.begin(), document.data().audioAssets.end(),
        [&](const AudioAssetDef& asset) {
            if (exceptAssetId && asset.assetId == *exceptAssetId) return false;
            return generatedSfxPathKey(asset.sourcePath) == needle;
        });
}

std::optional<GeneratedSfxOutputIdentity> stableGeneratedSfxOutputIdentity(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot) {
    const AssetId canonicalId = generatedAudioAssetId(definition.id);
    const std::string canonicalPath = generatedAudioRelativePath(definition.id);
    GeneratedSfxOutputIdentity identity;
    identity.assetId = canonicalId;
    identity.relativePath = canonicalPath;

    if (!definition.outputAssetId.empty()
        && document.hasAudioAsset(definition.outputAssetId)) {
        if (definition.outputAssetId != canonicalId
            || definition.outputPath != canonicalPath) {
            return std::nullopt;
        }
    } else {
        if (document.hasAudioAsset(canonicalId)) return std::nullopt;
        if (generatedSfxOutputPathTaken(document, canonicalPath)) return std::nullopt;
    }

    if (projectRoot.empty()) return identity;
    const auto confined = resolvePathInsideRoot(
        projectRoot, std::filesystem::u8path(identity.relativePath));
    if (!confined.ok) return std::nullopt;
    identity.finalPath = confined.value;
    identity.stagingPath = identity.finalPath.parent_path()
        / (identity.finalPath.stem().string() + ".artcade-pending.wav");
    return identity;
}

std::string generatedSfxPathKey(std::string_view path) {
    std::string key(path);
    std::replace(key.begin(), key.end(), '\\', '/');
    return folded(std::move(key));
}

GeneratedSfxOutputStatus generatedSfxOutputStatus(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition) {
    if (definition.outputAssetId.empty()
        || !document.hasAudioAsset(definition.outputAssetId)) {
        return GeneratedSfxOutputStatus::NeedsGeneration;
    }
    if (definition.generatedRecipeFingerprint
        == artcade::sfx::recipeFingerprint(definition.recipe)) {
        return GeneratedSfxOutputStatus::Ready;
    }
    return GeneratedSfxOutputStatus::Stale;
}

const char* generatedSfxOutputStatusLabel(GeneratedSfxOutputStatus status) {
    switch (status) {
    case GeneratedSfxOutputStatus::NeedsGeneration: return "Needs generation";
    case GeneratedSfxOutputStatus::Stale: return "Stale";
    case GeneratedSfxOutputStatus::Ready: return "Ready";
    }
    return "Needs generation";
}

GeneratedSfxAuthorityValidation GeneratedSfxAuthorityValidation::failure(
    std::string message) {
    GeneratedSfxAuthorityValidation result;
    result.ok = false;
    result.error = std::move(message);
    return result;
}

GeneratedSfxAuthorityValidation validateGeneratedSfxAuthority(
    const ProjectDoc& document) {
    std::unordered_set<AssetId> claimedOutputs;
    std::unordered_map<std::string, AssetId> generatedPaths;

    for (const artcade::sfx::GeneratedSfxDef& definition : document.generatedSfx) {
        const bool hasAsset = !definition.outputAssetId.empty();
        const bool hasPath = !definition.outputPath.empty();
        if (hasAsset != hasPath) {
            return GeneratedSfxAuthorityValidation::failure(
                "Generated SFX output asset and path must be set together");
        }
        if (!hasAsset) {
            if (!definition.generatedRecipeFingerprint.empty()) {
                return GeneratedSfxAuthorityValidation::failure(
                    "Generated SFX without an output cannot keep a recipe fingerprint");
            }
            continue;
        }

        const AssetId canonicalId = generatedAudioAssetId(definition.id);
        const std::string canonicalPath = generatedAudioRelativePath(definition.id);
        if (definition.outputAssetId != canonicalId
            || definition.outputPath != canonicalPath) {
            return GeneratedSfxAuthorityValidation::failure(
                "Generated SFX output must use its canonical asset id and path");
        }
        if (!claimedOutputs.insert(definition.outputAssetId).second) {
            return GeneratedSfxAuthorityValidation::failure(
                "Generated audio output cannot be owned by more than one recipe");
        }
        const std::string pathKey = generatedSfxPathKey(definition.outputPath);
        const auto [pathIt, inserted] = generatedPaths.emplace(pathKey, definition.outputAssetId);
        if (!inserted && pathIt->second != definition.outputAssetId) {
            return GeneratedSfxAuthorityValidation::failure(
                "Generated audio output path cannot be owned by more than one asset");
        }

        const AudioAssetDef* audio = findAudioAsset(document, definition.outputAssetId);
        if (!audio) {
            return GeneratedSfxAuthorityValidation::failure(
                "Generated SFX output must reference an existing audio asset");
        }
        if (audio->sourcePath != canonicalPath) {
            return GeneratedSfxAuthorityValidation::failure(
                "Generated SFX output path does not match its audio asset");
        }
        if (!audio->generatedFromSfxId
            || *audio->generatedFromSfxId != definition.id) {
            return GeneratedSfxAuthorityValidation::failure(
                "Generated audio provenance must match its owning recipe");
        }
        if (!audio->name.empty()) {
            return GeneratedSfxAuthorityValidation::failure(
                "Linked generated audio cannot store a second display name");
        }
    }

    for (const AudioAssetDef& audio : document.audioAssets) {
        const std::string pathKey = generatedSfxPathKey(audio.sourcePath);
        const auto pathOwner = generatedPaths.find(pathKey);
        if (pathOwner != generatedPaths.end() && pathOwner->second != audio.assetId) {
            return GeneratedSfxAuthorityValidation::failure(
                "Generated audio path is also used by another audio asset");
        }
        if (!audio.generatedFromSfxId || audio.generatedFromSfxId->empty()) continue;
        const auto recipe = std::find_if(document.generatedSfx.begin(), document.generatedSfx.end(),
            [&](const artcade::sfx::GeneratedSfxDef& definition) {
                return definition.id == *audio.generatedFromSfxId;
            });
        if (recipe != document.generatedSfx.end()
            && recipe->outputAssetId != audio.assetId) {
            return GeneratedSfxAuthorityValidation::failure(
                "Generated audio provenance conflicts with the active recipe output");
        }
    }

    return {};
}

void migrateGeneratedSfxAuthority(ProjectDoc& document) {
    for (artcade::sfx::GeneratedSfxDef& definition : document.generatedSfx) {
        definition.name = normalizeAudioDisplayName(definition.name);
        if (definition.outputAssetId.empty()) continue;
        AudioAssetDef* audio = findAudioAsset(document, definition.outputAssetId);
        if (!audio) continue;
        if (!audio->generatedFromSfxId || audio->generatedFromSfxId->empty())
            audio->generatedFromSfxId = definition.id;
        // v7 persisted the authoring display name twice. In v8 linked audio
        // derives it exclusively from GeneratedSfxDef.
        audio->name.clear();
    }
    for (AudioAssetDef& audio : document.audioAssets) {
        if (!audioIsLinkedGeneratedOutput(document, audio.assetId))
            audio.name = normalizeAudioDisplayName(audio.name);
    }
}

} // namespace ArtCade::EditorNative
