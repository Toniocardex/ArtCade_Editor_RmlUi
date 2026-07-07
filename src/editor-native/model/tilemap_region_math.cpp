#include "editor-native/model/tilemap_region_math.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace {
constexpr std::int64_t kLimit = static_cast<std::int64_t>(kMaxTilePaintOperationCells);

std::string tooLargeMessage(const char* what) {
    return std::string(what) + " exceeds the " + std::to_string(kMaxTilePaintOperationCells)
         + "-cell operation limit";
}

void addIfChanged(std::vector<TilemapCellChange>& changes, const TilemapComponent& component,
                  TilemapCellCoord cell, const TilemapCell& replacement) {
    const TilemapCell before = readTilemapCell(component, cell);
    if (before == replacement) return;   // no-op cell: same discipline as normalizePaintStrokeChanges
    changes.push_back(TilemapCellChange{cell, before, replacement});
}
} // namespace

std::optional<TilemapCellCoord> tryOffsetCell(TilemapCellCoord cell, int dx, int dy) {
    constexpr int kMax = std::numeric_limits<int>::max();
    constexpr int kMin = std::numeric_limits<int>::min();
    if ((dx > 0 && cell.cellX > kMax - dx) || (dx < 0 && cell.cellX < kMin - dx)) return std::nullopt;
    if ((dy > 0 && cell.cellY > kMax - dy) || (dy < 0 && cell.cellY < kMin - dy)) return std::nullopt;
    return TilemapCellCoord{cell.cellX + dx, cell.cellY + dy};
}

TileRegionBuildResult rectangleFillChanges(const TilemapComponent& component,
                                           TilemapCellCoord corner1, TilemapCellCoord corner2,
                                           TilemapCell replacement) {
    const int minX = std::min(corner1.cellX, corner2.cellX);
    const int maxX = std::max(corner1.cellX, corner2.cellX);
    const int minY = std::min(corner1.cellY, corner2.cellY);
    const int maxY = std::max(corner1.cellY, corner2.cellY);
    // Widened before any subtraction: the difference of two ints always fits
    // in int64_t, so this itself cannot overflow regardless of how far apart
    // the corners are.
    const std::int64_t width  = static_cast<std::int64_t>(maxX) - static_cast<std::int64_t>(minX) + 1;
    const std::int64_t height = static_cast<std::int64_t>(maxY) - static_cast<std::int64_t>(minY) + 1;

    // Reject on either dimension alone before ever multiplying them: once
    // both are known to be <= kLimit (65536), their product is bounded by
    // ~4.3e9, far inside int64_t range, so the multiplication below is safe.
    if (width > kLimit || height > kLimit || width * height > kLimit) {
        return TileRegionBuildResult{{}, tooLargeMessage("Rectangle")};
    }

    std::vector<TilemapCellChange> changes;
    changes.reserve(static_cast<std::size_t>(width * height));
    for (std::int64_t y = minY; y <= maxY; ++y) {
        for (std::int64_t x = minX; x <= maxX; ++x) {
            addIfChanged(changes, component, TilemapCellCoord{static_cast<int>(x), static_cast<int>(y)},
                        replacement);
        }
    }
    return TileRegionBuildResult{std::move(changes), std::nullopt};
}

TileRegionBuildResult rectangleOutlineChanges(const TilemapComponent& component,
                                              TilemapCellCoord corner1, TilemapCellCoord corner2,
                                              TilemapCell replacement) {
    const int minX = std::min(corner1.cellX, corner2.cellX);
    const int maxX = std::max(corner1.cellX, corner2.cellX);
    const int minY = std::min(corner1.cellY, corner2.cellY);
    const int maxY = std::max(corner1.cellY, corner2.cellY);
    const std::int64_t width  = static_cast<std::int64_t>(maxX) - static_cast<std::int64_t>(minX) + 1;
    const std::int64_t height = static_cast<std::int64_t>(maxY) - static_cast<std::int64_t>(minY) + 1;

    if (width > kLimit || height > kLimit) {
        return TileRegionBuildResult{{}, tooLargeMessage("Rectangle")};
    }

    // A 1-wide or 1-tall rectangle has no interior: every cell is border, so
    // the outline degenerates to the full strip (same shape as the filled
    // rectangle, just computed here to keep one entry point per tool).
    const bool degenerate = (width == 1 || height == 1);
    const std::int64_t perimeterCells = degenerate ? width * height : 2 * width + 2 * height - 4;
    if (perimeterCells > kLimit) {
        return TileRegionBuildResult{{}, tooLargeMessage("Rectangle outline")};
    }

    std::vector<TilemapCellChange> changes;
    changes.reserve(static_cast<std::size_t>(perimeterCells));

    if (degenerate) {
        for (std::int64_t y = minY; y <= maxY; ++y)
            for (std::int64_t x = minX; x <= maxX; ++x)
                addIfChanged(changes, component, TilemapCellCoord{static_cast<int>(x), static_cast<int>(y)},
                            replacement);
        return TileRegionBuildResult{std::move(changes), std::nullopt};
    }

    // General case (width >= 2, height >= 2): top and bottom edges in full,
    // then the two side edges excluding the corners already emitted above -
    // O(perimeter), the interior is never visited.
    for (std::int64_t x = minX; x <= maxX; ++x) {
        addIfChanged(changes, component, TilemapCellCoord{static_cast<int>(x), static_cast<int>(minY)},
                    replacement);
        addIfChanged(changes, component, TilemapCellCoord{static_cast<int>(x), static_cast<int>(maxY)},
                    replacement);
    }
    for (std::int64_t y = minY + 1; y <= maxY - 1; ++y) {
        addIfChanged(changes, component, TilemapCellCoord{static_cast<int>(minX), static_cast<int>(y)},
                    replacement);
        addIfChanged(changes, component, TilemapCellCoord{static_cast<int>(maxX), static_cast<int>(y)},
                    replacement);
    }
    return TileRegionBuildResult{std::move(changes), std::nullopt};
}

TileRegionBuildResult floodFillChanges(const TilemapComponent& component,
                                       TilemapCellCoord origin, TilemapCell replacement) {
    const TilemapCell target = readTilemapCell(component, origin);
    if (target == replacement) {
        return TileRegionBuildResult{};   // already the target value: no traversal, no warning
    }

    std::vector<TilemapCellChange> changes;
    std::unordered_set<std::int64_t> visited;
    std::vector<TilemapCellCoord> stack{origin};
    visited.insert(packTilemapCellCoord(origin));

    static constexpr int kOffsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

    while (!stack.empty()) {
        const TilemapCellCoord cell = stack.back();
        stack.pop_back();
        changes.push_back(TilemapCellChange{cell, target, replacement});
        if (changes.size() > kMaxTilePaintOperationCells) {
            return TileRegionBuildResult{
                {}, "Fill cancelled: the connected region is open or exceeds the operation limit"};
        }
        for (const auto& offset : kOffsets) {
            const std::optional<TilemapCellCoord> neighbor = tryOffsetCell(cell, offset[0], offset[1]);
            if (!neighbor) continue;   // int32 coordinate edge: a hard boundary, not UB
            if (!visited.insert(packTilemapCellCoord(*neighbor)).second) continue;
            if (!(readTilemapCell(component, *neighbor) == target)) continue;   // outside the region
            stack.push_back(*neighbor);
        }
    }
    return TileRegionBuildResult{std::move(changes), std::nullopt};
}

} // namespace ArtCade::EditorNative
