#include "editor-native/app/tile_palette_sheet_renderer.h"

#include "editor-native/app/checker_pattern.h"
#include "editor-native/model/tile_stamp.h"
#include "editor-native/model/tileset_grid_geometry.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace {

// Sheet-pixel rect -> its on-screen rect inside the sheet destination
// (proportional mapping, same shape as the Tileset Editor's
// destinationForTile).
Rectangle destRectForSourceRect(const TilesetGridRect& source, const TextureResource& texture,
                                const Rectangle& sheetDest) {
    const float textureW = static_cast<float>(texture.texture.width);
    const float textureH = static_cast<float>(texture.texture.height);
    return Rectangle{
        sheetDest.x + (static_cast<float>(source.x) / textureW) * sheetDest.width,
        sheetDest.y + (static_cast<float>(source.y) / textureH) * sheetDest.height,
        (static_cast<float>(source.width) / textureW) * sheetDest.width,
        (static_cast<float>(source.height) / textureH) * sheetDest.height,
    };
}

// The on-screen rect spanned by an inclusive grid-cell region.
Rectangle destRectForCellRegion(const TilesetGridGeometry& geometry,
                                const TextureResource& texture, const Rectangle& sheetDest,
                                int col0, int row0, int col1, int row1) {
    const TilesetGridRect first =
        tilesetSourceRectForGridCell(geometry, TilemapCellCoord{col0, row0});
    const TilesetGridRect last =
        tilesetSourceRectForGridCell(geometry, TilemapCellCoord{col1, row1});
    TilesetGridRect span;
    span.x = first.x;
    span.y = first.y;
    span.width  = (last.x + last.width) - first.x;
    span.height = (last.y + last.height) - first.y;
    return destRectForSourceRect(span, texture, sheetDest);
}

// Diagonal hatch inside a cell: the visual for a stamp hole (selected region
// slot that paints nothing) - distinct from both fill and plain grid. Each
// 45-degree line runs bottom-left to top-right, parameterized by t and
// clamped to the cell so no stroke bleeds outside it.
void drawHoleHatch(const Rectangle& cell, Color color) {
    const float step = std::max(5.f, std::min(cell.width, cell.height) / 3.f);
    for (float offset = -cell.height; offset < cell.width; offset += step) {
        const float tMin = std::max(0.f, -offset);
        const float tMax = std::min(cell.height, cell.width - offset);
        if (tMax <= tMin) continue;
        DrawLineEx(Vector2{cell.x + offset + tMin, cell.y + cell.height - tMin},
                   Vector2{cell.x + offset + tMax, cell.y + cell.height - tMax},
                   1.f, color);
    }
}

} // namespace

Rectangle tilePaletteSheetDestination(const TextureResource& texture,
                                      const ViewportRect& holeRect,
                                      const TilePaletteViewState& view) {
    Rectangle area{
        static_cast<float>(holeRect.x),
        static_cast<float>(holeRect.y),
        std::max(80.f, static_cast<float>(holeRect.width)),
        std::max(80.f, static_cast<float>(holeRect.height)),
    };
    const float textureW = static_cast<float>(texture.texture.width);
    const float textureH = static_cast<float>(texture.texture.height);
    float scale = std::min(area.width / textureW, area.height / textureH);
    if (scale > 1.f) scale = std::floor(scale);
    scale = std::clamp(scale, 0.25f, 16.f);
    scale *= clampTilePaletteZoom(view.zoom);
    const float destW = textureW * scale;
    const float destH = textureH * scale;
    return Rectangle{
        area.x + (area.width - destW) * 0.5f + view.pan.x,
        area.y + (area.height - destH) * 0.5f + view.pan.y,
        destW,
        destH,
    };
}

