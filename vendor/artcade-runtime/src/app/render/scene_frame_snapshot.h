#pragma once

#include "../../core/types.h"
#include "../../modules/presentation/include/presentation_snapshot.h"
#include "editor-overlay-renderer.h"
#include "sprite_frame_resolve.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace ArtCade {

// RU-02g (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo): one
// gameplay-visible entity's renderable data, fully resolved at snapshot-build
// time (GameplaySession::buildFrameSnapshot(), which has session-owned
// RuntimeEntityGateway/SpriteAnimator/VariableManager available) so the
// render pass never queries those live during draw. `text`/`gauge` mirror
// the authored components verbatim except for the dynamic parts, which are
// pre-resolved exactly like the render pass used to compute them inline:
// `text->text` already carries the fully formatted prefix+value+suffix
// string (or the static authored text, if unbound); `gaugeRatio` already
// carries the bound value / maxValue, clamped to [0,1].
struct RenderableEntitySnapshot {
    EntityId id = 0;
    Transform transform{};
    SpriteComponent sprite{};
    bool visibleInGame = true;
    AppRender::ResolvedSpriteDraw spriteFrame{};
    std::optional<TextComponent> text;
    std::optional<GaugeComponent> gauge;
    float gaugeRatio = 0.f;
};

/**
 * Immutable per-frame scene + presentation truth for render passes and overlays.
 *
 * Contract (single-threaded, synchronous frame loop):
 *   mutation + entity flush → scene_frame_build() → beginFrame()
 *   → render passes → presentScreen()
 *
 * Tilemap pointers alias the active SceneDef. The SceneDef must not be
 * mutated from frame build until render passes finish.
 */
struct SceneFrameSnapshot {
    uint64_t frameNumber = 0;
    uint64_t sceneRevision = 0;
    uint64_t presentationRevision = 0;

    SceneId sceneId;

    Vec2 worldSize{};
    Vec2 logicalViewport{};
    Vec4 backgroundColor{};
    std::unordered_map<std::string, SceneLayerSettings> layerSettings;

    ArtCade::Presentation::PresentationSnapshot presentation;
    EditorOverlayState overlay;

    float sceneFadeAlpha = 0.f;

    /** Merged grid for physics / legacy single-layer projects (aliases SceneDef). */
    const TilemapData* tilemap = nullptr;
    /** Per-layer paint grids keyed by layer id (aliases SceneDef). */
    const std::unordered_map<std::string, TilemapData>* tilemapLayers = nullptr;

    // RU-02g: gameplay-visible entities, already resolved and sorted in final
    // draw order (layer priority, then sprite.renderOrder, then original
    // discovery order) by GameplaySession::buildFrameSnapshot() - the render
    // pass only iterates this, no RuntimeEntityGateway/SpriteAnimator/
    // VariableManager queries left in the draw loop.
    std::vector<RenderableEntitySnapshot> renderables;
    // RU-02g: TimeManager::now(), resolved once per frame for parallax scroll
    // (previously queried live by scene_background_pass.cpp).
    float elapsedTime = 0.f;
};

// RU-02g (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo): the plan's
// named API (`GameplayFrameSnapshot GameplaySession::buildFrameSnapshot()
// const`) - an alias rather than a parallel type, per "riutilizzare il
// sistema esistente ... senza introdurre un secondo modello render": the
// existing SceneFrameSnapshot already is the gameplay frame snapshot once it
// carries `renderables`/`elapsedTime`.
using GameplayFrameSnapshot = SceneFrameSnapshot;

struct SceneFrameBuildInput {
    uint64_t frameNumber = 0;
    uint64_t sceneRevision = 0;
    const SceneDef* activeScene = nullptr;
    ArtCade::Presentation::PresentationSnapshot presentation;
    EditorOverlayState overlay{};
    float sceneFadeAlpha = 0.f;
};

/**
 * Captures authoritative frame geometry and presentation at the frame boundary.
 * @param input active scene and committed presentation (must not mutate after build)
 */
SceneFrameSnapshot scene_frame_build(const SceneFrameBuildInput& input);

} // namespace ArtCade
