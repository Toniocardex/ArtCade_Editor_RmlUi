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

// Display name of the ACTIVE layer of the active scene ("" when the scene has
// no layers - legacy documents). Used by the header and the empty state so
// the paint target is always spelled out.
std::string activeLayerName(const EditorCoordinator& coordinator) {
    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (!scene) return {};
    const std::string layerId = coordinator.activeLayerId(sceneId);
    const std::string target = layerId.empty() ? scene->defaultLayerId : layerId;
    for (const SceneLayerDef& layer : scene->layers) {
        if (layer.id == target) return layer.name;
    }
    return {};
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
    // body height, not just what a horizontal bar would leave over. No paint
    // tool buttons here - they already live in the toolbar's always-visible
    // #tilemap-context-tools (menubar), with keyboard shortcuts; repeating
    // them here would be two controls for the same action. Text labels for
    // Fit/zoom, not icons: those concepts have no codepoint already verified
    // against this build's tabler-icons.ttf, and a wrong glyph is worse than
    // a short word.
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
    // Without a selected tilemap the dock no longer just disappears: in the
    // Scene workspace (outside Play, with a scene open) it stays visible as
    // an empty state that names the active layer and offers the create CTA -
    // the paint target is a first-class concept, not something the user has
    // to reverse-engineer from a vanishing panel. It still hides in Play, in
    // other workspaces, and when the user explicitly closed it.
    const bool sceneContext = !coordinator.isPlaying()
        && coordinator.state().centerWorkspaceMode == CenterWorkspaceMode::Scene
        && coordinator.document().findScene(coordinator.state().activeSceneId) != nullptr;
    const bool showDock = coordinator.uiState().tilePaletteDockVisible
        && (inst != nullptr || sceneContext);

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

    // Header: the full paint target - which instance, on which layer, from
    // which tileset - not just the tileset name.
    std::string title = "TILE PALETTE";
    if (inst && inst->tilemap.has_value()) {
        title = "Painting: " + escapeRml(inst->instanceName);
        const SceneId& sceneId = coordinator.state().activeSceneId;
        if (const SceneDef* scene = coordinator.document().findScene(sceneId)) {
            const std::string layerId =
                coordinator.document().effectiveLayerId(sceneId, *inst);
            for (const SceneLayerDef& layer : scene->layers) {
                if (layer.id == layerId) {
                    title += " &#8212; " + escapeRml(layer.name);
                    break;
                }
            }
        }
        if (const TilesetAsset* ts =
                coordinator.document().findTilesetAsset(inst->tilemap->tilesetAssetId)) {
            title += " / " + escapeRml(assetDisplayName(ts->name, ts->assetId));
        }
    }
    if (Rml::Element* titleEl = document->GetElementById("tile-palette-dock-title")) {
        // Already escaped piecewise above (the dash entity must survive).
        titleEl->SetInnerRML(title);
    }

    if (inst) {
        body->SetInnerRML(buildDockBodyHtml(coordinator, *inst));
        return;
    }
    const std::string layerName = activeLayerName(coordinator);
    std::string html = "<div class=\"hierarchy-empty-state tile-palette-empty-state\">"
        "<span class=\"hierarchy-empty-icon\">&#xea3b;</span>"
        "<span class=\"hierarchy-empty-title\">No Tilemap selected";
    if (!layerName.empty()) html += " in &quot;" + escapeRml(layerName) + "&quot;";
    html += "</span>"
        "<span class=\"hierarchy-empty-copy\">Create a Tilemap entity on the active layer "
        "to start painting, or select an existing one in the Hierarchy.</span>"
        "<button class=\"panel-btn hierarchy-empty-action\" "
        "data-action=\"add-tilemap-entity\">Create Tilemap Entity</button></div>";
    body->SetInnerRML(html);
}

} // namespace ArtCade::EditorNative
