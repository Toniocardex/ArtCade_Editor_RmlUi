#include "editor-native/app/export/export_types.h"
#include "editor-native/app/export/export_transaction.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace ArtCade::EditorNative;
namespace fs = std::filesystem;

static void writeFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << text;
}

int main() {
    // Product name normalization
    bool changed = false;
    assert(normalizeProductFileName("My Game", &changed) == "My Game");
    assert(!changed);
    assert(normalizeProductFileName("CON", &changed) == "CON_Game");
    assert(changed);
    assert(normalizeProductFileName("a<>b", &changed).find('<') == std::string::npos);

    const fs::path root = fs::temp_directory_path() / "artcade_export_tx_test";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path projectRoot = root / "project";
    fs::create_directories(projectRoot);
    const fs::path destParent = root / "exports";
    fs::create_directories(destParent);

    assert(destinationIsInsideProjectRoot(projectRoot / "out", projectRoot));
    assert(!destinationIsInsideProjectRoot(destParent / "Game", projectRoot));

    const fs::path finalDir = destParent / "Game";
    {
        ExportTransaction tx = ExportTransaction::begin(finalDir);
        assert(!tx.stagingDirectory().empty());
        writeFile(tx.stagingDirectory() / "game.artcade", "ARTCADE1");
        ExportResult committed = tx.commit(false);
        assert(committed.ok);
        assert(fs::exists(finalDir / "game.artcade"));
    }
    {
        ExportTransaction tx = ExportTransaction::begin(finalDir);
        writeFile(tx.stagingDirectory() / "game.artcade", "ARTCADE1-v2");
        ExportResult noReplace = tx.commit(false);
        assert(!noReplace.ok);
    }
    {
        ExportTransaction tx = ExportTransaction::begin(finalDir);
        writeFile(tx.stagingDirectory() / "game.artcade", "ARTCADE1-v3");
        ExportResult replaced = tx.commit(true);
        assert(replaced.ok);
        std::ifstream in(finalDir / "game.artcade");
        std::string body((std::istreambuf_iterator<char>(in)), {});
        assert(body == "ARTCADE1-v3");
    }

    fs::remove_all(root);
    std::printf("export_foundation_test: OK\n");
    return 0;
}
