#include "editor-native/app/export/runtime_project_preflight.h"

#include "editor-native/model/project_io.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/model/tilemap_validation.h"
#include "core/text-component-format.h"
#include "project-current-format.h"

#include <nlohmann/json.hpp>
#include <optional>

namespace ArtCade::EditorNative {
namespace {

std::optional<std::string> validatePlaySpriteReferences(const ProjectDocument& document) {
    for (const auto& [typeId, type] : document.data().objectTypes) {
        if (type.spriteRenderer && !type.spriteRenderer->imageAssetId.empty()
            && !document.hasImageAsset(type.spriteRenderer->imageAssetId)) {
            return "Object Type \"" + typeId
                 + "\" SpriteRenderer references a missing image asset";
        }
        if (type.spritePresentation) {
            const SpritePresentationSource& source = type.spritePresentation->source;
            if (const auto* image = std::get_if<SpritePresentationImage>(&source)) {
                if (!image->imageAssetId.empty()
                    && !document.hasImageAsset(image->imageAssetId)) {
                    return "Object Type \"" + typeId
                         + "\" Sprite references a missing image asset";
                }
            } else if (const auto* animation =
                           std::get_if<SpritePresentationAnimation>(&source)) {
                if (animation->animationAssetId.empty()
                    || !document.hasSpriteAnimationAsset(animation->animationAssetId)) {
                    return "Object Type \"" + typeId
                         + "\" Sprite references a missing animation asset";
                }
                const SpriteAnimationAssetDef* asset =
                    document.findSpriteAnimationAsset(animation->animationAssetId);
                if (!animation->defaultClipId.empty() && asset) {
                    bool ownsClip = false;
                    for (const SpriteAnimationClipDef& clip : asset->clips) {
                        if (clip.id == animation->defaultClipId) {
                            ownsClip = true;
                            break;
                        }
                    }
                    if (!ownsClip) {
                        return "Object Type \"" + typeId
                             + "\" Sprite defaultClipId must belong to its animation asset";
                    }
                }
            }
        }
        if (type.spriteAnimator) {
            const AssetId& animationId = type.spriteAnimator->animationAssetId;
            if (animationId.empty()) {
                return "Object Type \"" + typeId
                     + "\" SpriteAnimator requires an animation asset";
            }
            if (!document.hasSpriteAnimationAsset(animationId)) {
                return "Object Type \"" + typeId
                     + "\" SpriteAnimator references a missing animation asset";
            }
            const SpriteAnimationAssetDef* asset =
                document.findSpriteAnimationAsset(animationId);
            if (!type.spriteAnimator->defaultClipId.empty() && asset) {
                bool ownsClip = false;
                for (const SpriteAnimationClipDef& clip : asset->clips) {
                    if (clip.id == type.spriteAnimator->defaultClipId) {
                        ownsClip = true;
                        break;
                    }
                }
                if (!ownsClip) {
                    return "Object Type \"" + typeId
                         + "\" SpriteAnimator defaultClipId must belong to its animation asset";
                }
            }
        }
    }
    for (const auto& [sceneId, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            const auto type = document.data().objectTypes.find(instance.objectTypeId);
            if (type == document.data().objectTypes.end()) continue;
            const ResolvedSpritePresentation presentation =
                resolveSpritePresentation(type->second, instance);
            if (presentation.renderer && !presentation.renderer->imageAssetId.empty()
                && !document.hasImageAsset(presentation.renderer->imageAssetId)) {
                return "Scene \"" + sceneId + "\" sprite renderer references a missing image asset";
            }
            if (presentation.animator) {
                const AssetId& animationId = presentation.animator->animationAssetId;
                if (animationId.empty()
                    || !document.hasSpriteAnimationAsset(animationId)) {
                    return "Scene \"" + sceneId
                         + "\" sprite animator references a missing animation asset";
                }
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> validatePlayTilemapReferences(const ProjectDocument& document) {
    for (const auto& [sceneId, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            if (!instance.tilemap) continue;
            if (const auto error = validateTilemapComponent(document, *instance.tilemap)) {
                return "Scene \"" + sceneId + "\" entity "
                    + std::to_string(instance.id) + " Tilemap: " + *error;
            }
            const TilesetAsset* tileset =
                document.findTilesetAsset(instance.tilemap->tilesetAssetId);
            if (!tileset || tileset->imageAssetId.empty()
                || !document.hasImageAsset(tileset->imageAssetId)) {
                return "Scene \"" + sceneId + "\" entity "
                    + std::to_string(instance.id)
                    + " Tilemap tileset references a missing image asset";
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> validatePlayPresentationComponents(const ProjectDocument& document) {
    for (const auto& [typeId, type] : document.data().objectTypes) {
        if (type.text) {
            std::string error;
            if (!validateTextComponent(*type.text, error)) {
                return "Object Type \"" + typeId + "\" Text: " + error;
            }
            if (!type.text->bindKey.empty()) {
                if (type.text->bindScope == "global") {
                    bool found = false;
                    std::optional<GameVariableDefinition::Type> boundType;
                    for (const GameVariableDefinition& variable :
                         document.data().globalVariables) {
                        if (variable.key == type.text->bindKey) {
                            found = true;
                            boundType = variable.type;
                            break;
                        }
                    }
                    if (!found) {
                        return "Object Type \"" + typeId
                             + "\" Text bindKey references a missing global variable";
                    }
                    if (!isTextFormatCompatibleWithVariableType(type.text->format, *boundType)) {
                        return "Object Type \"" + typeId
                             + "\" Text format is incompatible with bound global variable";
                    }
                } else if (type.text->bindScope == "local") {
                    bool found = false;
                    std::optional<GameVariableDefinition::Type> boundType;
                    for (const GameVariableDefinition& variable : type.localVariables) {
                        if (variable.key == type.text->bindKey) {
                            found = true;
                            boundType = variable.type;
                            break;
                        }
                    }
                    if (!found) {
                        return "Object Type \"" + typeId
                             + "\" Text bindKey references a missing local variable";
                    }
                    if (!isTextFormatCompatibleWithVariableType(type.text->format, *boundType)) {
                        return "Object Type \"" + typeId
                             + "\" Text format is incompatible with bound local variable";
                    }
                }
            }
        }
        if (type.gauge) {
            std::string error;
            if (!validateGaugeComponent(*type.gauge, error)) {
                return "Object Type \"" + typeId + "\" Gauge: " + error;
            }
            if (!type.gauge->bindKey.empty()) {
                if (type.gauge->bindScope == "global") {
                    bool found = false;
                    for (const GameVariableDefinition& variable :
                         document.data().globalVariables) {
                        if (variable.key == type.gauge->bindKey) {
                            found = true;
                            if (!isGaugeCompatibleWithVariableType(variable.type)) {
                                return "Object Type \"" + typeId
                                     + "\" Gauge requires a Number global variable";
                            }
                            break;
                        }
                    }
                    if (!found) {
                        return "Object Type \"" + typeId
                             + "\" Gauge bindKey references a missing global variable";
                    }
                } else if (type.gauge->bindScope == "local") {
                    bool found = false;
                    for (const GameVariableDefinition& variable : type.localVariables) {
                        if (variable.key == type.gauge->bindKey) {
                            found = true;
                            if (!isGaugeCompatibleWithVariableType(variable.type)) {
                                return "Object Type \"" + typeId
                                     + "\" Gauge requires a Number local variable";
                            }
                            break;
                        }
                    }
                    if (!found) {
                        return "Object Type \"" + typeId
                             + "\" Gauge bindKey references a missing local variable";
                    }
                }
            }
        }
    }
    return std::nullopt;
}

} // namespace

RuntimeProjectPreflightResult prepareRuntimeProjectSnapshot(
    const ProjectDocument& document) {
    RuntimeProjectPreflightResult result;

    // Export/Play is a gameplay boundary: reject invalid persistent tilemap
    // references before serializing a project the standalone renderer would
    // have to defensively omit at runtime. Keep this intentionally scoped to
    // ADR-0040's new contract; unrelated legacy Play permissiveness remains.
    if (const auto tilemapError = validatePlayTilemapReferences(document)) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::RuntimeValidationFailed, *tilemapError));
        return result;
    }

    if (const auto spriteError = validatePlaySpriteReferences(document)) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::InvalidAsset, *spriteError));
        return result;
    }
    if (const auto presentationError = validatePlayPresentationComponents(document)) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::RuntimeValidationFailed, *presentationError));
        return result;
    }

    result.compiledLogic = Logic::compileProjectLogic(document.data());
    if (!result.compiledLogic.ok()) {
        std::string message = "Logic Board validation failed";
        if (!result.compiledLogic.diagnostics.empty()) {
            message += " [" + result.compiledLogic.diagnostics.front().code + "] "
                     + result.compiledLogic.diagnostics.front().message;
        }
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::RuntimeValidationFailed, std::move(message)));
        return result;
    }

    const SerializeResult serialized = ProjectSerializer::serialize(document);
    if (!serialized.ok) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::RuntimeValidationFailed,
            "failed to serialize project for export"));
        return result;
    }

    std::string validationError;
    nlohmann::json parsed;
    bool parseOk = true;
    try { parsed = nlohmann::json::parse(serialized.value); }
    catch (...) { parseOk = false; }
    if (!parseOk || !ProjectJson::validate_current_project_json(parsed, validationError)) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::RuntimeValidationFailed,
            "canonical loader rejected the project"
                + (validationError.empty() ? std::string{}
                                           : " [" + validationError + "]")));
        return result;
    }

    result.canonicalProjectJson = serialized.value;
    result.ok = true;
    return result;
}

} // namespace ArtCade::EditorNative
