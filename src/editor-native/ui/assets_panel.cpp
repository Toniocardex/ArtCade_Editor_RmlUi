#include "editor-native/ui/assets_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cstddef>
#include <string>

namespace ArtCade::EditorNative {

namespace {

// While Play runs, every asset operation mutates the authoring document, so the
// buttons render disabled (the coordinator/import pipeline reject them anyway).
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

std::string removeButton(const char* action, const std::string& id, bool disabled) {
    return std::string("<button class=\"") + btnClass(disabled) + "\" data-action=\"" + action +
           "\" data-arg=\"" + id + "\"><span class=\"icon\">&#xeb41;</span></button>";
}

std::string smallButton(const char* action, const std::string& id,
                        const char* label, bool disabled) {
    return std::string("<button class=\"") + btnClass(disabled) + "\" data-action=\"" + action +
           "\" data-arg=\"" + id + "\">" + label + "</button>";
}

std::string row(const std::string& name, const std::string& trailing) {
    return "<div class=\"asset-row\"><span class=\"asset-name\">" + name + "</span>"
           + trailing + "</div>";
}

std::string actionRow(const std::string& name, const char* action,
                      const std::string& arg, const std::string& trailing) {
    return "<div class=\"asset-row\" data-dbl-action=\"" + std::string(action)
         + "\" data-arg=\"" + arg + "\"><span class=\"asset-name\">" + name + "</span>"
         + trailing + "</div>";
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

    // Empty catalog: one guidance block instead of four "None" rows (audit 4.6).
    const bool anyAsset = !doc.imageAssets.empty() || !doc.spriteAnimationAssets.empty()
                       || !doc.audioAssets.empty() || !doc.fontAssets.empty()
                       || !doc.tilesets.empty();
    if (!anyAsset) {
        html += "<div class=\"assets-empty\">No assets yet.<br/>"
                "Import images, audio and fonts to use them in your game.</div>";
        list->SetInnerRML(html);
        return;
    }

    // -- Images: name + Use (assign to selected sprite) + Remove --------------
    html += groupTitle("Images", doc.imageAssets.size());
    for (const ImageAssetDef& asset : doc.imageAssets) {
        const std::string id = escapeRml(asset.assetId);
        const std::string label = escapeRml(asset.name.empty() ? asset.assetId : asset.name);
        html += row(label,
            "<button class=\"" + std::string(btnClass(playing))
                + "\" data-action=\"set-sprite-asset\" data-arg=\"" + id
                + "\">Use</button>"
                + smallButton("create-sprite-animation", id, "Anim", playing)
                + smallButton("create-tileset-from-image", id, "Tileset", playing)
                + removeButton("remove-image-asset", id, playing));
    }

    // -- Sprite Animations: clip containers created from an image --------------
    html += groupTitle("Sprite Animations", doc.spriteAnimationAssets.size());
    for (const SpriteAnimationAssetDef& asset : doc.spriteAnimationAssets) {
        const std::string id = escapeRml(asset.id);
        html += actionRow(id, "open-sprite-animation", id,
            smallButton("open-sprite-animation", id, "Edit", playing)
            + smallButton("set-sprite-animation", id, "Use", playing)
            + removeButton("remove-sprite-animation", id, playing));
    }

    // -- Tilesets: sliced spritesheets created from an image -------------------
    html += groupTitle("Tilesets", doc.tilesets.size());
    for (const TilesetAsset& asset : doc.tilesets) {
        const std::string id = escapeRml(asset.assetId);
        const std::string label = escapeRml(asset.name.empty() ? asset.assetId : asset.name);
        html += actionRow(label, "open-tileset-editor", id,
            smallButton("open-tileset-editor", id, "Edit", playing)
            + removeButton("remove-tileset", id, playing));
    }

    // -- Audio: name + load mode + Remove -------------------------------------
    html += groupTitle("Audio", doc.audioAssets.size());
    for (const AudioAssetDef& asset : doc.audioAssets) {
        const std::string id = escapeRml(asset.assetId);
        const std::string label = escapeRml(asset.name.empty() ? asset.assetId : asset.name);
        const char* mode = asset.loadMode == AudioLoadMode::Stream ? "Stream" : "Sound";
        html += row(label, "<span class=\"asset-meta\">" + std::string(mode) + "</span>"
                            + removeButton("remove-audio-asset", id, playing));
    }

    // -- Fonts: name + size + Remove ------------------------------------------
    html += groupTitle("Fonts", doc.fontAssets.size());
    for (const FontAssetDef& asset : doc.fontAssets) {
        const std::string id = escapeRml(asset.assetId);
        const std::string label = escapeRml(asset.name.empty() ? asset.assetId : asset.name);
        html += row(label, "<span class=\"asset-meta\">" + std::to_string(asset.defaultPixelSize)
                            + "px</span>" + removeButton("remove-font-asset", id, playing));
    }

    list->SetInnerRML(html);
}

} // namespace ArtCade::EditorNative
