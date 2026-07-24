// =============================================================================
// artcade-package-test.cpp — Phase 17: .artcade ZIP round-trip test
//
// Strategy: build a minimal in-memory ZIP (STORE mode, no compression)
// containing a project.json, write it to a temp file, then load it via
// AssetLoader::loadArtcade() and assert the resulting ProjectDoc matches.
//
// Uses only AssetLoader + zip-reader — no Raylib window opened.
// Avoids any file-system assumption about the build environment.
// =============================================================================

#include "modules/asset-system/include/asset-loader.h"
#include "zip-reader.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace ArtCade::Modules;
namespace fs = std::filesystem;

// ---- minimal test harness --------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) { \
            ++g_passed; \
        } else { \
            std::cerr << "  FAIL: " #cond "  (line " << __LINE__ << ")\n"; \
            ++g_failed; \
        } \
    } while (false)

// ---- minimal STORE-mode ZIP writer ----------------------------------------
// Produces a valid ZIP with method=0 (STORE, no compression).
// Sufficient for testing the loader; the Python packer produces DEFLATE ZIPs
// which test the sinflate path in production.

namespace MinimalZip {

static void push16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
static void push32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >>  8));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 24));
}
static void pushStr(std::vector<uint8_t>& v, const std::string& s) {
    v.insert(v.end(), s.begin(), s.end());
}
static void pushBytes(std::vector<uint8_t>& v, const std::vector<uint8_t>& b) {
    v.insert(v.end(), b.begin(), b.end());
}

// Simple CRC-32 (lookup table, polynomial 0xEDB88320)
static uint32_t crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256] = {};
    static bool     ready      = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

struct Entry {
    std::string           name;
    std::vector<uint8_t>  data;
};

std::vector<uint8_t> build(const std::vector<Entry>& entries) {
    std::vector<uint8_t> buf;
    std::vector<uint32_t> offsets;   // local file header offsets

    // Local File Headers + data
    for (const auto& e : entries) {
        offsets.push_back(static_cast<uint32_t>(buf.size()));

        const uint32_t crc  = crc32(e.data.data(), e.data.size());
        const uint32_t sz   = static_cast<uint32_t>(e.data.size());
        const uint16_t fnLen = static_cast<uint16_t>(e.name.size());

        push32(buf, 0x04034b50u);   // Local file header signature
        push16(buf, 20);            // Version needed (2.0)
        push16(buf, 0);             // General purpose bit flag
        push16(buf, 0);             // Compression method: STORE
        push16(buf, 0);             // Last mod time
        push16(buf, 0);             // Last mod date
        push32(buf, crc);           // CRC-32
        push32(buf, sz);            // Compressed size
        push32(buf, sz);            // Uncompressed size
        push16(buf, fnLen);         // Filename length
        push16(buf, 0);             // Extra field length
        pushStr(buf, e.name);       // Filename
        pushBytes(buf, e.data);     // File data (STORE = raw)
    }

    // Central Directory
    const uint32_t cdOffset = static_cast<uint32_t>(buf.size());
    for (size_t idx = 0; idx < entries.size(); ++idx) {
        const auto& e = entries[idx];
        const uint32_t crc  = crc32(e.data.data(), e.data.size());
        const uint32_t sz   = static_cast<uint32_t>(e.data.size());
        const uint16_t fnLen = static_cast<uint16_t>(e.name.size());

        push32(buf, 0x02014b50u);   // Central directory signature
        push16(buf, 20);            // Version made by
        push16(buf, 20);            // Version needed
        push16(buf, 0);             // Bit flag
        push16(buf, 0);             // Compression method: STORE
        push16(buf, 0);             // Last mod time
        push16(buf, 0);             // Last mod date
        push32(buf, crc);           // CRC-32
        push32(buf, sz);            // Compressed size
        push32(buf, sz);            // Uncompressed size
        push16(buf, fnLen);         // Filename length
        push16(buf, 0);             // Extra field length
        push16(buf, 0);             // Comment length
        push16(buf, 0);             // Disk number start
        push16(buf, 0);             // Internal attributes
        push32(buf, 0);             // External attributes
        push32(buf, offsets[idx]);  // Offset of local file header
        pushStr(buf, e.name);       // Filename
    }

    const uint32_t cdSize  = static_cast<uint32_t>(buf.size()) - cdOffset;
    const uint16_t nEntries = static_cast<uint16_t>(entries.size());

    // End of Central Directory
    push32(buf, 0x06054b50u);   // EOCD signature
    push16(buf, 0);             // Disk number
    push16(buf, 0);             // Disk with central dir
    push16(buf, nEntries);      // Entries on this disk
    push16(buf, nEntries);      // Total entries
    push32(buf, cdSize);        // Size of central directory
    push32(buf, cdOffset);      // Offset of central directory
    push16(buf, 0);             // Comment length

    return buf;
}

