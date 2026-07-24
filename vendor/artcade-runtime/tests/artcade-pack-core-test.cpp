#include "artcade-pack/project_packer.h"
#include "artcade-crypto.h"
#include "artcade-archive/archive_util.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ArtCade;
namespace fs = std::filesystem;

static int fail(const char* msg) {
    std::fprintf(stderr, "FAIL: %s\n", msg);
    return 1;
}

int main() {
    const fs::path root = fs::temp_directory_path() / "artcade_pack_core_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    const std::string projectJson =
        R"({"formatVersion":10,"name":"PackTest","scenes":[],"assets":{}})";

    // Bytes-only pack (no filesystem source) — isolates ZIP + encrypt.
    ProjectPackRequest req;
    req.projectName = "PackTest";
    req.outputFile = root / "out.artcade";
    req.encryption = PackEncryption::DevelopmentPlaintext;
    {
        PackArchiveEntry e;
        e.archivePath = "project.json";
        e.source = std::vector<uint8_t>(projectJson.begin(), projectJson.end());
        req.entries.push_back(std::move(e));
    }

    ProjectPackResult plain = ProjectPacker{}.pack(req);
    if (!plain.ok) return fail(plain.error.c_str());

    {
        std::ifstream in(req.outputFile, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
        if (bytes.size() < 4 || bytes[0] != 'P' || bytes[1] != 'K')
            return fail("plaintext pack is not a ZIP");
    }

    req.outputFile = root / "out-enc.artcade";
    req.encryption = PackEncryption::ReleaseEncrypted;
    ProjectPackResult enc = ProjectPacker{}.pack(req);
    if (!enc.ok) return fail(enc.error.c_str());

    {
        std::ifstream in(req.outputFile, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
        if (!artcadeArchiveIsEncrypted(bytes)) return fail("missing ARTCADE1 magic");
        if (!artcadeDecryptArchive(bytes)) return fail("decrypt failed");
        if (bytes.size() < 4 || bytes[0] != 'P' || bytes[1] != 'K')
            return fail("decrypted payload is not a ZIP");
    }

    if (artcadeAssetKeyId().empty()) return fail("empty asset key id");

    fs::remove_all(root, ec);
    std::printf("artcade_pack_core_test: OK\n");
    return 0;
}
