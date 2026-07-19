#include "editor-native/app/tile_palette_sheet_renderer.h"

#include "editor-native/app/checker_pattern.h"
#include "editor-native/model/tile_palette_projection.h"
#include "editor-native/model/tile_stamp.h"
#include "editor-native/model/tileset_grid_geometry.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace {

// Slice 4 visual tokens — distinguish hover / committed / marquee without
// heavy fills. Line thickness stays 1 physical px at every zoom.
constexpr unsigned char kEmptyOverlayAlpha = 51;       // ~20% dim
constexpr unsigned char kEmptyHatchAlpha = 28;
constexpr unsigned char kSelectionFillAlpha = 36;      // ~14% accent
constexpr unsigned char kSelectionOutlineAlpha = 255;
constexpr unsigned char kHoverOutlineAlpha = 210;
constexpr unsigned char kMarqueeFillAlpha = 20;
constexpr unsigned char kGridAlphaLo = 38;              // zoom in [1, 3)
constexpr unsigned char kGridAlphaHi = 72;              // zoom >= 3
constexpr Color kAccent = {59, 130, 246, 255};
constexpr Color kAccentSoft = {96, 165, 250, 255};

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

void drawHoleHatch(const Rectangle& cell, Color color) {
    const float step = std::max(6.f, std::min(cell.width, cell.height) / 3.5f);
    for (float offset = -cell.height; offset < cell.width; offset += step) {
        const float tMin = std::max(0.f, -offset);
        const float tMax = std::min(cell.height, cell.width - offset);
        if (tMax <= tMin) continue;
        DrawLineEx(Vector2{cell.x + offset + tMin, cell.y + cell.height - tMin},
                   Vector2{cell.x + offset + tMax, cell.y + cell.height - tMax},
                   1.f, color);
    }
}

void drawDashedRect(const Rectangle& r, Color color, float dash = 4.f, float gap = 3.f) {
    const auto dashSeg = [&](float x0, float y0, float x1, float y1) {
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.5f) return;
        const float ux = dx / len;
        const float uy = dy / len;
        float t = 0.f;
        bool draw = true;
        while (t < len) {
            const float seg = draw ? dash : gap;
            const float t1 = std::min(len, t + seg);
            if (draw) {
                DrawLineEx(Vector2{x0 + ux * t, y0 + uy * t},
                           Vector2{x0 + ux * t1, y0 + uy * t1}, 1.f, color);
            }
            t = t1;
            draw = !draw;
        }
    };
    dashSeg(r.x, r.y, r.x + r.width, r.y);
    dashSeg(r.x + r.width, r.y, r.x + r.width, r.y + r.height);
    dashSeg(r.x + r.width, r.y + r.height, r.x, r.y + r.height);
    dashSeg(r.x, r.y + r.height, r.x, r.y);
}

Rectangle sheetRectFromProjection(const TilePaletteViewportProjection& proj) {
    return Rectangle{proj.sheetX, proj.sheetY, proj.sheetWidth, proj.sheetHeight};
}

constexpr float kScrollbarThickness = 8.f;
constexpr float kScrollbarMargin = 2.f;

struct ScrollbarLayout {
    bool horizontal = false;
    bool vertical = false;
    Rectangle trackH{};
    Rectangle thumbH{};
    Rectangle trackV{};
    Rectangle thumbV{};
};

