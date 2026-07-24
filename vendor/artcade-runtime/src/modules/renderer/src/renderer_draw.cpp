#include "../include/renderer.h"
#include "renderer_impl.h"

#include "../../../core/sprite-draw-math.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::Modules {

namespace {

constexpr float kOutlineTexelRadius = 2.f;
constexpr float kPlaceholderSpriteSize = 32.f;

} // namespace

void draw_text_command(const DrawCmd& cmd, const Font* font) {
    Color c{ cmd.cr, cmd.cg, cmd.cb, cmd.ca };
    float drawX = cmd.x;
    float drawY = cmd.y;
    if (cmd.align != 0) {
        const float w = font
            ? MeasureTextEx(*font, cmd.text.c_str(),
                            static_cast<float>(cmd.fontSize), 1.f).x
            : static_cast<float>(MeasureText(cmd.text.c_str(), cmd.fontSize));
        drawX -= (cmd.align == 1) ? w * 0.5f : w;
    }
    if (cmd.valign != 0) {
        const float h = font
            ? MeasureTextEx(*font, cmd.text.c_str(),
                            static_cast<float>(cmd.fontSize), 1.f).y
            : static_cast<float>(cmd.fontSize);
        drawY -= (cmd.valign == 1) ? h * 0.5f : h;
    }
    if (font) {
        DrawTextEx(*font, cmd.text.c_str(), Vector2{ drawX, drawY },
                   static_cast<float>(cmd.fontSize), 1.f, c);
    } else {
        DrawText(cmd.text.c_str(),
                 static_cast<int>(drawX), static_cast<int>(drawY),
                 cmd.fontSize, c);
    }
}

bool draw_image_command(TextureCache& texCache,
                        const std::string& textureKey,
                        const DrawCmd& cmd) {
    const Texture2D* tex = texCache.getByPath(textureKey);
    if (!tex || tex->id == 0) {
        texCache.load(textureKey);
        tex = texCache.getByPath(textureKey);
    }
    if (!tex || tex->id == 0) return false;

    DrawTexturePro(*tex,
                   Rectangle{ 0.f, 0.f,
                              static_cast<float>(tex->width),
                              static_cast<float>(tex->height) },
                   Rectangle{ cmd.x, cmd.y, cmd.x2, cmd.y2 },
                   Vector2{ 0.f, 0.f },
                   0.f,
                   Color{ 255, 255, 255, cmd.ca });
    return true;
}

std::string Renderer::resolvedTextureKey(const std::string& ref) const {
    return impl_->resources.resolve_texture_key(ref);
}

void Renderer::setTextureKeyResolver(
    std::function<std::string(const std::string&)> resolver) {
    impl_->resources.set_texture_key_resolver(std::move(resolver));
}

std::string Renderer::resolvedFontKey(const std::string& ref) const {
    return impl_->resources.resolve_font_key(ref);
}

void Renderer::setFontKeyResolver(
    std::function<std::string(const std::string&)> resolver) {
    impl_->resources.set_font_key_resolver(std::move(resolver));
}

void Renderer::drawSprite(const AssetId& assetId,
                          const Vec2& pos,
                          float rotation,
                          const Vec2& scale,
                          const Vec4& tint,
                          const Vec3& fillColor,
                          float alpha,
                          const std::string& shaderEffect,
                          const Vec2& pivot,
                          bool flipX,
                          bool flipY) {
    const bool outline = (shaderEffect == "outline");

    Vec4 drawTint = tint;
    if (shaderEffect == "hit_flash")
        drawTint = { 1.f, 1.f, 1.f, tint.a };

    const std::string texKey = resolvedTextureKey(assetId);
    const Texture2D* tex = impl_->resources.texture_cache().getByPath(texKey);
    if (!tex || tex->id == 0) {
        impl_->resources.texture_cache().load(texKey);
        tex = impl_->resources.texture_cache().getByPath(texKey);
    }
    if (!tex || tex->id == 0) return;

    const float texW = static_cast<float>(tex->width);
    const float texH = static_cast<float>(tex->height);
    Rectangle src = { 0.f, 0.f,
                      flipX ? -texW : texW,
                      flipY ? -texH : texH };
    Rectangle dst = { pos.x, pos.y,
                      texW * (scale.x < 0.f ? -scale.x : scale.x),
                      texH * (scale.y < 0.f ? -scale.y : scale.y) };
    const Vec2 originVec = SpriteDrawMath::drawOrigin(pivot, dst.width, dst.height);
    Vector2 origin = { originVec.x, originVec.y };
    const Color tintColor = renderer_to_color(drawTint, alpha);

    if (outline && impl_->spriteOutline.ready) {
        const float texelSize[2] = {
            1.f / static_cast<float>(tex->width),
            1.f / static_cast<float>(tex->height),
        };
        const float outlineSize = kOutlineTexelRadius;
        const float outlineColor[4] = { 0.f, 0.f, 0.f, tint.a * alpha };
        SetShaderValue(impl_->spriteOutline.shader,
                       impl_->spriteOutline.locTexelSize,
                       texelSize,
                       SHADER_UNIFORM_VEC2);
        SetShaderValue(impl_->spriteOutline.shader,
                       impl_->spriteOutline.locOutlineSize,
                       &outlineSize,
                       SHADER_UNIFORM_FLOAT);
        SetShaderValue(impl_->spriteOutline.shader,
                       impl_->spriteOutline.locOutlineColor,
                       outlineColor,
                       SHADER_UNIFORM_VEC4);
        BeginShaderMode(impl_->spriteOutline.shader);
        DrawTexturePro(*tex, src, dst, origin, rotation, tintColor);
        EndShaderMode();
        return;
    }

    DrawTexturePro(*tex, src, dst, origin, rotation, tintColor);
}

