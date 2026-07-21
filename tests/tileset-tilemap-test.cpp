// tileset-tilemap-test.cpp — tileset + tilemap authoring / play suites.

#include "editor_core_test_harness.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/generated_sfx_generation_preflight.h"
#include "editor-native/app/hierarchy_actions.h"
#include "editor-native/app/input_routing.h"
#include "editor-native/app/inspector_commit.h"
#include "editor-native/app/new_project_transaction.h"
#include "editor-native/app/asset_import.h"
#include "editor-native/app/project_file.h"
#include "editor-native/app/project_load.h"
#include "editor-native/app/project_script_file_service.h"
#include "editor-native/app/script_syntax_validator.h"
#include "editor-native/app/script_asset_workflow.h"
#include "editor-native/app/unsaved_guard.h"
#include "editor-native/app/pending_edit.h"
#include "editor-native/app/inspector_actions.h"
#include "editor-native/commands/box_collider_commands.h"
#include "editor-native/commands/linear_mover_commands.h"
#include "editor-native/commands/top_down_controller_commands.h"
#include "editor-native/commands/platformer_controller_commands.h"
#include "editor-native/commands/project_commands.h"
#include "editor-native/commands/scene_layer_commands.h"
#include "editor-native/commands/image_asset_commands.h"
#include "editor-native/commands/audio_asset_commands.h"
#include "editor-native/commands/generated_sfx_commands.h"
#include "artcade/sfx/recipe_json.hpp"
#include "editor-native/commands/generated_sfx_macros.h"
#include "editor-native/commands/font_asset_commands.h"
#include "editor-native/commands/script_asset_commands.h"
#include "editor-native/commands/script_attachment_commands.h"
#include "editor-native/commands/tileset_commands.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/commands/sprite_animation_commands.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/scene_commands.h"
#include "editor-native/commands/sprite_presentation_commands.h"
#include "editor-native/model/project_io.h"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/play_session.h"
#include "editor-native/model/script_source_stamp.h"
#include "editor-native/model/box_collider_view.h"
#include "editor-native/model/box_collider_geometry.h"
#include "editor-native/model/scene_frame_snapshot.h"
#include "editor-native/model/sprite_animation_slicing.h"
#include "editor-native/model/tile_stamp.h"
#include "editor-native/model/tileset_empty_scan.h"
#include "editor-native/model/tileset_grid_geometry.h"
#include "editor-native/model/tile_palette_projection.h"
#include "editor-native/model/tileset_slicing.h"
#include "editor-native/model/tilemap_chunk_math.h"
#include "editor-native/model/tilemap_cell_access.h"
#include "editor-native/model/tilemap_stroke_math.h"
#include "editor-native/model/tilemap_region_math.h"
#include "editor-native/model/tilemap_validation.h"
#include "editor-native/model/tilemap_render_view.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/view/scene_grid.h"
#include "editor-native/view/scene_view_camera.h"
#include "script-runtime.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include "artcade/sfx/presets.hpp"
#include "artcade/sfx/synthesizer.hpp"

#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ArtCade;
using namespace ArtCade::EditorNative;
using namespace ArtCade::EditorNative::CoreTest;

