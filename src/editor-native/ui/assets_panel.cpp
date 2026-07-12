#include "editor-native/ui/assets_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstddef>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

namespace {

// While Play runs, every asset operation mutates the authoring document, so the
// Import trigger renders disabled (the coordinator/import pipeline reject them
// anyway). Row menus need no equivalent: showPendingAssetMenu suppresses the
// menu during Play, hierarchy-menu parity.
const char* btnClass(bool disabled) { return disabled ? "panel-btn disabled" : "panel-btn"; }

// One import entry point for every asset kind (audit 4.6): a hover dropdown in
// the same local-visual-state pattern as the hierarchy "+ Create" menu.
std::string importMenu(bool disabled) {
    const auto entry = [&](const char* action, const char* label) {
        return std::string("<div class=\"menu-entry") + (disabled ? " disabled" : "")
             + "\" data-action=\"" + action + "\">" + label + "</div>";
    };
    return std::string("<div class=\"create-menu asset-import\">")
         + "<button class=\"" + btnClass(disabled)
         + "\"><span class=\"icon\">&#xeb0b;</span>Import <span class=\"icon-caret\">&#xeb5d;</span></button>"
           "<div class=\"create-dropdown\">"
         + entry("import-image", "Image")
         + entry("import-audio", "Audio")
         + entry("import-font",  "Font")
         + "</div></div>";
}

std::string groupTitle(const char* label, std::size_t count) {
    return std::string("<div class=\"asset-group-title\">") + label
         + "<span class=\"asset-count\">" + std::to_string(count) + "</span></div>";
}

// An image consumed by a derived asset (animation clip or tileset) has no row
// of its own: the derived asset is the usable thing, the raw sheet becomes its
// "from ..." subtitle. Same mental model the Inspector's Source dropdown
// already applies via its imageHasDerivedAnimation rule. Re-deriving from a
// consumed image stays possible through the derived row's menu ("New ... from
// Source Image").
bool imageIsConsumed(const ProjectDoc& doc, const AssetId& imageId) {
    for (const SpriteAnimationAssetDef& asset : doc.spriteAnimationAssets) {
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.imageId == imageId) return true;
        }
    }
    for (const TilesetAsset& tileset : doc.tilesets) {
        if (tileset.imageAssetId == imageId) return true;
    }
    return false;
}

// The row's single action affordance: "⌄" opening the Assets context menu
// (same .row-menu pattern as hierarchy rows and scene tabs). `kindTag` must
// match parseAssetMenuKind.
std::string menuAffordance(const char* kindTag, const std::string& id) {
    return "<span class=\"row-menu\" data-action=\"open-asset-menu\" data-arg=\""
         + escapeRml(std::string(kindTag) + "|" + id) + "\">&#xeb5d;</span>";
}

// One list row: kind icon + display name + optional trailing meta + "⌄" menu.
// title: a narrow sidebar can clip a long name (RmlUi has no text-overflow
// support to ellipsize it), so the full id is still readable on hover.
std::string assetRow(const char* iconCp, const std::string& display,
                     const std::string& tooltip, const char* dblAction,
                     const std::string& id, const std::string& meta,
                     const std::string& menu) {
    std::string row = "<div class=\"asset-row\"";
    if (dblAction && *dblAction) {
        row += " data-dbl-action=\"" + std::string(dblAction)
             + "\" data-arg=\"" + escapeRml(id) + "\"";
    }
    row += "><span class=\"icon asset-ico\">";
    if (iconCp) row += iconCp;
    row += "</span><span class=\"asset-name\" title=\"" + escapeRml(tooltip) + "\">"
         + escapeRml(display) + "</span>";
    row += meta;
    row += menu;
    row += "</div>";
    return row;
}

// Provenance subtitle under a derived asset's row ("from <source image>").
std::string sourceSubtitle(const AssetId& imageId) {
    if (imageId.empty()) return {};
    return "<div class=\"asset-sub\">from " + escapeRml(imageId) + "</div>";
}

} // namespace