/** STORE entry with compSize != uncompSize (must be rejected by zip-reader). */
std::vector<uint8_t> buildStoreSizeMismatch() {
    std::vector<uint8_t> zip;
    const std::string name = "bad.bin";
    const std::vector<uint8_t> data = {'A'};
    const uint32_t crc = crc32(data.data(), data.size());

    push32(zip, 0x04034b50u);
    push16(zip, 20);
    push16(zip, 0);
    push16(zip, 0);
    push16(zip, 0);
    push16(zip, 0);
    push32(zip, crc);
    push32(zip, 1);
    push32(zip, 4);
    push16(zip, static_cast<uint16_t>(name.size()));
    push16(zip, 0);
    pushStr(zip, name);
    pushBytes(zip, data);

    const uint32_t cdOff = static_cast<uint32_t>(zip.size());
    push32(zip, 0x02014b50u);
    push16(zip, 20);
    push16(zip, 20);
    push16(zip, 0);
    push16(zip, 0);
    push16(zip, 0);
    push16(zip, 0);
    push32(zip, crc);
    push32(zip, 1);
    push32(zip, 4);
    push16(zip, static_cast<uint16_t>(name.size()));
    push16(zip, 0);
    push16(zip, 0);
    push32(zip, 0);
    pushStr(zip, name);

    push32(zip, 0x06054b50u);
    push16(zip, 0);
    push16(zip, 0);
    push16(zip, 1);
    push16(zip, 1);
    push32(zip, cdOff);
    push32(zip, static_cast<uint32_t>(zip.size() - cdOff));

    return zip;
}

} // namespace MinimalZip

// ---- helpers ---------------------------------------------------------------

static std::string tmpZipPath() {
    return (fs::temp_directory_path() / "artcade_test_roundtrip.artcade").string();
}

static void writeToDisk(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

// ---- tests -----------------------------------------------------------------

static void test_minimal_zip_load() {
    std::cout << "Test 1: loadArtcade — minimal STORE-mode ZIP\n";

    // Build a minimal project.json content
    const std::string projJson = R"({
  "projectName":   "TestProject",
  "version":       "2.0.0",
  "formatVersion": 9,
  "targetFPS":     60,
  "activeSceneId": "scene_main",
  "mainScriptPath": "scripts/main.lua",
  "globalVariables": [],
  "entities": {
    "1": {
      "id": 1,
      "name": "Hero",
      "className": "Player",
      "tags": ["player"],
      "transform": { "position": [320, 240], "scale": [1,1], "rotation": 0 },
      "sprite": { "spriteAssetId": "", "tint": [1,1,1,1], "alpha": 1, "renderOrder": 0 }
    }
  },
  "scenes": {
    "scene_main": {
      "id":   "scene_main",
      "name": "Main Scene",
      "worldSize":       [1280, 720],
      "viewportSize":    [1280, 720],
      "backgroundColor": [0.1, 0.1, 0.2, 1.0],
      "entityIds": [1],
      "layers": [ { "id": "default", "name": "Default" } ],
      "defaultLayerId": "default"
    }
  }
})";

    const std::string mainLua = "-- test script\nfunction tick(dt) end\n";

    // Pack into a minimal ZIP (STORE mode)
    using namespace MinimalZip;
    auto zipBytes = build({
        { "project.json",    std::vector<uint8_t>(projJson.begin(), projJson.end()) },
        { "scripts/main.lua",std::vector<uint8_t>(mainLua.begin(),  mainLua.end())  },
    });

    const std::string zipPath = tmpZipPath();
    writeToDisk(zipPath, zipBytes);
    CHECK(fs::exists(zipPath));
    CHECK(fs::file_size(zipPath) > 0);

    // Load via AssetLoader
    AssetLoader loader;
    loader.init();

    ArtCade::ProjectDoc doc;
    bool ok = loader.loadArtcade(zipPath, doc);

    CHECK(ok);
    if (ok) {
        CHECK(doc.projectName   == "TestProject");
        CHECK(doc.version       == "2.0.0");
        CHECK(doc.activeSceneId == "scene_main");
        CHECK(doc.targetFPS        == 60.f);

        // Entity checks
        CHECK(doc.entities.size() == 1);
        auto it = doc.entities.find(1);
        CHECK(it != doc.entities.end());
        if (it != doc.entities.end()) {
            CHECK(it->second.name      == "Hero");
            CHECK(it->second.className == "Player");
            CHECK(it->second.transform.position.x == 320.f);
            CHECK(it->second.transform.position.y == 240.f);
        }

        // Scene checks
        CHECK(doc.scenes.size() == 1);
        auto sit = doc.scenes.find("scene_main");
        CHECK(sit != doc.scenes.end());
        if (sit != doc.scenes.end()) {
            CHECK(sit->second.name          == "Main Scene");
            CHECK(sit->second.entityIds.size() == 1);
            CHECK(sit->second.entityIds[0]     == 1u);
        }
    } else {
        std::cerr << "  [artcade-package] loadArtcade() returned false — check zip-reader\n";
    }

    loader.shutdown();

    // Cleanup
    fs::remove(zipPath);
}