ScrollbarLayout computeScrollbars(const TilePaletteViewportProjection& proj) {
    ScrollbarLayout bar;
    const float vw = proj.viewportWidth;
    const float vh = proj.viewportHeight;
    bar.horizontal = proj.sheetWidth > vw + 0.5f;
    bar.vertical = proj.sheetHeight > vh + 0.5f;
    if (bar.horizontal) {
        const float trackW = vw - (bar.vertical ? kScrollbarThickness + kScrollbarMargin : 0.f)
                           - 2.f * kScrollbarMargin;
        bar.trackH = Rectangle{
            proj.viewportX + kScrollbarMargin,
            proj.viewportY + vh - kScrollbarThickness - kScrollbarMargin,
            std::max(16.f, trackW),
            kScrollbarThickness};
        const float range = proj.sheetWidth - vw;
        const float thumbW = std::max(18.f, bar.trackH.width * (vw / proj.sheetWidth));
        const float t = range > 0.f ? (-proj.scrollOffset.x) / range : 0.f;
        bar.thumbH = Rectangle{
            bar.trackH.x + t * (bar.trackH.width - thumbW),
            bar.trackH.y,
            thumbW,
            bar.trackH.height};
    }
    if (bar.vertical) {
        const float trackH = vh - (bar.horizontal ? kScrollbarThickness + kScrollbarMargin : 0.f)
                           - 2.f * kScrollbarMargin;
        bar.trackV = Rectangle{
            proj.viewportX + vw - kScrollbarThickness - kScrollbarMargin,
            proj.viewportY + kScrollbarMargin,
            kScrollbarThickness,
            std::max(16.f, trackH)};
        const float range = proj.sheetHeight - vh;
        const float thumbH = std::max(18.f, bar.trackV.height * (vh / proj.sheetHeight));
        const float t = range > 0.f ? (-proj.scrollOffset.y) / range : 0.f;
        bar.thumbV = Rectangle{
            bar.trackV.x,
            bar.trackV.y + t * (bar.trackV.height - thumbH),
            bar.trackV.width,
            thumbH};
    }
    return bar;
}

void drawScrollbars(const ScrollbarLayout& bar) {
    if (bar.horizontal) {
        DrawRectangleRec(bar.trackH, Color{30, 30, 34, 200});
        DrawRectangleRec(bar.thumbH, Color{90, 90, 100, 220});
    }
    if (bar.vertical) {
        DrawRectangleRec(bar.trackV, Color{30, 30, 34, 200});
        DrawRectangleRec(bar.thumbV, Color{90, 90, 100, 220});
    }
}

void drawStatusOverlay(const ViewportRect& holeRect, const CanvasFont* canvasFont,
                       const std::string& message) {
    const Rectangle hole{
        static_cast<float>(holeRect.x), static_cast<float>(holeRect.y),
        static_cast<float>(holeRect.width), static_cast<float>(holeRect.height)};
    DrawRectangleRec(hole, Color{19, 19, 22, 220});
    const float size = 13.f;
    float textW = 0.f;
    if (canvasFont) {
        textW = measureCanvasText(*canvasFont, message, size);
        const float x = hole.x + std::max(8.f, (hole.width - textW) * 0.5f);
        const float y = hole.y + std::max(8.f, (hole.height - size) * 0.5f);
        drawCanvasText(*canvasFont, message, x, y, size, Color{161, 161, 170, 255});
    } else {
        textW = MeasureText(message.c_str(), 13);
        const float x = hole.x + std::max(8.f, (hole.width - textW) * 0.5f);
        const float y = hole.y + std::max(8.f, (hole.height - 13.f) * 0.5f);
        DrawText(message.c_str(), static_cast<int>(x), static_cast<int>(y), 13,
                 Color{161, 161, 170, 255});
    }
}

void drawBanner(const ViewportRect& holeRect, const CanvasFont* canvasFont,
                const std::string& message) {
    const float size = 11.f;
    const float pad = 6.f;
    float textW = canvasFont ? measureCanvasText(*canvasFont, message, size)
                             : static_cast<float>(MeasureText(message.c_str(), 11));
    const Rectangle banner{
        static_cast<float>(holeRect.x) + pad,
        static_cast<float>(holeRect.y + holeRect.height) - size - pad * 2.f - 4.f,
        std::min(static_cast<float>(holeRect.width) - pad * 2.f, textW + pad * 2.f),
        size + pad};
    DrawRectangleRec(banner, Color{28, 24, 18, 220});
    DrawRectangleLinesEx(banner, 1.f, Color{92, 70, 30, 200});
    if (canvasFont) {
        drawCanvasText(*canvasFont, message, banner.x + pad, banner.y + pad * 0.5f, size,
                       Color{216, 180, 74, 255});
    } else {
        DrawText(message.c_str(), static_cast<int>(banner.x + pad),
                 static_cast<int>(banner.y + 2.f), 11, Color{216, 180, 74, 255});
    }
}

} // namespace

