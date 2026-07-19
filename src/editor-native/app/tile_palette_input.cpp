#include "editor-native/app/tile_palette_input.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/model/tileset_grid_geometry.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::EditorNative {

namespace {

constexpr double kDoubleClickSeconds = 0.35;
constexpr float  kDoubleClickSlopPx  = 4.f;

// Mouse position -> grid cell, clamped into the grid: an active marquee keeps
// tracking even when the cursor leaves the sheet or crosses a spacing gutter.
TilemapCellCoord clampedCellForMouse(const TilesetGridGeometry& geometry,
                                     const Rectangle& dest, float mouseX, float mouseY) {
    const float scaleX = dest.width / static_cast<float>(geometry.imageWidth);
    const float scaleY = dest.height / static_cast<float>(geometry.imageHeight);
    const int px = static_cast<int>(std::floor((mouseX - dest.x) / scaleX));
    const int py = static_cast<int>(std::floor((mouseY - dest.y) / scaleY));
    const int stepX = geometry.tileWidth + geometry.spacingX;
    const int stepY = geometry.tileHeight + geometry.spacingY;
    TilemapCellCoord cell;
    cell.cellX = std::clamp((px - geometry.marginX) / stepX, 0, geometry.columns - 1);
    cell.cellY = std::clamp((py - geometry.marginY) / stepY, 0, geometry.rows - 1);
    if (px < geometry.marginX) cell.cellX = 0;
    if (py < geometry.marginY) cell.cellY = 0;
    return cell;
}

// The committed stamp's source region mapped to screen space, for
// pan-into-view. Falls back to the primary tile's own rect when the stamp has
// no grid provenance.
std::optional<Rectangle> stampScreenRegion(const TilemapTileStamp& stamp,
                                           const TilesetAsset& tileset,
                                           const TilesetGridGeometry& geometry,
                                           const TextureResource& texture,
                                           const Rectangle& dest) {
    TilesetGridRect span;
    if (stamp.sourceColumn >= 0 && stamp.sourceRow >= 0) {
        const int lastCol = std::min(stamp.sourceColumn + stamp.width - 1, geometry.columns - 1);
        const int lastRow = std::min(stamp.sourceRow + stamp.height - 1, geometry.rows - 1);
        if (lastCol < stamp.sourceColumn || lastRow < stamp.sourceRow) return std::nullopt;
        const TilesetGridRect first = tilesetSourceRectForGridCell(
            geometry, TilemapCellCoord{stamp.sourceColumn, stamp.sourceRow});
        const TilesetGridRect last = tilesetSourceRectForGridCell(
            geometry, TilemapCellCoord{lastCol, lastRow});
        span.x = first.x;
        span.y = first.y;
        span.width  = (last.x + last.width) - first.x;
        span.height = (last.y + last.height) - first.y;
    } else {
        const std::optional<TileId> primary = stampPrimaryTileId(stamp);
        if (!primary) return std::nullopt;
        const TileDefinition* tile = nullptr;
        for (const TileDefinition& t : tileset.tiles) {
            if (t.id == *primary) { tile = &t; break; }
        }
        if (!tile) return std::nullopt;
        span = TilesetGridRect{tile->x, tile->y, tile->width, tile->height};
    }
    const float scaleX = dest.width / static_cast<float>(texture.texture.width);
    const float scaleY = dest.height / static_cast<float>(texture.texture.height);
    return Rectangle{
        dest.x + static_cast<float>(span.x) * scaleX,
        dest.y + static_cast<float>(span.y) * scaleY,
        static_cast<float>(span.width) * scaleX,
        static_cast<float>(span.height) * scaleY,
    };
}

// Pans the committed stamp's region into the hole when the selection changed
// from outside the palette (Picker). One shot per stamp change; a user's own
// marquee always ends inside the hole, so this effectively fires for picks.
void panStampIntoView(EditorCoordinator& coordinator, const TilesetAsset& tileset,
                      const ViewportRect& holeRect, const TextureResource& texture,
                      TilePaletteInputState& state) {
    const std::optional<TilemapTileStamp>& stamp = coordinator.state().tilemapEditor.stamp;
    const bool changed = stamp.has_value() != state.lastPannedStamp.has_value()
        || (stamp && !sameTileStamp(*stamp, *state.lastPannedStamp));
    if (!changed) return;
    state.lastPannedStamp = stamp;
    if (!stamp || stamp->sourceTilesetAssetId != tileset.assetId) return;

    const std::optional<TilesetGridGeometry> geometry = computeTilesetGridGeometry(
        tileset.slicing, texture.texture.width, texture.texture.height);
    if (!geometry) return;
    const auto viewIt = coordinator.state().tilemapEditor.paletteViews.find(tileset.assetId);
    const TilePaletteViewState view = viewIt != coordinator.state().tilemapEditor.paletteViews.end()
        ? viewIt->second : TilePaletteViewState{};
    const Rectangle dest = tilePaletteSheetDestination(
        texture, holeRect, view, tileset.slicing.tileWidth, tileset.slicing.tileHeight);
    const std::optional<Rectangle> region =
        stampScreenRegion(*stamp, tileset, *geometry, texture, dest);
    if (!region) return;

    // Shift by the smallest delta that brings the region inside the hole;
    // a region larger than the hole aligns its top-left corner.
    const float holeX0 = static_cast<float>(holeRect.x);
    const float holeY0 = static_cast<float>(holeRect.y);
    const float holeX1 = static_cast<float>(holeRect.x + holeRect.width);
    const float holeY1 = static_cast<float>(holeRect.y + holeRect.height);
    float dx = 0.f;
    float dy = 0.f;
    if (region->width >= holeX1 - holeX0 || region->x < holeX0) dx = holeX0 - region->x;
    else if (region->x + region->width > holeX1) dx = holeX1 - (region->x + region->width);
    if (region->height >= holeY1 - holeY0 || region->y < holeY0) dy = holeY0 - region->y;
    else if (region->y + region->height > holeY1) dy = holeY1 - (region->y + region->height);
    if (dx != 0.f || dy != 0.f) {
        coordinator.apply(PanTilePaletteIntent{tileset.assetId, Vec2{dx, dy}});
    }
}

} // namespace