static void test_invalid_zip_returns_false() {
    std::cout << "Test 2: loadArtcade — corrupt ZIP returns false\n";

    const std::string path = (fs::temp_directory_path() / "artcade_corrupt.artcade").string();

    // Write garbage bytes
    std::vector<uint8_t> garbage = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x11, 0x22 };
    writeToDisk(path, garbage);

    AssetLoader loader;
    loader.init();

    ArtCade::ProjectDoc doc;
    bool ok = loader.loadArtcade(path, doc);
    CHECK(!ok);   // must return false, not crash

    loader.shutdown();
    fs::remove(path);
}

static void test_nonexistent_file_returns_false() {
    std::cout << "Test 3: loadArtcade — non-existent file returns false\n";

    AssetLoader loader;
    loader.init();

    ArtCade::ProjectDoc doc;
    bool ok = loader.loadArtcade("/does/not/exist.artcade", doc);
    CHECK(!ok);

    loader.shutdown();
}

static void test_reloading_same_artcade_path_clears_stale_extracted_files() {
    std::cout << "Test 4: loadArtcade - repeated path reload clears stale files\n";

    const std::string zipPath =
        (fs::temp_directory_path() / "artcade_test_stale_reload.artcade").string();

    const std::string projectWithOldScript = R"({
  "projectName": "StaleA",
  "formatVersion": 9,
  "activeSceneId": "s1",
  "mainScriptPath": "scripts/old.lua",
  "globalVariables": [],
  "entities": {},
  "scenes": { "s1": { "id": "s1", "name": "S1", "entityIds": [],
      "layers": [ { "id": "default", "name": "Default" } ], "defaultLayerId": "default" } }
})";
    const std::string projectWithoutOldScript = R"({
  "projectName": "StaleB",
  "formatVersion": 9,
  "activeSceneId": "s1",
  "mainScriptPath": "scripts/main.lua",
  "globalVariables": [],
  "entities": {},
  "scenes": { "s1": { "id": "s1", "name": "S1", "entityIds": [],
      "layers": [ { "id": "default", "name": "Default" } ], "defaultLayerId": "default" } }
})";
    const std::string oldLua = "-- old\n";
    const std::string mainLua = "-- main\n";

    using namespace MinimalZip;
    auto zipA = build({
        { "project.json", std::vector<uint8_t>(projectWithOldScript.begin(), projectWithOldScript.end()) },
        { "scripts/old.lua", std::vector<uint8_t>(oldLua.begin(), oldLua.end()) },
    });
    auto zipB = build({
        { "project.json", std::vector<uint8_t>(projectWithoutOldScript.begin(), projectWithoutOldScript.end()) },
        { "scripts/main.lua", std::vector<uint8_t>(mainLua.begin(), mainLua.end()) },
    });

    AssetLoader loader;
    loader.init();
    ArtCade::ProjectDoc doc;
    std::vector<uint8_t> bytes;

    writeToDisk(zipPath, zipA);
    CHECK(loader.loadArtcade(zipPath, doc));
    CHECK(loader.loadLuaBytecode("scripts/old.lua", bytes));

    writeToDisk(zipPath, zipB);
    bytes.clear();
    CHECK(loader.loadArtcade(zipPath, doc));
    CHECK(!loader.loadLuaBytecode("scripts/old.lua", bytes));
    CHECK(loader.loadLuaBytecode("scripts/main.lua", bytes));

    loader.shutdown();
    fs::remove(zipPath);
}

