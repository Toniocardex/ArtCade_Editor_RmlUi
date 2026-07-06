#include "editor-native/model/tilemap_chunk_math.h"

namespace ArtCade::EditorNative {

int floorDivChunk(int cell, int chunkSize) {
    const int quotient  = cell / chunkSize;
    const int remainder = cell % chunkSize;
    return (remainder != 0 && ((remainder < 0) != (chunkSize < 0))) ? quotient - 1 : quotient;
}

int floorModChunk(int cell, int chunkSize) {
    const int remainder = cell % chunkSize;
    return (remainder != 0 && ((remainder < 0) != (chunkSize < 0))) ? remainder + chunkSize : remainder;
}

TilemapChunkCoord cellToChunkCoord(int cellX, int cellY, int chunkSize) {
    return TilemapChunkCoord{floorDivChunk(cellX, chunkSize), floorDivChunk(cellY, chunkSize)};
}

TilemapLocalCoord cellToLocalCoord(int cellX, int cellY, int chunkSize) {
    return TilemapLocalCoord{floorModChunk(cellX, chunkSize), floorModChunk(cellY, chunkSize)};
}

int chunkAndLocalToCellX(int chunkX, int localX, int chunkSize) {
    return chunkX * chunkSize + localX;
}

int chunkAndLocalToCellY(int chunkY, int localY, int chunkSize) {
    return chunkY * chunkSize + localY;
}

} // namespace ArtCade::EditorNative
