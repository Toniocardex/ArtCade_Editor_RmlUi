#include "editor-native/app/tile_palette_input.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/model/tile_palette_projection.h"
#include "editor-native/model/tileset_grid_geometry.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::EditorNative {

namespace {

constexpr double kDoubleClickSeconds = 0.35;
constexpr float  kDoubleClickSlopPx  = 4.f;
constexpr float  kScrollbarThickness = 8.f;
constexpr float  kScrollbarMargin = 2.f;
constexpr float  kWheelScrollPx = 48.f;

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

std::optional<Rectangle> stampScreenRegion(const TilemapTileStamp& stamp,
                                           const TilesetAsset& tileset,
                                           const TilesetGridGeometry& geometry,
                                           const TextureResource& texture,
                                           const Rectangle& dest) {
    const std::optional<TilePaletteSourceBounds> bounds =
        tilePaletteSelectionBounds(stamp, tileset, geometry);
    if (!bounds) return std::nullopt;
    const float scaleX = dest.width / static_cast<float>(texture.texture.width);
    const float scaleY = dest.height / static_cast<float>(texture.texture.height);
    return Rectangle{
        dest.x + static_cast<float>(bounds->x) * scaleX,
        dest.y + static_cast<float>(bounds->y) * scaleY,
        static_cast<float>(bounds->width) * scaleX,
        static_cast<float>(bounds->height) * scaleY,
    };
}

void applyClampedScroll(EditorCoordinator& coordinator, const AssetId& tilesetId,
                        const TilePaletteViewportProjection& proj, Vec2 scroll) {
    scroll = clampTilePaletteScrollOffset(
        scroll, proj.viewportWidth, proj.viewportHeight, proj.sheetWidth, proj.sheetHeight);
    coordinator.apply(SetTilePaletteScrollIntent{tilesetId, scroll});
}

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
    const TilePaletteViewportProjection proj =
        tilePaletteProjectionForHole(texture, holeRect, view);
    const Rectangle dest{proj.sheetX, proj.sheetY, proj.sheetWidth, proj.sheetHeight};
    const std::optional<Rectangle> region =
        stampScreenRegion(*stamp, tileset, *geometry, texture, dest);
    if (!region) return;

    const float holeX0 = proj.viewportX;
    const float holeY0 = proj.viewportY;
    const float holeX1 = proj.viewportX + proj.viewportWidth;
    const float holeY1 = proj.viewportY + proj.viewportHeight;
    float dx = 0.f;
    float dy = 0.f;
    if (region->width >= holeX1 - holeX0 || region->x < holeX0) dx = holeX0 - region->x;
    else if (region->x + region->width > holeX1) dx = holeX1 - (region->x + region->width);
    if (region->height >= holeY1 - holeY0 || region->y < holeY0) dy = holeY0 - region->y;
    else if (region->y + region->height > holeY1) dy = holeY1 - (region->y + region->height);
    if (dx != 0.f || dy != 0.f) {
        applyClampedScroll(coordinator, tileset.assetId, proj,
                           Vec2{view.scrollOffset.x + dx, view.scrollOffset.y + dy});
    }
}

void consumePendingFit(EditorCoordinator& coordinator, const TilesetAsset& tileset,
                       const ViewportRect& holeRect, const TextureResource& texture,
                       const TilesetEmptyMaskView& emptyMask) {
    auto& pending = coordinator.state().tilemapEditor.pendingPaletteFit;
    const bool fitting = pending && pending->tilesetAssetId == tileset.assetId;
    const auto viewIt = coordinator.state().tilemapEditor.paletteViews.find(tileset.assetId);
    const bool needsInit = viewIt == coordinator.state().tilemapEditor.paletteViews.end()
        || !viewIt->second.initialized;

    if (!fitting && !needsInit) return;

    const float vw = static_cast<float>(std::max(1, holeRect.width));
    const float vh = static_cast<float>(std::max(1, holeRect.height));
    const float tw = static_cast<float>(texture.texture.width);
    const float th = static_cast<float>(texture.texture.height);
    const std::optional<TilesetGridGeometry> geometry = computeTilesetGridGeometry(
        tileset.slicing, texture.texture.width, texture.texture.height);
    const std::vector<bool>* flags =
        emptyMask.status == TilesetEmptyMaskStatus::Ready ? emptyMask.flags : nullptr;
    const TilePaletteSourceBounds content =
        tilePaletteContentBounds(tileset, geometry ? &*geometry : nullptr, flags);
    const TilePaletteSourceBounds sheet{
        0, 0, texture.texture.width, texture.texture.height};

    TilePaletteViewState next;
    if (fitting) {
        std::optional<TilePaletteSourceBounds> selection;
        if (geometry && coordinator.state().tilemapEditor.stamp) {
            selection = tilePaletteSelectionBounds(
                *coordinator.state().tilemapEditor.stamp, tileset, *geometry);
        }
        next = makeFitTilePaletteView(
            pending->kind, vw, vh, tw, th,
            tileset.slicing.tileWidth, tileset.slicing.tileHeight,
            sheet, content, selection);
    } else {
        next = makeInitialTilePaletteView(
            vw, vh, tw, th, tileset.slicing.tileWidth, tileset.slicing.tileHeight, content);
    }

    coordinator.apply(SetTilePaletteViewIntent{
        tileset.assetId, next.textureScale, next.scrollOffset});
}

