#pragma once

namespace ArtCade::EditorNative {

// Floor division/modulo for a cell coordinate against a chunk size. C++'s
// built-in `/` and `%` truncate toward zero, which is wrong for a grid that
// extends into negative coordinates: e.g. with chunkSize=16, cell -1 must
// land in chunk -1 at local index 15, but -1 / 16 == 0 and -1 % 16 == -1.
int floorDivChunk(int cell, int chunkSize);
int floorModChunk(int cell, int chunkSize);

struct TilemapChunkCoord {
    int chunkX = 0;
    int chunkY = 0;
};

struct TilemapLocalCoord {
    int localX = 0;   // in [0, chunkSize)
    int localY = 0;
};

// Combines floorDivChunk on both axes.
TilemapChunkCoord cellToChunkCoord(int cellX, int cellY, int chunkSize);

// Combines floorModChunk on both axes.
TilemapLocalCoord cellToLocalCoord(int cellX, int cellY, int chunkSize);

// Inverse of cellToChunkCoord/cellToLocalCoord: reconstructs the absolute
// cell coordinate from a chunk coordinate and a local coordinate within it.
int chunkAndLocalToCellX(int chunkX, int localX, int chunkSize);
int chunkAndLocalToCellY(int chunkY, int localY, int chunkSize);

} // namespace ArtCade::EditorNative
