#include "editor-native/ui/tileset_editor_controller.h"

#include "editor-native/ui/editor_ui.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/commands/tileset_commands.h"
#include "editor-native/model/tileset_slicing.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

TilesetEditorController::TilesetEditorController(
    EditorCoordinator& coordinator, Rml::ElementDocument* document)
    : coordinator_(coordinator), document_(document) {}

void TilesetEditorController::detach() {
    document_ = nullptr;
    applySlicingRequest_ = {};
    closeRequest_ = {};
    createFromImageRequest_ = {};
    imageSizeProvider_ = {};
    markup_.clear();
}

void TilesetEditorController::setApplySlicingRequest(ApplySlicingRequest request) {
    applySlicingRequest_ = std::move(request);
}

void TilesetEditorController::setCloseRequest(CloseRequest request) {
    closeRequest_ = std::move(request);
}

void TilesetEditorController::setCreateFromImageRequest(CreateFromImageRequest request) {
    createFromImageRequest_ = std::move(request);
}

void TilesetEditorController::setImageSizeProvider(ImageSizeProvider provider) {
    imageSizeProvider_ = std::move(provider);
}

void TilesetEditorController::refresh() {
    if (!document_) return;
    Rml::Element* panel = document_->GetElementById("tileset-editor");
    if (!panel) return;
    const TilesetEditorState& state = coordinator_.state().tilesetEditor;
    const TilesetAsset* asset =
        state.openAssetId ? coordinator_.document().findTilesetAsset(*state.openAssetId) : nullptr;
    if (!asset) {
        document_->Hide();
        if (!markup_.empty()) {
            markup_.clear();
            panel->SetInnerRML("");
        }
        return;
    }

    document_->Show();
    document_->PullToFront();
    const TilesetSlicing& s = state.pendingSlicing;
    const bool dirty = !sameTilesetSlicing(asset->slicing, s);
    const std::optional<std::pair<int, int>> imageSize = imageSizeProvider_
        ? imageSizeProvider_(asset->imageAssetId)
        : std::nullopt;

    // One labelled X/Y (or W/H) pair per row - halves the form height and
    // matches how the values are actually thought about.
    const auto slicePair = [](const char* label,
                              const char* actionA, int valueA,
                              const char* actionB, int valueB) {
        return std::string("<div class=\"tileset-slice-row\"><span class=\"prop-label\">") + label
             + "</span><input type=\"text\" class=\"prop-input\" data-action=\"" + actionA
             + "\" value=\"" + std::to_string(valueA)
             + "\"/><input type=\"text\" class=\"prop-input\" data-action=\"" + actionB
             + "\" value=\"" + std::to_string(valueB) + "\"/></div>";
    };

    std::string html;
    html += "<div class=\"tileset-editor-shell\">";

    // -- Header: eyebrow, editable name, source info, dirty chip, Close -------
    html += "<div class=\"tileset-editor-title\">"
            "<span class=\"tileset-editor-eyebrow\">Tileset Editor</span>"
            "<input type=\"text\" class=\"tileset-editor-name\" data-action=\"commit-tileset-name\""
            " data-arg=\"" + escapeRml(asset->assetId) + "\" value=\"" + escapeRml(asset->name) + "\"/>"
            "<span class=\"tileset-editor-source\">" + escapeRml(asset->imageAssetId);
    if (imageSize) {
        html += "&nbsp;&nbsp;&#183;&nbsp;&nbsp;" + std::to_string(imageSize->first) + " &#215; "
              + std::to_string(imageSize->second) + " px";
    }
    html += "</span>";
    if (dirty) {
        html += "<span class=\"tileset-status-chip warn\">Unapplied changes</span>";
    } else {
        html += "<span class=\"tileset-status-chip\">"
              + std::to_string(asset->tiles.size()) + " tiles</span>";
    }
    html += "<button id=\"tileset-close-btn\" class=\"panel-btn\""
            " data-action=\"close-tileset-editor\">Close</button></div>";

    html += "<div class=\"tileset-editor-main\">";

    // -- Canvas column: zoom toolbar, canvas, status bar -----------------------
    html += "<div class=\"tileset-canvas-col\">"
            "<div class=\"tileset-canvas-toolbar\">"
            "<span class=\"tileset-panel-title\">Source Image</span>"
            "<button class=\"panel-btn tileset-zoom-btn\" data-action=\"tileset-zoom-out\""
            " title=\"Zoom out\">&#8722;</button>"
            "<span id=\"tileset-zoom-readout\" class=\"tileset-zoom-readout\"></span>"
            "<button class=\"panel-btn tileset-zoom-btn\" data-action=\"tileset-zoom-in\""
            " title=\"Zoom in\">+</button>"
            "<button class=\"panel-btn tileset-zoom-fit\" data-action=\"tileset-zoom-fit\""
            " title=\"Reset zoom and pan\">Fit</button>"
            "</div>"
            "<div id=\"tileset-canvas\"></div>";
    html += "<div class=\"tileset-canvas-status\">";
    if (imageSize) {
        const TilesetSliceResult grid =
            computeTilesetSlicing(imageSize->first, imageSize->second, s);
        if (grid.tileCount > 0) {
            html += "<span class=\"tileset-status-strong\">" + std::to_string(grid.columns)
                  + " &#215; " + std::to_string(grid.rows) + " tiles</span><span>"
                  + std::to_string(s.tileWidth) + " &#215; " + std::to_string(s.tileHeight)
                  + " px tile</span>";
        }
    }
    html += "<span class=\"tileset-status-hint\">Wheel to zoom"
            "&nbsp;&nbsp;&#183;&nbsp;&nbsp;Middle mouse or Space + drag to pan"
            "&nbsp;&nbsp;&#183;&nbsp;&nbsp;Click a tile to select it</span></div></div>";

    // -- Settings column: slicing form, live feedback, actions, selected tile --
    html += "<div class=\"tileset-settings\"><div class=\"tileset-panel-title\">Slicing</div>";
    html += slicePair("Tile Size",
                      "commit-tileset-tile-width", s.tileWidth,
                      "commit-tileset-tile-height", s.tileHeight);
    html += slicePair("Margin",
                      "commit-tileset-margin-x", s.marginX,
                      "commit-tileset-margin-y", s.marginY);
    html += slicePair("Spacing",
                      "commit-tileset-spacing-x", s.spacingX,
                      "commit-tileset-spacing-y", s.spacingY);

    // Live coverage feedback from the same pure slicing math the renderer and
    // the Apply flow use; the core still revalidates on Apply (UI validation
    // is experience only, never the guard).
    if (imageSize) {
        const TilesetSliceResult grid =
            computeTilesetSlicing(imageSize->first, imageSize->second, s);
        if (grid.tileCount <= 0) {
            html += "<span class=\"tileset-slice-feedback err\">"
                    "Tile size does not fit the sheet &#8212; lower it</span>";
        } else if (grid.remainderX > 0 || grid.remainderY > 0) {
            html += "<span class=\"tileset-slice-feedback warn\">"
                  + std::to_string(grid.columns) + " &#215; " + std::to_string(grid.rows) + " = "
                  + std::to_string(grid.tileCount) + " tiles &#8212; leaves ";
            if (grid.remainderX > 0) {
                html += std::to_string(grid.remainderX) + " px right";
                if (grid.remainderY > 0) html += ", ";
            }
            if (grid.remainderY > 0) html += std::to_string(grid.remainderY) + " px bottom";
            html += " uncovered</span>";
        } else {
            html += "<span class=\"tileset-slice-feedback ok\">"
                  + std::to_string(grid.columns) + " &#215; " + std::to_string(grid.rows) + " = "
                  + std::to_string(grid.tileCount) + " tiles &#8212; covers the whole sheet</span>";
        }
    } else {
        html += "<span class=\"tileset-slice-feedback\">Source image not loaded</span>";
    }

    html += "<div class=\"tileset-settings-actions\">"
            "<button id=\"tileset-apply-btn\" class=\"panel-btn";
    if (dirty) html += " primary";
    html += "\" data-action=\"apply-tileset-slicing\""
            " title=\"Commit this slicing to the tileset\">Apply</button>"
            "<button id=\"tileset-reset-btn\" class=\"panel-btn\""
            " data-action=\"reset-tileset-slicing\""
            " title=\"Back to the committed slicing\">Reset</button></div>";

    html += "<div class=\"tileset-panel-title tileset-selected-title\">Selected Tile</div>";
    // Selection ids come from the pending grid (canvas click); resolve against
    // the committed tiles first, then the live pending grid, so the panel
    // always shows exactly what the canvas highlighted.
    std::optional<TileDefinition> selectedTile;
    if (state.selectedTileId) {
        for (const TileDefinition& tile : asset->tiles) {
            if (tile.id == *state.selectedTileId) { selectedTile = tile; break; }
        }
        if (!selectedTile && imageSize) {
            for (const TileDefinition& tile :
                 tilesForSlicing(imageSize->first, imageSize->second, s)) {
                if (tile.id == *state.selectedTileId) { selectedTile = tile; break; }
            }
        }
    }
    const TileDefinition* selected = selectedTile ? &*selectedTile : nullptr;
    if (selected) {
        html += "<div class=\"tileset-selected-row\">"
                "<div id=\"tileset-selected-thumb\" class=\"tileset-selected-thumb\"></div>"
                "<span class=\"tileset-selected-info\">" + escapeRml(selected->id)
              + "<br/>x " + std::to_string(selected->x) + " &#183; y " + std::to_string(selected->y)
              + " &#183; " + std::to_string(selected->width) + " &#215; "
              + std::to_string(selected->height) + " px</span></div>";
    } else {
        html += "<span class=\"tileset-selected-empty\">Click a tile in the sheet to select it</span>";
    }

    // Committed tiles as a visual grid (the Inspector palette's thumb-slot
    // pattern: transparent divs raylib paints the crops into). Capped so a
    // huge atlas cannot flood the layout with elements; the canvas remains
    // the full view.
    if (!asset->tiles.empty()) {
        constexpr std::size_t kMaxTileThumbs = 512;
        const std::size_t shown = std::min(asset->tiles.size(), kMaxTileThumbs);
        html += "<div class=\"tileset-panel-title tileset-tiles-title\">Tiles ("
              + std::to_string(asset->tiles.size()) + ")</div>";
        html += "<div id=\"tileset-tiles-grid\" class=\"tileset-tiles-grid\">";
        for (std::size_t i = 0; i < shown; ++i) {
            const TileDefinition& tile = asset->tiles[i];
            html += "<div id=\"tileset-grid-thumb-" + std::to_string(i)
                  + "\" class=\"tile-thumb\" data-action=\"select-tileset-tile\""
                    " data-arg=\"" + escapeRml(tile.id) + "\""
                    " title=\"Tile " + std::to_string(i + 1) + " - ID: " + escapeRml(tile.id)
                  + "\"></div>";
        }
        if (shown < asset->tiles.size()) {
            html += "<span class=\"tileset-tiles-more\">+"
                  + std::to_string(asset->tiles.size() - shown) + " more tiles</span>";
        }
        html += "</div>";
    }

    html += "<span class=\"tileset-settings-diagnostic\">Source: " + escapeRml(asset->imageAssetId)
          + "<br/>" + std::to_string(asset->tiles.size()) + " tile(s) committed</span>";
    html += "</div>";      // .tileset-settings
    html += "</div></div>"; // .tileset-editor-main, .tileset-editor-shell

    if (html != markup_) {
        markup_ = html;
        panel->SetInnerRML(html);
        updateZoomReadout();   // the readout span was just recreated empty
    }
}