void routeTilePaletteInput(EditorCoordinator& coordinator,
                           const TilesetAsset& tileset,
                           const ViewportRect& holeRect,
                           const ViewportRect& clipRect,
                           const RmlInputResult& rml,
                           const TilesetEmptyMaskView& emptyMask,
                           const TextureResource* texture,
                           TilePaletteInputState& state) {
    if (!texture || !texture->loaded || !holeRect.valid() || !clipRect.valid()) {
        cancelTilePaletteGesture(state);
        return;
    }
    // The gesture never survives its context: focus loss, a text field
    // grabbing the keyboard, Escape, or the tileset changing under it all
    // cancel with zero Commands (never an implicit commit).
    if (!IsWindowFocused() || rml.textFocus) {
        cancelTilePaletteGesture(state);
        return;
    }
    if (state.marquee.active
        && (IsKeyPressed(KEY_ESCAPE) || state.marqueeTilesetAssetId != tileset.assetId)) {
        cancelTilePaletteGesture(state);
    }

    panStampIntoView(coordinator, tileset, holeRect, *texture, state);

    const float mouseX = static_cast<float>(GetMouseX());
    const float mouseY = static_cast<float>(GetMouseY());
    const bool mouseInClip = clipRect.contains(GetMouseX(), GetMouseY());
    const std::optional<TilesetGridGeometry> geometry = computeTilesetGridGeometry(
        tileset.slicing, texture->texture.width, texture->texture.height);

    const auto currentView = [&]() {
        const auto it = coordinator.state().tilemapEditor.paletteViews.find(tileset.assetId);
        return it != coordinator.state().tilemapEditor.paletteViews.end()
            ? it->second : TilePaletteViewState{};
    };

    // Ctrl+wheel: cursor-anchored zoom (the palette claims the wheel via
    // RmlInputSuppression only under Ctrl; plain wheel scrolls the panel).
    const bool ctrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const float wheel = GetMouseWheelMove();
    if (ctrlDown && wheel != 0.0f && mouseInClip) {
        const TilePaletteViewState before = currentView();
        const Rectangle destBefore = tilePaletteSheetDestination(
            *texture, holeRect, before,
            tileset.slicing.tileWidth, tileset.slicing.tileHeight);
        const float scaleBefore = destBefore.width / static_cast<float>(texture->texture.width);
        coordinator.apply(SetTilePaletteZoomIntent{
            tileset.assetId, before.zoom * (1.0f + wheel * 0.1f)});
        const TilePaletteViewState zoomed = currentView();
        const Rectangle destAfter = tilePaletteSheetDestination(
            *texture, holeRect, zoomed,
            tileset.slicing.tileWidth, tileset.slicing.tileHeight);
        const float scaleAfter = destAfter.width / static_cast<float>(texture->texture.width);
        const float u = (mouseX - destBefore.x) / scaleBefore;
        const float v = (mouseY - destBefore.y) / scaleBefore;
        coordinator.apply(PanTilePaletteIntent{tileset.assetId, Vec2{
            mouseX - (destAfter.x + u * scaleAfter),
            mouseY - (destAfter.y + v * scaleAfter)}});
    }

    // Pan: middle-mouse, or Space + left-mouse. A pan is never a marquee.
    const bool spacePan = IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if ((IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || spacePan) && mouseInClip) {
        cancelTilePaletteGesture(state);
        const Vector2 d = GetMouseDelta();
        if (d.x != 0.f || d.y != 0.f) {
            coordinator.apply(PanTilePaletteIntent{tileset.assetId, Vec2{d.x, d.y}});
        }
        return;
    }

    if (!geometry) return;
    const Rectangle dest = tilePaletteSheetDestination(
        *texture, holeRect, currentView(),
        tileset.slicing.tileWidth, tileset.slicing.tileHeight);

    // Marquee start: left press on a grid cell (gutters and margins miss).
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseInClip && !state.marquee.active) {
        // App-side double-click: the hole carries no RmlUi dbl-action.
        const double now = GetTime();
        const bool doubleClick = (now - state.lastClickTime) <= kDoubleClickSeconds
            && std::fabs(mouseX - state.lastClickPos.x) <= kDoubleClickSlopPx
            && std::fabs(mouseY - state.lastClickPos.y) <= kDoubleClickSlopPx;
        state.lastClickTime = now;
        state.lastClickPos = Vector2{mouseX, mouseY};
        if (doubleClick) {
            state.lastClickTime = 0.0;   // a triple click is not two doubles
            coordinator.apply(OpenTilesetEditorIntent{tileset.assetId});
            return;
        }
        const float scaleX = dest.width / static_cast<float>(texture->texture.width);
        const float scaleY = dest.height / static_cast<float>(texture->texture.height);
        if (mouseX >= dest.x && mouseX < dest.x + dest.width
            && mouseY >= dest.y && mouseY < dest.y + dest.height) {
            const int px = static_cast<int>((mouseX - dest.x) / scaleX);
            const int py = static_cast<int>((mouseY - dest.y) / scaleY);
            if (const std::optional<TilemapCellCoord> cell =
                    tilesetGridCellAtSheetPixel(*geometry, px, py)) {
                state.marquee.active = true;
                state.marqueeTilesetAssetId = tileset.assetId;
                state.marquee.col0 = state.marquee.col1 = cell->cellX;
                state.marquee.row0 = state.marquee.row1 = cell->cellY;
            }
        }
        return;
    }

    if (!state.marquee.active) return;

    // Marquee update: keeps tracking while held, clamped into the grid even
    // when the cursor leaves the hole.
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        const TilemapCellCoord cell = clampedCellForMouse(*geometry, dest, mouseX, mouseY);
        state.marquee.col1 = cell.cellX;
        state.marquee.row1 = cell.cellY;
        return;
    }

    // Marquee release: build and commit the stamp. Empty-mask policy: Ready
    // selects with empty tiles as holes; Failed selects unfiltered (a decode
    // error must not brick the palette); Unavailable/Scanning blocks the
    // commit - a tile must never be selectable now and unselectable a frame
    // later when the scan lands.
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        const TilePaletteMarquee marquee = state.marquee;
        cancelTilePaletteGesture(state);
        const bool maskConclusive = emptyMask.status == TilesetEmptyMaskStatus::Ready
            || emptyMask.status == TilesetEmptyMaskStatus::Failed;
        if (!maskConclusive) return;
        const std::vector<bool>* flags =
            emptyMask.status == TilesetEmptyMaskStatus::Ready ? emptyMask.flags : nullptr;
        if (const std::optional<TilemapTileStamp> stamp = buildTileStampFromRegion(
                tileset, *geometry, marquee.col0, marquee.row0, marquee.col1, marquee.row1,
                flags)) {
            coordinator.apply(SelectPaintStampIntent{*stamp});
            state.lastPannedStamp = coordinator.state().tilemapEditor.stamp;
        }
    }
}

} // namespace ArtCade::EditorNative