bool pointInRect(float x, float y, const Rectangle& r) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

struct ScrollbarHit {
    bool horizontal = false;
    bool vertical = false;
    Rectangle trackH{};
    Rectangle thumbH{};
    Rectangle trackV{};
    Rectangle thumbV{};
};

ScrollbarHit makeScrollbarHit(const TilePaletteViewportProjection& proj) {
    ScrollbarHit bar;
    bar.horizontal = proj.sheetWidth > proj.viewportWidth + 0.5f;
    bar.vertical = proj.sheetHeight > proj.viewportHeight + 0.5f;
    if (bar.horizontal) {
        const float trackW = proj.viewportWidth
            - (bar.vertical ? kScrollbarThickness + kScrollbarMargin : 0.f)
            - 2.f * kScrollbarMargin;
        bar.trackH = Rectangle{
            proj.viewportX + kScrollbarMargin,
            proj.viewportY + proj.viewportHeight - kScrollbarThickness - kScrollbarMargin,
            std::max(16.f, trackW), kScrollbarThickness};
        const float range = proj.sheetWidth - proj.viewportWidth;
        const float thumbW = std::max(18.f, bar.trackH.width * (proj.viewportWidth / proj.sheetWidth));
        const float t = range > 0.f ? (-proj.scrollOffset.x) / range : 0.f;
        bar.thumbH = Rectangle{
            bar.trackH.x + t * (bar.trackH.width - thumbW), bar.trackH.y,
            thumbW, bar.trackH.height};
    }
    if (bar.vertical) {
        const float trackH = proj.viewportHeight
            - (bar.horizontal ? kScrollbarThickness + kScrollbarMargin : 0.f)
            - 2.f * kScrollbarMargin;
        bar.trackV = Rectangle{
            proj.viewportX + proj.viewportWidth - kScrollbarThickness - kScrollbarMargin,
            proj.viewportY + kScrollbarMargin,
            kScrollbarThickness, std::max(16.f, trackH)};
        const float range = proj.sheetHeight - proj.viewportHeight;
        const float thumbH = std::max(18.f, bar.trackV.height * (proj.viewportHeight / proj.sheetHeight));
        const float t = range > 0.f ? (-proj.scrollOffset.y) / range : 0.f;
        bar.thumbV = Rectangle{
            bar.trackV.x, bar.trackV.y + t * (bar.trackV.height - thumbH),
            bar.trackV.width, thumbH};
    }
    return bar;
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
    if (!IsWindowFocused() || rml.textFocus) {
        cancelTilePaletteGesture(state);
        return;
    }
    if (state.marquee.active
        && (IsKeyPressed(KEY_ESCAPE) || state.marqueeTilesetAssetId != tileset.assetId)) {
        cancelTilePaletteGesture(state);
    }

    consumePendingFit(coordinator, tileset, holeRect, *texture, emptyMask);

    // Drop consumed pending fit without a second Request: SetTilePaletteViewIntent
    // clears matching pending in the coordinator (see apply).
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

    TilePaletteViewportProjection proj =
        tilePaletteProjectionForHole(*texture, holeRect, currentView());

    // Clamp scroll if a resize left it illegal (no auto-fit — only clamp).
    {
        const Vec2 clamped = clampTilePaletteScrollOffset(
            currentView().scrollOffset, proj.viewportWidth, proj.viewportHeight,
            proj.sheetWidth, proj.sheetHeight);
        if (clamped.x != currentView().scrollOffset.x
            || clamped.y != currentView().scrollOffset.y) {
            coordinator.apply(SetTilePaletteScrollIntent{tileset.assetId, clamped});
            proj = tilePaletteProjectionForHole(*texture, holeRect, currentView());
        }
    }

    const ScrollbarHit bars = makeScrollbarHit(proj);

    // Scrollbar drag.
    if (state.scrollDrag != TilePaletteInputState::ScrollDrag::None) {
        if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            state.scrollDrag = TilePaletteInputState::ScrollDrag::None;
        } else {
            Vec2 scroll = currentView().scrollOffset;
            if (state.scrollDrag == TilePaletteInputState::ScrollDrag::Horizontal
                && proj.sheetWidth > proj.viewportWidth) {
                const float range = proj.sheetWidth - proj.viewportWidth;
                const float travel = bars.trackH.width - bars.thumbH.width;
                if (travel > 0.f) {
                    const float dt = mouseX - state.scrollDragGrab;
                    scroll.x = state.scrollDragOrigin - (dt / travel) * range;
                }
            } else if (state.scrollDrag == TilePaletteInputState::ScrollDrag::Vertical
                       && proj.sheetHeight > proj.viewportHeight) {
                const float range = proj.sheetHeight - proj.viewportHeight;
                const float travel = bars.trackV.height - bars.thumbV.height;
                if (travel > 0.f) {
                    const float dt = mouseY - state.scrollDragGrab;
                    scroll.y = state.scrollDragOrigin - (dt / travel) * range;
                }
            }
            applyClampedScroll(coordinator, tileset.assetId, proj, scroll);
            return;
        }
    }

    const bool ctrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool shiftDown = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && mouseInClip) {
        if (ctrlDown) {
            const TilePaletteViewState before = currentView();
            const float scaleBefore = before.textureScale;
            const int step = tilePaletteNextZoomStep(scaleBefore, wheel > 0.f);
            const float scaleAfter = tilePaletteScaleForStep(step);
            if (scaleAfter == scaleBefore) {
                // Already at min/max integer step — nothing to do.
            } else {
                coordinator.apply(SetTilePaletteZoomIntent{tileset.assetId, scaleAfter});
                const TilePaletteViewState zoomed = currentView();
                const TilePaletteViewportProjection after =
                    tilePaletteProjectionForHole(*texture, holeRect, zoomed);
                const Vec2 remapped = remapTilePaletteScrollForZoom(
                    scaleBefore, scaleAfter, before.scrollOffset,
                    mouseX - proj.viewportX, mouseY - proj.viewportY);
                applyClampedScroll(coordinator, tileset.assetId, after, remapped);
            }
        } else {
            Vec2 scroll = currentView().scrollOffset;
            if (shiftDown) scroll.x += wheel * kWheelScrollPx;
            else scroll.y += wheel * kWheelScrollPx;
            applyClampedScroll(coordinator, tileset.assetId, proj, scroll);
        }
    }

    // Pan: middle-mouse, or Space + left-mouse.
    const bool spacePan = IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if ((IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || spacePan) && mouseInClip) {
        cancelTilePaletteGesture(state);
        const Vector2 d = GetMouseDelta();
        if (d.x != 0.f || d.y != 0.f) {
            applyClampedScroll(coordinator, tileset.assetId, proj,
                               Vec2{currentView().scrollOffset.x + d.x,
                                    currentView().scrollOffset.y + d.y});
        }
        return;
    }

    if (!geometry) return;
    const Rectangle dest{proj.sheetX, proj.sheetY, proj.sheetWidth, proj.sheetHeight};

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouseInClip && !state.marquee.active) {
        if (bars.horizontal && pointInRect(mouseX, mouseY, bars.thumbH)) {
            state.scrollDrag = TilePaletteInputState::ScrollDrag::Horizontal;
            state.scrollDragGrab = mouseX;
            state.scrollDragOrigin = currentView().scrollOffset.x;
            return;
        }
        if (bars.vertical && pointInRect(mouseX, mouseY, bars.thumbV)) {
            state.scrollDrag = TilePaletteInputState::ScrollDrag::Vertical;
            state.scrollDragGrab = mouseY;
            state.scrollDragOrigin = currentView().scrollOffset.y;
            return;
        }
        // Track clicks jump the thumb (never start a marquee under the bar).
        if (bars.horizontal && pointInRect(mouseX, mouseY, bars.trackH)) {
            const float range = proj.sheetWidth - proj.viewportWidth;
            const float travel = bars.trackH.width - bars.thumbH.width;
            if (range > 0.f && travel > 0.f) {
                const float t = std::clamp(
                    (mouseX - bars.trackH.x - bars.thumbH.width * 0.5f) / travel, 0.f, 1.f);
                Vec2 scroll = currentView().scrollOffset;
                scroll.x = -t * range;
                applyClampedScroll(coordinator, tileset.assetId, proj, scroll);
            }
            return;
        }
        if (bars.vertical && pointInRect(mouseX, mouseY, bars.trackV)) {
            const float range = proj.sheetHeight - proj.viewportHeight;
            const float travel = bars.trackV.height - bars.thumbV.height;
            if (range > 0.f && travel > 0.f) {
                const float t = std::clamp(
                    (mouseY - bars.trackV.y - bars.thumbV.height * 0.5f) / travel, 0.f, 1.f);
                Vec2 scroll = currentView().scrollOffset;
                scroll.y = -t * range;
                applyClampedScroll(coordinator, tileset.assetId, proj, scroll);
            }
            return;
        }

        const double now = GetTime();
        const bool doubleClick = (now - state.lastClickTime) <= kDoubleClickSeconds
            && std::fabs(mouseX - state.lastClickPos.x) <= kDoubleClickSlopPx
            && std::fabs(mouseY - state.lastClickPos.y) <= kDoubleClickSlopPx;
        state.lastClickTime = now;
        state.lastClickPos = Vector2{mouseX, mouseY};
        if (doubleClick) {
            state.lastClickTime = 0.0;
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

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        const TilemapCellCoord cell = clampedCellForMouse(*geometry, dest, mouseX, mouseY);
        state.marquee.col1 = cell.cellX;
        state.marquee.row1 = cell.cellY;
        return;
    }

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