void Renderer::drawSpriteFrame(const AssetId& assetId,
                               float srcX, float srcY, float srcW, float srcH,
                               const Vec2& pos,
                               float rotation,
                               const Vec2& scale,
                               const Vec4& tint,
                               float alpha,
                               const Vec2& pivot,
                               bool flipX,
                               bool flipY) {
    const std::string texKey = resolvedTextureKey(assetId);
    const Texture2D* tex = impl_->resources.texture_cache().getByPath(texKey);
    if (!tex || tex->id == 0 || srcW <= 0.f || srcH <= 0.f) {
        drawSprite(assetId, pos, rotation, scale, tint, { tint.r, tint.g, tint.b },
                   alpha, "", pivot, flipX, flipY);
        return;
    }

    Rectangle src = { srcX, srcY,
                      flipX ? -srcW : srcW,
                      flipY ? -srcH : srcH };
    Rectangle dst = { pos.x, pos.y,
                      srcW * (scale.x < 0.f ? -scale.x : scale.x),
                      srcH * (scale.y < 0.f ? -scale.y : scale.y) };
    const Vec2 originVec = SpriteDrawMath::drawOrigin(pivot, dst.width, dst.height);
    Vector2 origin = { originVec.x, originVec.y };
    DrawTexturePro(*tex, src, dst, origin, rotation, renderer_to_color(tint, alpha));
}

Vec2 Renderer::spriteDestinationSize(const AssetId& assetId, const Vec2& scale) const {
    const float sx = std::abs(scale.x);
    const float sy = std::abs(scale.y);
    const std::string texKey = resolvedTextureKey(assetId);
    const Texture2D* tex = impl_->resources.texture_cache().getByPath(texKey);
    if (!tex || tex->id == 0) {
        return { kPlaceholderSpriteSize * sx, kPlaceholderSpriteSize * sy };
    }
    return {
        static_cast<float>(tex->width) * sx,
        static_cast<float>(tex->height) * sy,
    };
}

int Renderer::captureSpriteRegionFrame(const AssetId& assetId,
                                       float srcX, float srcY, float srcW, float srcH,
                                       int canvasW, int canvasH,
                                       unsigned char* rgbaOut,
                                       int rgbaOutLen) {
    return impl_->capture.capture_sprite_region(
        [this, &assetId, srcX, srcY, srcW, srcH](
            float dstX, float dstY, float dstW, float dstH) {
            return drawSpriteRegion(assetId, srcX, srcY, srcW, srcH,
                                    dstX, dstY, dstW, dstH);
        },
        srcW,
        srcH,
        canvasW,
        canvasH,
        rgbaOut,
        rgbaOutLen);
}

bool Renderer::drawSpriteRegion(const AssetId& assetId,
                                float srcX, float srcY, float srcW, float srcH,
                                float dstX, float dstY, float dstW, float dstH,
                                float alpha) {
    const std::string texKey = resolvedTextureKey(assetId);
    const Texture2D* tex = impl_->resources.texture_cache().getByPath(texKey);
    if (!tex || tex->id == 0) {
        impl_->resources.texture_cache().load(texKey);
        tex = impl_->resources.texture_cache().getByPath(texKey);
    }
    if (!tex || tex->id == 0) return false;

    Rectangle src = { srcX, srcY, srcW, srcH };
    Rectangle dst = { dstX, dstY, dstW, dstH };
    const unsigned char ca =
        static_cast<unsigned char>(std::clamp(alpha, 0.f, 1.f) * 255.f);
    DrawTexturePro(*tex, src, dst, { 0.f, 0.f }, 0.f, Color{ 255, 255, 255, ca });
    return true;
}

