#include "editor-native/ui/tile_palette_dock_panel.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/model/tile_stamp.h"
#include "editor-native/ui/editor_ui.h"
#include "editor-native/ui/ui_markup.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cmath>
#include <string>

namespace ArtCade::EditorNative {

namespace {

const SceneInstanceDef* selectedTilemapInstance(const EditorCoordinator& coordinator) {
    if (coordinator.isPlaying()) return nullptr;
    if (coordinator.state().centerWorkspaceMode != CenterWorkspaceMode::Scene) return nullptr;
    const SceneInstanceDef* inst = coordinator.document().findInstanceInScene(
        coordinator.state().activeSceneId, coordinator.selection().primaryEntity);
    if (!inst || !inst->tilemap.has_value()) return nullptr;
    return inst;
}

// Single-line status readout: what's selected on the left, which tileset
// it's from on the right. Richer than the toolbar's terse "Stamp: 2x2" (which
// stays visible even with the dock closed) - this is the detailed view for
// when the user is actually looking at the sheet.
std::string formatStampLabel(const TilemapTileStamp& stamp, const TilesetAsset& tileset) {
    std::string html = "<span class=\"value\">";
    if (stamp.width == 1 && stamp.height == 1) {
        std::string label = "1 tile";
        const std::optional<TileId> id = stampPrimaryTileId(stamp);
        for (std::size_t i = 0; id && i < tileset.tiles.size(); ++i) {
            if (tileset.tiles[i].id == *id) {
                label = "Tile " + std::to_string(i + 1);
                break;
            }
        }
        html += escapeRml(label) + "</span>";
        if (id) html += " &#183; " + escapeRml(*id);
    } else {
        std::size_t painted = 0;
        std::size_t holes = 0;
        for (const std::optional<TileId>& cell : stamp.tiles) {
            if (cell) ++painted;
            else ++holes;
        }
        html += std::to_string(stamp.width) + "&#215;"
              + std::to_string(stamp.height) + " stamp</span>"
              + " &#183; " + std::to_string(painted) + " painted";
        if (holes > 0) html += " &#183; " + std::to_string(holes) + " empty";
    }
    return html;
}

std::string buildDockBodyHtml(const EditorCoordinator& coordinator,
                              const SceneInstanceDef& inst) {
    const TilemapComponent& tm = *inst.tilemap;
    const TilesetAsset* tmTileset = coordinator.document().findTilesetAsset(tm.tilesetAssetId);
    std::string html;

    if (!tmTileset) {
        html += "<div class=\"tile-palette-empty\">Tileset is missing.</div>";
        return html;
    }
    if (!coordinator.document().findImageAsset(tmTileset->imageAssetId)) {
        html += "<div class=\"tile-palette-empty\">Tileset image is missing.</div>";
        return html;
    }
    if (tmTileset->tiles.empty()) {
        html += "<div class=\"tile-palette-empty\">This tileset has no sliced tiles."
                "<br/><button class=\"panel-btn\" data-action=\"open-tilemap-tileset-editor\">"
                "Open Tileset Editor</button></div>";
        return html;
    }

    // -- Left rail: Fit presets, zoom steps, grid toggle, stacked vertically
    // beside the sheet instead of above it - the sheet keeps the dock's full
    // body height, not just what a horizontal bar would leave over. No Tool/
    // Shape buttons here - those already live in the toolbar's always-visible
    // #tilemap-context-tools, and repeating them would be two controls for
    // the same action. Text labels, not icons: fit/zoom have no codepoint
    // already verified against this build's tabler-icons.ttf, and a wrong
    // glyph is worse than a short word.
    const auto viewIt = coordinator.state().tilemapEditor.paletteViews.find(tm.tilesetAssetId);
    const TilePaletteViewState view = viewIt != coordinator.state().tilemapEditor.paletteViews.end()
        ? viewIt->second : TilePaletteViewState{};
    const int scaleStep = std::max(1, static_cast<int>(std::lround(view.textureScale)));

    html += "<div class=\"tile-palette-dock-rail\">";
    const auto railBtn = [&](const char* action, const char* label, const char* title,
                             bool active) {
        html += "<button class=\"panel-btn";
        if (active) html += " active";
        html += "\" data-action=\"";
        html += action;
        html += "\" title=\"";
        html += title;
        html += "\">";
        html += label;
        html += "</button>";
    };
    railBtn("tile-palette-fit-content", "Fit", "Fit painted tiles (Home)", false);
    railBtn("tile-palette-fit-selection", "Sel", "Fit current stamp (Shift+Home)", false);
    railBtn("tile-palette-fit-sheet", "Sheet", "Fit whole sheet", false);
    html += "<div class=\"tile-palette-dock-rail-sep\"></div>";
    railBtn("tile-palette-zoom-1", "1&#215;", "Zoom 1&#215;", scaleStep == 1);
    railBtn("tile-palette-zoom-2", "2&#215;", "Zoom 2&#215;", scaleStep == 2);
    railBtn("tile-palette-zoom-3", "3&#215;", "Zoom 3&#215;", scaleStep == 3);
    railBtn("tile-palette-zoom-4", "4&#215;", "Zoom 4&#215;", scaleStep == 4);
    html += "<div class=\"tile-palette-dock-rail-sep\"></div>";
    html += "<button class=\"panel-btn icon-only";
    if (view.gridVisible) html += " active";
    html += "\" data-action=\"tile-palette-toggle-grid\" title=\"Toggle grid\">"
            "<span class=\"icon\">&#xea3b;</span></button>";
    html += "</div>";

    html += "<div class=\"tile-palette-dock-main\">";
    html += "<div class=\"tile-palette-sheet\" id=\"tile-palette\" title=\""
            "Wheel scroll &#183; Shift+Wheel horizontal &#183; Ctrl+Wheel zoom &#183; "
            "Middle mouse or Space+drag to pan &#183; Double-click to edit tileset\"></div>";

    // Slim status bar: what's selected (left) and which tileset it came from
    // (right). Always present so the paint target is explicit even before
    // touching the Scene View.
    html += "<div class=\"tile-palette-dock-status\">";
    const std::optional<TilemapTileStamp>& stamp = coordinator.state().tilemapEditor.stamp;
    if (stamp && stamp->sourceTilesetAssetId == tm.tilesetAssetId) {
        html += "<span class=\"tile-palette-dock-status-selection\">Selected: "
              + formatStampLabel(*stamp, *tmTileset) + "</span>";
    } else {
        html += "<span class=\"tile-palette-dock-status-selection\">Selected: "
                "<span class=\"value\">none</span> &#183; drag a block on the sheet</span>";
    }
    html += "<span class=\"tile-palette-dock-status-meta\">"
          + std::to_string(tmTileset->tiles.size()) + " tiles &#183; "
          + std::to_string(tmTileset->slicing.tileWidth) + "&#215;"
          + std::to_string(tmTileset->slicing.tileHeight) + "</span>";
    html += "</div></div>";
    return html;
}

} // namespace

void TilePaletteDockPanel::refresh(Rml::ElementDocument* document,
                                   const EditorCoordinator& coordinator) {
    if (!document) return;

    const SceneInstanceDef* inst = selectedTilemapInstance(coordinator);
    const bool showDock = inst != nullptr && coordinator.uiState().tilePaletteDockVisible;

    if (Rml::Element* dock = document->GetElementById("tile-palette-dock")) {
        dock->SetClass("hidden", !showDock);
        if (showDock) {
            dock->SetProperty(
                "height",
                std::to_string(static_cast<int>(coordinator.uiState().tilePaletteDockHeight))
                    + "px");
        }
    }
    if (Rml::Element* splitter = document->GetElementById("split-tile-palette")) {
        splitter->SetClass("hidden", !showDock);
    }

    Rml::Element* body = document->GetElementById("tile-palette-dock-body");
    if (!body) return;

    if (!showDock) {
        body->SetInnerRML("");
        return;
    }

    std::string title = "TILE PALETTE";
    if (inst && inst->tilemap.has_value()) {
        if (const TilesetAsset* ts =
                coordinator.document().findTilesetAsset(inst->tilemap->tilesetAssetId)) {
            title = assetDisplayName(ts->name, ts->assetId);
        }
    }
    if (Rml::Element* titleEl = document->GetElementById("tile-palette-dock-title")) {
        titleEl->SetInnerRML(escapeRml(title));
    }

    body->SetInnerRML(buildDockBodyHtml(coordinator, *inst));
}

} // namespace ArtCade::EditorNative