void TilesetEditorController::updateZoomReadout() {
    if (!document_) return;
    const TilesetEditorState& state = coordinator_.state().tilesetEditor;
    if (!state.openAssetId) return;
    if (Rml::Element* el = document_->GetElementById("tileset-zoom-readout")) {
        const int pct = static_cast<int>(state.zoom * 100.f + 0.5f);
        el->SetInnerRML(std::to_string(pct) + "%");
    }
}

bool TilesetEditorController::handleAction(const std::string& action, const std::string& arg,
                                         const std::string& value) {
    if (action == "create-tileset-from-image") {
        if (!coordinator_.isPlaying() && coordinator_.document().hasImageAsset(arg)) {
            if (createFromImageRequest_) {
                createFromImageRequest_(arg);
            } else {
                const std::string id = uniqueTilesetAssetId(coordinator_.document(), arg);
                const TilesetSlicing defaultSlicing;   // 32x32, no margin/spacing
                if (coordinator_.execute(
                        AddTilesetAssetCommand{id, id, arg, defaultSlicing}).ok) {
                    coordinator_.apply(OpenTilesetEditorIntent{id});
                }
            }
        }
    } else if (action == "open-tileset-editor") {
        if (!coordinator_.isPlaying()) coordinator_.apply(OpenTilesetEditorIntent{arg});
    } else if (action == "remove-tileset") {
        if (!arg.empty()) coordinator_.execute(RemoveTilesetAssetCommand{arg});
    } else if (action == "close-tileset-editor") {
        // Also serves as Cancel - one action string for both, per the
        // single-entry-point paletto (mirrors close-sprite-animation's own
        // single close path). The application handler owns the unapplied-
        // changes guard; without one, closing discards the pending state.
        if (closeRequest_) closeRequest_();
        else coordinator_.apply(CloseTilesetEditorIntent{});
    } else if (action == "commit-tileset-name") {
        if (!arg.empty() && !value.empty()) {
            coordinator_.execute(RenameTilesetCommand{arg, value});
        }
    } else if (action == "commit-tileset-tile-width" || action == "commit-tileset-tile-height"
               || action == "commit-tileset-margin-x" || action == "commit-tileset-margin-y"
               || action == "commit-tileset-spacing-x" || action == "commit-tileset-spacing-y") {
        const std::optional<float> parsed = parseNumberField(value);
        if (!parsed.has_value()) {
            coordinator_.logError("Tileset slicing value is not a number");
        } else {
            TilesetSlicing slicing = coordinator_.state().tilesetEditor.pendingSlicing;
            const int rounded = static_cast<int>(std::round(*parsed));
            if (action == "commit-tileset-tile-width") slicing.tileWidth = rounded;
            else if (action == "commit-tileset-tile-height") slicing.tileHeight = rounded;
            else if (action == "commit-tileset-margin-x") slicing.marginX = rounded;
            else if (action == "commit-tileset-margin-y") slicing.marginY = rounded;
            else if (action == "commit-tileset-spacing-x") slicing.spacingX = rounded;
            else slicing.spacingY = rounded;
            coordinator_.apply(SetPendingTilesetSlicingIntent{slicing});
        }
    } else if (action == "apply-tileset-slicing") {
        if (applySlicingRequest_) applySlicingRequest_();
    } else if (action == "select-tileset-tile") {
        if (!arg.empty()) coordinator_.apply(SelectTilesetTileIntent{arg});
    } else if (action == "reset-tileset-slicing") {
        const TilesetEditorState& state = coordinator_.state().tilesetEditor;
        const TilesetAsset* asset = state.openAssetId
            ? coordinator_.document().findTilesetAsset(*state.openAssetId) : nullptr;
        if (asset) coordinator_.apply(SetPendingTilesetSlicingIntent{asset->slicing});
    } else if (action == "tileset-zoom-in" || action == "tileset-zoom-out") {
        const float factor = (action == "tileset-zoom-in") ? 1.25f : 0.8f;
        coordinator_.apply(SetTilesetEditorZoomIntent{
            coordinator_.state().tilesetEditor.zoom * factor});
    } else if (action == "tileset-zoom-fit") {
        // Back to the default framing: fit scale, centred (zoom 1, pan 0).
        const Vec2 pan = coordinator_.state().tilesetEditor.pan;
        coordinator_.apply(SetTilesetEditorZoomIntent{1.f});
        coordinator_.apply(PanTilesetEditorIntent{{-pan.x, -pan.y}});
    } else {
        return false;
    }
    return true;
}

} // namespace ArtCade::EditorNative
