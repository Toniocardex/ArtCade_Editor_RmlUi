#include "../include/app.h"

#include "app_modules.h"

#include "../../modules/presentation/include/presentation_mode.h"
#include "../../modules/game-state/include/splash-state.h"
#include "../../modules/sprite-animator/include/animation-clips-registry.h"
#include "../../modules/logic-core/include/logic-core.h"
// D-20: needed directly now - this file constructs a standalone
// Scripts::ScriptRuntime validator (scriptValidator) to preflight attached
// scripts before any gameplay world accepts them; it used to get the type
// transitively via app_modules.h's Application::Modules::scriptRuntime field,
// which D-20 removed (ScriptRuntime is GameplaySession-owned now).
#include "../../modules/script-runtime/include/script-runtime.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace ArtCade {

void Application::applyRuntimeSettings(const ProjectRuntimeSettings& settings,
                                       ViewportPolicy policy) {
    const float fps = settings.targetFPS;
    const float safeFps = (std::isfinite(fps) && fps >= 1.f) ? fps : 60.f;
    targetDt_ = 1.f / safeFps;
    physicsMode_ = settings.physicsMode;
    if (mod_ && mod_->gameplaySession) mod_->gameplaySession->setPhysicsMode(physicsMode_);

    if (mod_ && mod_->gameplaySession) {
        const float gravity = std::isfinite(settings.gravity) ? settings.gravity : 9.81f;
        mod_->gameplaySession->setGravity({0.f, gravity});
    }
    if (mod_ && mod_->timeManager) {
        const float timeScale =
            std::isfinite(settings.timeScale) && settings.timeScale > 0.f
            ? settings.timeScale
            : 1.f;
        mod_->timeManager->setTimeScale(timeScale, "gameplay");
    }

    if (!mod_ || !mod_->renderer || !mod_->sceneManager) return;
    const SceneDef* scene = mod_->sceneManager->activeScene();
    if (!scene) return;

    if (policy == ViewportPolicy::EditorPreview) {
        // Native edit window tracks world size 1:1.
        if (scene->worldSize.x > 0.f && scene->worldSize.y > 0.f) {
            mod_->renderer->setWindowSize(
                static_cast<uint32_t>(scene->worldSize.x),
                static_cast<uint32_t>(scene->worldSize.y),
                "ArtCade V2");
        }
        mod_->editorViewport->set_presentation_mode(
            ArtCade::Presentation::PresentationMode::SceneEdit);
        mod_->renderer->setGameViewCompositorEnabled(false);
        return;
    }

    mod_->editorViewport->set_presentation_mode(
        ArtCade::Presentation::PresentationMode::PlayEmbedded);
    mod_->renderer->setGameViewCompositorEnabled(true);
    mod_->renderer->setOutputPolicy(settings.outputPolicy);

    if (scene->viewportSize.x > 0.f && scene->viewportSize.y > 0.f) {
        mod_->renderer->setWindowSizeForLogicalViewport(
            static_cast<uint32_t>(scene->viewportSize.x),
            static_cast<uint32_t>(scene->viewportSize.y),
            "ArtCade V2");
    }
    // Camera start is World::resetCameraForActiveScene (ADR-0018). Do not push
    // SceneDef::cameraStart into the renderer independently of World.
}

