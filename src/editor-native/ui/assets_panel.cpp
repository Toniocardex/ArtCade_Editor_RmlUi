#include "editor-native/ui/assets_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/generated_sfx_commands.h"
#include "editor-native/ui/editor_ui.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

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
         + "<button class=\"asset-import-trigger " + btnClass(disabled)
         + "\"><span class=\"asset-import-title\"><span class=\"icon\">&#xeaad;</span>Import assets <span class=\"icon-caret\">&#xeb5d;</span></span>"
           "<span class=\"asset-import-subtitle\">Files or generated sound</span></button>"
           "<div class=\"create-dropdown\">"
         + entry("import-image", "Image")
         + entry("import-audio", "Audio")
         + entry("import-font",  "Font")
         + entry("import-script", "Lua Script")
         + entry("create-script", "Create Lua Script")
         + entry("create-generated-sfx", "Generated SFX")
         + "</div></div>";
}

std::string groupTitle(const char* label, std::size_t count) {
    return std::string("<div class=\"asset-group-title\">") + label
         + "<span class=\"asset-count\">" + std::to_string(count) + "</span></div>";
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

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool matchesAssetFilter(const std::string& filter,
                        std::initializer_list<std::string> searchable) {
    if (filter.empty()) return true;
    const std::string needle = lower(filter);
    for (const std::string& value : searchable) {
        if (lower(value).find(needle) != std::string::npos) return true;
    }
    return false;
}

} // namespace

