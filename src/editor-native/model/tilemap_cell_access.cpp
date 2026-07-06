#include "editor-native/model/tilemap_cell_access.h"

#include <algorithm>

namespace ArtCade::EditorNative {

namespace {
TilemapChunk* findChunk(TilemapComponent& component, TilemapChunkCoord coord) {
    for (TilemapChunk& chunk : component.chunks) {
        if (chunk.chunkX == coord.chunkX && chunk.chunkY == coord.chunkY) return &chunk;
    }
    return nullptr;
}

const TilemapChunk* findChunk(const TilemapComponent& component, TilemapChunkCoord coord) {
    for (const TilemapChunk& chunk : component.chunks) {
        if (chunk.chunkX == coord.chunkX && chunk.chunkY == coord.chunkY) return &chunk;
    }
    return nullptr;
}

bool chunkIsEmpty(const TilemapChunk& chunk) {
    for (const TilemapCell& cell : chunk.cells) {
        if (cell.has_value()) return false;
    }
    return true;
}
} // namespace

TilemapCell readTilemapCell(const TilemapComponent& component, TilemapCellCoord cell) {
    const TilemapChunkCoord chunkCoord =
        cellToChunkCoord(cell.cellX, cell.cellY, component.chunkSize);
    const TilemapChunk* chunk = findChunk(component, chunkCoord);
    if (!chunk) return std::nullopt;
    const TilemapLocalCoord local = cellToLocalCoord(cell.cellX, cell.cellY, component.chunkSize);
    const std::size_t index =
        static_cast<std::size_t>(local.localY) * static_cast<std::size_t>(component.chunkSize)
        + static_cast<std::size_t>(local.localX);
    if (index >= chunk->cells.size()) return std::nullopt;
    return chunk->cells[index];
}

void writeTilemapCell(TilemapComponent& component, TilemapCellCoord cell, TilemapCell value) {
    const TilemapChunkCoord chunkCoord =
        cellToChunkCoord(cell.cellX, cell.cellY, component.chunkSize);
    TilemapChunk* chunk = findChunk(component, chunkCoord);
    if (!chunk) {
        TilemapChunk fresh;
        fresh.chunkX = chunkCoord.chunkX;
        fresh.chunkY = chunkCoord.chunkY;
        fresh.cells.assign(
            static_cast<std::size_t>(component.chunkSize) * static_cast<std::size_t>(component.chunkSize),
            std::nullopt);
        component.chunks.push_back(std::move(fresh));
        chunk = &component.chunks.back();
    }
    const TilemapLocalCoord local = cellToLocalCoord(cell.cellX, cell.cellY, component.chunkSize);
    const std::size_t index =
        static_cast<std::size_t>(local.localY) * static_cast<std::size_t>(component.chunkSize)
        + static_cast<std::size_t>(local.localX);
    chunk->cells[index] = std::move(value);
}

void pruneEmptyChunks(TilemapComponent& component) {
    component.chunks.erase(
        std::remove_if(component.chunks.begin(), component.chunks.end(), chunkIsEmpty),
        component.chunks.end());
}

} // namespace ArtCade::EditorNative