void AssetsPanel::refresh(Rml::ElementDocument* document,
                          const EditorCoordinator& coordinator) const {
    if (!document) return;
    Rml::Element* list = document->GetElementById("assets-list");
    if (!list) return;

    const ProjectDoc& doc = coordinator.document().data();
    const bool playing = coordinator.isPlaying();
    std::string html;

    html += importMenu(playing);

    // Empty catalog: one guidance block instead of empty group rows (audit 4.6).
    const bool anyAsset = !doc.imageAssets.empty() || !doc.spriteAnimationAssets.empty()
                       || !doc.audioAssets.empty() || !doc.fontAssets.empty()
                       || !doc.tilesets.empty();
    if (!anyAsset) {
        html += "<div class=\"assets-empty\">No assets yet.<br/>"
                "Import images, audio and fonts to use them in your game.</div>";
        list->SetInnerRML(html);
        return;
    }

    // Empty groups are omitted entirely: the Import dropdown above is the entry
    // point for every kind, so a permanent "Audio 0" row is pure noise.

    // -- Images: only sheets not yet consumed by a derived asset --------------
    std::vector<const ImageAssetDef*> freeImages;
    for (const ImageAssetDef& asset : doc.imageAssets) {
        if (!imageIsConsumed(doc, asset.assetId)) freeImages.push_back(&asset);
    }
    if (!freeImages.empty()) {
        html += groupTitle("Images", freeImages.size());
        for (const ImageAssetDef* asset : freeImages) {
            html += assetRow("&#xeb0a;", assetDisplayName(asset->name, asset->assetId),
                             asset->assetId, nullptr, asset->assetId, "",
                             menuAffordance("image", asset->assetId));
        }
    }

    // -- Animations: clip containers created from an image ---------------------
    if (!doc.spriteAnimationAssets.empty()) {
        html += groupTitle("Animations", doc.spriteAnimationAssets.size());
        for (const SpriteAnimationAssetDef& asset : doc.spriteAnimationAssets) {
            html += assetRow("&#xed46;", assetDisplayName(asset.name, asset.id),
                             asset.id, "open-sprite-animation", asset.id, "",
                             menuAffordance("anim", asset.id));
            if (!asset.clips.empty()) html += sourceSubtitle(asset.clips.front().imageId);
        }
    }

    // -- Tilesets: sliced spritesheets created from an image --------------------
    if (!doc.tilesets.empty()) {
        html += groupTitle("Tilesets", doc.tilesets.size());
        for (const TilesetAsset& asset : doc.tilesets) {
            html += assetRow("&#xea3b;", assetDisplayName(asset.name, asset.assetId),
                             asset.assetId, "open-tileset-editor", asset.assetId, "",
                             menuAffordance("tileset", asset.assetId));
            html += sourceSubtitle(asset.imageAssetId);
        }
    }

    // -- Audio: name + load mode ------------------------------------------------
    if (!doc.audioAssets.empty()) {
        html += groupTitle("Audio", doc.audioAssets.size());
        for (const AudioAssetDef& asset : doc.audioAssets) {
            const char* mode = asset.loadMode == AudioLoadMode::Stream ? "Stream" : "Sound";
            html += assetRow(nullptr, assetDisplayName(asset.name, asset.assetId),
                             asset.assetId, nullptr, asset.assetId,
                             "<span class=\"asset-meta\">" + std::string(mode) + "</span>",
                             menuAffordance("audio", asset.assetId));
        }
    }

    // -- Fonts: name + size ------------------------------------------------------
    if (!doc.fontAssets.empty()) {
        html += groupTitle("Fonts", doc.fontAssets.size());
        for (const FontAssetDef& asset : doc.fontAssets) {
            html += assetRow(nullptr, assetDisplayName(asset.name, asset.assetId),
                             asset.assetId, nullptr, asset.assetId,
                             "<span class=\"asset-meta\">"
                                 + std::to_string(asset.defaultPixelSize) + "px</span>",
                             menuAffordance("font", asset.assetId));
        }
    }

    list->SetInnerRML(html);
}

} // namespace ArtCade::EditorNative