static void test_directory_load_still_works() {
    std::cout << "Test 5: loadDirectory — regression check still passes\n";

    // Build a temp directory with a minimal project.json
    const fs::path tmpDir = fs::temp_directory_path() / "artcade_test_dir";
    fs::create_directories(tmpDir);

    const std::string proj = R"({
  "projectName": "DirProject",
  "version": "1.0.0",
  "formatVersion": 9,
  "activeSceneId": "s1",
  "globalVariables": [],
  "entities": {},
  "scenes": {
    "s1": { "id":"s1", "name":"S1", "entityIds": [],
        "layers": [ { "id": "default", "name": "Default" } ], "defaultLayerId": "default" }
  }
})";
    {
        std::ofstream f((tmpDir / "project.json").string());
        f << proj;
    }

    AssetLoader loader;
    loader.init();

    ArtCade::ProjectDoc doc;
    bool ok = loader.loadDirectory(tmpDir.string(), doc);

    CHECK(ok);
    CHECK(doc.projectName   == "DirProject");
    CHECK(doc.activeSceneId == "s1");
    CHECK(doc.scenes.size() == 1);

    loader.shutdown();

    // Cleanup
    fs::remove_all(tmpDir);
}

static void test_zip_slip_entry_rejected() {
    std::cout << "Test 6: zipExtractAll rejects parent traversal paths\n";

    const fs::path zipPath = fs::temp_directory_path() / "artcade_zip_slip_test.zip";
    const fs::path outDir  = fs::temp_directory_path() / "artcade_zip_slip_out";
    fs::remove_all(outDir);

    const std::vector<uint8_t> payload = {'x'};
    const auto zip = MinimalZip::build({
        { "../evil.txt", payload },
    });
    {
        std::ofstream f(zipPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(zip.data()),
                static_cast<std::streamsize>(zip.size()));
    }

    CHECK(!ArtCade::zipExtractAll(zipPath.string(), outDir.string()));
    fs::remove(zipPath);
    fs::remove_all(outDir);
}

static void test_store_size_mismatch_rejected() {
    std::cout << "Test 7: zipExtractAll rejects STORE entries with mismatched sizes\n";

    const fs::path zipPath = fs::temp_directory_path() / "artcade_zip_store_test.zip";
    const fs::path outDir  = fs::temp_directory_path() / "artcade_zip_store_out";
    fs::remove_all(outDir);

    const auto zip = MinimalZip::buildStoreSizeMismatch();
    {
        std::ofstream f(zipPath, std::ios::binary);
        f.write(reinterpret_cast<const char*>(zip.data()),
                static_cast<std::streamsize>(zip.size()));
    }

    CHECK(!ArtCade::zipExtractAll(zipPath.string(), outDir.string()));
    fs::remove(zipPath);
    fs::remove_all(outDir);
}

static bool extractTempZip(const std::vector<uint8_t>& zip, const std::string& stem) {
    const fs::path zipPath = fs::temp_directory_path() / (stem + ".zip");
    const fs::path outDir = fs::temp_directory_path() / (stem + "_out");
    fs::remove_all(outDir);
    writeToDisk(zipPath.string(), zip);
    const bool ok = ArtCade::zipExtractAll(zipPath.string(), outDir.string());
    fs::remove(zipPath);
    fs::remove_all(outDir);
    return ok;
}

static void test_zip_integrity_checks() {
    std::cout << "Test 8: zipExtractAll verifies CRC, names and duplicates\n";
    const std::vector<uint8_t> payload = {'o', 'k'};

    auto badCrc = MinimalZip::build({{ "file.txt", payload }});
    const size_t dataOffset = 30u + std::string("file.txt").size();
    badCrc[dataOffset] ^= 0xFFu;
    CHECK(!extractTempZip(badCrc, "artcade_zip_bad_crc"));

    auto localNameMismatch = MinimalZip::build({{ "file.txt", payload }});
    localNameMismatch[30] = 'x';
    CHECK(!extractTempZip(localNameMismatch, "artcade_zip_bad_local_name"));

    const auto duplicate = MinimalZip::build({
        { "same.txt", payload },
        { "same.txt", payload },
    });
    CHECK(!extractTempZip(duplicate, "artcade_zip_duplicate"));
}

