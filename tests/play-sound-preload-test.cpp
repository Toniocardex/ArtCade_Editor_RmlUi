#include "editor-native/model/play_sound_preload.h"

#include "editor-native/model/path_confinement.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace ArtCade;
using namespace ArtCade::EditorNative;

static int g_failed = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "FAIL " << #cond << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
        ++g_failed; \
    } \
} while (0)

static std::filesystem::path makeTempRoot() {
    const auto root = std::filesystem::temp_directory_path()
        / ("artcade-play-sound-preload-" + std::to_string(
            static_cast<unsigned long long>(
                std::chrono::steady_clock::now().time_since_epoch().count())));
    std::filesystem::create_directories(root / "assets" / "audio");
    return root;
}

static void writeTinyFile(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

static void testPlansReferencedStaticSoundsOnly() {
    const std::filesystem::path root = makeTempRoot();
    writeTinyFile(root / "assets" / "audio" / "jump.wav", "RIFF");
    writeTinyFile(root / "assets" / "audio" / "unused.wav", "RIFF");

    PlayAssetCatalogSnapshot snap;
    snap.audioAssets.emplace(
        "jump.wav", RuntimeAudioAsset{"jump.wav", "assets/audio/jump.wav",
                                      AudioLoadMode::StaticSound});
    // unused.wav is on disk but not in the Play snapshot — must not be planned.

    std::vector<PlaySoundPreloadEntry> plan;
    std::string error;
    CHECK(planPlaySoundPreload(snap, root, &plan, &error));
    CHECK(error.empty());
    CHECK(plan.size() == 1);
    CHECK(plan[0].assetId == "jump.wav");
    CHECK(plan[0].sourcePath == "assets/audio/jump.wav");
    CHECK(plan[0].resolvedPath == root / "assets" / "audio" / "jump.wav");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

static void testThreeDistinctAssets() {
    const std::filesystem::path root = makeTempRoot();
    writeTinyFile(root / "assets" / "audio" / "a.wav", "A");
    writeTinyFile(root / "assets" / "audio" / "b.wav", "B");
    writeTinyFile(root / "assets" / "audio" / "c.wav", "C");

    PlayAssetCatalogSnapshot snap;
    snap.audioAssets.emplace(
        "a", RuntimeAudioAsset{"a", "assets/audio/a.wav", AudioLoadMode::StaticSound});
    snap.audioAssets.emplace(
        "b", RuntimeAudioAsset{"b", "assets/audio/b.wav", AudioLoadMode::StaticSound});
    snap.audioAssets.emplace(
        "c", RuntimeAudioAsset{"c", "assets/audio/c.wav", AudioLoadMode::StaticSound});

    std::vector<PlaySoundPreloadEntry> plan;
    CHECK(planPlaySoundPreload(snap, root, &plan, nullptr));
    CHECK(plan.size() == 3);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

static void testRejectsMissingFile() {
    const std::filesystem::path root = makeTempRoot();
    PlayAssetCatalogSnapshot snap;
    snap.audioAssets.emplace(
        "gone", RuntimeAudioAsset{"gone", "assets/audio/gone.wav",
                                  AudioLoadMode::StaticSound});

    std::vector<PlaySoundPreloadEntry> plan;
    std::string error;
    CHECK(!planPlaySoundPreload(snap, root, &plan, &error));
    CHECK(plan.empty());
    CHECK(error.find("missing") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

static void testRejectsPathEscape() {
    const std::filesystem::path root = makeTempRoot();
    PlayAssetCatalogSnapshot snap;
    snap.audioAssets.emplace(
        "escape", RuntimeAudioAsset{"escape", "../outside.wav",
                                    AudioLoadMode::StaticSound});

    std::vector<PlaySoundPreloadEntry> plan;
    std::string error;
    CHECK(!planPlaySoundPreload(snap, root, &plan, &error));
    CHECK(plan.empty());
    CHECK(!error.empty());

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

static void testRejectsNonStaticLoadMode() {
    const std::filesystem::path root = makeTempRoot();
    writeTinyFile(root / "assets" / "audio" / "theme.ogg", "OGG");
    PlayAssetCatalogSnapshot snap;
    snap.audioAssets.emplace(
        "theme", RuntimeAudioAsset{"theme", "assets/audio/theme.ogg",
                                   AudioLoadMode::Stream});

    std::vector<PlaySoundPreloadEntry> plan;
    std::string error;
    CHECK(!planPlaySoundPreload(snap, root, &plan, &error));
    CHECK(plan.empty());
    CHECK(error.find("StaticSound") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

static void testEmptySnapshotOk() {
    const std::filesystem::path root = makeTempRoot();
    PlayAssetCatalogSnapshot snap;
    std::vector<PlaySoundPreloadEntry> plan;
    CHECK(planPlaySoundPreload(snap, root, &plan, nullptr));
    CHECK(plan.empty());
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

int main() {
    testPlansReferencedStaticSoundsOnly();
    testThreeDistinctAssets();
    testRejectsMissingFile();
    testRejectsPathEscape();
    testRejectsNonStaticLoadMode();
    testEmptySnapshotOk();
    if (g_failed != 0) {
        std::cerr << g_failed << " failed\n";
        return 1;
    }
    std::cout << "play-sound-preload-test: ok\n";
    return 0;
}