TilePaletteViewportProjection tilePaletteProjectionForHole(
    const TextureResource& texture,
    const ViewportRect& holeRect,
    const TilePaletteViewState& view) {
    return computeTilePaletteProjection(
        view,
        static_cast<float>(holeRect.x), static_cast<float>(holeRect.y),
        static_cast<float>(std::max(1, holeRect.width)),
        static_cast<float>(std::max(1, holeRect.height)),
        static_cast<float>(texture.texture.width),
        static_cast<float>(texture.texture.height));
}

Rectangle tilePaletteSheetDestination(const TextureResource& texture,
                                      const ViewportRect& holeRect,
                                      const TilePaletteViewState& view,
                                      int /*tileWidthPx*/, int /*tileHeightPx*/) {
    return sheetRectFromProjection(tilePaletteProjectionForHole(texture, holeRect, view));
}

void renderTilePaletteSheet(const TilesetAsset& tileset,
                            const TilemapEditorState& tilemapEditor,
                            const ViewportRect& holeRect,
                            const ViewportRect& clipRect,
                            const TextureResource* texture,
                            const TilesetEmptyMaskView& emptyMask,
                            const TilePaletteMarquee& marquee,
                            const CanvasFont* canvasFont) {
    if (!holeRect.valid() || !clipRect.valid()) return;

    BeginScissorMode(clipRect.x, clipRect.y, clipRect.width, clipRect.height);

    if (!texture || !texture->loaded) {
        drawStatusOverlay(holeRect, canvasFont,
                          texture ? "Loading tileset image\xE2\x80\xA6"
                                  : "Missing tileset image");
        EndScissorMode();
        return;
    }

    const auto viewIt = tilemapEditor.paletteViews.find(tileset.assetId);
    const TilePaletteViewState view =
        viewIt != tilemapEditor.paletteViews.end() ? viewIt->second : TilePaletteViewState{};

    const TilePaletteViewportProjection proj =
        tilePaletteProjectionForHole(*texture, holeRect, view);
    const Rectangle dest = sheetRectFromProjection(proj);

    const Rectangle clip{
        static_cast<float>(clipRect.x), static_cast<float>(clipRect.y),
        static_cast<float>(clipRect.width), static_cast<float>(clipRect.height)};
    drawTransparencyChecker(dest, clip);
    const Rectangle source{
        0.f, 0.f,
        static_cast<float>(texture->texture.width),
        static_cast<float>(texture->texture.height),
    };
    // Nearest-neighbour is set at load (TextureCache) so integer zoom stays sharp.
    DrawTexturePro(texture->texture, source, dest, Vector2{0.f, 0.f}, 0.f, WHITE);

    const std::optional<TilesetGridGeometry> geometry = computeTilesetGridGeometry(
        tileset.slicing, texture->texture.width, texture->texture.height);

    const bool maskReady = emptyMask.status == TilesetEmptyMaskStatus::Ready
        && emptyMask.flags && emptyMask.flags->size() == tileset.tiles.size();

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

    const bool showGrid = view.gridVisible && proj.textureScale >= 1.f;
    const unsigned char gridAlpha =
        proj.textureScale >= 3.f ? kGridAlphaHi : kGridAlphaLo;

    const int mouseX = GetMouseX();
    const int mouseY = GetMouseY();
    const bool mouseInClip = clipRect.contains(mouseX, mouseY);

    // Pass 1: empty attenuation + grid (never thicken with zoom).
    for (std::size_t i = 0; i < tileset.tiles.size(); ++i) {
        const TileDefinition& tile = tileset.tiles[i];
        if (tile.width <= 0 || tile.height <= 0) continue;
        const Rectangle cell = destRectForSourceRect(
            TilesetGridRect{tile.x, tile.y, tile.width, tile.height}, *texture, dest);
        const bool empty = maskReady && (*emptyMask.flags)[i];
        if (empty) {
            DrawRectangleRec(cell, Color{8, 8, 10, kEmptyOverlayAlpha});
            drawHoleHatch(cell, Color{40, 40, 48, kEmptyHatchAlpha});
        }
        if (showGrid) {
            DrawRectangleLinesEx(cell, 1.f,
                                 Color{212, 212, 216,
                                       static_cast<unsigned char>(empty ? gridAlpha / 2
                                                                        : gridAlpha)});
        }
    }

    // Pass 2: hover (outline only) and id-fallback selection.
    for (std::size_t i = 0; i < tileset.tiles.size(); ++i) {
        const TileDefinition& tile = tileset.tiles[i];
        if (tile.width <= 0 || tile.height <= 0) continue;
        const Rectangle cell = destRectForSourceRect(
            TilesetGridRect{tile.x, tile.y, tile.width, tile.height}, *texture, dest);
        const bool empty = maskReady && (*emptyMask.flags)[i];
        if (fallbackSelectedIds.count(tile.id) != 0) {
            DrawRectangleRec(cell, Color{kAccent.r, kAccent.g, kAccent.b, kSelectionFillAlpha});
            DrawRectangleLinesEx(cell, 1.f,
                                 Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b,
                                       kSelectionOutlineAlpha});
            continue;
        }
        const bool hovered = mouseInClip && !marquee.active && !empty
            && static_cast<float>(mouseX) >= cell.x
            && static_cast<float>(mouseX) < cell.x + cell.width
            && static_cast<float>(mouseY) >= cell.y
            && static_cast<float>(mouseY) < cell.y + cell.height;
        if (hovered) {
            DrawRectangleLinesEx(cell, 1.f,
                                 Color{228, 228, 231, kHoverOutlineAlpha});
        }
    }

    // Committed stamp region (provenance): fill / hole hatch + 1 px outline.
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
                    DrawRectangleRec(slot,
                                     Color{kAccent.r, kAccent.g, kAccent.b, kSelectionFillAlpha});
                } else {
                    drawHoleHatch(slot, Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, 70});
                }
            }
        }
        const int lastCol = std::min(stamp->sourceColumn + stamp->width - 1, geometry->columns - 1);
        const int lastRow = std::min(stamp->sourceRow + stamp->height - 1, geometry->rows - 1);
        if (lastCol >= stamp->sourceColumn && lastRow >= stamp->sourceRow) {
            DrawRectangleLinesEx(
                destRectForCellRegion(*geometry, *texture, dest,
                                      stamp->sourceColumn, stamp->sourceRow, lastCol, lastRow),
                1.f, Color{kAccentSoft.r, kAccentSoft.g, kAccentSoft.b, kSelectionOutlineAlpha});
        }
    }

    // Live marquee: lighter fill + dashed brighter outline.
    if (marquee.active && geometry) {
        const int col0 = std::clamp(std::min(marquee.col0, marquee.col1), 0, geometry->columns - 1);
        const int col1 = std::clamp(std::max(marquee.col0, marquee.col1), 0, geometry->columns - 1);
        const int row0 = std::clamp(std::min(marquee.row0, marquee.row1), 0, geometry->rows - 1);
        const int row1 = std::clamp(std::max(marquee.row0, marquee.row1), 0, geometry->rows - 1);
        const Rectangle band =
            destRectForCellRegion(*geometry, *texture, dest, col0, row0, col1, row1);
        DrawRectangleRec(band, Color{250, 250, 250, kMarqueeFillAlpha});
        drawDashedRect(band, Color{250, 250, 250, 235});
    }

    DrawRectangleLinesEx(dest, 1.f, Color{100, 100, 110, 160});
    drawScrollbars(computeScrollbars(proj));

    if (emptyMask.status == TilesetEmptyMaskStatus::Failed) {
        drawBanner(holeRect, canvasFont, "Empty-tile scan failed \xC2\xB7 selection unfiltered");
    }

    EndScissorMode();
}

} // namespace ArtCade::EditorNative