bool Application::loadProject(const std::string& projectPath) {
    const auto endsWith = [](const std::string& value, const char* suffix) {
        const std::size_t suffixLength = std::strlen(suffix);
        return value.size() >= suffixLength
            && value.compare(value.size() - suffixLength, suffixLength, suffix) == 0;
    };

    ProjectDoc doc;
    const bool loaded = endsWith(projectPath, ".artcade")
        ? mod_->assetLoader->loadArtcade(projectPath, doc)
        : mod_->assetLoader->loadDirectory(projectPath, doc);
    if (!loaded) {
        std::cerr << "[App] Could not load project: " << projectPath << "\n";
        return false;
    }

    // Sole Logic Board host: compile objectTypes[].logicBoard into
    // LogicRuntime programs at load. scripts/main.lua remains My Script / GameAPI
    // only — generated Logic Lua never overwrites it.
    const ArtCade::Logic::LogicCompileResult compiled =
        ArtCade::Logic::compileProjectLogic(doc);
    if (!compiled.ok()) {
        std::cerr << "[App] Logic Board compile failed:\n";
        for (const ArtCade::Logic::LogicDiagnostic &d : compiled.diagnostics) {
            if (d.severity != ArtCade::Logic::DiagnosticSeverity::Error) {
                continue;
            }
            std::cerr << "  [" << d.code << "] ";
            if (!d.objectTypeId.empty()) {
                std::cerr << d.objectTypeId << ": ";
            }
            std::cerr << d.message << "\n";
        }
        return false;
    }
    std::string logicError;
    if (!mod_->gameplaySession
        || !mod_->gameplaySession->loadLogicPrograms(compiled.programs, &logicError)) {
        std::cerr << "[App] Could not load Logic Board programs: " << logicError << "\n";
        return false;
    }

    // Strict immutable manual-source snapshot. Resolve the authoring graph in
    // deterministic Object Type + persisted attachment order, read each linked
    // source exactly once through AssetLoader confinement, and preflight it
    // before any gameplay world is accepted.
    std::unordered_map<AssetId, Scripts::ScriptProgram> nextScriptPrograms;
    std::unordered_map<ObjectTypeId, std::vector<ScriptAttachmentDef>>
        nextScriptAttachments;
    std::vector<ObjectTypeId> scriptTypeIds;
    scriptTypeIds.reserve(doc.objectTypes.size());
    for (const auto& [typeId, unused] : doc.objectTypes) {
        (void)unused;
        scriptTypeIds.push_back(typeId);
    }
    std::sort(scriptTypeIds.begin(), scriptTypeIds.end());
    std::vector<AssetId> linkedScriptIds;
    std::unordered_set<AssetId> seenScriptIds;
    for (const ObjectTypeId& typeId : scriptTypeIds) {
        const EntityDef& type = doc.objectTypes.at(typeId);
        if (!type.scripts) continue;
        nextScriptAttachments.emplace(typeId, type.scripts->attachments);
        for (const ScriptAttachmentDef& attachment : type.scripts->attachments) {
            if (attachment.enabled && seenScriptIds.insert(attachment.scriptAssetId).second)
                linkedScriptIds.push_back(attachment.scriptAssetId);
        }
    }
    std::unordered_map<AssetId, const ScriptAssetDef*> scriptAssets;
    for (const ScriptAssetDef& asset : doc.scriptAssets) {
        if (!scriptAssets.emplace(asset.assetId, &asset).second) {
            std::cerr << "[App] Duplicate Script Asset id: " << asset.assetId << "\n";
            return false;
        }
    }
    const Scripts::ScriptRuntimeLimits scriptLimits;
    Scripts::ScriptRuntime scriptValidator{scriptLimits};
    for (const AssetId& assetId : linkedScriptIds) {
        const auto asset = scriptAssets.find(assetId);
        if (asset == scriptAssets.end()) {
            std::cerr << "[App] Attached Script Asset is missing: " << assetId << "\n";
            return false;
        }
        std::string source;
        if (!mod_->assetLoader->loadScriptSource(
                asset->second->sourcePath, scriptLimits.maxSourceBytes, source)) {
            std::cerr << "[App] Could not read attached script: "
                      << asset->second->sourcePath << "\n";
            return false;
        }
        Scripts::ScriptProgram program{
            assetId, asset->second->sourcePath, std::move(source)};
        std::string scriptError;
        if (!scriptValidator.validateProgram(program, &scriptError)) {
            std::cerr << "[App] Invalid attached script " << program.sourcePath
                      << ": " << scriptError << "\n";
            return false;
        }
        nextScriptPrograms.emplace(assetId, std::move(program));
    }
    mod_->gameplaySession->setScriptCatalog(
        std::move(nextScriptPrograms), std::move(nextScriptAttachments));

    mod_->gameplaySession->loadWorldProject(doc);
    if (mod_->spriteAnimator) {
        registerAnimationClipsFromAssets(*mod_->spriteAnimator, doc.imageAssets);
        appendAnimationClipsFromAssets(*mod_->spriteAnimator, doc.spriteAnimationAssets);
    }
    if (mod_->audio) mod_->audio->setRuntimeAssetCatalog(doc.audioAssets);
    // ADR-0039 §10: prepares Logic/Script scopes only - no On Start here.
    // Activation is deferred to Application::tickGameplayActivation(), once
    // the startup phase (splash or immediate activation, decided below by
    // license tier) reaches Activating.
    if (!mod_->gameplaySession->prepareActiveSceneGameplay()) return false;
    applyRuntimeSettings(runtimeSettingsFromProjectDoc(doc), ViewportPolicy::NativePlay);

    tileColors_.clear();
    for (const auto& tile : doc.tilePalette) tileColors_[tile.id] = tile.color;

    tilesets_.clear();
    for (const auto& tileset : doc.tilesets) tilesets_[tileset.assetId] = tileset;
    mod_->sceneManager->setTilesets(doc.tilesets);
    {
        std::vector<SceneLayerDef> layers;
        auto scene_it = doc.scenes.find(doc.activeSceneId);
        if (scene_it == doc.scenes.end() && !doc.scenes.empty()) {
            scene_it = doc.scenes.begin();
        }
        if (scene_it != doc.scenes.end()) {
            layers = scene_it->second.layers;
        }
        mod_->sceneManager->setSceneLayers(std::move(layers));
    }

    // My Script / legacy entry: prefer .luac, else companion .lua.
    // Editor-native projects often leave the default mainScriptPath set without
    // shipping either file — gameplay is Script Assets + Logic Board. Missing
    // main is then a soft skip; present-but-invalid bytecode remains fatal.
    if (!doc.mainScriptPath.empty()) {
        std::vector<uint8_t> bytecode;
        const bool haveBytecode =
            mod_->assetLoader->loadLuaBytecode(doc.mainScriptPath, bytecode)
            && !bytecode.empty();
        if (haveBytecode) {
            std::string bytecodeError;
            if (!mod_->gameplaySession->loadLuaBytecode(
                    bytecode.data(), bytecode.size(), &bytecodeError)) {
                std::cerr << "[App] Invalid main script bytecode: "
                          << doc.mainScriptPath;
                if (!bytecodeError.empty()) std::cerr << " (" << bytecodeError << ")";
                std::cerr << "\n";
                return false;
            }
        } else {
            std::string sourcePath = doc.mainScriptPath;
            if (endsWith(sourcePath, ".luac")) {
                sourcePath.replace(sourcePath.size() - 5, 5, ".lua");
            }
            std::string source;
            if (mod_->assetLoader->loadScriptSource(
                    sourcePath, scriptLimits.maxSourceBytes, source)
                && !source.empty()) {
                mod_->gameplaySession->loadLuaSource(source);
            } else {
                std::cerr << "[App] Main script not found (" << doc.mainScriptPath
                          << "); continuing with Logic Board / Script Assets only\n";
            }
        }
    }

    licenseTier_ = doc.licenseTier;
    if (mod_->dialogManager) {
        mod_->dialogManager->loadDialogsFromDirectory(mod_->assetLoader->projectRoot());
    }

    // ADR-0039 §10/§20 (Free/Pro policy v1): Free blocks standalone startup
    // on the splash; every other tier activates gameplay on the very next
    // frame. Editor/WASM builds never call loadProject() (see
    // app.cpp::run()), so startupPhase_ stays Inactive there regardless.
    if (licenseTier_ == "free") {
        splash_ = std::make_unique<ArtCade::Modules::SplashState>("free");
        startupPhase_ = GameplayStartupPhase::Splash;
    } else {
        splash_.reset();
        startupPhase_ = GameplayStartupPhase::Activating;
    }

    std::cout << "[App] Project loaded: " << doc.projectName
              << " (license=" << licenseTier_ << ")\n";
    return true;
}

} // namespace ArtCade
