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

    // -- UpdateTilePaintStrokeIntent: a revisited cell keeps its first `before`
    // and its last `after` (the exact empty -> tileA -> tileB example) --------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        c.apply(SelectPaintTileIntent{"tile-1"});
        CHECK(c.apply(BeginTilePaintStrokeIntent{kSceneA, kHero, EditorTool::Brush, {0, 0}}).ok);
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{1, 0}}).ok);
        c.apply(SelectPaintTileIntent{"tile-2"});
        CHECK(c.apply(UpdateTilePaintStrokeIntent{{0, 0}}).ok);   // revisit (0,0), tile-2 now selected

        const auto& changes = c.state().tilemapEditor.pendingStroke->changes;
        CHECK(changes.size() == 2);   // (0,0) and (1,0), each exactly once
        bool foundOrigin = false;
        for (const auto& [key, change] : changes) {
            if (change.cell.cellX == 0 && change.cell.cellY == 0) {
                foundOrigin = true;
                CHECK(!change.before.has_value());          // empty before the stroke
                CHECK(change.after.has_value());
                CHECK(change.after->tileId == "tile-2");     // last touch wins
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

    // -- SelectPaintTileIntent (Picker): zero dirty/revision/history impact ----
    {
        EditorCoordinator c{makeSpriteDoc()};
        const uint64_t revision = c.document().revision();
        const std::size_t before = c.undoSize();
        CHECK(c.apply(SelectPaintTileIntent{"tile-1"}).ok);
        CHECK(c.state().tilemapEditor.selectedTileId.has_value());
        CHECK(*c.state().tilemapEditor.selectedTileId == "tile-1");
        CHECK(c.document().revision() == revision);
        CHECK(c.undoSize() == before);
        CHECK(!c.document().isDirty());
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
        CHECK(begin.invalidation == EditorInvalidation::Inspector);
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
        CHECK(rect.replacement.has_value() && rect.replacement->tileId == "tile-1");
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
    // -- Entity deleted mid-drag: pendingRectangle is cleared, and so is
    // selectedTileId (there is no longer a target tileset it could belong to
    // - Scene View Selection & Tool Context slice); rectangleOutlineMode is a
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
        CHECK(!c.state().tilemapEditor.selectedTileId.has_value());
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

    // -- Play rejected: tilemap references a missing tileset ------------------
    {
        ProjectDoc doc = makeSpriteDoc();
        TilemapComponent tm;
        tm.tilesetAssetId = "no-such-tileset";
        doc.scenes.at(kSceneA).instances.front().tilemap = tm;
        EditorCoordinator c{doc};
        CHECK(!c.playProject().ok);
        CHECK(!c.isPlaying());
    }

    // -- Play rejected: tileset references a missing image asset ---------------
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
        CHECK(!c.playProject().ok);
    }

    // -- Play materializes a RuntimeTilemap identical to tilemapRenderCells,
    // and registers the tileset's image in the asset catalog ------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{0, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{-2, 3}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {100.f, 50.f}}).ok);

        CHECK(c.playCurrentScene().ok);
        const RuntimeEntity* runtime = c.playSession()->findEntity(kHero);
        CHECK(runtime != nullptr);
        CHECK(runtime->tilemap.has_value());
        CHECK(runtime->tilemap->imageAssetId == "img-hero");   // tiles-1's underlying image
        CHECK(c.playSession()->assets().imageAssets.count("img-hero") == 1);

        const TilemapComponent& authored = *c.document().findInstanceInScene(kSceneA, kHero)->tilemap;
        const TilesetAsset* tileset = c.document().findTilesetAsset("tiles-1");
        const std::optional<std::vector<TilemapResolvedCell>> expected =
            resolveTilemapCellsStrict(authored, *tileset);
        CHECK(expected.has_value());
        CHECK(runtime->tilemap->cellSize.x == authored.cellSize.x);
        CHECK(runtime->tilemap->cellSize.y == authored.cellSize.y);
        CHECK(runtime->tilemap->cells.size() == expected->size());
        CHECK(runtime->tilemap->cells.size() == 2);
        for (std::size_t i = 0; i < expected->size(); ++i) {
            CHECK(runtime->tilemap->cells[i].cellX == (*expected)[i].cellX);
            CHECK(runtime->tilemap->cells[i].cellY == (*expected)[i].cellY);
            CHECK(runtime->tilemap->cells[i].sourceRect.x == (*expected)[i].source.x);
            CHECK(runtime->tilemap->cells[i].sourceRect.y == (*expected)[i].source.y);
        }

        // The snapshot's destination is computed from the entity's *current*
        // transform (100, 50) at collect time, not baked in at materialize.
        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        const auto tilemapIt = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(tilemapIt != snap.tilemaps.end());
        const std::vector<SceneFrameTilemapCell> expectedFrame =
            tilemapRenderCells(authored, *tileset, {100.f, 50.f});
        CHECK(tilemapIt->cells.size() == expectedFrame.size());
        for (std::size_t i = 0; i < expectedFrame.size(); ++i) {
            CHECK(tilemapIt->cells[i].destination.x == expectedFrame[i].destination.x);
            CHECK(tilemapIt->cells[i].destination.y == expectedFrame[i].destination.y);
        }
    }

    // -- A tilemap follows its owning entity if it moves during Play
    // (LinearMover): destination is recomputed from the current transform
    // every frame, never cached at materialize time --------------------------
    {
        EditorCoordinator c{makeInheritedDoc()};   // gives kHero a real "Hero" object type
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{1, 1}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.execute(AddLinearMoverCommand{"Hero"}).ok);
        CHECK(c.execute(SetLinearMoverDirectionCommand{"Hero", Vec2{1.f, 0.f}}).ok);
        CHECK(c.execute(SetLinearMoverSpeedCommand{"Hero", 100.f}).ok);
        const uint64_t revision = c.document().revision();

        CHECK(c.playCurrentScene().ok);
        const SceneFrameSnapshot before = collectSceneFrameSnapshot(*c.playSession());
        const auto beforeIt = std::find_if(before.tilemaps.begin(), before.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(beforeIt != before.tilemaps.end());
        CHECK(beforeIt->cells.size() == 1);
        const float startX = beforeIt->cells[0].destination.x;
        const float startY = beforeIt->cells[0].destination.y;

        c.advanceRuntime(0.5f);   // 0.5s * 100 units/s = 50 units along +X

        const SceneFrameSnapshot after = collectSceneFrameSnapshot(*c.playSession());
        const auto afterIt = std::find_if(after.tilemaps.begin(), after.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(afterIt != after.tilemaps.end());
        CHECK(afterIt->cells.size() == 1);
        CHECK(afterIt->cells[0].destination.x == startX + 50.f);
        CHECK(afterIt->cells[0].destination.y == startY);
        CHECK(c.document().revision() == revision);   // simulation never touches ProjectDocument
    }

    // -- An unknown TileId in a chunk rejects Play atomically, rather than
    // silently starting with content quietly missing. Normal authoring
    // commands already reject this state before it can be saved, so this is a
    // hand-crafted ProjectDoc, mirroring the file's own "dangling asset"
    // test pattern (e.g. the missing-tileset/-image cases above) ------------
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
        CHECK(!c.playProject().ok);
        CHECK(!c.isPlaying());
    }

    // -- An entity with a TilemapComponent but no painted cells materializes
    // an empty RuntimeTilemap, not a missing one - the image is still known --
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        CHECK(c.playCurrentScene().ok);
        const RuntimeEntity* runtime = c.playSession()->findEntity(kHero);
        CHECK(runtime->tilemap.has_value());
        CHECK(runtime->tilemap->cells.empty());
    }

    // -- Two tilemaps on different tilesets sharing one image asset load it
    // exactly once (same PlayAssetCatalogSnapshot dedup as sprites) -----------
    {
        ProjectDoc doc = makeSpriteDoc();
        SceneInstanceDef other;
        other.id = 99;
        other.objectTypeId = "Other";
        other.instanceName = "Other";
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
        CHECK(c.playSession()->assets().imageAssets.size() == 1);   // one image, two tilesets
        CHECK(c.playSession()->assets().imageAssets.count("img-hero") == 1);
    }

    // -- Layer order determines Play's RENDER order only, never simulation
    // order: RuntimeScene::entities stays structural (creation order),
    // RuntimeScene::renderOrder carries the visual back-to-front order -------
    {
        EditorCoordinator c{ProjectDoc{}};
        const SceneId sceneId = "layered-scene";
        CHECK(c.execute(CreateSceneCommand{sceneId, "Layered"}).ok);   // creates "layer-1" (default)
        CHECK(c.execute(AddSceneLayerCommand{sceneId, "bg", "Background", 0}).ok);
        CHECK(c.execute(AddSceneLayerCommand{sceneId, "fg", "Foreground", 2}).ok);
        // Created in structural order 501, 502, 503, but placed on layers so
        // the VISUAL order is the reverse: 503 (bg), 501 (default), 502 (fg).
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
            sceneId, 501, "type-mid", "Mid", "Mid", Vec2{}, ""}).ok);          // default layer
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
            sceneId, 502, "type-fg", "Fg", "Fg", Vec2{}, "fg"}).ok);
        CHECK(c.execute(CreateEntityWithDefaultTypeCommand{
            sceneId, 503, "type-bg", "Bg", "Bg", Vec2{}, "bg"}).ok);

        CHECK(c.apply(SelectSceneIntent{sceneId}).ok);
        CHECK(c.playCurrentScene().ok);

        // Simulation container: untouched structural order.
        const std::vector<RuntimeEntity>& entities = c.playSession()->entities();
        CHECK(entities.size() == 3);
        CHECK(entities[0].id == 501);
        CHECK(entities[1].id == 502);
        CHECK(entities[2].id == 503);

        // Render order: back-to-front visual order, as indices into entities.
        const std::vector<std::size_t>& renderOrder = c.playSession()->scene().renderOrder;
        CHECK(renderOrder.size() == 3);
        CHECK(entities[renderOrder[0]].id == 503);   // Background first
        CHECK(entities[renderOrder[1]].id == 501);   // default layer, in the middle
        CHECK(entities[renderOrder[2]].id == 502);   // Foreground last

        // The Play snapshot (what actually gets drawn) follows render order.
        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        CHECK(snap.entities.size() == 3);
        CHECK(snap.entities[0].entityId == 503);
        CHECK(snap.entities[1].entityId == 501);
        CHECK(snap.entities[2].entityId == 502);
    }

    // -- collectSceneFrameSnapshot(PlaySession&): a painted tilemap appears in
    // snapshot.tilemaps and in frame.entities (SceneView render order) without
    // relying on the generic editor placeholder ----------------------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{1, 1}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.playCurrentScene().ok);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        const auto tilemapIt = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(tilemapIt != snap.tilemaps.end());
        CHECK(tilemapIt->cells.size() == 1);
        const auto entityIt = std::find_if(snap.entities.begin(), snap.entities.end(),
            [](const SceneFrameEntity& e) { return e.entityId == kHero; });
        CHECK(entityIt != snap.entities.end());
        // Play snapshot must be pickable at the painted cell (same draw-order
        // contract SceneView uses - regression for tilemaps invisible in Play).
        const SceneFrameRect& dest = tilemapIt->cells[0].destination;
        CHECK(pickEntityAt(snap, Vec2{dest.x + dest.width * 0.5f, dest.y + dest.height * 0.5f})
              == kHero);
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

    // -- Negative coordinates and a rectangular (non-square) cellSize ----------
    {
        EditorCoordinator c{makeSpriteDoc()};
        sliceTilesOne(c);
        TilemapComponent tm;
        tm.tilesetAssetId = "tiles-1";
        tm.cellSize = {16.f, 48.f};
        CHECK(c.execute(AddTilemapComponentCommand{kSceneA, kHero, tm}).ok);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{-3, -2}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {0.f, 0.f}}).ok);

        CHECK(c.playCurrentScene().ok);
        const RuntimeEntity* runtime = c.playSession()->findEntity(kHero);
        CHECK(runtime->tilemap.has_value());
        CHECK(runtime->tilemap->cells.size() == 1);
        // Runtime storage is local cell coordinates, not baked world rects.
        CHECK(runtime->tilemap->cells[0].cellX == -3);
        CHECK(runtime->tilemap->cells[0].cellY == -2);
        CHECK(runtime->tilemap->cellSize.x == 16.f);
        CHECK(runtime->tilemap->cellSize.y == 48.f);

        const SceneFrameSnapshot snap = collectSceneFrameSnapshot(*c.playSession());
        const auto tilemapIt = std::find_if(snap.tilemaps.begin(), snap.tilemaps.end(),
            [](const SceneFrameTilemap& t) { return t.entityId == kHero; });
        CHECK(tilemapIt != snap.tilemaps.end());
        CHECK(tilemapIt->cells.size() == 1);
        CHECK(tilemapIt->cells[0].destination.x == -3.f * 16.f);
        CHECK(tilemapIt->cells[0].destination.y == -2.f * 48.f);
        CHECK(tilemapIt->cells[0].destination.width == 16.f);
        CHECK(tilemapIt->cells[0].destination.height == 48.f);
    }

    // -- Save/load round-trip parity (the "export parity" criterion reinterpreted
    // for this repo, which has no separate exported-game runtime): a fresh
    // PlaySession materialized from a reloaded ProjectDocument produces the
    // identical snapshot as the one materialized before saving ----------------
    {
        EditorCoordinator c{makeSpriteDoc()};
        setUpTilemapForPainting(c);
        std::vector<TilemapCellChange> changes;
        changes.push_back(TilemapCellChange{{14, 0}, std::nullopt,
                                            TilemapCellValue{"tile-1", TileTransformFlags::None}});
        changes.push_back(TilemapCellChange{{-3, -3}, std::nullopt,
                                            TilemapCellValue{"tile-2", TileTransformFlags::FlipX}});
        CHECK(c.execute(PaintTilemapCellsCommand{kSceneA, kHero, changes}).ok);
        CHECK(c.execute(SetEntityTransformCommand{kSceneA, kHero, {40.f, 10.f}}).ok);
        CHECK(c.playCurrentScene().ok);
        const SceneFrameSnapshot directPlay = collectSceneFrameSnapshot(*c.playSession());
        CHECK(c.stopPlaying().ok);

        const std::filesystem::path path = testTempDir() / "tilemap-play-roundtrip.artcade-project";
        CHECK(saveProjectToFile(c, path).ok);
        EditorCoordinator reloaded{ProjectDoc{}};
        CHECK(loadProjectFromFile(reloaded, path).ok);
        CHECK(reloaded.playCurrentScene().ok);
        const SceneFrameSnapshot reloadedPlay = collectSceneFrameSnapshot(*reloaded.playSession());

        CHECK(directPlay.tilemaps.size() == 1);
        CHECK(directPlay.tilemaps.size() == reloadedPlay.tilemaps.size());
        CHECK(directPlay.tilemaps[0].imageAssetId == reloadedPlay.tilemaps[0].imageAssetId);
        CHECK(directPlay.tilemaps[0].cells.size() == reloadedPlay.tilemaps[0].cells.size());
        for (std::size_t i = 0; i < directPlay.tilemaps[0].cells.size(); ++i) {
            CHECK(directPlay.tilemaps[0].cells[i].destination.x
                  == reloadedPlay.tilemaps[0].cells[i].destination.x);
            CHECK(directPlay.tilemaps[0].cells[i].destination.y
                  == reloadedPlay.tilemaps[0].cells[i].destination.y);
            CHECK(directPlay.tilemaps[0].cells[i].source.x == reloadedPlay.tilemaps[0].cells[i].source.x);
            CHECK(directPlay.tilemaps[0].cells[i].source.y == reloadedPlay.tilemaps[0].cells[i].source.y);
        }
    }
    return reportAndExit("tileset-tilemap-test");
}