void Renderer::drawRect(float x, float y, float w, float h, const Vec4& color,
                        bool screenSpace) {
    Color c = renderer_to_color(color);
    DrawCmd cmd;
    cmd.type = DrawCmd::Type::Rect;
    cmd.x = x; cmd.y = y; cmd.x2 = w; cmd.y2 = h;
    cmd.cr = c.r; cmd.cg = c.g; cmd.cb = c.b; cmd.ca = c.a;
    (screenSpace ? impl_->screenQueue : impl_->drawQueue)
        .push_back(std::move(cmd));
}

void Renderer::drawRectImmediate(float x, float y, float w, float h, const Vec4& color) {
    DrawRectangleV({ x, y }, { w, h }, renderer_to_color(color));
}

void Renderer::drawImage(const AssetId& assetId, float x, float y, float w, float h,
                         float alpha, bool screenSpace) {
    if (assetId.empty() || w <= 0.f || h <= 0.f || alpha <= 0.f) return;
    DrawCmd cmd;
    cmd.type = DrawCmd::Type::Image;
    cmd.x = x; cmd.y = y; cmd.x2 = w; cmd.y2 = h;
    cmd.assetId = assetId;
    cmd.ca = static_cast<unsigned char>(std::clamp(alpha, 0.f, 1.f) * 255.f);
    (screenSpace ? impl_->screenQueue : impl_->drawQueue)
        .push_back(std::move(cmd));
}

void Renderer::drawLine(float x1, float y1, float x2, float y2, const Vec4& color) {
    Color c = renderer_to_color(color);
    DrawCmd cmd;
    cmd.type = DrawCmd::Type::Line;
    cmd.x = x1; cmd.y = y1; cmd.x2 = x2; cmd.y2 = y2;
    cmd.cr = c.r; cmd.cg = c.g; cmd.cb = c.b; cmd.ca = c.a;
    impl_->drawQueue.push_back(std::move(cmd));
}

void Renderer::drawCircle(float x, float y, float radius, const Vec4& color) {
    Color c = renderer_to_color(color);
    DrawCmd cmd;
    cmd.type = DrawCmd::Type::Circle;
    cmd.x = x; cmd.y = y; cmd.r = radius;
    cmd.cr = c.r; cmd.cg = c.g; cmd.cb = c.b; cmd.ca = c.a;
    impl_->drawQueue.push_back(std::move(cmd));
}

void Renderer::drawText(const std::string& text, float x, float y,
                        int fontSize, const Vec4& color,
                        const std::string& fontPath, int align,
                        bool screenSpace, int valign) {
    Color c = renderer_to_color(color);
    DrawCmd cmd;
    cmd.type = DrawCmd::Type::Text;
    cmd.x = x;
    cmd.y = y;
    cmd.fontSize = fontSize;
    cmd.align = align;
    cmd.valign = valign;
    cmd.text = text;
    cmd.fontPath = fontPath;
    cmd.cr = c.r; cmd.cg = c.g; cmd.cb = c.b; cmd.ca = c.a;
    (screenSpace ? impl_->screenQueue : impl_->drawQueue)
        .push_back(std::move(cmd));
}

bool Renderer::registerFontFromMemory(const std::string& path,
                                      const unsigned char* data, int len,
                                      const std::string& ext,
                                      int baseSize) {
    return impl_->resources.register_font_from_memory(path, data, len, ext, baseSize);
}

void Renderer::invalidateFontAsset(const std::string& path) {
    impl_->resources.invalidate_font_asset(path);
}

void Renderer::evictCachedAssets() {
    impl_->resources.evict_cached_assets();
}

uint32_t Renderer::loadTexture(const std::string& path) {
    return impl_->resources.load_texture(path);
}

bool Renderer::registerImageFromMemory(const std::string& assetId,
                                       const unsigned char* data, int len,
                                       const std::string& ext) {
    return impl_->resources.register_image_from_memory(assetId, data, len, ext);
}

void Renderer::invalidateImageAsset(const std::string& assetPath) {
    impl_->resources.invalidate_image_asset(assetPath);
}

void Renderer::unloadTexture(uint32_t handle) {
    impl_->resources.unload_texture(handle);
}

bool Renderer::isTextureLoaded(const AssetId& assetId) const {
    return impl_->resources.is_texture_loaded(resolvedTextureKey(assetId));
}

} // namespace ArtCade::Modules