void AssetsPanel::refresh(Rml::ElementDocument* document,
                          const EditorCoordinator& coordinator) const {
    if (!document) return;
    Rml::Element* list = document->GetElementById("assets-list");
    if (!list) return;

    const ProjectDoc& doc = coordinator.document().data();
    const bool playing = coordinator.isPlaying();
    const std::string& filter = coordinator.uiState().assetFilter;

    // Keep the live input outside assets-list: list refreshes may replace every
    // row, but never the focused form control or its editing buffer.
    if (Rml::Element* slot = document->GetElementById("assets-search-slot")) {
        if (!slot->HasChildNodes()) {
            slot->SetInnerRML(
                "<span class=\"assets-search-icon\">&#xeb1c;</span>"
                "<input id=\"assets-filter-input\" type=\"text\""
                " class=\"assets-search-field\" data-action=\"set-asset-filter\""
                " placeholder=\"Search assets...\"/>");
        }
    }
    Rml::Element* input = document->GetElementById("assets-filter-input");
    Rml::Context* context = document->GetContext();
    if (input && (!context || context->GetFocusElement() != input)) {
        input->SetAttribute("value", filter);
        if (auto* control = rmlui_dynamic_cast<Rml::ElementFormControl*>(input))
            control->SetValue(filter);
    }

    std::string html;

    html += importMenu(playing);

    // Category counts are totals from the catalog, independent of filtering.
    std::size_t shown = 0;
    std::string groups;
    groups += groupTitle("Images", doc.imageAssets.size());
    for (const ImageAssetDef& asset : doc.imageAssets) {
        if (matchesAssetFilter(filter, {asset.name, asset.assetId, "Images",
                                        asset.sourcePath})) {
            groups += assetRow("&#xeb0a;", assetDisplayName(asset.name, asset.assetId),
                               asset.assetId, nullptr, asset.assetId, "",
                               menuAffordance("image", asset.assetId));
            ++shown;
        }
    }

    // -- Animations: clip containers created from an image ---------------------
    groups += groupTitle("Animations", doc.spriteAnimationAssets.size());
    for (const SpriteAnimationAssetDef& asset : doc.spriteAnimationAssets) {
        const std::string source = asset.clips.empty() ? std::string() : asset.clips.front().imageId;
        if (matchesAssetFilter(filter, {asset.name, asset.id, "Animations", source})) {
            groups += assetRow("&#xed46;", assetDisplayName(asset.name, asset.id),
                               asset.id, "open-sprite-animation", asset.id, "",
                               menuAffordance("anim", asset.id));
            if (!source.empty()) groups += sourceSubtitle(source);
            ++shown;
        }
    }

    // -- Tilesets: sliced spritesheets created from an image --------------------
    groups += groupTitle("Tilesets", doc.tilesets.size());
    for (const TilesetAsset& asset : doc.tilesets) {
        if (matchesAssetFilter(filter, {asset.name, asset.assetId, "Tilesets",
                                        asset.imageAssetId})) {
            groups += assetRow("&#xea3b;", assetDisplayName(asset.name, asset.assetId),
                               asset.assetId, "open-tileset-editor", asset.assetId, "",
                               menuAffordance("tileset", asset.assetId));
            groups += sourceSubtitle(asset.imageAssetId);
            ++shown;
        }
    }

    // -- Audio: name + load mode ------------------------------------------------
    groups += groupTitle("Generated SFX", doc.generatedSfx.size());
    for (const artcade::sfx::GeneratedSfxDef& definition : doc.generatedSfx) {
        if (matchesAssetFilter(filter, {definition.name, definition.id,
                                        "Generated SFX", definition.outputPath})) {
            const GeneratedSfxOutputStatus status =
                generatedSfxOutputStatus(coordinator.document(), definition);
            groups += assetRow("&#xed46;", definition.name, definition.id,
                               "open-generated-sfx", definition.id,
                               "<span class=\"asset-meta\">"
                                   + std::string(generatedSfxOutputStatusLabel(status))
                                   + "</span>",
                               menuAffordance("sfx", definition.id));
            ++shown;
        }
    }

    // -- Audio: independent / detached assets only (linked SFX outputs stay
    // under Generated SFX so one logical asset does not appear twice). --------
    std::size_t independentAudioCount = 0;
    for (const AudioAssetDef& asset : doc.audioAssets) {
        if (!coordinator.document().findGeneratedSfxByOutputAssetId(asset.assetId))
            ++independentAudioCount;
    }
    groups += groupTitle("Audio", independentAudioCount);
    for (const AudioAssetDef& asset : doc.audioAssets) {
        if (coordinator.document().findGeneratedSfxByOutputAssetId(asset.assetId))
            continue;
        const std::string displayName =
            resolveAudioAssetDisplayName(coordinator.document(), asset);
        if (matchesAssetFilter(filter, {displayName, asset.assetId, "Audio",
                                        asset.sourcePath})) {
            const char* mode = asset.loadMode == AudioLoadMode::Stream ? "Stream" : "Sound";
            groups += assetRow(nullptr, displayName,
                               asset.assetId, nullptr, asset.assetId,
                               "<span class=\"asset-meta\">" + std::string(mode) + "</span>",
                               menuAffordance("audio", asset.assetId));
            ++shown;
        }
    }

    // -- Fonts: name + size ------------------------------------------------------
    groups += groupTitle("Fonts", doc.fontAssets.size());
    for (const FontAssetDef& asset : doc.fontAssets) {
        if (matchesAssetFilter(filter, {asset.name, asset.assetId, "Fonts",
                                        asset.sourcePath})) {
            groups += assetRow(nullptr, assetDisplayName(asset.name, asset.assetId),
                               asset.assetId, nullptr, asset.assetId,
                               "<span class=\"asset-meta\">"
                                   + std::to_string(asset.defaultPixelSize) + "px</span>",
                               menuAffordance("font", asset.assetId));
            ++shown;
        }
    }

    // -- Scripts: external authoring source metadata ---------------------------
    groups += groupTitle("Scripts", doc.scriptAssets.size());
    for (const ScriptAssetDef& asset : doc.scriptAssets) {
        if (matchesAssetFilter(filter, {asset.name, asset.assetId, "Scripts",
                                        asset.sourcePath})) {
            groups += assetRow("&#xf2d2;", assetDisplayName(asset.name, asset.assetId),
                               asset.sourcePath, "open-script", asset.assetId, "",
                               menuAffordance("script", asset.assetId));
            ++shown;
        }
    }

    if (!filter.empty() && shown == 0) {
        html += "<div class=\"assets-filter-empty\">No assets match &quot;"
             + escapeRml(filter) + "&quot;.</div>";
    }
    html += groups;

    list->SetInnerRML(html);
}

} // namespace ArtCade::EditorNative