static void test_runtime_project_paths_are_sandboxed() {
    std::cout << "Test 9: runtime asset paths stay inside the project root\n";
    const fs::path root = fs::temp_directory_path() / "artcade_path_sandbox";
    const fs::path outside = fs::temp_directory_path() / "artcade_outside.lua";
    fs::remove_all(root);
    fs::create_directories(root / "scripts");
    const std::string sandboxProj = R"({
  "projectName": "SandboxProject",
  "formatVersion": 9,
  "activeSceneId": "s1",
  "globalVariables": [],
  "entities": {},
  "scenes": { "s1": { "id": "s1", "name": "S1", "entityIds": [],
      "layers": [ { "id": "default", "name": "Default" } ], "defaultLayerId": "default" } }
})";
    writeToDisk((root / "project.json").string(),
                std::vector<uint8_t>(sandboxProj.begin(), sandboxProj.end()));
    writeToDisk((root / "scripts" / "main.lua").string(), std::vector<uint8_t>{'o', 'k'});
    writeToDisk(outside.string(), std::vector<uint8_t>{'n', 'o'});

    AssetLoader loader;
    loader.init();
    ArtCade::ProjectDoc doc;
    CHECK(loader.loadDirectory(root.string(), doc));
    std::vector<uint8_t> bytes;
    CHECK(loader.loadLuaBytecode("scripts/main.lua", bytes));
    CHECK(!loader.loadLuaBytecode("../artcade_outside.lua", bytes));
    CHECK(!loader.loadLuaBytecode(outside.string(), bytes));
    CHECK(loader.resolveImagePath("../outside.png").empty());
    loader.shutdown();
    fs::remove_all(root);
    fs::remove(outside);
}

static void test_stale_format_version_rejected() {
    std::cout << "Test 10: loadDirectory - stale formatVersion is rejected, never migrated\n";

    const fs::path tmpDir = fs::temp_directory_path() / "artcade_test_stale_format";
    fs::create_directories(tmpDir);

    const std::string proj = R"({
  "projectName": "StaleFormatProject",
  "formatVersion": 8,
  "activeSceneId": "s1",
  "globalVariables": [],
  "entities": {},
  "scenes": {
    "s1": { "id":"s1", "name":"S1", "entityIds": [],
        "layers": [ { "id": "default", "name": "Default" } ], "defaultLayerId": "default" }
  }
})";
    {
        std::ofstream f((tmpDir / "project.json").string());
        f << proj;
    }

    AssetLoader loader;
    loader.init();
    ArtCade::ProjectDoc doc;
    CHECK(!loader.loadDirectory(tmpDir.string(), doc));

    loader.shutdown();
    fs::remove_all(tmpDir);
}