void renderTilePaletteSheet(const TilesetAsset& tileset,
                            const TilemapEditorState& tilemapEditor,
                            const ViewportRect& holeRect,
                            const ViewportRect& clipRect,
                            const TextureResource* texture,
                            const TilesetEmptyMaskView& emptyMask,
                            const TilePaletteMarquee& marquee) {
    if (!holeRect.valid() || !clipRect.valid()) return;
    if (!texture || !texture->loaded) return;   // the hole keeps its RCSS background

    const auto viewIt = tilemapEditor.paletteViews.find(tileset.assetId);
    const TilePaletteViewState view =
        viewIt != tilemapEditor.paletteViews.end() ? viewIt->second : TilePaletteViewState{};

    BeginScissorMode(clipRect.x, clipRect.y, clipRect.width, clipRect.height);
    const Rectangle dest = tilePaletteSheetDestination(*texture, holeRect, view);
    const Rectangle clip{
        static_cast<float>(clipRect.x), static_cast<float>(clipRect.y),
        static_cast<float>(clipRect.width), static_cast<float>(clipRect.height)};
    drawTransparencyChecker(dest, clip);
    const Rectangle source{
        0.f, 0.f,
        static_cast<float>(texture->texture.width),
        static_cast<float>(texture->texture.height),
    };
    DrawTexturePro(texture->texture, source, dest, Vector2{0.f, 0.f}, 0.f, WHITE);

    const std::optional<TilesetGridGeometry> geometry = computeTilesetGridGeometry(
        tileset.slicing, texture->texture.width, texture->texture.height);

    const bool maskReady = emptyMask.status == TilesetEmptyMaskStatus::Ready
        && emptyMask.flags && emptyMask.flags->size() == tileset.tiles.size();

    // The committed stamp, when it belongs to this tileset. With known
    // provenance the whole N x M region is drawn from sourceColumn/sourceRow
    // (holes hatched, empty edge rows included); without it (a picker-made
    // stamp on a non-grid-aligned tile) the highlight falls back to id match.
    const TilemapTileStamp* stamp =
        (tilemapEditor.stamp && tilemapEditor.stamp->sourceTilesetAssetId == tileset.assetId)
            ? &*tilemapEditor.stamp : nullptr;
    const bool stampHasProvenance = stamp && stamp->sourceColumn >= 0 && stamp->sourceRow >= 0;
    std::unordered_set<TileId> fallbackSelectedIds;
    if (stamp && !stampHasProvenance) {
        for (const std::optional<TileId>& id : stamp->tiles) {
            if (id) fallbackSelectedIds.insert(*id);
        }
    }

    // Per-tile pass: dim empties, grid line, hover, fallback selection.
    const int mouseX = GetMouseX();
    const int mouseY = GetMouseY();
    const bool mouseInClip = clipRect.contains(mouseX, mouseY);
    for (std::size_t i = 0; i < tileset.tiles.size(); ++i) {
        const TileDefinition& tile = tileset.tiles[i];
        if (tile.width <= 0 || tile.height <= 0) continue;
        const Rectangle cell = destRectForSourceRect(
            TilesetGridRect{tile.x, tile.y, tile.width, tile.height}, *texture, dest);
        const bool empty = maskReady && (*emptyMask.flags)[i];
        if (empty) {
            DrawRectangleRec(cell, Color{10, 10, 12, 150});
        }
        const bool hovered = mouseInClip && !marquee.active && !empty
            && static_cast<float>(mouseX) >= cell.x
            && static_cast<float>(mouseX) < cell.x + cell.width
            && static_cast<float>(mouseY) >= cell.y
            && static_cast<float>(mouseY) < cell.y + cell.height;
        if (fallbackSelectedIds.count(tile.id) != 0) {
            DrawRectangleRec(cell, Color{59, 130, 246, 72});
            DrawRectangleLinesEx(cell, 2.f, Color{96, 165, 250, 255});
        } else if (hovered) {
            DrawRectangleRec(cell, Color{212, 212, 216, 26});
            DrawRectangleLinesEx(cell, 1.5f, Color{212, 212, 216, 220});
        } else {
            DrawRectangleLinesEx(cell, 1.f,
                                 Color{212, 212, 216,
                                       static_cast<unsigned char>(empty ? 45 : 110)});
        }
    }

    // Committed stamp region (provenance path): per-slot fill / hole hatch,
    // then one border around the full N x M bounds - a fully-empty edge row
    // stays visibly part of the selection.
    if (stampHasProvenance && geometry) {
        for (int row = 0; row < stamp->height; ++row) {
            for (int col = 0; col < stamp->width; ++col) {
                const TilemapCellCoord cell{stamp->sourceColumn + col, stamp->sourceRow + row};
                if (!tilesetLinearIndexForGridCell(*geometry, cell)) continue;
                const Rectangle slot = destRectForSourceRect(
                    tilesetSourceRectForGridCell(*geometry, cell), *texture, dest);
                const std::optional<TileId>& id =
                    stamp->tiles[static_cast<std::size_t>(row) * stamp->width + col];
                if (id) {
                    DrawRectangleRec(slot, Color{59, 130, 246, 72});
                } else {
                    drawHoleHatch(slot, Color{96, 165, 250, 90});
                }
            }
        }
        const int lastCol = std::min(stamp->sourceColumn + stamp->width - 1, geometry->columns - 1);
        const int lastRow = std::min(stamp->sourceRow + stamp->height - 1, geometry->rows - 1);
        if (lastCol >= stamp->sourceColumn && lastRow >= stamp->sourceRow) {
            DrawRectangleLinesEx(
                destRectForCellRegion(*geometry, *texture, dest,
                                      stamp->sourceColumn, stamp->sourceRow, lastCol, lastRow),
                2.f, Color{96, 165, 250, 255});
        }
    }

    // Live marquee preview, visually distinct from the committed selection.
    if (marquee.active && geometry) {
        const int col0 = std::clamp(std::min(marquee.col0, marquee.col1), 0, geometry->columns - 1);
        const int col1 = std::clamp(std::max(marquee.col0, marquee.col1), 0, geometry->columns - 1);
        const int row0 = std::clamp(std::min(marquee.row0, marquee.row1), 0, geometry->rows - 1);
        const int row1 = std::clamp(std::max(marquee.row0, marquee.row1), 0, geometry->rows - 1);
        const Rectangle band =
            destRectForCellRegion(*geometry, *texture, dest, col0, row0, col1, row1);
        DrawRectangleRec(band, Color{212, 212, 216, 30});
        DrawRectangleLinesEx(band, 1.5f, Color{228, 228, 231, 230});
    }

    DrawRectangleLinesEx(dest, 2.f, Color{120, 120, 130, 235});
    EndScissorMode();
}

} // namespace ArtCade::EditorNative