int main() {

    // -- computeEmptyTileFlags: transparent, opaque and clipped tiles ----------
    {
        // 4x2 RGBA image: left 2x2 block opaque, right 2x2 block transparent.
        std::vector<std::uint8_t> rgba(4 * 2 * 4, 0);
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                rgba[(static_cast<std::size_t>(y) * 4 + x) * 4 + 3] = 255;

        const auto tile = [](int x, int y, int w, int h) {
            TileDefinition t;
            t.x = x; t.y = y; t.width = w; t.height = h;
            return t;
        };
        const std::vector<TileDefinition> tiles = {
            tile(0, 0, 2, 2),     // fully opaque
            tile(2, 0, 2, 2),     // fully transparent
            tile(1, 0, 2, 2),     // straddles both -> has visible pixels
            tile(4, 0, 2, 2),     // fully outside the image -> empty
            tile(3, 0, 2, 2),     // partially outside, in-image part transparent
        };
        const std::vector<bool> flags = computeEmptyTileFlags(rgba.data(), 4, 2, tiles);
        CHECK(flags.size() == tiles.size());
        CHECK(!flags[0]);
        CHECK(flags[1]);
        CHECK(!flags[2]);
        CHECK(flags[3]);
        CHECK(flags[4]);
    }

    // -- computeEmptyTileFlags: null/degenerate image marks nothing empty ------
    {
        const std::vector<TileDefinition> tiles(3);
        const std::vector<bool> nullFlags = computeEmptyTileFlags(nullptr, 4, 4, tiles);
        CHECK(nullFlags.size() == 3);
        CHECK(!nullFlags[0] && !nullFlags[1] && !nullFlags[2]);
        std::vector<std::uint8_t> rgba(16, 0);
        const std::vector<bool> zeroFlags = computeEmptyTileFlags(rgba.data(), 0, 4, tiles);
        CHECK(zeroFlags.size() == 3);
        CHECK(!zeroFlags[0]);
    }

    // ======================= tileset grid geometry =======================
    // -- Canonical geometry agrees with the slicer on every rect ---------------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32; slicing.tileHeight = 32;
        slicing.marginX = 4; slicing.marginY = 2;
        slicing.spacingX = 2; slicing.spacingY = 4;
        const auto geometry = computeTilesetGridGeometry(slicing, 140, 76);
        CHECK(geometry.has_value());
        CHECK(geometry->columns == 3);   // usable 132, step 34: 1 + (132-32)/34
        CHECK(geometry->rows == 2);      // usable 72,  step 36: 1 + (72-32)/36
        const std::vector<TileDefinition> tiles = tilesForSlicing(140, 76, slicing);
        CHECK(tiles.size() == static_cast<std::size_t>(geometry->columns) * geometry->rows);
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const int col = static_cast<int>(i) % geometry->columns;
            const int row = static_cast<int>(i) / geometry->columns;
            const TilesetGridRect rect =
                tilesetSourceRectForGridCell(*geometry, TilemapCellCoord{col, row});
            CHECK(tiles[i].x == rect.x && tiles[i].y == rect.y);
            CHECK(tiles[i].width == rect.width && tiles[i].height == rect.height);
            // Round trip: the authored rect maps back to exactly this cell.
            const auto cell = tilesetGridCellForTileRect(*geometry, tiles[i]);
            CHECK(cell.has_value() && cell->cellX == col && cell->cellY == row);
            CHECK(tilesetLinearIndexForGridCell(*geometry, *cell) == i);
        }
    }

    // -- Pixel hit-testing: tile interior, gutters, margins, outside -----------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 16; slicing.tileHeight = 16;
        slicing.marginX = 4; slicing.marginY = 4;
        slicing.spacingX = 2; slicing.spacingY = 2;
        const auto g = computeTilesetGridGeometry(slicing, 60, 60);   // 3x3 grid
        CHECK(g.has_value() && g->columns == 3 && g->rows == 3);
        const auto at = [&](int px, int py) { return tilesetGridCellAtSheetPixel(*g, px, py); };
        CHECK(at(4, 4).has_value() && at(4, 4)->cellX == 0 && at(4, 4)->cellY == 0);
        CHECK(at(19, 4).has_value());               // last pixel of tile 0
        CHECK(!at(20, 4).has_value());              // spacing gutter
        CHECK(at(22, 4)->cellX == 1);               // first pixel of tile 1
        CHECK(!at(0, 10).has_value());              // margin
        CHECK(!at(-1, -1).has_value());
        CHECK(at(55, 55)->cellX == 2 && at(55, 55)->cellY == 2);   // last tile pixel
        CHECK(!at(58, 30).has_value());             // past the last column
    }

    // -- Non-grid-aligned tile rect has no cell --------------------------------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32; slicing.tileHeight = 32;
        const auto g = computeTilesetGridGeometry(slicing, 128, 64);
        TileDefinition odd; odd.id = "odd"; odd.x = 5; odd.y = 0; odd.width = 32; odd.height = 32;
        CHECK(!tilesetGridCellForTileRect(*g, odd).has_value());
        TileDefinition wrongSize; wrongSize.x = 0; wrongSize.y = 0;
        wrongSize.width = 16; wrongSize.height = 32;
        CHECK(!tilesetGridCellForTileRect(*g, wrongSize).has_value());
        CHECK(!computeTilesetGridGeometry(slicing, 0, 64).has_value());
    }

    // ======================= tile stamps =======================
    // -- makeSingleTileStamp / stampIsValid / stampPrimaryTileId ---------------
    {
        const TilemapTileStamp one = makeSingleTileStamp("tiles-1", "tile-7", 2, 3);
        CHECK(stampIsValid(one));
        CHECK(one.sourceTilesetAssetId == "tiles-1");
        CHECK(one.sourceColumn == 2 && one.sourceRow == 3);
        CHECK(one.width == 1 && one.height == 1);
        CHECK(stampPrimaryTileId(one) == std::optional<TileId>{"tile-7"});

        TilemapTileStamp bad = one;
        bad.sourceTilesetAssetId.clear();
        CHECK(!stampIsValid(bad));                       // provenance required
        bad = one; bad.width = 0;
        CHECK(!stampIsValid(bad));
        bad = one; bad.tiles.clear();
        CHECK(!stampIsValid(bad));                       // slot count mismatch
        bad = one; bad.tiles = {std::nullopt};
        CHECK(!stampIsValid(bad));                       // all holes
        CHECK(!stampPrimaryTileId(bad).has_value());
        // width*height overflow: rejected via division before multiplication.
        bad = one; bad.width = 70000; bad.height = 70000;
        CHECK(!stampIsValid(bad));
        bad = one; bad.width = 257; bad.height = 257;    // 66049 > 65536 cap
        bad.tiles.assign(257u * 257u, std::optional<TileId>{"t"});
        CHECK(!stampIsValid(bad));
    }

    // -- stampPlacementsAt: full footprint per anchor (Brush semantics) --------
    {
        TilemapTileStamp stamp;
        stamp.sourceTilesetAssetId = "tiles-1";
        stamp.width = 2; stamp.height = 2;
        stamp.tiles = {TileId("a"), TileId("b"), TileId("c"), TileId("d")};
        const auto placements = stampPlacementsAt(stamp, {10, 20});
        CHECK(placements.size() == 4);                   // one click = four cells
        CHECK((placements[0].cell == TilemapCellCoord{10, 20} && placements[0].tileId == "a"));
        CHECK((placements[1].cell == TilemapCellCoord{11, 20} && placements[1].tileId == "b"));
        CHECK((placements[2].cell == TilemapCellCoord{10, 21} && placements[2].tileId == "c"));
        CHECK((placements[3].cell == TilemapCellCoord{11, 21} && placements[3].tileId == "d"));

        stamp.tiles[1] = std::nullopt;                   // hole
        CHECK(stampPlacementsAt(stamp, {10, 20}).size() == 3);

        // int32 edge: overflowing placements are dropped, not wrapped.
        constexpr int kMax = std::numeric_limits<int>::max();
        stamp.tiles[1] = TileId("b");
        CHECK(stampPlacementsAt(stamp, {kMax, 0}).size() == 2);   // right column lost
    }

    // -- stampPatternTileAt: Euclidean modulo (Rectangle/Fill semantics) -------
    {
        TilemapTileStamp stamp;
        stamp.sourceTilesetAssetId = "tiles-1";
        stamp.width = 2; stamp.height = 3;
        stamp.tiles = {TileId("a"), TileId("b"),
                       TileId("c"), TileId("d"),
                       TileId("e"), std::nullopt};
        const TilemapCellCoord anchor{0, 0};
        CHECK(stampPatternTileAt(stamp, anchor, {0, 0}) == std::optional<TileId>{"a"});
        CHECK(stampPatternTileAt(stamp, anchor, {3, 4}) == std::optional<TileId>{"d"});
        // Negative deltas wrap: (-1,-1) -> col 1, row 2 -> the hole.
        CHECK(!stampPatternTileAt(stamp, anchor, {-1, -1}).has_value());
        CHECK(stampPatternTileAt(stamp, anchor, {-2, -3}) == std::optional<TileId>{"a"});
        CHECK(stampPatternTileAt(stamp, {5, 7}, {5, 7}) == std::optional<TileId>{"a"});
    }

    // -- buildTileStampFromRegion ----------------------------------------------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32; slicing.tileHeight = 32;
        const auto g = computeTilesetGridGeometry(slicing, 128, 64);   // 4x2
        TilesetAsset ts;
        ts.assetId = "tiles-1";
        ts.imageAssetId = "img-1";
        ts.slicing = slicing;
        ts.tiles = tilesForSlicing(128, 64, slicing);                  // tile-1..tile-8

        const auto full = buildTileStampFromRegion(ts, *g, 0, 0, 1, 1, nullptr);
        CHECK(full.has_value() && stampIsValid(*full));
        CHECK(full->sourceTilesetAssetId == "tiles-1");
        CHECK(full->sourceColumn == 0 && full->sourceRow == 0);
        CHECK(full->width == 2 && full->height == 2);
        CHECK(full->tiles[0] == std::optional<TileId>{"tile-1"});
        CHECK(full->tiles[1] == std::optional<TileId>{"tile-2"});
        CHECK(full->tiles[2] == std::optional<TileId>{"tile-5"});
        CHECK(full->tiles[3] == std::optional<TileId>{"tile-6"});

        // Reversed corners: identical stamp.
        const auto reversed = buildTileStampFromRegion(ts, *g, 1, 1, 0, 0, nullptr);
        CHECK(reversed.has_value() && reversed->tiles == full->tiles
              && reversed->sourceColumn == 0 && reversed->sourceRow == 0);

        // Out-of-grid corners are clamped; fully outside selects nothing.
        const auto clamped = buildTileStampFromRegion(ts, *g, -3, -3, 0, 0, nullptr);
        CHECK(clamped.has_value() && clamped->width == 1 && clamped->height == 1
              && clamped->tiles[0] == std::optional<TileId>{"tile-1"});
        CHECK(!buildTileStampFromRegion(ts, *g, 10, 10, 12, 12, nullptr).has_value());

        // Empty mask turns tiles into holes; a first row entirely empty still
        // keeps the full N x M bounds and provenance (top row of holes).
        std::vector<bool> empty(ts.tiles.size(), false);
        empty[0] = empty[1] = empty[2] = empty[3] = true;   // whole first sheet row
        const auto holed = buildTileStampFromRegion(ts, *g, 0, 0, 1, 1, &empty);
        CHECK(holed.has_value());
        CHECK(holed->width == 2 && holed->height == 2);
        CHECK(holed->sourceRow == 0);                       // bounds include the empty row
        CHECK(!holed->tiles[0].has_value() && !holed->tiles[1].has_value());
        CHECK(holed->tiles[2] == std::optional<TileId>{"tile-5"});

        // All-empty region selects nothing; misaligned mask is never indexed.
        const auto allEmpty = buildTileStampFromRegion(ts, *g, 0, 0, 3, 0, &empty);
        CHECK(!allEmpty.has_value());
        std::vector<bool> misaligned(3, false);
        CHECK(!buildTileStampFromRegion(ts, *g, 0, 0, 1, 1, &misaligned).has_value());
    }

    // ======================= provider-based region builders =======================
    // -- rectangleFillChanges with a 2x2 pattern provider ----------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        TilemapTileStamp stamp;
        stamp.sourceTilesetAssetId = "tiles-1";
        stamp.width = 2; stamp.height = 2;
        stamp.tiles = {TileId("a"), TileId("b"), TileId("c"), TileId("d")};
        const TilemapCellCoord anchor{0, 0};
        const auto provider = [&](TilemapCellCoord cell) {
            const std::optional<TileId> id = stampPatternTileAt(stamp, anchor, cell);
            return id ? TileReplacementDecision::paint(*id) : TileReplacementDecision::skip();
        };
        const TileRegionBuildResult r = rectangleFillChanges(tm, {0, 0}, {3, 3}, provider);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 16);
        for (const TilemapCellChange& c : r.changes) {
            const char* expected = (c.cell.cellY % 2 == 0)
                ? (c.cell.cellX % 2 == 0 ? "a" : "b")
                : (c.cell.cellX % 2 == 0 ? "c" : "d");
            CHECK(c.after.has_value() && c.after->tileId == expected);
        }
    }

    // -- Provider Skip cells are simply absent from the delta ------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const auto provider = [](TilemapCellCoord cell) {
            if (cell.cellX == 1) return TileReplacementDecision::skip();
            return TileReplacementDecision::paint("t");
        };
        const TileRegionBuildResult fill = rectangleFillChanges(tm, {0, 0}, {2, 2}, provider);
        CHECK(!fill.error.has_value());
        CHECK(fill.changes.size() == 6);                    // 9 minus the skipped column
        for (const TilemapCellChange& c : fill.changes) CHECK(c.cell.cellX != 1);
        const TileRegionBuildResult outline = rectangleOutlineChanges(tm, {0, 0}, {2, 2}, provider);
        CHECK(!outline.error.has_value());
        CHECK(outline.changes.size() == 6);                 // 8 border cells minus two skipped
    }

    // -- Provider flood fill: holes never interrupt the traversal --------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        // Bounded empty region: a 3x1 corridor walled by "wall" tiles.
        const TilemapCell wall = TilemapCellValue{"wall", TileTransformFlags::None};
        for (int x = -1; x <= 3; ++x) {
            writeTilemapCell(tm, {x, -1}, wall);
            writeTilemapCell(tm, {x, 1}, wall);
        }
        writeTilemapCell(tm, {-1, 0}, wall);
        writeTilemapCell(tm, {3, 0}, wall);
        const auto provider = [](TilemapCellCoord cell) {
            if (cell.cellX == 1) return TileReplacementDecision::skip();   // hole mid-corridor
            return TileReplacementDecision::paint("p");
        };
        const TileRegionBuildResult r = floodFillChanges(tm, {0, 0}, provider);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 2);                       // cells 0 and 2: the hole did not stop cell 2
        for (const TilemapCellChange& c : r.changes) CHECK(c.after->tileId == "p");
    }

    // -- Provider flood fill: cap counts visited cells, not emitted changes ----
    {
        TilemapComponent tm; tm.chunkSize = 16;             // fully open empty grid
        const auto allSkip = [](TilemapCellCoord) { return TileReplacementDecision::skip(); };
        const TileRegionBuildResult r = floodFillChanges(tm, {0, 0}, allSkip);
        CHECK(r.error.has_value());                         // open region must still be rejected
        CHECK(r.changes.empty());
    }

    // -- Provider repainting the origin's own value still fills the rest -------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell a = TilemapCellValue{"a", TileTransformFlags::None};
        const TilemapCell wall = TilemapCellValue{"wall", TileTransformFlags::None};
        writeTilemapCell(tm, {0, 0}, a);
        writeTilemapCell(tm, {1, 0}, a);
        for (int x = -1; x <= 2; ++x) {
            writeTilemapCell(tm, {x, -1}, wall);
            writeTilemapCell(tm, {x, 1}, wall);
        }
        writeTilemapCell(tm, {-1, 0}, wall);
        writeTilemapCell(tm, {2, 0}, wall);
        // Pattern paints "a" on even columns (== origin's value) and "b" on odd.
        const auto provider = [](TilemapCellCoord cell) {
            return TileReplacementDecision::paint(cell.cellX % 2 == 0 ? "a" : "b");
        };
        const TileRegionBuildResult r = floodFillChanges(tm, {0, 0}, provider);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 1);                       // only the odd column actually changes
        CHECK((r.changes[0].cell == TilemapCellCoord{1, 0}));
        CHECK(r.changes[0].after->tileId == "b");
    }

    // -- computeTilesetSlicing: a perfectly divisible sheet --------------------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const TilesetSliceResult r = computeTilesetSlicing(128, 64, slicing);
        CHECK(r.columns == 4);
        CHECK(r.rows == 2);
        CHECK(r.tileCount == 8);
        CHECK(r.remainderX == 0);
        CHECK(r.remainderY == 0);
    }

    // -- computeTilesetSlicing: a non-divisible sheet leaves a remainder -------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const TilesetSliceResult r = computeTilesetSlicing(100, 70, slicing);
        CHECK(r.columns == 3);   // floors: 100/32 -> 3, 4px left over
        CHECK(r.rows == 2);      // 70/32 -> 2, 6px left over
        CHECK(r.tileCount == 6);
        CHECK(r.remainderX == 4);
        CHECK(r.remainderY == 6);
    }

    // -- computeTilesetSlicing: margin trims usable area on both edges --------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        slicing.marginX = 8;
        slicing.marginY = 4;
        const TilesetSliceResult r = computeTilesetSlicing(80, 40, slicing);
        // usable = 80 - 16 = 64 -> 2 cols; 40 - 8 = 32 -> 1 row
        CHECK(r.columns == 2);
        CHECK(r.rows == 1);
        CHECK(r.tileCount == 2);
    }

    // -- computeTilesetSlicing: spacing between tiles reduces how many fit -----
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        slicing.spacingX = 2;
        slicing.spacingY = 2;
        const TilesetSliceResult r = computeTilesetSlicing(100, 66, slicing);
        // step = 34; 1 + (100-32)/34 = 3 cols; 1 + (66-32)/34 = 2 rows
        CHECK(r.columns == 3);
        CHECK(r.rows == 2);
    }

    // -- computeTilesetSlicing: rectangular (non-square) tiles -----------------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 16;
        slicing.tileHeight = 48;
        const TilesetSliceResult r = computeTilesetSlicing(64, 96, slicing);
        CHECK(r.columns == 4);
        CHECK(r.rows == 2);
        CHECK(r.tileCount == 8);
    }

    // -- computeTilesetSlicing: degenerate/invalid dimensions -> zero tiles ----
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(computeTilesetSlicing(0, 64, slicing).tileCount == 0);
        CHECK(computeTilesetSlicing(128, 64, TilesetSlicing{0, 32, 0, 0, 0, 0}).tileCount == 0);
        CHECK(computeTilesetSlicing(16, 16, slicing).tileCount == 0);  // smaller than one tile
    }

    // -- tilesForSlicing: sequential ids, correct rects, row-major order -------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const std::vector<TileDefinition> tiles = tilesForSlicing(64, 64, slicing);
        CHECK(tiles.size() == 4);
        CHECK(tiles[0].id == "tile-1");
        CHECK(tiles[0].x == 0);
        CHECK(tiles[0].y == 0);
        CHECK(tiles[1].id == "tile-2");
        CHECK(tiles[1].x == 32);
        CHECK(tiles[1].y == 0);
        CHECK(tiles[2].id == "tile-3");
        CHECK(tiles[2].x == 0);
        CHECK(tiles[2].y == 32);
        CHECK(tiles[3].width == 32);
        CHECK(tiles[3].height == 32);
    }

    // -- tilesForSlicing: zero tiles for a sheet smaller than one tile ---------
    {
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(tilesForSlicing(10, 10, slicing).empty());
    }

    // -- reconcileTiles: a matching rect keeps its old stable id ---------------
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"wall-corner", 0, 0, 32, 32});
        oldTiles.push_back(TileDefinition{"wall-flat", 32, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});    // same rect as wall-corner
        newTiles.push_back(TileDefinition{"tile-2", 32, 0, 32, 32});   // same rect as wall-flat

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 2);
        CHECK(result[0].id == "wall-corner");
        CHECK(result[1].id == "wall-flat");
    }

    // -- reconcileTiles: a genuinely new rect gets a fresh, non-colliding id ---
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"wall-corner", 0, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});     // matches wall-corner
        newTiles.push_back(TileDefinition{"tile-2", 32, 0, 32, 32});    // new rect

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 2);
        CHECK(result[0].id == "wall-corner");
        CHECK(result[1].id != "wall-corner");
        CHECK(!result[1].id.empty());
    }

    // -- reconcileTiles: an old tile with no matching rect is dropped ----------
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"wall-corner", 0, 0, 32, 32});
        oldTiles.push_back(TileDefinition{"wall-flat", 32, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});   // only wall-corner survives

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 1);
        CHECK(result[0].id == "wall-corner");
    }

    // -- reconcileTiles: a fresh id never collides with a kept old id ----------
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"tile-1", 100, 100, 32, 32});   // old id happens to look generated

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"x", 100, 100, 32, 32});   // matches -> keeps "tile-1"
        newTiles.push_back(TileDefinition{"y", 200, 200, 32, 32});   // new -> must not become "tile-1" too

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 2);
        CHECK(result[0].id == "tile-1");
        CHECK(result[1].id != "tile-1");
    }

    // -- reconcileTiles: a removed tile's id is never recycled by a new rect -
    // a painted cell still referencing it must become an orphan (detected and
    // cleared by the re-slice cascade), never silently show the new content --
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"n", 0, 0, 64, 64});   // no rect match

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 1);
        CHECK(result[0].id != "tile-1");
    }

    // -- reconcileTiles: a fresh rect listed before a kept rect must not take
    // the id the kept rect is about to reuse ------------------------------------
    {
        std::vector<TileDefinition> oldTiles;
        oldTiles.push_back(TileDefinition{"tile-1", 32, 0, 32, 32});

        std::vector<TileDefinition> newTiles;
        newTiles.push_back(TileDefinition{"a", 0, 0, 32, 32});    // fresh, listed first
        newTiles.push_back(TileDefinition{"b", 32, 0, 32, 32});   // keeps "tile-1"

        const std::vector<TileDefinition> result = reconcileTiles(oldTiles, newTiles);
        CHECK(result.size() == 2);
        CHECK(result[1].id == "tile-1");
        CHECK(result[0].id != result[1].id);
    }

    // -- AddTilesetAssetCommand: success, then duplicate/unknown-image/invalid-
    // slicing all fail without mutation, then undo/redo -------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};   // has image asset "img-hero"
        TilesetSlicing slicing;
        slicing.tileWidth = 16;
        slicing.tileHeight = 16;
        const EditorOperationResult r =
            c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero", slicing});
        CHECK(r.ok);
        CHECK(c.document().hasTilesetAsset("tiles-a"));
        const TilesetAsset* asset = c.document().findTilesetAsset("tiles-a");
        CHECK(asset->name == "Dungeon");
        CHECK(asset->imageAssetId == "img-hero");
        CHECK(asset->slicing.tileWidth == 16);
        CHECK(asset->tiles.empty());   // not sliced yet

        CHECK(!c.execute(AddTilesetAssetCommand{"tiles-a", "Dup", "img-hero", slicing}).ok);
        CHECK(!c.execute(AddTilesetAssetCommand{"tiles-b", "X", "no-such-image", slicing}).ok);
        TilesetSlicing bad; bad.tileWidth = 0; bad.tileHeight = 16;
        CHECK(!c.execute(AddTilesetAssetCommand{"tiles-c", "X", "img-hero", bad}).ok);
        CHECK(!c.document().hasTilesetAsset("tiles-b"));
        CHECK(!c.document().hasTilesetAsset("tiles-c"));

        CHECK(c.undo().ok);
        CHECK(!c.document().hasTilesetAsset("tiles-a"));
        CHECK(c.redo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->name == "Dungeon");
    }

    // -- RemoveTilesetAssetCommand: success, unknown-id rejection, undo restores
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero", slicing}).ok);
        CHECK(!c.execute(RemoveTilesetAssetCommand{"nope"}).ok);
        CHECK(c.execute(RemoveTilesetAssetCommand{"tiles-a"}).ok);
        CHECK(!c.document().hasTilesetAsset("tiles-a"));
        CHECK(c.undo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->imageAssetId == "img-hero");
    }

    // -- RenameTilesetCommand: mirrors RenameObjectTypeCommand's guard shape ---
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero", slicing}).ok);
        CHECK(!c.execute(RenameTilesetCommand{"tiles-a", ""}).ok);          // empty name
        CHECK(!c.execute(RenameTilesetCommand{"nope", "X"}).ok);           // unknown id
        const std::size_t before = c.undoSize();
        CHECK(c.execute(RenameTilesetCommand{"tiles-a", "Dungeon"}).ok);   // same name -> no-op
        CHECK(c.undoSize() == before);
        CHECK(c.execute(RenameTilesetCommand{"tiles-a", "Caves"}).ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->name == "Caves");
        CHECK(c.undo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->name == "Dungeon");
        CHECK(c.redo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->name == "Caves");
    }

    // -- ChangeTilesetSlicingCommand: atomically swaps config + tiles (the
    // caller-supplied tiles, not computed by the command itself); undo
    // restores both the old config and the old tiles ---------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero", slicing}).ok);

        TilesetSlicing next = slicing;
        next.tileWidth = 16;
        std::vector<TileDefinition> nextTiles = tilesForSlicing(64, 64, next);
        CHECK(!nextTiles.empty());
        CHECK(!c.execute(ChangeTilesetSlicingCommand{"nope", next, nextTiles}).ok);
        TilesetSlicing invalid; invalid.tileWidth = -1; invalid.tileHeight = 16;
        CHECK(!c.execute(ChangeTilesetSlicingCommand{"tiles-a", invalid, {}}).ok);

        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-a", next, nextTiles}).ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->slicing.tileWidth == 16);
        CHECK(c.document().findTilesetAsset("tiles-a")->tiles.size() == nextTiles.size());

        CHECK(c.undo().ok);
        CHECK(c.document().findTilesetAsset("tiles-a")->tiles.empty());   // restored to Add's empty tiles
        CHECK(c.document().findTilesetAsset("tiles-a")->slicing.tileWidth == 32);
    }

    // -- ChangeTilesetSlicingCommand: identical slicing + tiles is a no-op -
    // no revision, no undo entry (AC-MUT-001) ----------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const std::vector<TileDefinition> tiles = tilesForSlicing(64, 64, slicing);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-a", "Dungeon", "img-hero",
                                               slicing, tiles}).ok);
        const std::size_t before = c.undoSize();
        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-a", slicing, tiles}).ok);
        CHECK(c.undoSize() == before);
    }

    // -- AddTilesetAssetCommand: tiles pre-sliced at creation land on the
    // asset and survive undo/redo (create-from-image's auto-slice path) --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        const TilesetSlicing slicing;   // default 32x32
        const std::vector<TileDefinition> tiles = tilesForSlicing(64, 64, slicing);
        CHECK(tiles.size() == 4);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-b", "Autosliced", "img-hero",
                                               slicing, tiles}).ok);
        CHECK(c.document().findTilesetAsset("tiles-b")->tiles.size() == 4);
        CHECK(c.undo().ok);
        CHECK(!c.document().hasTilesetAsset("tiles-b"));
        CHECK(c.redo().ok);
        CHECK(c.document().findTilesetAsset("tiles-b")->tiles.size() == 4);
        CHECK(c.document().findTilesetAsset("tiles-b")->tiles[0].id == "tile-1");
    }

    // -- uniqueTilesetAssetId: "<image>.tileset" first, then numbered ----------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(uniqueTilesetAssetId(c.document(), "img-hero") == "img-hero.tileset");
        CHECK(c.execute(AddTilesetAssetCommand{"img-hero.tileset", "T", "img-hero",
                                               TilesetSlicing{}}).ok);
        CHECK(uniqueTilesetAssetId(c.document(), "img-hero") == "img-hero-2.tileset");
    }

    // -- adjacentTileIndex: arrow-key selection movement on the pending grid -
    // stays put at every edge, rejects invalid grids/indices ------------------
    {
        // 4 x 3 grid: indices 0..11, row-major.
        CHECK(adjacentTileIndex(4, 3, 0, 1, 0) == std::optional<int>{1});
        CHECK(adjacentTileIndex(4, 3, 0, 0, 1) == std::optional<int>{4});
        CHECK(adjacentTileIndex(4, 3, 5, -1, 0) == std::optional<int>{4});
        CHECK(adjacentTileIndex(4, 3, 5, 0, -1) == std::optional<int>{1});
        CHECK(!adjacentTileIndex(4, 3, 3, 1, 0).has_value());    // right edge
        CHECK(!adjacentTileIndex(4, 3, 0, -1, 0).has_value());   // left edge
        CHECK(!adjacentTileIndex(4, 3, 0, 0, -1).has_value());   // top edge
        CHECK(!adjacentTileIndex(4, 3, 8, 0, 1).has_value());    // bottom edge
        CHECK(!adjacentTileIndex(0, 3, 0, 1, 0).has_value());    // degenerate grid
        CHECK(!adjacentTileIndex(4, 3, 12, 1, 0).has_value());   // index out of range
        CHECK(!adjacentTileIndex(4, 3, -1, 1, 0).has_value());
    }

    // -- sameTilesetSlicing / sameTileDefinitions: the one "unchanged"
    // definition shared by the no-op guard and the close guard's dirty check ---
    {
        TilesetSlicing a;
        TilesetSlicing b;
        CHECK(sameTilesetSlicing(a, b));
        b.spacingY = 1;
        CHECK(!sameTilesetSlicing(a, b));

        std::vector<TileDefinition> ta = {TileDefinition{"t", 0, 0, 32, 32}};
        std::vector<TileDefinition> tb = ta;
        CHECK(sameTileDefinitions(ta, tb));
        tb[0].id = "u";
        CHECK(!sameTileDefinitions(ta, tb));
        tb = ta;
        tb[0].width = 16;
        CHECK(!sameTileDefinitions(ta, tb));
        tb = ta;
        tb.push_back(TileDefinition{"v", 32, 0, 32, 32});
        CHECK(!sameTileDefinitions(ta, tb));
    }

    // -- Save/reload round-trips a tileset, including a populated tiles array --
    {
        ProjectDoc doc = makeDoc();
        ImageAssetDef hero; hero.assetId = "img-hero"; hero.sourcePath = "sprites/hero.ppm";
        doc.imageAssets.push_back(hero);

        TilesetAsset asset;
        asset.assetId = "tiles-a";
        asset.name = "Dungeon";
        asset.imageAssetId = "img-hero";
        asset.slicing.tileWidth = 16;
        asset.slicing.tileHeight = 16;
        asset.slicing.marginX = 1;
        asset.slicing.spacingY = 1;
        asset.tiles = tilesForSlicing(64, 64, asset.slicing);
        CHECK(!asset.tiles.empty());
        doc.tilesets.push_back(asset);

        EditorCoordinator c{doc};
        const std::filesystem::path path = testTempDir() / "tileset.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const TilesetAsset* rt = reloaded.document().findTilesetAsset("tiles-a");
        CHECK(rt != nullptr);
        CHECK(rt->name == "Dungeon");
        CHECK(rt->imageAssetId == "img-hero");
        CHECK(rt->slicing.tileWidth == 16);
        CHECK(rt->slicing.marginX == 1);
        CHECK(rt->slicing.spacingY == 1);
        CHECK(rt->tiles.size() == asset.tiles.size());
        CHECK(rt->tiles[0].id == asset.tiles[0].id);
        CHECK(rt->tiles[0].width == 16);
    }

    // -- Validation: an unknown imageAssetId is rejected (both save and load
    // run the same ProjectValidator::validate) --------------------------------
    {
        ProjectDoc doc = makeDoc();
        TilesetAsset asset;
        asset.assetId = "tiles-a";
        asset.imageAssetId = "no-such-image";
        asset.slicing.tileWidth = 16;
        asset.slicing.tileHeight = 16;
        doc.tilesets.push_back(asset);

        EditorCoordinator c{doc};
        const std::filesystem::path path = testTempDir() / "bad-tileset.artcade-project";
        CHECK(!saveProjectToFile(c, path).ok);
    }

    // -- Open/Close Tileset Editor: open seeds pendingSlicing from the asset's
    // current slicing (workspace-only, no document mutation); close resets ----
    {
        EditorCoordinator c{makeSpriteDoc()};   // "tiles-1" -> img-hero, default 32x32
        CHECK(!c.state().tilesetEditor.openAssetId.has_value());
        CHECK(!c.apply(OpenTilesetEditorIntent{"no-such-tileset"}).ok);

        const uint64_t revision = c.document().revision();
        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.state().tilesetEditor.openAssetId.has_value());
        CHECK(*c.state().tilesetEditor.openAssetId == "tiles-1");
        CHECK(c.state().tilesetEditor.pendingSlicing.tileWidth == 32);
        CHECK(c.document().revision() == revision);   // workspace-only

        CHECK(c.apply(CloseTilesetEditorIntent{}).ok);
        CHECK(!c.state().tilesetEditor.openAssetId.has_value());
    }

    // -- SetPendingTilesetSlicingIntent: live preview, clamped, no document
    // mutation, rejected while the editor is closed ----------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilesetSlicing bad{-5, 0, -1, -1, -1, -1};
        CHECK(!c.apply(SetPendingTilesetSlicingIntent{bad}).ok);   // editor not open

        const uint64_t revision = c.document().revision();
        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.apply(SetPendingTilesetSlicingIntent{bad}).ok);
        CHECK(c.state().tilesetEditor.pendingSlicing.tileWidth == 1);    // clamped >= 1
        CHECK(c.state().tilesetEditor.pendingSlicing.tileHeight == 1);
        CHECK(c.state().tilesetEditor.pendingSlicing.marginX == 0);      // clamped >= 0
        CHECK(c.document().revision() == revision);

        TilesetSlicing ok{16, 16, 2, 2, 1, 1};
        CHECK(c.apply(SetPendingTilesetSlicingIntent{ok}).ok);
        CHECK(c.state().tilesetEditor.pendingSlicing.tileWidth == 16);
        CHECK(c.document().revision() == revision);
        // The asset itself is untouched until a later slice's Apply flow commits it.
        CHECK(c.document().findTilesetAsset("tiles-1")->slicing.tileWidth == 32);
    }

    // -- Tileset Editor zoom/pan: workspace-only, rejected while closed --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(!c.apply(SetTilesetEditorZoomIntent{2.f}).ok);
        CHECK(!c.apply(PanTilesetEditorIntent{{4.f, 4.f}}).ok);

        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.apply(SetTilesetEditorZoomIntent{2.f}).ok);
        CHECK(c.state().tilesetEditor.zoom == 2.f);
        CHECK(c.apply(PanTilesetEditorIntent{{4.f, 6.f}}).ok);
        CHECK(c.state().tilesetEditor.pan.x == 4.f);
        CHECK(c.state().tilesetEditor.pan.y == 6.f);
    }

    // -- Deleting the open tileset asset auto-closes the editor ----------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.apply(OpenTilesetEditorIntent{"tiles-1"}).ok);
        CHECK(c.state().tilesetEditor.openAssetId.has_value());
        CHECK(c.execute(RemoveTilesetAssetCommand{"tiles-1"}).ok);
        CHECK(!c.state().tilesetEditor.openAssetId.has_value());
    }

    // -- floorDivChunk/floorModChunk: positive coordinates ---------------------
    {
        CHECK(floorDivChunk(0, 16) == 0);
        CHECK(floorDivChunk(15, 16) == 0);
        CHECK(floorDivChunk(16, 16) == 1);
        CHECK(floorDivChunk(31, 16) == 1);
        CHECK(floorModChunk(0, 16) == 0);
        CHECK(floorModChunk(15, 16) == 15);
        CHECK(floorModChunk(16, 16) == 0);
    }

    // -- floorDivChunk/floorModChunk: negative coordinates - the exact
    // truncating-division gotcha this module exists to avoid ------------------
    {
        CHECK(floorDivChunk(-1, 16) == -1);    // naive -1/16 == 0 would be wrong
        CHECK(floorDivChunk(-16, 16) == -1);
        CHECK(floorDivChunk(-17, 16) == -2);
        CHECK(floorModChunk(-1, 16) == 15);    // naive -1%16 == -1 would be wrong
        CHECK(floorModChunk(-16, 16) == 0);
        CHECK(floorModChunk(-17, 16) == 15);
    }

    // -- cellToChunkCoord/cellToLocalCoord: combined, mixed-sign case ----------
    {
        const TilemapChunkCoord chunk = cellToChunkCoord(-1, 20, 16);
        CHECK(chunk.chunkX == -1);
        CHECK(chunk.chunkY == 1);
        const TilemapLocalCoord local = cellToLocalCoord(-1, 20, 16);
        CHECK(local.localX == 15);
        CHECK(local.localY == 4);
    }

    // -- chunkAndLocalToCellX/Y: exact inverse round-trip, including negatives -
    {
        const int xs[] = {-33, -17, -16, -1, 0, 1, 15, 16, 31, 100};
        const int ys[] = {-40, -16, -1, 0, 7, 16, 63};
        for (const int x : xs) {
            for (const int y : ys) {
                const TilemapChunkCoord chunk = cellToChunkCoord(x, y, 16);
                const TilemapLocalCoord local = cellToLocalCoord(x, y, 16);
                CHECK(chunkAndLocalToCellX(chunk.chunkX, local.localX, 16) == x);
                CHECK(chunkAndLocalToCellY(chunk.chunkY, local.localY, 16) == y);
            }
        }
    }

    // -- tilemapRenderCells: multi-chunk, negative chunk coords, correct
    // destination (origin + cell*cellSize) and source (the tile's own rect) -
    {
        TilesetAsset tileset;
        tileset.assetId = "tiles-1";
        tileset.tiles.push_back(TileDefinition{"grass", 0, 0, 16, 16});
        tileset.tiles.push_back(TileDefinition{"stone", 16, 0, 16, 16});

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        tm.chunkSize = 2;

        TilemapChunk chunkA;   // chunk (0,0): cells (0,0)=grass, (1,0)=empty
        chunkA.chunkX = 0;
        chunkA.chunkY = 0;
        chunkA.cells = {TilemapCellValue{"grass", TileTransformFlags::None},
                       std::nullopt, std::nullopt, std::nullopt};
        TilemapChunk chunkB;   // chunk (-1,-1): local (1,1) -> absolute cell (-1,-1)=stone
        chunkB.chunkX = -1;
        chunkB.chunkY = -1;
        chunkB.cells = {std::nullopt, std::nullopt, std::nullopt,
                       TilemapCellValue{"stone", TileTransformFlags::FlipY}};
        tm.chunks.push_back(chunkA);
        tm.chunks.push_back(chunkB);

        const std::vector<SceneFrameTilemapCell> cells =
            tilemapRenderCells(tm, tileset, Vec2{100.f, 200.f});
        CHECK(cells.size() == 2);

        CHECK(cells[0].destination.x == 100.f);         // origin + cell(0,0)*32
        CHECK(cells[0].destination.y == 200.f);
        CHECK(cells[0].destination.width == 32.f);
        CHECK(cells[0].source.x == 0.f);                 // "grass" tile rect
        CHECK(cells[0].source.width == 16.f);

        CHECK(cells[1].destination.x == 100.f - 32.f);   // origin + cell(-1,-1)*32
        CHECK(cells[1].destination.y == 200.f - 32.f);
        CHECK(cells[1].source.x == 16.f);                // "stone" tile rect
        CHECK(cells[1].source.width == 16.f);
    }

    // -- tilemapRenderCells: a cell referencing an unknown tile id is skipped,
    // not crashed on or substituted with a placeholder ------------------------
    {
        TilesetAsset tileset;
        tileset.assetId = "tiles-1";
        tileset.tiles.push_back(TileDefinition{"grass", 0, 0, 16, 16});

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 1;
        TilemapChunk chunk;
        chunk.cells = {TilemapCellValue{"no-such-tile", TileTransformFlags::None}};
        tm.chunks.push_back(chunk);

        CHECK(tilemapRenderCells(tm, tileset, Vec2{}).empty());
    }

    // -- tilemapAtlasSourceRect: half-texel inset prevents atlas bleeding ------
    {
        const SceneFrameRect atlas{64.f, 128.f, 32.f, 32.f};
        const SceneFrameRect inset = tilemapAtlasSourceRect(atlas);
        CHECK(inset.x == 64.5f);
        CHECK(inset.y == 128.5f);
        CHECK(inset.width == 31.f);
        CHECK(inset.height == 31.f);

        const SceneFrameRect rectangular = tilemapAtlasSourceRect({32.f, 64.f, 16.f, 32.f});
        CHECK(rectangular.x == 32.5f);
        CHECK(rectangular.y == 64.5f);
        CHECK(rectangular.width == 15.f);
        CHECK(rectangular.height == 31.f);

        const SceneFrameRect onePxWide{0.f, 0.f, 1.f, 8.f};
        CHECK(tilemapAtlasSourceRect(onePxWide).x == onePxWide.x);
        CHECK(tilemapAtlasSourceRect(onePxWide).y == onePxWide.y);
        CHECK(tilemapAtlasSourceRect(onePxWide).width == onePxWide.width);
        CHECK(tilemapAtlasSourceRect(onePxWide).height == onePxWide.height);

        const SceneFrameRect tiny{0.f, 0.f, 0.5f, 0.5f};
        CHECK(tilemapAtlasSourceRect(tiny).x == tiny.x);
        CHECK(tilemapAtlasSourceRect(tiny).width == tiny.width);
    }

    // -- AddTilemapComponentCommand: success, then rejections, no partial
    // mutation on any rejection ------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        tm.chunkSize = 16;
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap.has_value());
        CHECK(inst->tilemap->tilesetAssetId == "tiles-1");
        CHECK(inst->tilemap->cellSize.x == 32.f);
        CHECK(inst->tilemap->chunkSize == 16);
        CHECK(inst->tilemap->chunks.empty());

        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);   // double-add

        TilemapComponent unknownTileset = tm;
        unknownTileset.tilesetAssetId = "no-such-tileset";
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, 9999, unknownTileset}).ok);

        TilemapComponent badCellSize = tm;
        badCellSize.cellSize = {0.f, 32.f};
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, 9999, badCellSize}).ok);

        TilemapComponent badChunkSize = tm;
        badChunkSize.chunkSize = 0;
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, 9999, badChunkSize}).ok);
        badChunkSize.chunkSize = kMaxTilemapChunkSize + 1;
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, 9999, badChunkSize}).ok);

        CHECK(!c.document().findInstanceInScene(kSceneA, 9999));   // no rejection left a partial instance
    }

    // -- AddTilemapComponentCommand: a populated-but-invalid component is
    // rejected - proves the command runs the full shared validator, not just
    // top-level field checks ---------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};   // "tiles-1" has no tiles at all yet
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.chunkX = 0;
        chunk.chunkY = 0;
        chunk.cells.assign(4, std::nullopt);
        chunk.cells[0] = TilemapCellValue{"no-such-tile", TileTransformFlags::None};
        tm.chunks.push_back(chunk);
        CHECK(!c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->tilemap.has_value());
    }

    // -- AddTilemapComponentCommand: undo/redo ---------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        CHECK(c.undo().ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->tilemap.has_value());
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-1");
    }

    // -- CreateTilemapEntityCommand: one atomic mutation creates type +
    // instance (on the requested layer) + tilemap component; a single undo
    // removes all of it, redo restores it ---------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);   // tiles-1 sliced 32x32 -> cellSize derives from slicing
        const std::size_t before = c.undoSize();
        CHECK(c.execute(CreateTilemapEntityCommand{
            kSceneA, 100, "object-tm", "Tilemap 1", "Tilemap 1",
            Vec2{0.f, 0.f}, "layer-1", "tiles-1"}).ok);
        CHECK(c.undoSize() == before + 1);   // one undo entry for the whole gesture
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, 100);
        CHECK(inst != nullptr);
        CHECK(inst->instanceName == "Tilemap 1");
        CHECK(inst->objectTypeId == "object-tm");
        CHECK(inst->layerId == "layer-1");
        CHECK(inst->tilemap.has_value());
        CHECK(inst->tilemap->tilesetAssetId == "tiles-1");
        CHECK(inst->tilemap->cellSize.x == 32.f);
        CHECK(inst->tilemap->cellSize.y == 32.f);
        CHECK(inst->tilemap->chunks.empty());
        CHECK(c.document().hasObjectType("object-tm"));

        CHECK(c.undo().ok);   // single undo removes instance AND type
        CHECK(!c.document().findInstanceInScene(kSceneA, 100));
        CHECK(!c.document().hasObjectType("object-tm"));
        CHECK(c.redo().ok);
        const SceneInstanceDef* again = c.document().findInstanceInScene(kSceneA, 100);
        CHECK(again != nullptr);
        CHECK(again->tilemap.has_value());
        CHECK(c.document().hasObjectType("object-tm"));
    }

    // -- CreateTilemapEntityCommand: empty layerId resolves to the scene
    // default (canonical format: no instance carries "") ------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.execute(CreateTilemapEntityCommand{
            kSceneA, 100, "object-tm", "Tilemap 1", "Tilemap 1",
            Vec2{0.f, 0.f}, /*layerId*/ "", "tiles-1"}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, 100)->layerId == "layer-1");
    }

    // -- CreateTilemapEntityCommand: rejections leave no partial mutation ------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneLayerDef locked;
        locked.id = "locked-layer";
        locked.name = "Locked";
        locked.locked = true;
        doc.scenes.at(kSceneA).layers.push_back(locked);
        EditorCoordinator c{doc};
        const uint64_t revision = c.document().revision();
        // Missing tileset.
        CHECK(!c.execute(CreateTilemapEntityCommand{
            kSceneA, 100, "object-tm", "T", "T", Vec2{0.f, 0.f},
            "layer-1", "no-such-tileset"}).ok);
        // Locked target layer.
        CHECK(!c.execute(CreateTilemapEntityCommand{
            kSceneA, 100, "object-tm", "T", "T", Vec2{0.f, 0.f},
            "locked-layer", "tiles-1"}).ok);
        // Unknown target layer.
        CHECK(!c.execute(CreateTilemapEntityCommand{
            kSceneA, 100, "object-tm", "T", "T", Vec2{0.f, 0.f},
            "no-such-layer", "tiles-1"}).ok);
        // Existing object type / instance ids.
        CHECK(!c.execute(CreateTilemapEntityCommand{
            kSceneA, kHero, "object-tm", "T", "T", Vec2{0.f, 0.f},
            "layer-1", "tiles-1"}).ok);
        CHECK(c.execute(CreateTilemapEntityCommand{
            kSceneA, 100, "object-tm", "T", "T", Vec2{0.f, 0.f},
            "layer-1", "tiles-1"}).ok);
        CHECK(!c.execute(CreateTilemapEntityCommand{
            kSceneA, 101, "object-tm", "T", "T", Vec2{0.f, 0.f},
            "layer-1", "tiles-1"}).ok);
        // Only the one successful create mutated the document.
        CHECK(!c.document().findInstanceInScene(kSceneA, 101));
        CHECK(c.document().revision() != revision);
        CHECK(c.undoSize() == 1);
    }

    // -- addTilemapEntity action: create on the active layer, then select the
    // new instance, arm Brush and show the Tile Palette dock --------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);
        CHECK(c.apply(SetTilePaletteDockVisibleIntent{false}).ok);
        const std::size_t before = c.undoSize();
        CHECK(addTilemapEntity(c).ok);
        CHECK(c.undoSize() == before + 1);
        const EntityId newId = c.selection().primaryEntity;
        CHECK(newId != INVALID_ENTITY);
        CHECK(newId != kHero);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, newId);
        CHECK(inst != nullptr);
        CHECK(inst->instanceName == "Tilemap 1");
        CHECK(inst->layerId == "layer-1");   // the active layer
        CHECK(inst->tilemap.has_value());
        CHECK(inst->tilemap->tilesetAssetId == "tiles-1");
        CHECK(inst->tilemap->cellSize.x == 32.f);   // from the tileset's slicing
        CHECK(c.state().activeTool == EditorTool::Brush);
        CHECK(c.uiState().tilePaletteDockVisible);
        // A second create picks the next free name.
        CHECK(addTilemapEntity(c).ok);
        const SceneInstanceDef* second =
            c.document().findInstanceInScene(kSceneA, c.selection().primaryEntity);
        CHECK(second != nullptr);
        CHECK(second->instanceName == "Tilemap 2");
    }

    // -- addTilemapEntity preflight: no tileset / locked active layer fail
    // without any mutation -------------------------------------------------------
    {
        EditorCoordinator c{makeDoc()};   // no tilesets in the project
        const uint64_t revision = c.document().revision();
        CHECK(!addTilemapEntity(c).ok);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == 0);
    }
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.execute(SetLayerLockedCommand{kSceneA, "layer-1", true}).ok);
        const std::size_t before = c.undoSize();
        const uint64_t revision = c.document().revision();
        CHECK(!addTilemapEntity(c).ok);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
    }

    // -- Locking the layer of the entity mid-gesture cancels the pending
    // stroke/rectangle (a locked layer rejects edits NOW, not just new Begins) --
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.execute(SetLayerLockedCommand{kSceneA, "layer-1", true}).ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());

        // Same for a rectangle drag in progress.
        CHECK(c.execute(SetLayerLockedCommand{kSceneA, "layer-1", false}).ok);
        c.apply(SelectEntityIntent{kHero});
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle.has_value());
        CHECK(c.execute(SetLayerLockedCommand{kSceneA, "layer-1", true}).ok);
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
    }

    // -- RemoveTilemapComponentCommand: success, no-component rejection, undo
    // restores exactly ----------------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(!c.execute(RemoveTilemapComponentCommand{kSceneA, kHero}).ok);   // nothing to remove

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {48.f, 24.f};
        tm.chunkSize = 8;
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        CHECK(c.execute(RemoveTilemapComponentCommand{kSceneA, kHero}).ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero)->tilemap.has_value());

        CHECK(c.undo().ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap.has_value());
        CHECK(inst->tilemap->cellSize.x == 48.f);
        CHECK(inst->tilemap->cellSize.y == 24.f);
        CHECK(inst->tilemap->chunkSize == 8);
    }

    // -- SetTilemapTilesetCommand: success, same-value no-op, unknown-tileset
    // rejection, missing-component rejection, undo/redo -----------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-2", "Forest", "img-hero",
                                               TilesetSlicing{}}).ok);
        CHECK(!c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "tiles-2"}).ok);   // no component yet

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        const std::size_t before = c.undoSize();
        CHECK(c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "tiles-1"}).ok);   // same value -> no-op
        CHECK(c.undoSize() == before);

        CHECK(!c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "no-such-tileset"}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-1");

        CHECK(c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "tiles-2"}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-2");
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-1");
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-2");
    }

    // -- SetTilemapTilesetCommand: rejected when the new tileset doesn't
    // contain a tile id already used by an existing chunk - no mutation, no
    // undo entry ----------------------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);   // "tiles-1" now has "tile-1".."tile-4"
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-2", "Forest", "img-hero",
                                               TilesetSlicing{}}).ok);
        // "tiles-2" gets a tile list with deliberately non-overlapping ids, not
        // via tilesForSlicing (which would also start at "tile-1").
        std::vector<TileDefinition> forestTiles;
        forestTiles.push_back(TileDefinition{"forest-a", 0, 0, 32, 32});
        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-2", TilesetSlicing{}, forestTiles}).ok);

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.chunkX = 0;
        chunk.chunkY = 0;
        chunk.cells.assign(4, std::nullopt);
        chunk.cells[0] = TilemapCellValue{"tile-1", TileTransformFlags::None};   // real tiles-1 tile
        tm.chunks.push_back(chunk);
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        const std::size_t before = c.undoSize();
        CHECK(!c.execute(SetTilemapTilesetCommand{kSceneA, kHero, "tiles-2"}).ok);   // "tile-1" not in tiles-2
        CHECK(c.undoSize() == before);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->tilesetAssetId == "tiles-1");
    }

    // -- SetTilemapCellSizeCommand: success, same-value no-op, non-positive/
    // non-finite rejection, missing-component rejection, undo/redo ------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(!c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {16.f, 16.f}}).ok);

        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        const std::size_t before = c.undoSize();
        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {32.f, 32.f}}).ok);   // no-op
        CHECK(c.undoSize() == before);

        CHECK(!c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {0.f, 32.f}}).ok);
        CHECK(!c.execute(SetTilemapCellSizeCommand{kSceneA, kHero,
                                                   {std::numeric_limits<float>::infinity(), 32.f}}).ok);

        CHECK(c.execute(SetTilemapCellSizeCommand{kSceneA, kHero, {48.f, 16.f}}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->cellSize.x == 48.f);
        CHECK(c.undo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->cellSize.x == 32.f);
        CHECK(c.redo().ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->cellSize.x == 48.f);
    }

    // -- CloneInstanceCommand copies tilemap verbatim - regression test, not
    // just an inspection of the whole-struct-copy claim -------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {40.f, 40.f};
        tm.chunkSize = 12;
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        CHECK(c.execute(CloneInstanceCommand{kSceneA, kHero, 900, "Hero Clone", {5.f, 5.f}}).ok);
        const SceneInstanceDef* clone = c.document().findInstanceInScene(kSceneA, 900);
        CHECK(clone->tilemap.has_value());
        CHECK(clone->tilemap->tilesetAssetId == "tiles-1");
        CHECK(clone->tilemap->cellSize.x == 40.f);
        CHECK(clone->tilemap->chunkSize == 12);
    }

    // -- Deleting an entity with a tilemap, then undoing, restores it intact ---
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {20.f, 20.f};
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        CHECK(c.execute(DeleteEntityCommand{kSceneA, kHero}).ok);
        CHECK(!c.document().findInstanceInScene(kSceneA, kHero));
        CHECK(c.undo().ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst != nullptr);
        CHECK(inst->tilemap.has_value());
        CHECK(inst->tilemap->cellSize.x == 20.f);
    }

    // -- RemoveTilesetAssetCommand rejects removal while a TilemapComponent
    // still references it - asset and component both unchanged, no undo entry -
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        const std::size_t before = c.undoSize();
        CHECK(!c.execute(RemoveTilesetAssetCommand{"tiles-1"}).ok);
        CHECK(c.undoSize() == before);
        CHECK(c.document().hasTilesetAsset("tiles-1"));
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap.has_value());
    }

    // -- RemoveTilesetAssetCommand with no referencing tilemap: unchanged,
    // regression guard on the existing no-consumer removal path ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.execute(RemoveTilesetAssetCommand{"tiles-1"}).ok);
        CHECK(!c.document().hasTilesetAsset("tiles-1"));
        CHECK(c.undo().ok);
        CHECK(c.document().hasTilesetAsset("tiles-1"));
    }

    // -- Save/reload round-trips an empty TilemapComponent - the slice's own
    // literal completion criterion. Also confirms the legacy tilemap/
    // tilemapLayers fields are never touched by this editor's own save/load,
    // for any project, not only ones with a TilemapComponent ------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {32.f, 32.f};
        tm.chunkSize = 16;
        hero.tilemap = tm;

        EditorCoordinator c{doc};
        const std::filesystem::path path = testTempDir() / "tilemap-empty.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const SceneInstanceDef* rt = reloaded.document().findInstanceInScene(kSceneA, kHero);
        CHECK(rt != nullptr);
        CHECK(rt->tilemap.has_value());
        CHECK(rt->tilemap->tilesetAssetId == "tiles-1");
        CHECK(rt->tilemap->cellSize.x == 32.f);
        CHECK(rt->tilemap->cellSize.y == 32.f);
        CHECK(rt->tilemap->chunkSize == 16);
        CHECK(rt->tilemap->chunks.empty());

        const SceneDef* scene = reloaded.document().findScene(kSceneA);
        CHECK(scene->tilemap.cols == 0);            // legacy field, never populated by this editor
        CHECK(scene->tilemapLayers.empty());        // legacy field, never populated by this editor
    }

    // -- Save/reload round-trips a hand-constructed populated chunk: negative
    // chunk coords, a mix of null/filled cells, and a non-None flags value ----
    {
        ProjectDoc doc = makeSpriteDoc();
        TilesetAsset* tiles1 = nullptr;
        for (TilesetAsset& t : doc.tilesets) if (t.assetId == "tiles-1") tiles1 = &t;
        tiles1->tiles.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});
        tiles1->tiles.push_back(TileDefinition{"tile-2", 32, 0, 32, 32});

        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.chunkX = -3;
        chunk.chunkY = -1;
        chunk.cells = {
            TilemapCellValue{"tile-1", TileTransformFlags::FlipX},
            std::nullopt,
            std::nullopt,
            TilemapCellValue{"tile-2", TileTransformFlags::None},
        };
        tm.chunks.push_back(chunk);
        hero.tilemap = tm;

        EditorCoordinator c{doc};
        const std::filesystem::path path = testTempDir() / "tilemap-populated.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const SceneInstanceDef* rt = reloaded.document().findInstanceInScene(kSceneA, kHero);
        CHECK(rt->tilemap->chunks.size() == 1);
        const TilemapChunk& rtChunk = rt->tilemap->chunks.front();
        CHECK(rtChunk.chunkX == -3);
        CHECK(rtChunk.chunkY == -1);
        CHECK(rtChunk.cells.size() == 4);
        CHECK(rtChunk.cells[0].has_value());
        CHECK(rtChunk.cells[0]->tileId == "tile-1");
        CHECK(rtChunk.cells[0]->flags == TileTransformFlags::FlipX);
        CHECK(!rtChunk.cells[1].has_value());
        CHECK(!rtChunk.cells[2].has_value());
        CHECK(rtChunk.cells[3].has_value());
        CHECK(rtChunk.cells[3]->tileId == "tile-2");
        CHECK(rtChunk.cells[3]->flags == TileTransformFlags::None);
    }

    // -- Validation: unknown tilesetAssetId is rejected ------------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "no-such-tileset";
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-tileset.artcade-project").ok);
    }

    // -- Validation: non-positive cellSize is rejected -------------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {0.f, 32.f};
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-cellsize.artcade-project").ok);
    }

    // -- Validation: chunkSize of 0, negative, and > kMaxTilemapChunkSize are
    // all rejected - the last one specifically guards the overflow fix --------
    {
        for (const int badChunkSize : {0, -1, kMaxTilemapChunkSize + 1}) {
            ProjectDoc doc = makeSpriteDoc();
            SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
            TilemapComponent tm;
            tm.tilesetAssetId = "tiles-1";
            tm.chunkSize = badChunkSize;
            hero.tilemap = tm;
            EditorCoordinator c{doc};
            CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-chunksize.artcade-project").ok);
        }
    }

    // -- Validation: a chunk with the wrong cell count (!= chunkSize squared)
    // is rejected ----------------------------------------------------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 4;   // expects 16 cells
        TilemapChunk chunk;
        chunk.cells.assign(4, std::nullopt);   // wrong: only 4
        tm.chunks.push_back(chunk);
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-cellcount.artcade-project").ok);
    }

    // -- Validation: two chunks sharing the same (chunkX, chunkY) are rejected -
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk a; a.chunkX = 1; a.chunkY = 1; a.cells.assign(4, std::nullopt);
        TilemapChunk b; b.chunkX = 1; b.chunkY = 1; b.cells.assign(4, std::nullopt);
        tm.chunks.push_back(a);
        tm.chunks.push_back(b);
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-dup-chunk.artcade-project").ok);
    }

    // -- Validation: a cell referencing a tile id absent from the tileset is
    // rejected --------------------------------------------------------------------
    {
        ProjectDoc doc = makeSpriteDoc();   // "tiles-1" has zero TileDefinitions
        SceneInstanceDef& hero = doc.scenes.at(kSceneA).instances.front();
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 2;
        TilemapChunk chunk;
        chunk.cells.assign(4, std::nullopt);
        chunk.cells[0] = TilemapCellValue{"tile-1", TileTransformFlags::None};   // not in tiles-1
        tm.chunks.push_back(chunk);
        hero.tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!saveProjectToFile(c, testTempDir() / "tm-bad-tileid.artcade-project").ok);
    }

    // -- ComponentKind::Tilemap flows correctly through DomainChange -----------
    {
        EditorCoordinator c{makeSpriteDoc()};
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        const EditorOperationResult r = c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm});
        CHECK(r.ok);
        CHECK(r.change.kind == DomainChangeKind::ComponentAdded);
        CHECK(r.change.componentKind == ComponentKind::Tilemap);
    }

    // -- rasterizeCellLine: same start/end -> one cell -------------------------
    {
        const auto path = rasterizeCellLine(TilemapCellCoord{3, 5}, TilemapCellCoord{3, 5});
        CHECK(path.size() == 1);
        CHECK(path[0].cellX == 3 && path[0].cellY == 5);
    }

    // -- rasterizeCellLine: horizontal/vertical/diagonal, no gaps --------------
    {
        const auto h = rasterizeCellLine(TilemapCellCoord{0, 0}, TilemapCellCoord{4, 0});
        CHECK(h.size() == 5);
        for (std::size_t i = 0; i < h.size(); ++i) {
            CHECK(h[i].cellX == static_cast<int>(i));
            CHECK(h[i].cellY == 0);
        }
        const auto v = rasterizeCellLine(TilemapCellCoord{2, 0}, TilemapCellCoord{2, 3});
        CHECK(v.size() == 4);
        const auto d = rasterizeCellLine(TilemapCellCoord{0, 0}, TilemapCellCoord{3, 3});
        CHECK(d.size() == 4);
    }

    // -- rasterizeCellLine: a long span (fast mouse move) stays fully 8-
    // connected, no gaps, both endpoints included --------------------------------
    {
        const auto path = rasterizeCellLine(TilemapCellCoord{0, 0}, TilemapCellCoord{20, 3});
        CHECK(path.front().cellX == 0 && path.front().cellY == 0);
        CHECK(path.back().cellX == 20 && path.back().cellY == 3);
        for (std::size_t i = 1; i < path.size(); ++i) {
            int dx = path[i].cellX - path[i - 1].cellX;
            int dy = path[i].cellY - path[i - 1].cellY;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            CHECK(dx <= 1 && dy <= 1);
        }
    }

    // -- rasterizeCellLine: explicit chunk-boundary crossings ------------------
    {
        CHECK(rasterizeCellLine(TilemapCellCoord{-1, 0}, TilemapCellCoord{0, 0}).size() == 2);
        CHECK(rasterizeCellLine(TilemapCellCoord{15, 0}, TilemapCellCoord{16, 0}).size() == 2);
        CHECK(rasterizeCellLine(TilemapCellCoord{0, -16}, TilemapCellCoord{0, -15}).size() == 2);
    }

    // -- readTilemapCell/writeTilemapCell/pruneEmptyChunks ---------------------
    {
        TilemapComponent tm;
        tm.chunkSize = 4;
        CHECK(!readTilemapCell(tm, TilemapCellCoord{0, 0}).has_value());   // no chunk yet
        writeTilemapCell(tm, TilemapCellCoord{0, 0}, TilemapCellValue{"tile-1", TileTransformFlags::None});
        CHECK(tm.chunks.size() == 1);
        CHECK(tm.chunks[0].cells.size() == 16);
        const TilemapCell read = readTilemapCell(tm, TilemapCellCoord{0, 0});
        CHECK(read.has_value());
        CHECK(read->tileId == "tile-1");

        writeTilemapCell(tm, TilemapCellCoord{-1, -1}, TilemapCellValue{"tile-2", TileTransformFlags::None});
        CHECK(tm.chunks.size() == 2);
        CHECK(readTilemapCell(tm, TilemapCellCoord{-1, -1})->tileId == "tile-2");

        writeTilemapCell(tm, TilemapCellCoord{-1, -1}, std::nullopt);
        pruneEmptyChunks(tm);
        CHECK(tm.chunks.size() == 1);   // the (-1,-1) chunk is gone, (0,0)'s chunk remains

        writeTilemapCell(tm, TilemapCellCoord{0, 0}, std::nullopt);
        pruneEmptyChunks(tm);
        CHECK(tm.chunks.empty());
    }

    // -- normalizePaintStrokeChanges: drops before==after, keeps real changes --
    {
        std::unordered_map<std::int64_t, TilemapCellChange> changes;
        changes[packTilemapCellCoord({0, 0})] =
            TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}};
        changes[packTilemapCellCoord({1, 0})] = TilemapCellChange{
            {1, 0}, TilemapCellValue{"tile-2", TileTransformFlags::None},
            TilemapCellValue{"tile-2", TileTransformFlags::None}};   // no-op: same value
        changes[packTilemapCellCoord({2, 0})] = TilemapCellChange{{2, 0}, std::nullopt, std::nullopt};   // no-op
        const std::vector<TilemapCellChange> result = normalizePaintStrokeChanges(changes);
        CHECK(result.size() == 1);
        CHECK(result[0].cell.cellX == 0);
    }

    // -- BeginTilePaintStrokeIntent: success, seeds pendingStroke correctly ----
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.state().tilemapEditor.pendingStroke->changes.size() == 1);
        CHECK(c.state().tilemapEditor.pendingStroke->tool == EditorTool::Brush);
    }

    // -- BeginTilePaintStrokeIntent: missing component rejected ----------------
    {
        EditorCoordinator c{makeSpriteDoc()};   // kHero has no tilemap
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- BeginTilePaintStrokeIntent: locked layer rejected ---------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneDef& scene = doc.scenes.at(kSceneA);
        SceneLayerDef locked;
        locked.id = "locked-layer";
        locked.name = "Locked";
        locked.locked = true;
        scene.layers.push_back(locked);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        // Hand-authored directly on the fixture (not via AddTilemapComponentCommand,
        // which now itself rejects adding a component on a locked layer) - this
        // test's target is BeginTilePaintStrokeIntent's own lock guard.
        scene.instances.front().tilemap = tm;
        scene.instances.front().layerId = "locked-layer";
        EditorCoordinator c{doc};
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- BeginTilePaintStrokeIntent: missing tileset rejected ------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        TilemapComponent tm;
        tm.tilesetAssetId = "no-such-tileset";
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- BeginTilePaintStrokeIntent: rejected while Play is running ------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.playCurrentScene().ok);
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
    }

    // -- UpdateTilePaintStrokeIntent: fast movement interpolates with no gaps,
    // across a chunk boundary and into negative coordinates -------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {14, 0}}).ok);
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{18, 0}}).ok);   // crosses chunk boundary (chunkSize=16)
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{-2, 0}}).ok);   // crosses into negative
        CHECK(c.state().tilemapEditor.pendingStroke->changes.size()
              == static_cast<std::size_t>(18 - (-2) + 1));   // every cell from -2..18, no gaps
    }

    // -- UpdateTilePaintStrokeIntent: a revisited cell keeps its first `before`,
    // and the stroke's stamp is captured at Begin - reselecting mid-stroke
    // never changes the stroke already in progress ----------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{1, 0}}).ok);
        c.apply(SelectPaintTileIntent{"tile-2"});                 // mid-stroke reselect
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{0, 0}}).ok);   // revisit (0,0)

        const auto& changes = c.state().tilemapEditor.pendingStroke->changes;
        CHECK(changes.size() == 2);   // (0,0) and (1,0), each exactly once
        bool foundOrigin = false;
        for (const auto& [key, change] : changes) {
            if (change.cell.cellX == 0 && change.cell.cellY == 0) {
                foundOrigin = true;
                CHECK(!change.before.has_value());          // empty before the stroke
                CHECK(change.after.has_value());
                CHECK(change.after->tileId == "tile-1");     // Begin-captured stamp, not "tile-2"
            }
        }
        CHECK(foundOrigin);
    }

    // -- Full stroke lifecycle via the router's own normalize+execute shape:
    // pointer-up produces exactly one Command regardless of cell count --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{5, 0}}).ok);
        const std::size_t before = c.undoSize();
        {
            const PendingTileStroke& stroke = *c.state().tilemapEditor.pendingStroke;
            std::vector<TilemapCellChange> changes = normalizePaintStrokeChanges(stroke.changes);
            CHECK(!changes.empty());
            CHECK(c.execute(PaintTilemapCellsCommand{stroke.sceneId, stroke.entityId,
                                                     std::move(changes)}).ok);
        }
        c.apply(EndTilePaintStrokeIntent{});
        CHECK(c.undoSize() == before + 1);   // exactly one Command, however many cells
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
    }

    // -- No-op stroke (painting a cell with the tile it already has) produces
    // an empty normalized delta and no Command --------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        {
            const PendingTileStroke& stroke = *c.state().tilemapEditor.pendingStroke;
            std::vector<TilemapCellChange> changes = normalizePaintStrokeChanges(stroke.changes);
            CHECK(c.execute(PaintTilemapCellsCommand{stroke.sceneId, stroke.entityId,
                                                     std::move(changes)}).ok);
        }
        c.apply(EndTilePaintStrokeIntent{});

        const std::size_t before = c.undoSize();
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        const PendingTileStroke& stroke = *c.state().tilemapEditor.pendingStroke;
        const std::vector<TilemapCellChange> changes = normalizePaintStrokeChanges(stroke.changes);
        CHECK(changes.empty());   // already tile-1 there - before == after
        c.apply(EndTilePaintStrokeIntent{});
        CHECK(c.undoSize() == before);   // normalization skipped the Command entirely
    }

    // -- Escape/lost-focus-equivalent cancel: no Command, no dirty -------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.apply(CancelTilePaintStrokeIntent{}).ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
    }

    // -- SelectPaintTileIntent (Picker): adapts to a 1x1 stamp with tileset
    // provenance; zero dirty/revision/history impact ---------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);   // executes Commands: the doc is already dirty here
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        const bool dirtyBefore = c.document().isDirty();
        CHECK(c.apply(SelectPaintTileIntent{"tile-1"}).ok);
        CHECK(c.state().tilemapEditor.stamp.has_value());
        const TilemapTileStamp& stamp = *c.state().tilemapEditor.stamp;
        CHECK(stamp.sourceTilesetAssetId == "tiles-1");
        CHECK(stamp.width == 1 && stamp.height == 1);
        CHECK(stampPrimaryTileId(stamp) == std::optional<TileId>{"tile-1"});
        // sliceTilesOne slices a 64x64 sheet into 32x32 tiles: "tile-1" is
        // grid cell (0,0), resolved as provenance by the adapter.
        CHECK(stamp.sourceColumn == 0 && stamp.sourceRow == 0);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
        CHECK(c.document().isDirty() == dirtyBefore);   // selection never touches dirty
    }

    // -- SelectPaintTileIntent without a tilemap target is rejected ------------
    {
        EditorCoordinator c{makeSpriteDoc()};   // kHero has no tilemap, nothing selected
        CHECK(!c.apply(SelectPaintTileIntent{"tile-1"}).ok);
        CHECK(!c.state().tilemapEditor.stamp.has_value());
    }

    // ======================= multi-tile stamps (coordinator) =======================
    // sliceTilesOne: 64x64 sheet, 32x32 tiles -> tile-1..tile-4 in a 2x2 grid.
    // -- SelectPaintStampIntent: accepts a valid 2x2, rejects the invalid ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        TilemapTileStamp block;
        block.sourceTilesetAssetId = "tiles-1";
        block.sourceColumn = 0; block.sourceRow = 0;
        block.width = 2; block.height = 2;
        block.tiles = {TileId("tile-1"), TileId("tile-2"), TileId("tile-3"), TileId("tile-4")};
        CHECK(c.apply(SelectPaintStampIntent{block}).ok);
        CHECK(c.state().tilemapEditor.stamp->width == 2);

        TilemapTileStamp allHoles = block;
        allHoles.tiles.assign(4, std::nullopt);
        CHECK(!c.apply(SelectPaintStampIntent{allHoles}).ok);       // structurally invalid
        TilemapTileStamp wrongTileset = block;
        wrongTileset.sourceTilesetAssetId = "tiles-other";
        CHECK(!c.apply(SelectPaintStampIntent{wrongTileset}).ok);   // provenance mismatch
        TilemapTileStamp unknownId = block;
        unknownId.tiles[0] = TileId("no-such-tile");
        CHECK(!c.apply(SelectPaintStampIntent{unknownId}).ok);      // id not in the tileset
        CHECK(c.state().tilemapEditor.stamp->tiles == block.tiles); // last valid stamp survives
    }

    // -- Brush with a 2x2 stamp: a single click paints the whole footprint -----
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        TilemapTileStamp block;
        block.sourceTilesetAssetId = "tiles-1";
        block.width = 2; block.height = 2;
        block.tiles = {TileId("tile-1"), TileId("tile-2"), TileId("tile-3"), TileId("tile-4")};
        CHECK(c.apply(SelectPaintStampIntent{block}).ok);
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {5, 7}}).ok);
        const auto& changes = c.state().tilemapEditor.pendingStroke->changes;
        CHECK(changes.size() == 4);   // one click = four cells
        const auto afterAt = [&](int x, int y) -> std::string {
            const auto it = changes.find(packTilemapCellCoord({x, y}));
            return (it != changes.end() && it->second.after) ? it->second.after->tileId : "";
        };
        CHECK(afterAt(5, 7) == "tile-1");
        CHECK(afterAt(6, 7) == "tile-2");
        CHECK(afterAt(5, 8) == "tile-3");
        CHECK(afterAt(6, 8) == "tile-4");
        c.apply(EndTilePaintStrokeIntent{});
    }

    // -- Brush with a holed stamp: three cells, the hole touches nothing -------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        TilemapTileStamp block;
        block.sourceTilesetAssetId = "tiles-1";
        block.width = 2; block.height = 2;
        block.tiles = {TileId("tile-1"), std::nullopt, TileId("tile-3"), TileId("tile-4")};
        CHECK(c.apply(SelectPaintStampIntent{block}).ok);
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        const auto& changes = c.state().tilemapEditor.pendingStroke->changes;
        CHECK(changes.size() == 3);
        CHECK(changes.find(packTilemapCellCoord({1, 0})) == changes.end());   // the hole
        c.apply(EndTilePaintStrokeIntent{});
    }

    // -- Brush drag: every interpolated anchor stamps a footprint; overlapping
    // footprints keep one entry per coordinate with the first `before` ---------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        TilemapTileStamp block;
        block.sourceTilesetAssetId = "tiles-1";
        block.width = 2; block.height = 2;
        block.tiles = {TileId("tile-1"), TileId("tile-2"), TileId("tile-3"), TileId("tile-4")};
        CHECK(c.apply(SelectPaintStampIntent{block}).ok);
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{3, 0}}).ok);   // fast horizontal drag
        const auto& changes = c.state().tilemapEditor.pendingStroke->changes;
        // Anchors 0..3 each stamp a 2x2 -> columns 0..4, rows 0..1, no gaps.
        CHECK(changes.size() == 10);
        for (int x = 0; x <= 4; ++x) {
            CHECK(changes.find(packTilemapCellCoord({x, 0})) != changes.end());
            CHECK(changes.find(packTilemapCellCoord({x, 1})) != changes.end());
        }
        // Overlap: cell (1,0) was tile-2 (anchor 0) then tile-1 (anchor 1) -
        // one entry, before still the original empty, after the last touch.
        const auto& overlap = changes.at(packTilemapCellCoord({1, 0}));
        CHECK(!overlap.before.has_value());
        CHECK(overlap.after->tileId == "tile-1");
        c.apply(EndTilePaintStrokeIntent{});
    }

    // -- Rectangle with a 2x2 stamp: modular pattern anchored at startCell,
    // wrapping correctly when dragged up-left of the anchor --------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        TilemapTileStamp block;
        block.sourceTilesetAssetId = "tiles-1";
        block.width = 2; block.height = 2;
        block.tiles = {TileId("tile-1"), TileId("tile-2"), TileId("tile-3"), TileId("tile-4")};
        CHECK(c.apply(SelectPaintStampIntent{block}).ok);
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.apply(UpdateTileRectangleIntent{{-1, -1}}).ok);   // drag up-left
        CHECK(c.apply(CommitTileRectangleIntent{}).ok);
        const TilemapComponent& tm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        // Euclidean modulo from anchor (0,0): (-1,-1) wraps to slot (1,1).
        CHECK(readTilemapCell(tm, {-1, -1})->tileId == "tile-4");
        CHECK(readTilemapCell(tm, {0, -1})->tileId == "tile-3");
        CHECK(readTilemapCell(tm, {-1, 0})->tileId == "tile-2");
        CHECK(readTilemapCell(tm, {0, 0})->tileId == "tile-1");
    }

    // -- Fill with a 2x2 stamp: pattern floods the bounded region --------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        // Wall off a 2x2 interior with tile-1, then pattern-fill it.
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        c.apply(SetRectangleShapeModeIntent{false});
        CHECK(c.apply(UpdateTileRectangleIntent{{3, 3}}).ok);
        CHECK(c.apply(CommitTileRectangleIntent{}).ok);
        std::vector<TilemapCellChange> clearInterior;
        for (int y = 1; y <= 2; ++y)
            for (int x = 1; x <= 2; ++x)
                clearInterior.push_back(TilemapCellChange{
                    {x, y}, TilemapCellValue{"tile-1", TileTransformFlags::None}, std::nullopt});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, clearInterior}).ok);

        TilemapTileStamp block;
        block.sourceTilesetAssetId = "tiles-1";
        block.width = 2; block.height = 2;
        block.tiles = {TileId("tile-1"), TileId("tile-2"), TileId("tile-3"), TileId("tile-4")};
        CHECK(c.apply(SelectPaintStampIntent{block}).ok);
        CHECK(c.apply(FillTilemapIntent{kSceneA, kHero, {1, 1}}).ok);
        const TilemapComponent& tm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        CHECK(readTilemapCell(tm, {1, 1})->tileId == "tile-1");   // anchor (1,1) = slot (0,0)
        CHECK(readTilemapCell(tm, {2, 1})->tileId == "tile-2");
        CHECK(readTilemapCell(tm, {1, 2})->tileId == "tile-3");
        CHECK(readTilemapCell(tm, {2, 2})->tileId == "tile-4");
    }

    // -- Palette views: per-tileset, workspace-only, validated, pruned ---------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        const uint64_t revision = c.document().revision();
        CHECK(!c.apply(SetTilePaletteZoomIntent{"tiles-1", 0.f}).ok);            // non-positive
        CHECK(!c.apply(SetTilePaletteZoomIntent{"no-such-tileset", 2.f}).ok);    // unknown tileset
        CHECK(c.apply(SetTilePaletteZoomIntent{"tiles-1", 4.f}).ok);
        const auto pan = c.apply(PanTilePaletteIntent{"tiles-1", Vec2{10.f, -5.f}});
        CHECK(pan.ok);
        CHECK(pan.invalidation == EditorInvalidation::None);   // scroll-only, no dock rebuild
        const auto& views = c.state().tilemapEditor.paletteViews;
        CHECK(views.at("tiles-1").textureScale == 4.f);
        CHECK(views.at("tiles-1").scrollOffset.x == 10.f
              && views.at("tiles-1").scrollOffset.y == -5.f);
        CHECK(views.at("tiles-1").initialized);
        const auto scroll = c.apply(SetTilePaletteScrollIntent{"tiles-1", Vec2{-20.f, -8.f}});
        CHECK(scroll.ok);
        CHECK(scroll.invalidation == EditorInvalidation::None);
        CHECK(views.at("tiles-1").scrollOffset.x == -20.f
              && views.at("tiles-1").scrollOffset.y == -8.f);
        CHECK(c.apply(SetTilePaletteZoomIntent{"tiles-1", 100.f}).ok);           // clamped
        CHECK(views.at("tiles-1").textureScale == TilePaletteViewLimits::kZoomMax);
        CHECK(c.document().revision() == revision);   // workspace-only, never dirties

        // Deleting the tileset prunes its view on the next reconcile.
        CHECK(c.execute(RemoveTilemapComponentCommand{kSceneA, kHero}).ok);
        CHECK(c.execute(RemoveTilesetAssetCommand{"tiles-1"}).ok);
        CHECK(views.find("tiles-1") == views.end());
    }

    // -- Palette projection: absolute texture scale + scroll (no fit×min floor) -
    {
        TilePaletteViewState view;
        view.textureScale = 2.f;
        view.scrollOffset = Vec2{12.f, -4.f};
        const TilePaletteViewportProjection proj =
            computeTilePaletteProjection(view, 0.f, 0.f, 400.f, 300.f, 256.f, 128.f);
        CHECK(proj.textureScale == 2.f);
        CHECK(proj.sheetWidth == 512.f);
        CHECK(proj.sheetHeight == 256.f);
        CHECK(proj.sheetX == 12.f);
        CHECK(proj.sheetY == -4.f);

        // 16px tiles: readable step prefers 2× (32 px on screen).
        CHECK(tilePaletteStepForReadableTiles(16, 16) == 2);

        const TilePaletteSourceBounds content{16, 16, 64, 32};
        const TilePaletteViewState fitted = makeInitialTilePaletteView(
            200.f, 100.f, 256.f, 128.f, 16, 16, content);
        CHECK(fitted.initialized);
        CHECK(fitted.textureScale >= 1.f);
    }

    // -- Slice 5: Tile Palette dock auto-opens on Tilemap select; height stays
    // session-only memory in EditorUiState (clamped); Fit requests are pending
    // Intents; sparse empty masks shrink content bounds; large sheets clamp scroll.
    {
        EditorCoordinator c{makeSpriteDoc()};
        CHECK(c.apply(SetTilePaletteDockVisibleIntent{false}).ok);
        CHECK(!c.uiState().tilePaletteDockVisible);
        setUpTilemapForPainting(c);   // SelectEntity on Tilemap → dock visible
        CHECK(c.uiState().tilePaletteDockVisible);

        // Manual hide sticks until the next Tilemap select.
        CHECK(c.apply(SetTilePaletteDockVisibleIntent{false}).ok);
        CHECK(!c.uiState().tilePaletteDockVisible);
        CHECK(c.apply(SelectEntityIntent{kHero}).ok);
        CHECK(c.uiState().tilePaletteDockVisible);

        // Selecting a non-Tilemap entity does not force the dock closed.
        CHECK(c.apply(SetTilePaletteDockVisibleIntent{false}).ok);
        CHECK(c.execute(CreateEntityCommand{kSceneA, 777, "Enemy", "plain", {}}).ok);
        CHECK(c.apply(SelectEntityIntent{777}).ok);
        CHECK(!c.uiState().tilePaletteDockVisible);

        // Dock height memory + clamp (UiState only — never ProjectDocument).
        const uint64_t revision = c.document().revision();
        CHECK(c.apply(ResizePanelIntent{ResizePanelIntent::Panel::TilePaletteDock, 9999.f}).ok);
        CHECK(c.uiState().tilePaletteDockHeight == PanelLimits::kTilePaletteDockMax);
        CHECK(c.apply(ResizePanelIntent{ResizePanelIntent::Panel::TilePaletteDock, 1.f}).ok);
        CHECK(c.uiState().tilePaletteDockHeight == PanelLimits::kTilePaletteDockMin);
        CHECK(c.apply(ResizePanelIntent{ResizePanelIntent::Panel::TilePaletteDock, 280.f}).ok);
        CHECK(c.uiState().tilePaletteDockHeight == 280.f);
        CHECK(c.document().revision() == revision);

        // Fit Intent only queues pendingPaletteFit; app bakes SetTilePaletteView.
        CHECK(c.apply(RequestTilePaletteFitIntent{"tiles-1", TilePaletteFitKind::Content}).ok);
        CHECK(c.state().tilemapEditor.pendingPaletteFit.has_value());
        CHECK(c.state().tilemapEditor.pendingPaletteFit->kind == TilePaletteFitKind::Content);
        CHECK(c.apply(RequestTilePaletteFitIntent{"tiles-1", TilePaletteFitKind::Selection}).ok);
        CHECK(c.state().tilemapEditor.pendingPaletteFit->kind == TilePaletteFitKind::Selection);
        CHECK(c.document().revision() == revision);

        const TilesetAsset* tileset = c.document().findTilesetAsset("tiles-1");
        CHECK(tileset != nullptr);
        CHECK(tileset->tiles.size() == 4);
        // Sparse sheet: only tile-4 (bottom-right) is non-empty → content shrinks.
        std::vector<bool> empties{true, true, true, false};
        const TilePaletteSourceBounds sparse =
            tilePaletteContentBounds(*tileset, nullptr, &empties);
        CHECK(sparse.valid());
        CHECK(sparse.x == tileset->tiles[3].x);
        CHECK(sparse.y == tileset->tiles[3].y);
        CHECK(sparse.width == tileset->tiles[3].width);
        CHECK(sparse.height == tileset->tiles[3].height);

        const TilePaletteSourceBounds fullSheet{0, 0, 256, 128};
        const TilePaletteSourceBounds contentIsland{64, 32, 32, 32};
        const TilePaletteViewState fitContent = makeFitTilePaletteView(
            TilePaletteFitKind::Content, 200.f, 100.f, 256.f, 128.f, 16, 16,
            fullSheet, contentIsland, std::nullopt);
        const TilePaletteViewState fitSheet = makeFitTilePaletteView(
            TilePaletteFitKind::Sheet, 200.f, 100.f, 256.f, 128.f, 16, 16,
            fullSheet, contentIsland, std::nullopt);
        CHECK(fitContent.initialized && fitSheet.initialized);
        // Framing a small island yields a larger scale than fitting the whole sheet.
        CHECK(fitContent.textureScale >= fitSheet.textureScale);

        // Fit Selection without stamp bounds falls back to Content, never Sheet.
        const TilePaletteViewState fitSelFallback = makeFitTilePaletteView(
            TilePaletteFitKind::Selection, 200.f, 100.f, 256.f, 128.f, 16, 16,
            fullSheet, contentIsland, std::nullopt);
        CHECK(fitSelFallback.initialized);
        CHECK(fitSelFallback.textureScale == fitContent.textureScale);
        CHECK(fitSelFallback.scrollOffset.x == fitContent.scrollOffset.x);
        CHECK(fitSelFallback.scrollOffset.y == fitContent.scrollOffset.y);

        // Zoom remapping keeps the texture point under the viewport centre.
        const Vec2 beforeScroll{-40.f, -10.f};
        const Vec2 remapped = remapTilePaletteScrollForZoom(
            2.f, 4.f, beforeScroll, 100.f, 50.f);
        CHECK(std::fabs(remapped.x - (100.f - (100.f - beforeScroll.x) * 2.f)) < 0.01f);
        CHECK(std::fabs(remapped.y - (50.f - (50.f - beforeScroll.y) * 2.f)) < 0.01f);

        // Fractional fit scale: wheel steps to the adjacent integer, not lround.
        CHECK(tilePaletteNextZoomStep(1.4f, true) == 2);
        CHECK(tilePaletteNextZoomStep(1.4f, false) == 1);
        CHECK(tilePaletteNextZoomStep(2.0f, true) == 3);
        CHECK(tilePaletteNextZoomStep(2.0f, false) == 1);
        CHECK(tilePaletteNextZoomStep(1.0f, false) == 1);

        // Large sheet at high zoom: scroll clamps into reachable range.
        TilePaletteViewState huge;
        huge.textureScale = 8.f;
        huge.scrollOffset = Vec2{-99999.f, 99999.f};
        const TilePaletteViewportProjection bigProj =
            computeTilePaletteProjection(huge, 0.f, 0.f, 320.f, 180.f, 2048.f, 2048.f);
        const Vec2 clamped = clampTilePaletteScrollOffset(
            huge.scrollOffset, bigProj.viewportWidth, bigProj.viewportHeight,
            bigProj.sheetWidth, bigProj.sheetHeight);
        CHECK(clamped.x == 320.f - bigProj.sheetWidth);
        CHECK(clamped.y == 0.f);
        CHECK(clamped.x > huge.scrollOffset.x);
        CHECK(clamped.y < huge.scrollOffset.y);
    }

    // -- PaintTilemapCellsCommand: paints a multi-chunk, negative-coordinate
    // delta correctly ------------------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);   // chunkSize = 16
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{14, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{18, 0}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{-3, -3}, std::nullopt,
                                            TilemapCellValue{"tile-3", TileTransformFlags::FlipX}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        const TilemapComponent& tm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        CHECK(tm.chunks.size() == 3);
        CHECK(readTilemapCell(tm, {14, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {18, 0})->tileId == "tile-2");
        CHECK(readTilemapCell(tm, {-3, -3})->tileId == "tile-3");
        CHECK(readTilemapCell(tm, {-3, -3})->flags == TileTransformFlags::FlipX);
    }

    // -- PaintTilemapCellsCommand: Undo restores exactly, Redo reproduces ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 1}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.undo().ok);
        const SceneInstanceDef* undone = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(!readTilemapCell(*undone->tilemap, {0, 0}).has_value());
        CHECK(!readTilemapCell(*undone->tilemap, {1, 1}).has_value());
        CHECK(undone->tilemap->chunks.empty());
        CHECK(c.redo().ok);
        const SceneInstanceDef* redone = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(readTilemapCell(*redone->tilemap, {0, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(*redone->tilemap, {1, 1})->tileId == "tile-2");
    }

    // -- PaintTilemapCellsCommand: empty delta, missing entity, missing
    // component, unknown tile id in `after` all rejected cleanly ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(!c.execute(PaintTilemapCellsCommand{kSceneA, kHero, {}}).ok);
        CHECK(!c.execute(PaintTilemapCellsCommand{
            kSceneA, 9999,
            {TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}}}}).ok);
        CHECK(!c.execute(PaintTilemapCellsCommand{
            kSceneA, kHero,
            {TilemapCellChange{{0, 0}, std::nullopt,
                              TilemapCellValue{"no-such-tile", TileTransformFlags::None}}}}).ok);

        EditorCoordinator noTilemap{makeSpriteDoc()};   // kHero has no Tilemap component
        CHECK(!noTilemap.execute(PaintTilemapCellsCommand{
            kSceneA, kHero,
            {TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}}}}).ok);
    }

    // -- PaintTilemapCellsCommand: missing tileset rejected --------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        TilemapComponent tm;
        tm.tilesetAssetId = "no-such-tileset";
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!c.execute(PaintTilemapCellsCommand{
            kSceneA, kHero,
            {TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}}}}).ok);
    }

    // -- PaintTilemapCellsCommand: duplicate cell in the delta rejected --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        CHECK(!c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
    }

    // -- PaintTilemapCellsCommand: before-mismatch fails atomically, the
    // document keeps the value an intervening mutation actually set ----------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> first;
        first.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                          TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, first}).ok);

        // A stale delta claiming (0,0) was still empty - as if built before
        // the paint above landed.
        std::vector<TilemapCellChange> stale;
        stale.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                          TilemapCellValue{"tile-2", TileTransformFlags::None}});
        const std::size_t before = c.undoSize();
        CHECK(!c.execute(PaintTilemapCellsCommand{kSceneA, kHero, stale}).ok);
        CHECK(c.undoSize() == before);
        CHECK(readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap,
                              {0, 0})->tileId == "tile-1");
    }

    // -- PaintTilemapCellsCommand: one invalid change among N valid ones
    // rejects the whole Command - none of the N valid ones land ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 0}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{2, 0}, std::nullopt,
                                            TilemapCellValue{"no-such-tile", TileTransformFlags::None}});
        CHECK(!c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->chunks.empty());
    }

    // -- Eraser removing a chunk's last tile removes the chunk; Undo recreates
    // it with its original cell intact -----------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.chunkSize = 1;   // one cell per chunk: simplest possible "last tile in chunk"
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);

        std::vector<TilemapCellChange> paint;
        paint.push_back(TilemapCellChange{{5, 5}, std::nullopt,
                                          TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, paint}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->chunks.size() == 1);

        std::vector<TilemapCellChange> erase;
        erase.push_back(TilemapCellChange{{5, 5}, TilemapCellValue{"tile-1", TileTransformFlags::None},
                                          std::nullopt});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, erase}).ok);
        CHECK(c.document().findInstanceInScene(kSceneA, kHero)->tilemap->chunks.empty());

        CHECK(c.undo().ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap->chunks.size() == 1);
        CHECK(readTilemapCell(*inst->tilemap, {5, 5})->tileId == "tile-1");
    }

    // -- Persistence: paint across multiple chunks (including negative
    // coordinates and a non-None flags value), save, reload, verify identical
    // chunk content/layout -----------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{14, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{-3, -3}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::FlipX}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);

        const std::filesystem::path path = testTempDir() / "tilemap-painted.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const SceneInstanceDef* rt = reloaded.document().findInstanceInScene(kSceneA, kHero);
        CHECK(rt->tilemap.has_value());
        CHECK(rt->tilemap->chunks.size() == 2);
        CHECK(readTilemapCell(*rt->tilemap, {14, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(*rt->tilemap, {-3, -3})->tileId == "tile-2");
        CHECK(readTilemapCell(*rt->tilemap, {-3, -3})->flags == TileTransformFlags::FlipX);
    }

    // -- Regression: an unpainted Tilemap still yields an empty-cells snapshot
    // entry (Slice 5's placeholder fallback is unaffected) ---------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        const auto it = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(it != snap.tilemaps.end());
        CHECK(it->cells.empty());
    }

    // -- Regression: a freshly painted cell is pickable via pickEntityAt -------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {0.f, 0.f}}).ok);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        CHECK(pickEntityAt(snap, Vec2{10.f, 10.f}) == kHero);   // inside the painted 32x32 cell at origin
    }

    // ================= Eraser-via-right-click momentary override =================
    // effectiveTilemapTool(): no override -> the persistent tool -------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);   // Brush only sticks with a selection that supports it
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        CHECK(c.effectiveTilemapTool() == EditorTool::Brush);
    }

    // BeginTemporaryToolOverrideIntent: layers over the persistent tool without
    // ever writing activeTool - this is the whole point of the design ------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        const auto begin = c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        CHECK(begin.ok);
        CHECK(begin.invalidation == (EditorInvalidation::Inspector | EditorInvalidation::Toolbar));
        CHECK(c.effectiveTilemapTool() == EditorTool::Eraser);
        CHECK(c.state().activeTool == EditorTool::Brush);   // persistent selection untouched
    }

    // EndTemporaryToolOverrideIntent: clears it, reverting to the persistent tool -
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        const auto end = c.apply(EndTemporaryToolOverrideIntent{});
        CHECK(end.ok);
        CHECK(c.effectiveTilemapTool() == EditorTool::Brush);
    }

    // EndTemporaryToolOverrideIntent with nothing set: harmless no-op, safe for
    // every termination path (commit/cancel/failed-Begin) to call unconditionally
    {
        EditorCoordinator c{makeSpriteDoc()};
        const auto end = c.apply(EndTemporaryToolOverrideIntent{});
        CHECK(end.ok);
        CHECK(end.invalidation == EditorInvalidation::None);
    }

    // Full gesture: Brush selected, right-click-Eraser overrides for one stroke,
    // paints as Eraser, then ends - the persistent tool is Brush throughout and
    // straight after, matching how a real Brush->right-click->release cycle must
    // feel (no re-click needed to keep painting with Brush) ----------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        std::vector<TilemapCellChange> seed;
        seed.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                         TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, seed}).ok);

        c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        CHECK(c.effectiveTilemapTool() == EditorTool::Eraser);
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
        CHECK(c.state().activeTool == EditorTool::Brush);   // still Brush mid-gesture

        const PendingTileStroke& stroke = *c.state().tilemapEditor.pendingStroke;
        std::vector<TilemapCellChange> changes = normalizePaintStrokeChanges(stroke.changes);
        CHECK(!changes.empty());
        CHECK(c.execute(PaintTilemapCellsCommand{stroke.sceneId, stroke.entityId, changes}).ok);
        c.apply(EndTilePaintStrokeIntent{});
        c.apply(EndTemporaryToolOverrideIntent{});

        CHECK(!readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {0, 0}).has_value());
        CHECK(c.state().activeTool == EditorTool::Brush);
        CHECK(c.effectiveTilemapTool() == EditorTool::Brush);   // back to Brush, no re-click needed
        CHECK(!c.state().tilemapEditor.temporaryToolOverride.has_value());
    }

    // Entity deleted mid-stroke while an override is active: reconcileWorkspace
    // must drop the override too, or it would be stuck on Eraser with no stroke
    // left to ever trigger its own cleanup ----------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SetActiveToolIntent{EditorTool::Brush});
        c.apply(BeginTemporaryToolOverrideIntent{EditorTool::Eraser});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Eraser, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.temporaryToolOverride.has_value());
        CHECK(c.execute(DeleteEntityCommand{kSceneA, kHero}).ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(!c.state().tilemapEditor.temporaryToolOverride.has_value());
        // The deleted entity was the selection too - with nothing left to
        // paint, reconcileTilemapEditingContext() (run by reconcileWorkspace
        // after the DeleteEntityCommand) falls the persistent tool back to
        // Select as well, not just the override.
        CHECK(c.effectiveTilemapTool() == EditorTool::Select);
        CHECK(c.state().activeTool == EditorTool::Select);
    }

    // -- computeTilesetResliceImpact: counts only the orphans - surviving
    // painted ids and unrelated tilesets contribute nothing --------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);   // "tiles-1" sliced tile-1..4, tilemap on kHero
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 0}, std::nullopt,
            TilemapCellValue{"tile-4", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);

        std::vector<TileDefinition> keepOne;   // hypothetical: keeps tile-1, drops tile-4
        keepOne.push_back(TileDefinition{"tile-1", 0, 0, 32, 32});
        const TilesetResliceImpact partial =
            computeTilesetResliceImpact(c.document(), "tiles-1", keepOne);
        CHECK(partial.removedReferencedTiles == 1);
        CHECK(partial.orphanedCells == 1);
        CHECK(partial.affectedTilemaps == 1);

        const TilesetResliceImpact unrelated =
            computeTilesetResliceImpact(c.document(), "no-such-tileset", keepOne);
        CHECK(unrelated.removedReferencedTiles == 0);
        CHECK(unrelated.orphanedCells == 0);
        CHECK(unrelated.affectedTilemaps == 0);
    }

    // -- Cascade: a 64x64 re-slice orphans every painted cell; apply clears
    // them (chunk pruned, document still valid), undo restores cells, tiles
    // and slicing exactly, redo clears again ------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 0}, std::nullopt,
            TilemapCellValue{"tile-4", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);

        TilesetSlicing big;
        big.tileWidth = 64;
        big.tileHeight = 64;
        const std::vector<TileDefinition> fresh = tilesForSlicing(64, 64, big);
        CHECK(fresh.size() == 1);
        const std::vector<TileDefinition> reconciled =
            reconcileTiles(c.document().findTilesetAsset("tiles-1")->tiles, fresh);
        CHECK(reconciled.size() == 1);
        CHECK(reconciled[0].id != "tile-1");   // removed ids are never recycled

        const TilesetResliceImpact impact =
            computeTilesetResliceImpact(c.document(), "tiles-1", reconciled);
        CHECK(impact.removedReferencedTiles == 2);
        CHECK(impact.orphanedCells == 2);
        CHECK(impact.affectedTilemaps == 1);

        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-1", big, reconciled}).ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap->chunks.empty());   // both cells cleared -> chunk pruned
        CHECK(!validateTilemapComponent(c.document(), *inst->tilemap).has_value());

        CHECK(c.undo().ok);
        inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(readTilemapCell(*inst->tilemap, {0, 0}).has_value());
        CHECK(readTilemapCell(*inst->tilemap, {0, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(*inst->tilemap, {1, 0})->tileId == "tile-4");
        CHECK(c.document().findTilesetAsset("tiles-1")->tiles.size() == 4);
        CHECK(c.document().findTilesetAsset("tiles-1")->slicing.tileWidth == 32);
        CHECK(!validateTilemapComponent(c.document(), *inst->tilemap).has_value());

        CHECK(c.redo().ok);
        inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap->chunks.empty());
        CHECK(c.document().findTilesetAsset("tiles-1")->slicing.tileWidth == 64);
    }

    // -- Cascade clears only the orphaned cells: a painted cell whose rect
    // (and so id) survives the re-slice keeps its content ----------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 0}, std::nullopt,
            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);

        // spacingX=32 over the 64x64 sheet keeps one 32x32 column: rects
        // (0,0) and (0,32) survive as tile-1/tile-3; tile-2 and tile-4 go.
        TilesetSlicing spaced;
        spaced.tileWidth = 32;
        spaced.tileHeight = 32;
        spaced.spacingX = 32;
        const std::vector<TileDefinition> fresh = tilesForSlicing(64, 64, spaced);
        CHECK(fresh.size() == 2);
        const std::vector<TileDefinition> reconciled =
            reconcileTiles(c.document().findTilesetAsset("tiles-1")->tiles, fresh);
        CHECK(reconciled[0].id == "tile-1");
        CHECK(reconciled[1].id == "tile-3");

        const TilesetResliceImpact impact =
            computeTilesetResliceImpact(c.document(), "tiles-1", reconciled);
        CHECK(impact.removedReferencedTiles == 1);   // only tile-2 is painted
        CHECK(impact.orphanedCells == 1);

        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-1", spaced, reconciled}).ok);
        const SceneInstanceDef* inst = c.document().findInstanceInScene(kSceneA, kHero);
        CHECK(inst->tilemap->chunks.size() == 1);   // survivor keeps its chunk alive
        CHECK(readTilemapCell(*inst->tilemap, {0, 0})->tileId == "tile-1");
        CHECK(!readTilemapCell(*inst->tilemap, {1, 0}).has_value());
        CHECK(!validateTilemapComponent(c.document(), *inst->tilemap).has_value());
    }

    // ======================= tryOffsetCell =======================
    {
        constexpr int kMax = std::numeric_limits<int>::max();
        constexpr int kMin = std::numeric_limits<int>::min();
        CHECK(tryOffsetCell(TilemapCellCoord{0, 0}, 1, 0)->cellX == 1);
        CHECK(tryOffsetCell(TilemapCellCoord{5, -5}, -1, 1)->cellX == 4);
        CHECK(tryOffsetCell(TilemapCellCoord{5, -5}, -1, 1)->cellY == -4);
        CHECK(!tryOffsetCell(TilemapCellCoord{kMax, 0}, 1, 0).has_value());
        CHECK(!tryOffsetCell(TilemapCellCoord{kMin, 0}, -1, 0).has_value());
        CHECK(!tryOffsetCell(TilemapCellCoord{0, kMax}, 0, 1).has_value());
        CHECK(!tryOffsetCell(TilemapCellCoord{0, kMin}, 0, -1).has_value());
        CHECK(tryOffsetCell(TilemapCellCoord{kMax, 0}, -1, 0).has_value());   // moving away from the edge is fine
    }

    // ======================= rectangleFillChanges =======================
    // -- 3x3 solid: exactly 9 cells, all empty -> tile-1 -----------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleFillChanges(tm, {0, 0}, {2, 2}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 9);
        for (const TilemapCellChange& c : r.changes) {
            CHECK(!c.before.has_value());
            CHECK(c.after == tile1);
        }
    }

    // -- Inverted corners produce the identical result as a forward drag ------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult forward = rectangleFillChanges(tm, {0, 0}, {2, 2}, tile1);
        const TileRegionBuildResult inverted = rectangleFillChanges(tm, {2, 2}, {0, 0}, tile1);
        CHECK(forward.changes.size() == 9);
        CHECK(inverted.changes.size() == 9);
    }

    // -- Negative coordinates ---------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleFillChanges(tm, {-5, -5}, {-3, -3}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 9);
    }

    // -- No-op: painting a region that's already entirely the target tile -----
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        writeTilemapCell(tm, {0, 0}, tile1);
        writeTilemapCell(tm, {1, 0}, tile1);
        const TileRegionBuildResult r = rectangleFillChanges(tm, {0, 0}, {1, 0}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- Within-limit large area succeeds (200 x 300 = 60000 <= 65536) ---------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleFillChanges(tm, {0, 0}, {199, 299}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 60000);
    }

    // -- Over-limit rectangle rejected, no partial changes ---------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleFillChanges(tm, {0, 0}, {299, 299}, tile1);   // 90000 cells
        CHECK(r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- Extreme coordinates, small area: widened arithmetic gives the exact
    // result with no overflow ---------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        constexpr int kMax = std::numeric_limits<int>::max();
        const TileRegionBuildResult r = rectangleFillChanges(tm, {kMax - 2, 0}, {kMax, 0}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 3);
    }

    // -- Astronomically huge span (near-full int32 range on both axes):
    // rejected immediately, no overflow, no hang ---------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        constexpr int kMax = std::numeric_limits<int>::max();
        constexpr int kMin = std::numeric_limits<int>::min();
        const TileRegionBuildResult r = rectangleFillChanges(tm, {kMin, kMin}, {kMax, kMax}, tile1);
        CHECK(r.error.has_value());
        CHECK(r.changes.empty());
    }

    // ======================= rectangleOutlineChanges =======================
    // -- 5x5 outline: perimeter only (16 cells), not the 25-cell area ----------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {4, 4}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 16);
    }

    // -- 1x1 ---------------------------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {3, 3}, {3, 3}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 1);
    }

    // -- 1xN (single column): the full strip, no interior to exclude -----------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {0, 6}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 7);
    }

    // -- Nx1 (single row) ---------------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {6, 0}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 7);
    }

    // -- 2x2: every cell is border, no interior ----------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {1, 1}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 4);
    }

    // -- Outline stays O(perimeter): a 300x300 outline is nowhere near its own
    // 90000-cell area (which alone would exceed the limit) ----------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {299, 299}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 1196);   // 2*300 + 2*300 - 4
    }

    // -- Outline over the limit rejected (huge perimeter) ------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {40000, 40000}, tile1);
        CHECK(r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- No-op outline: border already uniform -----------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        for (int x = 0; x <= 2; ++x) {
            writeTilemapCell(tm, {x, 0}, tile1);
            writeTilemapCell(tm, {x, 2}, tile1);
        }
        writeTilemapCell(tm, {0, 1}, tile1);
        writeTilemapCell(tm, {2, 1}, tile1);
        const TileRegionBuildResult r = rectangleOutlineChanges(tm, {0, 0}, {2, 2}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.empty());   // all 8 border cells already tile-1
    }

    // ======================= floodFillChanges =======================
    // -- target == replacement: immediate empty result, no traversal ----------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell tile1 = TilemapCellValue{"tile-1", TileTransformFlags::None};
        writeTilemapCell(tm, {0, 0}, tile1);
        const TileRegionBuildResult r = floodFillChanges(tm, {0, 0}, tile1);
        CHECK(!r.error.has_value());
        CHECK(r.changes.empty());
    }
    {
        // Empty -> empty (nullopt == nullopt) is also a same-value no-op.
        TilemapComponent tm; tm.chunkSize = 16;
        const TileRegionBuildResult r = floodFillChanges(tm, {100, 100}, std::nullopt);
        CHECK(!r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- Enclosed region bounded by a different tile id: fill stops at the
    // border, only the interior empty cells change ------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell wall = TilemapCellValue{"tile-2", TileTransformFlags::None};
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        // 5x5 ring of walls (cells (0,0)..(4,4)) around a 3x3 empty interior.
        for (int x = 0; x <= 4; ++x) {
            writeTilemapCell(tm, {x, 0}, wall);
            writeTilemapCell(tm, {x, 4}, wall);
        }
        for (int y = 0; y <= 4; ++y) {
            writeTilemapCell(tm, {0, y}, wall);
            writeTilemapCell(tm, {4, y}, wall);
        }
        const TileRegionBuildResult r = floodFillChanges(tm, {2, 2}, fillTile);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 9);   // the 3x3 interior only
        for (const TilemapCellChange& c : r.changes) {
            CHECK(c.cell.cellX >= 1 && c.cell.cellX <= 3);
            CHECK(c.cell.cellY >= 1 && c.cell.cellY <= 3);
            CHECK(!c.before.has_value());
            CHECK(c.after == fillTile);
        }
    }

    // -- Diagonal-only connectivity does not spread (4-connected only) --------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell wall = TilemapCellValue{"tile-2", TileTransformFlags::None};
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        // 5x5 grid, walls everywhere except four single-cell pockets at the
        // "diagonal" corners (1,1)/(3,1)/(1,3)/(3,3) - each fully enclosed by
        // walls on all four sides, so 4-connectivity cannot cross between
        // them even though they sit diagonally adjacent to one another.
        for (int y = 0; y <= 4; ++y) {
            for (int x = 0; x <= 4; ++x) {
                const bool pocket = (x == 1 || x == 3) && (y == 1 || y == 3);
                if (!pocket) writeTilemapCell(tm, {x, y}, wall);
            }
        }
        const TileRegionBuildResult r = floodFillChanges(tm, {1, 1}, fillTile);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 1);   // stays in its own pocket; the other three untouched
        CHECK(r.changes[0].cell.cellX == 1 && r.changes[0].cell.cellY == 1);
    }

    // -- Negative coordinates -----------------------------------------------------
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell wall = TilemapCellValue{"tile-2", TileTransformFlags::None};
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        for (int x = -3; x <= -1; ++x) {
            writeTilemapCell(tm, {x, -3}, wall);
            writeTilemapCell(tm, {x, -1}, wall);
        }
        for (int y = -3; y <= -1; ++y) {
            writeTilemapCell(tm, {-3, y}, wall);
            writeTilemapCell(tm, {-1, y}, wall);
        }
        const TileRegionBuildResult r = floodFillChanges(tm, {-2, -2}, fillTile);
        CHECK(!r.error.has_value());
        CHECK(r.changes.size() == 1);
        CHECK(r.changes[0].cell.cellX == -2 && r.changes[0].cell.cellY == -2);
    }

    // -- Open (unenclosed) empty region: cancelled at the limit, zero changes -
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        const TileRegionBuildResult r = floodFillChanges(tm, {0, 0}, fillTile);   // fully open grid
        CHECK(r.error.has_value());
        CHECK(r.changes.empty());
    }

    // -- Near-INT_MAX origin on an open region: bounded on two sides by the
    // coordinate edge, still hits the operation limit rather than overflowing -
    {
        TilemapComponent tm; tm.chunkSize = 16;
        const TilemapCell fillTile = TilemapCellValue{"tile-1", TileTransformFlags::None};
        constexpr int kMax = std::numeric_limits<int>::max();
        const TileRegionBuildResult r = floodFillChanges(tm, {kMax, kMax}, fillTile);
        CHECK(r.error.has_value());   // still open toward -x/-y -> hits the cap, never overflows
        CHECK(r.changes.empty());
    }

    // -- Rectangle/Fill deltas commit and undo/redo through the same
    // PaintTilemapCellsCommand as Brush/Eraser - no parallel Command --------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        const TilemapComponent& tm0 = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        const TileRegionBuildResult built =
            rectangleFillChanges(tm0, {0, 0}, {2, 2}, TilemapCellValue{"tile-1", TileTransformFlags::None});
        CHECK(!built.error.has_value());
        CHECK(built.changes.size() == 9);
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, built.changes}).ok);
        for (int y = 0; y <= 2; ++y) {
            for (int x = 0; x <= 2; ++x) {
                CHECK(readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {x, y})
                          ->tileId == "tile-1");
            }
        }
        CHECK(c.undo().ok);
        CHECK(!readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {0, 0}).has_value());
        CHECK(c.redo().ok);
        CHECK(readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {0, 0})->tileId
              == "tile-1");
    }

    // -- Persistence: a Rectangle-painted delta survives save/reload ------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        const TilemapComponent& tm0 = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        const TileRegionBuildResult built =
            rectangleFillChanges(tm0, {0, 0}, {2, 2}, TilemapCellValue{"tile-1", TileTransformFlags::None});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, built.changes}).ok);

        const std::filesystem::path path = testTempDir() / "tilemap-rectangle.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        const TilemapComponent& tm = *reloaded.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        for (int y = 0; y <= 2; ++y) {
            for (int x = 0; x <= 2; ++x) {
                CHECK(readTilemapCell(tm, {x, y})->tileId == "tile-1");
            }
        }
    }

    // ======================= BeginTileRectangleIntent =======================
    // -- success: captures scene/entity/tile/shape/start+current cell ---------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);   // no tile selected
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {2, 3}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle.has_value());
        const PendingTileRectangle& rect = *c.state().tilemapEditor.pendingRectangle;
        CHECK(rect.sceneId == kSceneA);
        CHECK(rect.entityId == kHero);
        CHECK(rect.startCell.cellX == 2 && rect.startCell.cellY == 3);
        CHECK(rect.currentCell.cellX == 2 && rect.currentCell.cellY == 3);
        CHECK(!rect.outlineOnly);
        CHECK(stampPrimaryTileId(rect.stamp) == std::optional<TileId>{"tile-1"});
        CHECK(rect.stamp.sourceTilesetAssetId == "tiles-1");
        CHECK(rect.previewChanges.size() == 1);
    }

    // -- missing component / locked layer / missing tileset / Play running
    // rejected, mirroring BeginTilePaintStrokeIntent's own guards ---------------
    {
        EditorCoordinator c{makeSpriteDoc()};   // kHero has no tilemap
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
    }
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneDef& scene = doc.scenes.at(kSceneA);
        SceneLayerDef locked;
        locked.id = "locked-layer";
        locked.name = "Locked";
        locked.locked = true;
        scene.layers.push_back(locked);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        // Hand-authored directly on the fixture (not via AddTilemapComponentCommand,
        // which now itself rejects adding a component on a locked layer) - this
        // test's target is BeginTileRectangleIntent's own lock guard.
        scene.instances.front().tilemap = tm;
        scene.instances.front().layerId = "locked-layer";
        EditorCoordinator c{doc};
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
    }
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.playCurrentScene().ok);
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
    }

    // -- Shape captured at Begin: flipping the toggle mid-drag never changes
    // the in-progress rectangle --------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetRectangleShapeModeIntent{true});   // Outline mode active
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle->outlineOnly);
        c.apply(SetRectangleShapeModeIntent{false});   // flip mid-drag
        CHECK(c.apply(UpdateTileRectangleIntent{{3, 3}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle->outlineOnly);   // unchanged: still Outline
        CHECK(c.state().tilemapEditor.pendingRectangle->previewChanges.size() == 12);   // 4x4 perimeter
    }

    // -- Tile reselected mid-drag never changes the in-progress rectangle ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        c.apply(SelectPaintTileIntent{"tile-2"});   // reselect mid-drag: must not affect this rectangle
        CHECK(c.apply(UpdateTileRectangleIntent{{1, 1}}).ok);
        CHECK(c.apply(CommitTileRectangleIntent{}).ok);
        const TilemapComponent& tm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        CHECK(readTilemapCell(tm, {0, 0})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {1, 1})->tileId == "tile-1");
    }

    // -- Selecting a different entity mid-drag cancels the pending rectangle,
    // never redirects or auto-commits it: a gesture belongs to whatever was
    // selected when it started, and a selection change always discards it
    // (Scene View Selection & Tool Context slice) -------------------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef other;
        other.id = 99;
        other.objectTypeId = "Other";
        other.instanceName = "Other";
        doc.scenes.at(kSceneA).instances.push_back(other);
        EditorCoordinator c{doc};
        sliceTilesOne(c);
        TilemapComponent tmA; tmA.tilesetAssetId = "tiles-1";
        TilemapComponent tmB; tmB.tilesetAssetId = "tiles-1";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tmA}).ok);
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, 99, tmB}).ok);

        c.apply(SelectEntityIntent{kHero});   // the stamp adapter resolves the selected tilemap
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);   // starts on kHero
        CHECK(c.state().tilemapEditor.pendingRectangle.has_value());
        c.apply(SelectEntityIntent{99});   // user clicks the other entity mid-drag
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());   // discarded, not redirected
        CHECK(!c.apply(UpdateTileRectangleIntent{{1, 1}}).ok);          // nothing left to update
        CHECK(!c.apply(CommitTileRectangleIntent{}).ok);                // nothing left to commit

        const TilemapComponent& heroTm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        const TilemapComponent& otherTm = *c.document().findInstanceInScene(kSceneA, 99)->tilemap;
        CHECK(!readTilemapCell(heroTm, {0, 0}).has_value());     // abandoned rectangle never painted
        CHECK(!readTilemapCell(otherTm, {0, 0}).has_value());    // never touched
    }

    // ======================= UpdateTileRectangleIntent =======================
    // -- Preview cache only recomputes when currentCell actually changes -------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.apply(UpdateTileRectangleIntent{{2, 2}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle->previewChanges.size() == 9);
        const EditorOperationResult again = c.apply(UpdateTileRectangleIntent{{2, 2}});   // same cell
        CHECK(again.ok);
        CHECK(again.invalidation == EditorInvalidation::None);
        CHECK(c.state().tilemapEditor.pendingRectangle->previewChanges.size() == 9);
    }

    // ======================= CommitTileRectangleIntent =======================
    // -- Over-limit rectangle rejected at commit: no Command, pending cleared,
    // and the preview never blanked mid-drag (kept the last valid box) ---------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.apply(UpdateTileRectangleIntent{{299, 299}}).ok);   // 90000 cells: over the limit
        CHECK(c.state().tilemapEditor.pendingRectangle->previewChanges.size() == 1);   // unchanged from Begin
        const std::size_t before = c.undoSize();
        CHECK(!c.apply(CommitTileRectangleIntent{}).ok);
        CHECK(c.undoSize() == before);
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
    }

    // -- No-op commit (rectangle over an already-uniform region): no Command,
    // pending still cleared -------------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> seed;
        seed.push_back(TilemapCellChange{{0, 0}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, seed}).ok);

        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);   // single cell, already tile-1
        const std::size_t before = c.undoSize();
        CHECK(c.apply(CommitTileRectangleIntent{}).ok);
        CHECK(c.undoSize() == before);   // no-op: no Command
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
    }

    // ======================= CancelTileRectangleIntent =======================
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle.has_value());
        CHECK(c.apply(CancelTileRectangleIntent{}).ok);
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
    }

    // ======================= Mutual exclusion =======================
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(!c.apply(BeginTileRectangleIntent{kSceneA, kHero, {1, 1}}).ok);   // stroke already pending
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
        c.apply(EndTilePaintStrokeIntent{});

        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(!c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {1, 1}}).ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());   // rectangle pending: stroke rejected
    }

    // -- Switching tool mid-drag cancels whichever operation is pending --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.apply(SetActiveToolIntent{EditorTool::Rectangle}).ok);
        CHECK(!c.state().tilemapEditor.pendingStroke.has_value());
        CHECK(c.state().activeTool == EditorTool::Rectangle);

        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.apply(SetActiveToolIntent{EditorTool::Fill}).ok);
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
    }

    // ======================= reconcileWorkspace =======================
    // -- Entity deleted mid-drag: pendingRectangle is cleared, and so is the
    // stamp (there is no longer a target tileset it could belong to - Scene
    // View Selection & Tool Context slice); rectangleOutlineMode is a
    // general drawing preference, not tied to any tileset, so it survives ------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        c.apply(SetRectangleShapeModeIntent{true});
        CHECK(c.apply(BeginTileRectangleIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.state().tilemapEditor.pendingRectangle.has_value());
        CHECK(c.execute(DeleteEntityCommand{kSceneA, kHero}).ok);
        CHECK(!c.state().tilemapEditor.pendingRectangle.has_value());
        CHECK(!c.state().tilemapEditor.stamp.has_value());
        CHECK(c.state().tilemapEditor.rectangleOutlineMode);
    }

    // ======================= FillTilemapIntent =======================
    // -- success: floods a bounded interior region, exactly one Command --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> walls;
        for (int x = 0; x <= 4; ++x) {
            walls.push_back(TilemapCellChange{{x, 0}, std::nullopt, TilemapCellValue{"tile-2", TileTransformFlags::None}});
            walls.push_back(TilemapCellChange{{x, 4}, std::nullopt, TilemapCellValue{"tile-2", TileTransformFlags::None}});
        }
        for (int y = 1; y <= 3; ++y) {
            walls.push_back(TilemapCellChange{{0, y}, std::nullopt, TilemapCellValue{"tile-2", TileTransformFlags::None}});
            walls.push_back(TilemapCellChange{{4, y}, std::nullopt, TilemapCellValue{"tile-2", TileTransformFlags::None}});
        }
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, walls}).ok);

        c.apply(SelectPaintTileIntent{"tile-1"});
        const std::size_t before = c.undoSize();
        CHECK(c.apply(FillTilemapIntent{kSceneA, kHero, {2, 2}}).ok);
        CHECK(c.undoSize() == before + 1);
        const TilemapComponent& tm = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        CHECK(readTilemapCell(tm, {2, 2})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {1, 1})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {3, 3})->tileId == "tile-1");
        CHECK(readTilemapCell(tm, {0, 0})->tileId == "tile-2");   // wall untouched
    }

    // -- No tile selected rejected ------------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(!c.apply(FillTilemapIntent{kSceneA, kHero, {0, 0}}).ok);
    }

    // -- Rejected while Play is running -------------------------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.playCurrentScene().ok);
        CHECK(!c.apply(FillTilemapIntent{kSceneA, kHero, {0, 0}}).ok);
    }

    // -- Open region: cancelled, no Command, no dirty -----------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(!c.apply(FillTilemapIntent{kSceneA, kHero, {0, 0}}).ok);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
    }

    // -- True no-op (target already the selected tile): no Command, no dirty ---
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> seed;
        seed.push_back(TilemapCellChange{{2, 2}, std::nullopt, TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, seed}).ok);
        c.apply(SelectPaintTileIntent{"tile-1"});
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(c.apply(FillTilemapIntent{kSceneA, kHero, {2, 2}}).ok);   // target already tile-1: true no-op
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
    }

    // ======================= SetRectangleShapeModeIntent =======================
    {
        EditorCoordinator c{makeSpriteDoc()};
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(!c.state().tilemapEditor.rectangleOutlineMode);
        CHECK(c.apply(SetRectangleShapeModeIntent{true}).ok);
        CHECK(c.state().tilemapEditor.rectangleOutlineMode);
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
        CHECK(!c.document().isDirty());
    }

    // -- A tilemap referencing a missing tileset no longer blocks Play --------
    // RU-03 (D-01): entity-owned TilemapComponent data is never loaded or
    // validated by GameplaySession/World (the dropped rendering capability
    // documented above) - a dangling tilesetAssetId simply goes unused,
    // rather than rejecting Play the way the old hand-written materialize()
    // did.
    {
        ProjectDoc doc = makeSpriteDoc();
        TilemapComponent tm;
        tm.tilesetAssetId = "no-such-tileset";
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(c.playProject().ok);
    }

    // -- A tileset referencing a missing image asset no longer blocks Play ----
    {
        ProjectDoc doc = makeSpriteDoc();
        TilesetAsset orphan;
        orphan.assetId = "tiles-orphan";
        orphan.imageAssetId = "missing-image";
        doc.tilesets.push_back(orphan);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-orphan";
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(c.playProject().ok);
    }

    // RU-03 (D-01): "Play materializes a RuntimeTilemap identical to
    // tilemapRenderCells" and "a tilemap follows its owning entity during
    // Play" removed - both asserted per-instance/entity-owned tilemap
    // rendering during Play, which is a known, deliberately accepted gap now
    // (World's RenderableEntitySnapshot has no per-entity tilemap field; only
    // the legacy scene-level merged grid renders - see play_session.h). See
    // "An empty (unpainted) tilemap is fully invisible in Play" below, which
    // now covers the painted case too since neither renders anymore.

    // -- An unknown TileId in a chunk no longer blocks Play -------------------
    // RU-03 (D-01): same dropped validation as the missing-tileset/-image
    // cases above - entity-owned tilemap content is never resolved by Play,
    // so an unresolvable TileId simply goes unused rather than rejecting.
    // Normal authoring commands still reject this state before it can be
    // saved, so this remains a hand-crafted ProjectDoc.
    {
        ProjectDoc doc = makeSpriteDoc();
        TileDefinition validTile;
        validTile.id = "tile-1";
        validTile.width = 32;
        validTile.height = 32;
        doc.tilesets.front().tiles.push_back(validTile);

        TilemapComponent tm;
        tm.tilesetAssetId = doc.tilesets.front().assetId;
        tm.chunkSize = 16;
        TilemapChunk chunk;
        chunk.chunkX = 0;
        chunk.chunkY = 0;
        chunk.cells.assign(16 * 16, std::nullopt);
        chunk.cells[0] = TilemapCellValue{"no-such-tile", TileTransformFlags::None};   // unresolvable
        tm.chunks.push_back(chunk);
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;

        EditorCoordinator c{doc};
        CHECK(c.playProject().ok);
    }

    // -- Two tilemaps on different tilesets sharing one image asset ------------
    // RU-03: PlaySession no longer exposes the loaded asset catalog (D-01);
    // image dedup is GameplaySession/asset-system's own concern, already
    // covered there. This only checks Play still starts with two tilemaps on
    // different tilesets that share one underlying image.
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef other;
        other.id = 99;
        other.objectTypeId = "Other";
        other.instanceName = "Other";
        other.layerId = "layer-1";
        doc.scenes.at(kSceneA).instances.push_back(other);
        EditorCoordinator c{doc};
        TilesetSlicing slicing;
        slicing.tileWidth = 32;
        slicing.tileHeight = 32;
        const std::vector<TileDefinition> tiles1 = tilesForSlicing(64, 64, slicing);
        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-1", slicing, tiles1}).ok);
        CHECK(c.execute(AddTilesetAssetCommand{"tiles-2", "Second", "img-hero", slicing}).ok);
        const std::vector<TileDefinition> tiles2 = tilesForSlicing(64, 64, slicing);
        CHECK(c.execute(ChangeTilesetSlicingCommand{"tiles-2", slicing, tiles2}).ok);

        TilemapComponent tmA; tmA.tilesetAssetId = "tiles-1";
        TilemapComponent tmB; tmB.tilesetAssetId = "tiles-2";
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tmA}).ok);
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, 99, tmB}).ok);

        CHECK(c.playCurrentScene().ok);
    }

    // -- Layer order determines Play's RENDER order -----------------------
    // RU-03: PlaySession no longer exposes the simulation container directly
    // (D-01) - only the render hand-off. Layer-priority sorting is
    // GameplaySession's own (already characterized by runtime-cpp's test
    // suite); this only checks Play still starts with layered entities that
    // have no sprite renderer (so they don't appear in GameplaySession's
    // renderables at all - only sprite-bearing entities are enumerated).
    {
        EditorCoordinator c{ProjectDoc{}};
        const SceneId sceneId = "layered-scene";
        CHECK(c.execute(CreateSceneCommand{sceneId, "Layered"}).ok);   // creates "layer-1" (default)
        CHECK(c.execute(AddSceneLayerCommand{sceneId, "bg", "Background", 0}).ok);
        CHECK(c.execute(AddSceneLayerCommand{sceneId, "fg", "Foreground", 2}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
            sceneId, 501, "type-mid", "Mid", "Mid", Vec2{}, ""}).ok);          // default layer
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
            sceneId, 502, "type-fg", "Fg", "Fg", Vec2{}, "fg"}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
            sceneId, 503, "type-bg", "Bg", "Bg", Vec2{}, "bg"}).ok);

        CHECK(c.apply(SelectSceneIntent{sceneId}).ok);
        CHECK(c.playCurrentScene().ok);
    }

    // -- An empty (unpainted) tilemap is fully invisible in Play - unlike Edit,
    // which deliberately shows the generic placeholder for it -----------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.playCurrentScene().ok);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        CHECK(std::none_of(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; }));
        CHECK(std::none_of(snap.entities.begin(), snap.entities.end(),
            [](const SceneFrameEntity& e) { return e.entityId == kHero; }));
        CHECK(std::none_of(snap.sprites.begin(), snap.sprites.end(),
            [](const SceneFrameSprite& s) { return s.entityId == kHero; }));

        // Edit, by contrast, still shows the placeholder for the same document.
        const SceneFrameSnapshot editSnap =
            collectSceneFrameSnapshot(c.document(), kSceneA, INVALID_ENTITY);
        CHECK(std::any_of(editSnap.entities.begin(), editSnap.entities.end(),
            [](const SceneFrameEntity& e) { return e.entityId == kHero; }));
    }

    // -- Painted tilemap cells render in Play (ADR-0001 / editor Scene View) ---
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{1, 0}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.playCurrentScene().ok);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        const SceneFrameTilemap* playTm = nullptr;
        for (const SceneFrameTilemap& tm : snap.tilemaps) {
            if (tm.entityId == kHero) { playTm = &tm; break; }
        }
        CHECK(playTm != nullptr);
        CHECK(playTm->cells.size() == 2);
        CHECK(!playTm->imageAssetId.empty());
        CHECK(std::any_of(snap.entities.begin(), snap.entities.end(),
            [](const SceneFrameEntity& e) { return e.entityId == kHero; }));
    }

    // -- Stop leaves ProjectDocument, dirty, revision and Undo untouched -------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        const uint64_t revision = c.document().revision();
        const std::size_t undoBefore = c.undoSize();

        CHECK(c.playCurrentScene().ok);
        CHECK(c.stopPlaying().ok);
        CHECK(!c.isPlaying());
        // Revision is unchanged by Play/Stop - the paint above is the only
        // authoring mutation, so the document is dirty because of that, not
        // because of Play.
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == undoBefore);
        CHECK(readTilemapCell(*c.document().findInstanceInScene(kSceneA, kHero)->tilemap, {0, 0})
                  ->tileId == "tile-1");
    }

    // RU-03 previously removed Play tilemap rendering; ADR-0001 editor Play
    // path is restored above ("Painted tilemap cells render in Play").
    // Save/load of TilemapComponent and cell-coordinate math remain covered
    // by earlier tests in this file.
    return reportAndExit("tileset-tilemap-test");
}
