#include "../include/app.h"

#include "app_modules.h"

#include "../../modules/editor-api/include/editor-api.h"
#include "../../modules/presentation/include/presentation_types.h"
#include "../../modules/presentation/include/editor_viewport_service.h"
#include "render_pass_id.h"
#include "render_pipeline.h"
#include "view_render_features.h"
#include "../../modules/game-state/include/splash-state.h"

#include "passes/debug_pass.h"
#include "passes/grid_pass.h"
#include "passes/gizmo_pass.h"
#include "passes/scene_background_pass.h"
#include "passes/scene_entities_pass.h"
#include "frame_coordinator.h"
#include "scene_frame_context.h"
#include "scene_frame_snapshot.h"

#include <vector>

#ifdef ARTCADE_WASM
#include <emscripten.h>
#endif

#ifndef NDEBUG
#include <cassert>
#endif

namespace ArtCade {

namespace RenderPipeline = ArtCade::Modules;

namespace {

RenderPipeline::ViewRenderFeatures build_view_features(
    const EditorOverlayState& overlay) {
    RenderPipeline::ViewRenderFeatures features{};
    features.drawGrid = overlay.inEditMode && overlay.guidesEnabled;
    features.drawGizmos = overlay.inEditMode;
    features.drawSelection = overlay.inEditMode && overlay.selectedId != 0u;
#ifdef ARTCADE_WASM
    features.drawPhysicsDebug =
        EditorAPI::s_physicsDebugDraw && !overlay.inEditMode;
#else
    features.drawPhysicsDebug = false;
#endif
    return features;
}

#ifndef NDEBUG
/**
 * Tilemap-alias window: frame build → beginFrame → render passes → present.
 * Debug builds only; release has no runtime guard.
 */
struct SceneFrameRenderScope {
    bool& active;
    explicit SceneFrameRenderScope(bool& flag) : active(flag) {
        assert(!active && "scene frame render scope must not nest");
        active = true;
    }
    ~SceneFrameRenderScope() {
        assert(active && "scene frame render scope ended out of balance");
        active = false;
    }
};
#endif

} // namespace

void Application::renderActiveScene() {
#ifndef NDEBUG
    SceneFrameRenderScope renderScope(sceneFrameRenderActive_);
#endif

    applyPendingSceneInvalidations();

    const SceneDef* activeScene = mod_->sceneManager->activeScene();
#ifdef ARTCADE_WASM
    const bool inEditOverlay = EditorAPI::s_mode == 0;
#else
    const bool inEditOverlay = false;
#endif
    // Edit margins stay darker than the scene background so the framed world reads.
    const Vec4 clearColor = inEditOverlay
        ? Vec4{0.028f, 0.032f, 0.042f, 1.f}
        : Vec4{0.015f, 0.018f, 0.025f, 1.f};

    mod_->renderer->setGameCameraModifiers({
        static_cast<double>(mod_->cameraManager->shakeOffset().x),
        static_cast<double>(mod_->cameraManager->shakeOffset().y),
        1.,
        static_cast<double>(mod_->cameraManager->shakeRotationOffset()),
    });

#ifdef ARTCADE_WASM
    const EditorOverlayState overlay{
        EditorAPI::s_mode == 0,
        EditorAPI::s_editorGuidesEnabled,
        EditorAPI::s_editorGridSize,
        EditorAPI::s_selectedEntityId,
    };
    std::vector<EntityId> selectedEntityIds = EditorAPI::s_selectedEntityIds;
    if (selectedEntityIds.empty() && EditorAPI::s_selectedEntityId != 0u)
        selectedEntityIds.push_back(EditorAPI::s_selectedEntityId);
#else
    const EditorOverlayState overlay{false, false, 0.f, 0u};
    std::vector<EntityId> selectedEntityIds;
#endif

    const float sceneFadeAlpha = mod_->entityGateway
        ? mod_->entityGateway->sceneFadeAlpha()
        : 0.f;

    const uint64_t sceneRevision = mod_->gameplaySession
        ? mod_->gameplaySession->sceneRevision()
        : 0u;

    // Camera init is World::resetCameraForActiveScene (ADR-0018); do not
    // override with SceneDef::cameraStart here.

    // RU-02g (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo):
    // frame_coordinator_build_frame() still builds the scene/presentation
    // truth (needs host-owned Renderer/EditorViewportService, T-02 in the
    // debt register); buildFrameSnapshot() then adds the resolved gameplay
    // entity render data (renderables/elapsedTime) using session-owned
    // RuntimeEntityGateway/SpriteAnimator/VariableManager/TimeManager, so the
    // render passes below never query those live during draw.
    const SceneFrameSnapshot frameSnapshot =
        mod_->gameplaySession->buildFrameSnapshot(frame_coordinator_build_frame({
            ++frameNumber_,
            sceneRevision,
            activeScene,
            mod_->renderer.get(),
            mod_->editorViewport.get(),
            overlay,
            sceneFadeAlpha,
        }));

    mod_->renderer->beginFrame(
        frameSnapshot.presentation,
        frameSnapshot.worldSize,
        frameSnapshot.logicalViewport,
        clearColor);
    const RenderPipeline::ViewRenderFeatures features =
        build_view_features(frameSnapshot.overlay);
    const std::vector<RenderPipeline::RenderPassId> passOrder =
        RenderPipeline::RenderPipelineBuilder::buildPipeline(
            frameSnapshot.presentation,
            features,
            activeScene != nullptr).appPassOrder;

    SceneFrameContext frameCtx{};
    frameCtx.frameSnapshot = &frameSnapshot;
    frameCtx.renderer = mod_->renderer.get();
    frameCtx.spriteAnimator = mod_->spriteAnimator;
    frameCtx.entityGateway = mod_->entityGateway;
    frameCtx.sceneManager = mod_->sceneManager;
    frameCtx.selectedEntityIds = &selectedEntityIds;
    frameCtx.tilesets = &tilesets_;
    frameCtx.tileColors = &tileColors_;

    bool worldPassEnded = false;
    bool dialogRendered = false;
    for (const RenderPipeline::RenderPassId passId : passOrder) {
        switch (passId) {
        case RenderPipeline::RenderPassId::GameView:
            mod_->renderer->beginGameViewPass(clearColor);
            break;
        case RenderPipeline::RenderPassId::SceneBackdrop:
            AppRenderPasses::execute_scene_background_pass(frameCtx);
            break;
        case RenderPipeline::RenderPassId::Grid:
            AppRenderPasses::execute_grid_pass(frameCtx);
            break;
        case RenderPipeline::RenderPassId::SceneEntities:
            AppRenderPasses::execute_scene_entities_pass(frameCtx);
            break;
        case RenderPipeline::RenderPassId::Gizmo:
            AppRenderPasses::execute_gizmo_pass(frameCtx);
            break;
        case RenderPipeline::RenderPassId::Debug:
            if (mod_->gameplaySession)
                AppRenderPasses::execute_debug_pass(
                    *mod_->renderer, mod_->gameplaySession->debugWorldView());
            break;
        case RenderPipeline::RenderPassId::Blit:
            if (mod_->dialogManager && mod_->dialogManager->isActive()) {
                mod_->dialogManager->render();
                dialogRendered = true;
            }
            mod_->renderer->endWorldPass();
            mod_->renderer->blitGameViewToBackbuffer();
            worldPassEnded = true;
            break;
        }
    }

    if (!dialogRendered && mod_->dialogManager && mod_->dialogManager->isActive())
        mod_->dialogManager->render();

    if (!worldPassEnded)
        mod_->renderer->endWorldPass();
    mod_->renderer->endScreenPass();
    if (splash_) {
        splash_->render(
            static_cast<int>(mod_->renderer->windowWidth()),
            static_cast<int>(mod_->renderer->windowHeight()));
    }
    mod_->renderer->presentScreen();
    mod_->renderer->setGameCameraModifiers({});
}

} // namespace ArtCade
