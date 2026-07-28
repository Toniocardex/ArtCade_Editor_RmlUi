#include "app/render/tilemap_component_resolve.h"

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace ArtCade;
using namespace ArtCade::AppRender;

namespace {

void expect(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", message);
}

bool hasIssue(
    const TilemapComponentResolveResult& result,
    TilemapResolveIssueCode code) {
    for (const TilemapResolveIssue& issue : result.issues) {
        if (issue.code == code) return true;
    }
    return false;
}

TilesetAsset makeTileset() {
    TilesetAsset tileset;
    tileset.assetId = "tiles";
    tileset.imageAssetId = "atlas";
    tileset.tiles = {
        {"grass", 0, 0, 8, 8},
        {"stone", 8, 0, 8, 8},
        {"bad", -1, 0, 8, 8},
        {"dot", 16, 0, 1, 1},
    };
    return tileset;
}

TilemapComponent makeComponent() {
    TilemapComponent component;
    component.tilesetAssetId = "tiles";
    component.cellSize = {16.f, 24.f};
    component.chunkSize = 2;
    return component;
}

} // namespace

int main() {
    const TilesetAsset tileset = makeTileset();

    {
        TilemapComponent component = makeComponent();
        TilemapChunk chunk;
        chunk.chunkX = -1;
        chunk.chunkY = 2;
        chunk.cells.resize(4);
        chunk.cells[0] = TilemapCellValue{"grass", TileTransformFlags::None};
        chunk.cells[3] = TilemapCellValue{"stone", TileTransformFlags::None};
        component.chunks.push_back(std::move(chunk));

        const auto result = resolveTilemapComponent(component, tileset);
        expect(result.draw.has_value(), "populated chunk resolves");
        expect(result.draw->imageAssetId == "atlas", "image asset resolved");
        expect(result.draw->cellSize.x == 16.f && result.draw->cellSize.y == 24.f,
               "non-square cell size preserved");
        expect(result.draw->cells.size() == 2, "two valid cells resolved");
        expect(result.draw->cells[0].cellX == -2 && result.draw->cells[0].cellY == 4,
               "negative chunk coordinate resolved");
        expect(result.draw->cells[1].cellX == -1 && result.draw->cells[1].cellY == 5,
               "row-major local coordinates resolved");
        expect(result.draw->regions.size() == result.draw->cells.size(),
               "cell metadata and draw regions stay aligned");
        expect(result.draw->regions[0].srcX == 0.5f
                   && result.draw->regions[0].srcW == 7.f,
               "half-texel inset applied");
        expect(result.draw->regions[0].dstX == -32.f
                   && result.draw->regions[0].dstY == 96.f
                   && result.draw->regions[0].dstW == 16.f
                   && result.draw->regions[0].dstH == 24.f,
               "entity-local destination cached once");
        expect(result.draw->regions[1].dstX == -16.f
                   && result.draw->regions[1].dstY == 120.f,
               "row-major destination remains local");
    }

    {
        TilemapComponent component = makeComponent();
        TilemapChunk chunk;
        chunk.cells.resize(4);
        chunk.cells[0] = TilemapCellValue{"missing", TileTransformFlags::None};
        chunk.cells[1] = TilemapCellValue{"bad", TileTransformFlags::None};
        chunk.cells[2] = TilemapCellValue{"grass", TileTransformFlags::FlipX};
        chunk.cells[3] = TilemapCellValue{"dot", TileTransformFlags::None};
        component.chunks.push_back(std::move(chunk));

        const auto result = resolveTilemapComponent(component, tileset);
        expect(result.draw.has_value() && result.draw->cells.size() == 1,
               "bad cells are skipped while valid cells render");
        expect(result.draw->regions[0].srcW == 1.f
                   && result.draw->regions[0].srcH == 1.f,
               "one-pixel source remains uninset");
        expect(hasIssue(result, TilemapResolveIssueCode::MissingTileDefinition),
               "missing tile diagnosed");
        expect(hasIssue(result, TilemapResolveIssueCode::InvalidTileDefinition),
               "invalid tile diagnosed");
        expect(hasIssue(result, TilemapResolveIssueCode::UnsupportedCellTransform),
               "unsupported transform diagnosed");
    }

    {
        TilemapComponent component = makeComponent();
        component.chunkSize = 0;
        const auto result = resolveTilemapComponent(component, tileset);
        expect(!result.draw.has_value()
                   && hasIssue(result, TilemapResolveIssueCode::InvalidChunkSize),
               "zero chunk size rejects safely");
    }

    {
        TilemapComponent component = makeComponent();
        component.cellSize.x = -1.f;
        const auto result = resolveTilemapComponent(component, tileset);
        expect(!result.draw.has_value()
                   && hasIssue(result, TilemapResolveIssueCode::InvalidCellSize),
               "invalid cell size rejects safely");
    }

    {
        TilemapComponent component = makeComponent();
        component.cellSize.y = std::numeric_limits<float>::quiet_NaN();
        const auto result = resolveTilemapComponent(component, tileset);
        expect(!result.draw.has_value()
                   && hasIssue(result, TilemapResolveIssueCode::InvalidCellSize),
               "non-finite cell size rejects safely");
    }

    {
        TilemapComponent component = makeComponent();
        TilesetAsset wrongTileset = tileset;
        wrongTileset.assetId = "other-tiles";
        const auto result = resolveTilemapComponent(component, wrongTileset);
        expect(!result.draw.has_value()
                   && hasIssue(result, TilemapResolveIssueCode::TilesetAssetMismatch),
               "tileset id mismatch rejects component");
    }

    {
        TilemapComponent component = makeComponent();
        TilesetAsset noImage = tileset;
        noImage.imageAssetId.clear();
        const auto result = resolveTilemapComponent(component, noImage);
        expect(!result.draw.has_value()
                   && hasIssue(result, TilemapResolveIssueCode::EmptyImageAssetId),
               "empty tileset image rejects component");
    }

    {
        TilemapComponent component = makeComponent();
        TilemapChunk chunk;
        chunk.chunkX = INT_MAX;
        chunk.cells.resize(2);
        chunk.cells[1] = TilemapCellValue{"grass", TileTransformFlags::None};
        component.chunks.push_back(std::move(chunk));
        const auto result = resolveTilemapComponent(component, tileset);
        expect(result.draw.has_value() && result.draw->cells.empty(),
               "overflowing coordinate is skipped");
        expect(hasIssue(result, TilemapResolveIssueCode::InvalidChunkCoordinate),
               "overflowing coordinate diagnosed");
    }

    {
        TilemapComponent component = makeComponent();
        component.chunkSize = 1;
        TilemapChunk chunk;
        chunk.cells.resize(2);
        chunk.cells[0] = TilemapCellValue{"grass", TileTransformFlags::None};
        chunk.cells[1] = TilemapCellValue{"stone", TileTransformFlags::None};
        component.chunks.push_back(std::move(chunk));
        const auto result = resolveTilemapComponent(component, tileset);
        expect(result.draw.has_value() && result.draw->cells.size() == 1,
               "oversized chunk does not spill into next row");
    }

    {
        TilemapComponent component = makeComponent();
        const auto result = resolveTilemapComponent(component, tileset);
        expect(result.draw.has_value() && result.draw->cells.empty(),
               "empty tilemap remains a valid empty draw");
    }

    for (const int side : {16, 64, 128}) {
        TilemapComponent component = makeComponent();
        component.chunkSize = side;
        TilemapChunk chunk;
        chunk.cells.resize(
            static_cast<std::size_t>(side) * static_cast<std::size_t>(side),
            TilemapCellValue{"grass", TileTransformFlags::None});
        component.chunks.push_back(std::move(chunk));

        const auto result = resolveTilemapComponent(component, tileset);
        const std::size_t expected =
            static_cast<std::size_t>(side) * static_cast<std::size_t>(side);
        expect(result.draw.has_value() && result.draw->cells.size() == expected,
               side == 16 ? "256-cell tilemap resolves once"
                 : side == 64 ? "4096-cell tilemap resolves once"
                              : "16384-cell tilemap resolves once");
        if (result.draw && !result.draw->cells.empty()) {
            const auto& lastCell = result.draw->cells.back();
            const auto& lastRegion = result.draw->regions.back();
            expect(lastCell.cellX == side - 1 && lastCell.cellY == side - 1,
                   "large tilemap preserves deterministic cell order");
            expect(lastRegion.dstX
                           == static_cast<float>(side - 1) * component.cellSize.x
                       && lastRegion.dstY
                           == static_cast<float>(side - 1) * component.cellSize.y,
                   "large tilemap caches entity-local destinations");
        }
    }

    std::puts("tilemap_component_resolve_test: all passed");
    return 0;
}
