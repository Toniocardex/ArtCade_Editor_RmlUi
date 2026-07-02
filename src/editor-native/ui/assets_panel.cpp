#include "editor-native/ui/assets_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <string>

namespace ArtCade::EditorNative {

namespace {

// While Play runs, every asset operation mutates the authoring document, so the
// buttons render disabled (the coordinator/import pipeline reject them anyway).
const char* btnClass(bool disabled) { return disabled ? "panel-btn disabled" : "panel-btn"; }

std::string importButton(const char* action, const char* label, bool disabled) {
    return std::string("<button class=\"") + btnClass(disabled) + "\" data-action=\"" + action +
           "\"><span class=\"icon\">&#xeb0b;</span>" + label + "</button>";
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

    // -- Images: name + Use (assign to selected sprite) + Remove --------------
    html += "<div class=\"asset-group-title\">Images</div>";
    html += importButton("import-image", "Import Image", playing);
    if (doc.imageAssets.empty()) html += "<div class=\"assets-empty\">None</div>";
    for (const ImageAssetDef& asset : doc.imageAssets) {
        const std::string id = escapeRml(asset.assetId);
        const std::string label = escapeRml(asset.name.empty() ? asset.assetId : asset.name);
        html += row(label,
            "<button class=\"" + std::string(btnClass(playing))
                + "\" data-action=\"set-sprite-asset\" data-arg=\"" + id
                + "\">Use</button>"
                + smallButton("create-sprite-animation", id, "Anim", playing)
                + removeButton("remove-image-asset", id, playing));
    }

    // -- Sprite Animations: asset metadata backed by one image -----------------
    html += "<div class=\"asset-group-title\">Sprite Animations</div>";
    if (doc.spriteAnimationAssets.empty()) html += "<div class=\"assets-empty\">None</div>";
    for (const SpriteAnimationAssetDef& asset : doc.spriteAnimationAssets) {
        const std::string id = escapeRml(asset.id);
        html += actionRow(id, "open-sprite-animation", id,
            smallButton("open-sprite-animation", id, "Edit", playing)
            + smallButton("set-sprite-animation", id, "Use", playing)
            + removeButton("remove-sprite-animation", id, playing));
    }

    // -- Audio: name + load mode + Remove -------------------------------------
    html += "<div class=\"asset-group-title\">Audio</div>";
    html += importButton("import-audio", "Import Audio", playing);
    if (doc.audioAssets.empty()) html += "<div class=\"assets-empty\">None</div>";
    for (const AudioAssetDef& asset : doc.audioAssets) {
        const std::string id = escapeRml(asset.assetId);
        const std::string label = escapeRml(asset.name.empty() ? asset.assetId : asset.name);
        const char* mode = asset.loadMode == AudioLoadMode::Stream ? "Stream" : "Sound";
        html += row(label, "<span class=\"asset-meta\">" + std::string(mode) + "</span>"
                            + removeButton("remove-audio-asset", id, playing));
    }

    // -- Fonts: name + size + Remove ------------------------------------------
    html += "<div class=\"asset-group-title\">Fonts</div>";
    html += importButton("import-font", "Import Font", playing);
    if (doc.fontAssets.empty()) html += "<div class=\"assets-empty\">None</div>";
    for (const FontAssetDef& asset : doc.fontAssets) {
        const std::string id = escapeRml(asset.assetId);
        const std::string label = escapeRml(asset.name.empty() ? asset.assetId : asset.name);
        html += row(label, "<span class=\"asset-meta\">" + std::to_string(asset.defaultPixelSize)
                            + "px</span>" + removeButton("remove-font-asset", id, playing));
    }

    list->SetInnerRML(html);
}

} // namespace ArtCade::EditorNative