static void test_ru01a_component_readers_round_trip() {
    std::cout << "Test 11: loadDirectory - boxCollider2D/chunked tilemap/tileset slicing/fontAssets round-trip\n";

    const fs::path tmpDir = fs::temp_directory_path() / "artcade_test_ru01a_readers";
    fs::create_directories(tmpDir);

    const std::string proj = R"({
  "projectName": "Ru01aReaders",
  "formatVersion": 9,
  "activeSceneId": "s1",
  "globalVariables": [],
  "entities": {
    "1": {
      "id": 1,
      "name": "Wall",
      "className": "Wall",
      "boxCollider2D": {
        "offset": { "x": 1, "y": 2 },
        "size": { "x": 40, "y": 24 },
        "enabled": true,
        "mode": "oneWayPlatform"
      }
    }
  },
  "scenes": {
    "s1": { "id": "s1", "name": "S1", "entityIds": [1],
        "layers": [ { "id": "default", "name": "Default" } ], "defaultLayerId": "default",
        "instances": [
          { "id": 2, "objectTypeId": "Ground", "layerId": "default",
            "tilemap": {
              "tilesetAssetId": "tiles-1",
              "cellSize": { "x": 16, "y": 16 },
              "chunkSize": 8,
              "chunks": [
                { "chunkX": 0, "chunkY": 0,
                  "cells": [ { "tileId": "t0", "flags": 1 }, null ] }
              ]
            }
          }
        ]
    }
  },
  "tilesets": {
    "tiles-1": {
      "assetId": "tiles-1",
      "name": "Tiles",
      "imageAssetId": "tiles-image",
      "slicing": { "tileWidth": 16, "tileHeight": 16, "marginX": 1, "marginY": 2,
                   "spacingX": 3, "spacingY": 4 },
      "tiles": [ { "id": "t0", "x": 0, "y": 0, "width": 16, "height": 16 } ]
    }
  },
  "fontAssets": [
    { "assetId": "font-1", "name": "Body", "sourcePath": "fonts/body.ttf",
      "defaultPixelSize": 18, "glyphPreset": "european" }
  ]
})";
    {
        std::ofstream f((tmpDir / "project.json").string());
        f << proj;
    }

    AssetLoader loader;
    loader.init();
    ArtCade::ProjectDoc doc;
    CHECK(loader.loadDirectory(tmpDir.string(), doc));

    const auto entityIt = doc.entities.find(1);
    CHECK(entityIt != doc.entities.end());
    if (entityIt != doc.entities.end()) {
        CHECK(entityIt->second.boxCollider2D.has_value());
        if (entityIt->second.boxCollider2D.has_value()) {
            const auto& bc = *entityIt->second.boxCollider2D;
            CHECK(bc.offset.x == 1.f && bc.offset.y == 2.f);
            CHECK(bc.size.x == 40.f && bc.size.y == 24.f);
            CHECK(bc.enabled);
            CHECK(bc.mode == ArtCade::BoxColliderMode::OneWayPlatform);
        }
    }

    const auto sceneIt = doc.scenes.find("s1");
    CHECK(sceneIt != doc.scenes.end());
    if (sceneIt != doc.scenes.end()) {
        CHECK(sceneIt->second.instances.size() == 1);
        if (!sceneIt->second.instances.empty()) {
            const auto& instance = sceneIt->second.instances.front();
            CHECK(instance.tilemap.has_value());
            if (instance.tilemap.has_value()) {
                CHECK(instance.tilemap->tilesetAssetId == "tiles-1");
                CHECK(instance.tilemap->cellSize.x == 16.f && instance.tilemap->cellSize.y == 16.f);
                CHECK(instance.tilemap->chunkSize == 8);
                CHECK(instance.tilemap->chunks.size() == 1);
                if (!instance.tilemap->chunks.empty()) {
                    const auto& chunk = instance.tilemap->chunks.front();
                    CHECK(chunk.chunkX == 0 && chunk.chunkY == 0);
                    CHECK(chunk.cells.size() == 2);
                    if (chunk.cells.size() == 2) {
                        CHECK(chunk.cells[0].has_value());
                        if (chunk.cells[0].has_value())
                            CHECK(chunk.cells[0]->tileId == "t0");
                        CHECK(!chunk.cells[1].has_value());
                    }
                }
            }
        }
    }

    CHECK(doc.tilesets.size() == 1);
    if (!doc.tilesets.empty()) {
        const auto& ts = doc.tilesets.front();
        CHECK(ts.slicing.tileWidth == 16 && ts.slicing.tileHeight == 16);
        CHECK(ts.slicing.marginX == 1 && ts.slicing.marginY == 2);
        CHECK(ts.slicing.spacingX == 3 && ts.slicing.spacingY == 4);
        CHECK(ts.tiles.size() == 1);
        if (!ts.tiles.empty()) CHECK(ts.tiles.front().id == "t0");
    }

    CHECK(doc.fontAssets.size() == 1);
    if (!doc.fontAssets.empty()) {
        CHECK(doc.fontAssets.front().assetId == "font-1");
        CHECK(doc.fontAssets.front().defaultPixelSize == 18);
    }

    loader.shutdown();
    fs::remove_all(tmpDir);
}

// ---- main ------------------------------------------------------------------

int main() {
    std::cout << "=== ArtCade Package (.artcade) Tests ===\n";

    test_minimal_zip_load();
    test_invalid_zip_returns_false();
    test_nonexistent_file_returns_false();
    test_reloading_same_artcade_path_clears_stale_extracted_files();
    test_directory_load_still_works();
    test_zip_slip_entry_rejected();
    test_store_size_mismatch_rejected();
    test_zip_integrity_checks();
    test_runtime_project_paths_are_sandboxed();
    test_stale_format_version_rejected();
    test_ru01a_component_readers_round_trip();

    std::cout << "\nResults: " << g_passed << " passed, "
              << g_failed  << " failed\n";
    return g_failed > 0 ? 1 : 0;
}
