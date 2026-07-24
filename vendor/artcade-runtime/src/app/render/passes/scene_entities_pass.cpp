#include "scene_entities_pass.h"

#include "../scene_frame_snapshot.h"

#include "../../../modules/renderer/include/renderer.h"
#include "../../../modules/scene-system/include/scene-manager.h"

#include <string>
#include <unordered_map>

namespace ArtCade::AppRenderPasses {

namespace {

void textAnchorAlign(const std::string& a, int& hOut, int& vOut) {
    if (a.find("left") != std::string::npos)        hOut = 2;
    else if (a.find("right") != std::string::npos)  hOut = 0;
    else                                            hOut = 1;

    const bool isNewAnchor = a.find('-') != std::string::npos || a == "center";
    if (a.find("top") != std::string::npos)         vOut = 2;
    else if (a.find("bottom") != std::string::npos) vOut = 0;
    else if (isNewAnchor)                           vOut = 1;
    else                                            vOut = 0;
}

} // namespace

// RU-02g (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md, editor repo): entity
// discovery, layer-based sort, visibility, animator frame resolution and
// text/gauge variable-binding resolution all moved into
// GameplaySession::buildFrameSnapshot() (gameplay_session.cpp) - this pass
// only reads `frame.renderables`, already sorted in final draw order, and
// draws. No RuntimeEntityGateway/SpriteAnimator/VariableManager query is left
// here. settingsById/parallaxById still come from ctx.sceneManager, the same
// authoring-adjacent aliasing precedent as frame.tilemap/tilemapLayers below.
void execute_scene_entities_pass(SceneFrameContext& ctx) {
    if (!ctx.frameSnapshot || !ctx.sceneManager || !ctx.renderer)
        return;

    const SceneFrameSnapshot& frame = *ctx.frameSnapshot;
    if (frame.sceneId.empty()) return;

    const bool inEditMode = frame.overlay.inEditMode;
    std::unordered_map<std::string, SceneLayerSettings> settingsById;
    std::unordered_map<std::string, Vec2> parallaxById;

    const auto& layers = ctx.sceneManager->sceneLayers();
    for (const auto& layer : layers) {
        SceneLayerSettings settings;
        const auto sit = frame.layerSettings.find(layer.id);
        if (sit != frame.layerSettings.end())
            settings = sit->second;
        settingsById.emplace(layer.id, settings);
        if (settings.parallax.x != 1.f || settings.parallax.y != 1.f)
            parallaxById.emplace(
                layer.id, Vec2{ settings.parallax.x, settings.parallax.y });
    }

    const Vec2 cameraTopLeft = ctx.renderer->getCameraPosition();
    const auto layerDrawPos =
        [&parallaxById, cameraTopLeft, inEditMode]
        (const std::string& layerId, const Vec2& worldPos) -> Vec2 {
            if (inEditMode || layerId.empty()) return worldPos;
            const auto it = parallaxById.find(layerId);
            if (it == parallaxById.end()) return worldPos;
            return {
                worldPos.x + cameraTopLeft.x * (1.f - it->second.x),
                worldPos.y + cameraTopLeft.y * (1.f - it->second.y),
            };
        };

    for (const RenderableEntitySnapshot& item : frame.renderables) {
        const Transform& transform = item.transform;
        const SpriteComponent& sprite = item.sprite;
        [renderer = ctx.renderer, inEditMode, &settingsById, &layerDrawPos]
        (const RenderableEntitySnapshot& item, const Transform& transform,
         const SpriteComponent& sprite) {
            if (!inEditMode && sprite.alpha <= 0.001f) return;
            const auto layerIt = settingsById.find(sprite.layerId);
            const SceneLayerSettings* layer =
                layerIt != settingsById.end() ? &layerIt->second : nullptr;
            if (layer && (!layer->visible || layer->opacity <= 0.f)) return;

            const Vec2 pos = layerDrawPos(sprite.layerId, transform.position);

            float alpha = sprite.alpha;
            if (layer) alpha *= layer->opacity;
            const bool hasSpriteSheet = !sprite.spriteAssetId.empty();
            if (inEditMode && hasSpriteSheet && !item.visibleInGame) {
                alpha *= 0.45f;
            }
            if (inEditMode && !hasSpriteSheet) alpha = layer ? layer->opacity : 1.f;

            const bool hasText = item.text.has_value();
            const bool hasGauge = item.gauge.has_value();
            const bool visualOnly = !hasSpriteSheet && (hasText || hasGauge);

            const auto& draw = item.spriteFrame;
            if (AppRender::sprite_frame_has_pixels(draw.frame)) {
                const std::string& sheet =
                    draw.assetId.empty() ? sprite.spriteAssetId : draw.assetId;
                renderer->drawSpriteFrame(
                    sheet,
                    static_cast<float>(draw.frame.x),
                    static_cast<float>(draw.frame.y),
                    static_cast<float>(draw.frame.w),
                    static_cast<float>(draw.frame.h),
                    pos, transform.rotation, transform.scale,
                    sprite.tint, alpha, sprite.pivot, sprite.flipX, sprite.flipY);
            } else if (hasSpriteSheet && !visualOnly) {
                renderer->drawSprite(
                    sprite.spriteAssetId,
                    pos, transform.rotation, transform.scale,
                    sprite.tint, sprite.fillColor, alpha,
                    sprite.shaderEffect, sprite.pivot, sprite.flipX, sprite.flipY);
            }

            if (!hasText) return;
            const TextComponent& text = *item.text;
            // text.text already carries the fully resolved display string
            // (prefix + formatted bound value + suffix, or the static
            // authored text) - resolved once in buildFrameSnapshot().
            Vec4 color = text.color;
            if (layer) color.a *= layer->opacity;
            if (inEditMode && !item.visibleInGame) color.a *= 0.45f;
            int hAlign = 0, vAlign = 0;
            textAnchorAlign(text.align, hAlign, vAlign);
            renderer->drawText(
                text.text,
                pos.x + text.offsetX,
                pos.y + text.offsetY,
                text.size, color, text.fontPath, hAlign, text.screenSpace,
                vAlign);
        }(item, transform, sprite);
    }

    for (const RenderableEntitySnapshot& item : frame.renderables) {
        const Transform& transform = item.transform;
        const SpriteComponent& sprite = item.sprite;
        [renderer = ctx.renderer, inEditMode, &settingsById, &layerDrawPos]
        (const RenderableEntitySnapshot& item, const Transform& transform,
         const SpriteComponent& sprite) {
            if (!item.gauge.has_value()) return;
            const auto layerIt = settingsById.find(sprite.layerId);
            const SceneLayerSettings* layer =
                layerIt != settingsById.end() ? &layerIt->second : nullptr;
            if (layer && (!layer->visible || layer->opacity <= 0.f)) return;

            const GaugeComponent& gauge = *item.gauge;
            const float ratio = item.gaugeRatio;

            Vec4 bg = gauge.bgColor;
            Vec4 fill = gauge.fillColor;
            if (layer) {
                bg.a *= layer->opacity;
                fill.a *= layer->opacity;
            }
            if (inEditMode && !item.visibleInGame) {
                bg.a *= 0.45f;
                fill.a *= 0.45f;
            }
            const Vec2 gpos = layerDrawPos(sprite.layerId, transform.position);
            const float gx = gpos.x + gauge.offsetX;
            const float gy = gpos.y + gauge.offsetY;
            renderer->drawRect(gx, gy, gauge.width, gauge.height, bg, gauge.screenSpace);
            if (gauge.direction == "vertical") {
                const float fh = gauge.height * ratio;
                renderer->drawRect(gx, gy + (gauge.height - fh), gauge.width, fh,
                                   fill, gauge.screenSpace);
            } else {
                renderer->drawRect(gx, gy, gauge.width * ratio, gauge.height,
                                   fill, gauge.screenSpace);
            }
        }(item, transform, sprite);
    }

    if (frame.sceneFadeAlpha > 0.f)
        ctx.renderer->drawFadeOverlay(frame.sceneFadeAlpha);
}

} // namespace ArtCade::AppRenderPasses
