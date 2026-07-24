#include "asset-json.h"

#include "json-primitives.h"

namespace ArtCade::ProjectJson {

void read_image_asset(const nlohmann::json& assetJson,
                      const std::string& mapKey,
                      ImageAssetDef& out) {
    if (!assetJson.is_object())
        return;

    out = ImageAssetDef{};
    const std::string libId   = assetJson.value("id", mapKey);
    const std::string libPath = assetJson.value("path", std::string{});
    out.assetId = libPath.empty() ? libId : libPath;
    out.name = assetJson.value("name", libId);
    out.sourcePath = libPath;

    if (assetJson.contains("imagePoints") && assetJson["imagePoints"].is_array()) {
        for (const auto& pt : assetJson["imagePoints"]) {
            if (!pt.is_object())
                continue;
            ImagePointDef ip;
            ip.id = pt.value("id", std::string{});
            ip.x  = pt.value("x", 0.f);
            ip.y  = pt.value("y", 0.f);
            if (!ip.id.empty())
                out.imagePoints.push_back(ip);
        }
    }

    if (assetJson.contains("defaultPivot"))
        out.defaultPivot = read_vec2(assetJson["defaultPivot"], out.defaultPivot);

    if (assetJson.contains("clips") && assetJson["clips"].is_array()) {
        for (const auto& cv : assetJson["clips"]) {
            if (!cv.is_object())
                continue;
            AnimationClipDef clip;
            clip.name = cv.value("name", std::string{});
            clip.fps  = cv.value("fps", 12.f);
            clip.loop = cv.value("loop", true);
            if (cv.contains("frames") && cv["frames"].is_array()) {
                for (const auto& fr : cv["frames"]) {
                    if (!fr.is_object())
                        continue;
                    AnimationFrameRect rect;
                    rect.x = fr.value("x", 0.f);
                    rect.y = fr.value("y", 0.f);
                    rect.w = fr.value("w", 0.f);
                    rect.h = fr.value("h", 0.f);
                    if (rect.w > 0.f && rect.h > 0.f)
                        clip.frames.push_back(rect);
                }
            }
            if (!clip.name.empty() && !clip.frames.empty())
                out.clips.push_back(std::move(clip));
        }
    }
}

void read_image_assets(const nlohmann::json& doc, std::vector<ImageAssetDef>& out) {
    out.clear();
    if (doc.contains("imageAssets") && doc["imageAssets"].is_array()) {
        for (const auto& item : doc["imageAssets"]) {
            if (!item.is_object()) continue;
            ImageAssetDef asset;
            asset.assetId = item.value("assetId", item.value("id", std::string{}));
            asset.name = item.value("name", asset.assetId);
            asset.sourcePath = item.value(
                "sourcePath", item.value("relativePath", item.value("path", std::string{})));
            if (!asset.assetId.empty()) out.push_back(std::move(asset));
        }
        return;
    }
    if (!doc.contains("assets") || !doc["assets"].is_object())
        return;

    for (auto& [key, av] : doc["assets"].items()) {
        if (!av.is_object())
            continue;
        ImageAssetDef asset;
        read_image_asset(av, key, asset);
        out.push_back(std::move(asset));
    }
}

void read_sprite_animation_assets(
    const nlohmann::json& doc,
    std::vector<SpriteAnimationAssetDef>& out) {
    out.clear();
    if (!doc.contains("spriteAnimationAssets")
        || !doc["spriteAnimationAssets"].is_array()) return;
    for (const auto& item : doc["spriteAnimationAssets"]) {
        if (!item.is_object()) continue;
        SpriteAnimationAssetDef asset;
        asset.id = item.value("id", std::string{});
        asset.name = item.value("name", asset.id);
        asset.sourceImageAssetId = item.value(
            "sourceImageAssetId", item.value("source_image_asset_id", std::string{}));
        if (item.contains("frames") && item["frames"].is_array()) {
            for (const auto& frameJson : item["frames"]) {
                if (!frameJson.is_object()) continue;
                SpriteFrameDef frame;
                frame.id = frameJson.value("id", std::string{});
                frame.x = frameJson.value("x", 0);
                frame.y = frameJson.value("y", 0);
                frame.width = frameJson.value("width", frameJson.value("w", 0));
                frame.height = frameJson.value("height", frameJson.value("h", 0));
                if (!frame.id.empty() && frame.width > 0 && frame.height > 0) {
                    asset.frames.push_back(std::move(frame));
                }
            }
        }
        if (item.contains("clips") && item["clips"].is_array()) {
            for (const auto& clipJson : item["clips"]) {
                if (!clipJson.is_object()) continue;
                SpriteAnimationClipDef clip;
                clip.id = clipJson.value("id", std::string{});
                clip.name = clipJson.value("name", clip.id);
                clip.framesPerSecond = clipJson.value("framesPerSecond", 8.f);
                const std::string mode = clipJson.value("playbackMode", std::string("loop"));
                if (mode != "loop" && mode != "once") continue;
                clip.playbackMode = mode == "once"
                    ? AnimationPlaybackMode::Once : AnimationPlaybackMode::Loop;
                if (clipJson.contains("frameIds") && clipJson["frameIds"].is_array()) {
                    for (const auto& frameIdJson : clipJson["frameIds"]) {
                        if (!frameIdJson.is_string()) continue;
                        const std::string frameId = frameIdJson.get<std::string>();
                        if (!frameId.empty()) clip.frameIds.push_back(frameId);
                    }
                }
                if (!clip.id.empty()) asset.clips.push_back(std::move(clip));
            }
        }
        if (!asset.id.empty()) out.push_back(std::move(asset));
    }
}

void read_audio_assets(const nlohmann::json& doc, std::vector<AudioAssetDef>& out) {
    out.clear();
    if (!doc.contains("audioAssets")) return;
    const auto parseOne = [](const nlohmann::json& item,
                             const std::string& fallbackId,
                             AudioAssetDef& asset) {
        if (!item.is_object()) return false;
        asset.assetId = item.value("assetId", item.value("id", fallbackId));
        asset.name = item.value("name", asset.assetId);
        asset.sourcePath = item.value(
            "sourcePath", item.value("relativePath", item.value("path", std::string{})));
        const std::string mode = item.value("loadMode", std::string("static"));
        if (mode != "static" && mode != "static_sound" && mode != "stream")
            return false;
        asset.loadMode = mode == "stream" ? AudioLoadMode::Stream
                                           : AudioLoadMode::StaticSound;
        if (item.contains("generatedFromSfxId") && item["generatedFromSfxId"].is_string()) {
            const std::string from = item["generatedFromSfxId"].get<std::string>();
            if (!from.empty()) asset.generatedFromSfxId = from;
        }
        return !asset.assetId.empty();
    };
    const auto& assets = doc["audioAssets"];
    if (assets.is_array()) {
        for (const auto& item : assets) {
            AudioAssetDef asset;
            if (parseOne(item, {}, asset)) out.push_back(std::move(asset));
        }
    } else if (assets.is_object()) {
        for (auto& [key, item] : assets.items()) {
            AudioAssetDef asset;
            if (parseOne(item, key, asset)) out.push_back(std::move(asset));
        }
    }
}

void read_font_assets(const nlohmann::json& doc, std::vector<FontAssetDef>& out) {
    out.clear();
    if (!doc.contains("fontAssets") || !doc["fontAssets"].is_array()) return;
    for (const auto& item : doc["fontAssets"]) {
        if (!item.is_object()) continue;
        FontAssetDef asset;
        asset.assetId = item.value("assetId", std::string{});
        if (asset.assetId.empty()) continue;
        asset.name = item.value("name", asset.assetId);
        asset.sourcePath = item.value("sourcePath", std::string{});
        asset.defaultPixelSize = item.value("defaultPixelSize", asset.defaultPixelSize);
        const std::string preset = item.value("glyphPreset", std::string("european"));
        if (preset == "basicLatin") asset.glyphPreset = FontGlyphPreset::BasicLatin;
        else if (preset == "customText") asset.glyphPreset = FontGlyphPreset::CustomText;
        else asset.glyphPreset = FontGlyphPreset::European;
        out.push_back(std::move(asset));
    }
}

} // namespace ArtCade::ProjectJson
