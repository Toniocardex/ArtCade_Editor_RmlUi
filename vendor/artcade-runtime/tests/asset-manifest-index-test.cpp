#include "modules/asset-system/include/asset-manifest-index.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ArtCade::Modules;
namespace fs = std::filesystem;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) ++g_passed; \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

static void test_resolve_id_and_path() {
    AssetManifestIndex idx;
    idx.addImageEntry("img_hero", "assets/images/hero.png");
    idx.addFontEntry("font_ui", "assets/fonts/ui.ttf");

    CHECK(idx.resolveImageKey("img_hero") == "assets/images/hero.png");
    CHECK(idx.resolveImageKey("assets/images/hero.png") == "assets/images/hero.png");
    CHECK(idx.resolveImageKey("assets/images/unknown.png") == "assets/images/unknown.png");
    CHECK(idx.resolveImageKey("") == "");
    CHECK(idx.resolveFontKey("font_ui") == "assets/fonts/ui.ttf");
}

static void test_manifest_json_file() {
    const fs::path dir = fs::temp_directory_path() / "artcade_manifest_test";
    fs::create_directories(dir);
    const fs::path path = dir / "manifest.json";
    {
        std::ofstream f(path);
        f << R"({
  "version": "1.0.0",
  "assets": [
    { "id": "snd1", "type": "audio", "relativePath": "assets/audio/jump.ogg" },
    { "id": "img1", "type": "image", "relativePath": "assets/images/a.png" },
    { "id": "font1", "type": "font", "relativePath": "assets/fonts/ui.ttf" }
  ]
})";
    }

    AssetManifestIndex idx;
    CHECK(idx.loadFromJsonFile(path.string()));
    CHECK(idx.resolveImageKey("img1") == "assets/images/a.png");
    CHECK(idx.resolveAudioKey("snd1") == "assets/audio/jump.ogg");
    CHECK(idx.resolveFontKey("font1") == "assets/fonts/ui.ttf");
}

int main() {
    test_resolve_id_and_path();
    test_manifest_json_file();
    std::cout << "asset-manifest-index-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return g_failed > 0 ? 1 : 0;
}
