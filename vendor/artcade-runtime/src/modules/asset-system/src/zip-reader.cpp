#include "zip-reader.h"

#include "artcade-crypto.h"
#include "external/sinfl.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace ArtCade {
namespace {

constexpr uint32_t kMaxEntryUncompressedBytes = 256u * 1024u * 1024u;
constexpr uint32_t kMaxArchiveBytes = 64u * 1024u * 1024u;
constexpr uint64_t kMaxTotalExtractedBytes = 512u * 1024u * 1024u;
constexpr uint16_t kMaxEntryCount = 10000u;
constexpr uint32_t kCompressionRatioAllowance = 1024u * 1024u;
constexpr uint32_t kMaxCompressionRatio = 1000u;

constexpr uint32_t SIG_LOCAL = 0x04034b50u;
constexpr uint32_t SIG_CD = 0x02014b50u;
constexpr uint32_t SIG_EOCD = 0x06054b50u;

inline uint16_t rd16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8u);
}

inline uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

bool isSafeRelativeEntryName(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.front() == '\\') return false;
    if (name.size() >= 2 && name[1] == ':') return false;
    std::string component;
    for (char c : name) {
        if (c == '/' || c == '\\') {
            if (component.empty() || component == "." || component == "..") return false;
            component.clear();
        } else {
            component.push_back(c);
        }
    }
    if (component == "." || component == "..") return false;
    return !component.empty() || name.back() == '/' || name.back() == '\\';
}

uint32_t crc32(const uint8_t* data, size_t len) {
    static const auto table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < values.size(); ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u)
                    ? (0xEDB88320u ^ (value >> 1u))
                    : (value >> 1u);
            }
            values[i] = value;
        }
        return values;
    }();
    uint32_t value = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i)
        value = table[(value ^ data[i]) & 0xFFu] ^ (value >> 8u);
    return value ^ 0xFFFFFFFFu;
}

struct CDEntry {
    std::string name;
    uint16_t flags = 0;
    uint16_t method = 0;
    uint32_t crc = 0;
    uint32_t compSize = 0;
    uint32_t uncompSize = 0;
    uint32_t localOff = 0;
};

bool findEOCD(const uint8_t* buf, size_t len, uint32_t& cdOff,
              uint32_t& cdSize, uint16_t& count) {
    if (len < 22u) return false;
    const size_t scanStart = len - 22u;
    const size_t scanEnd = len > 65535u + 22u ? len - 65535u - 22u : 0u;

    for (ptrdiff_t i = static_cast<ptrdiff_t>(scanStart);
         i >= static_cast<ptrdiff_t>(scanEnd); --i) {
        const auto offset = static_cast<size_t>(i);
        if (rd32(buf + offset) != SIG_EOCD) continue;
        const uint16_t commentLen = rd16(buf + offset + 20u);
        if (offset + 22u + commentLen != len) continue;
        if (rd16(buf + offset + 4u) != 0 || rd16(buf + offset + 6u) != 0)
            return false;
        if (rd16(buf + offset + 8u) != rd16(buf + offset + 10u))
            return false;

        count = rd16(buf + offset + 10u);
        cdSize = rd32(buf + offset + 12u);
        cdOff = rd32(buf + offset + 16u);
        if (count == 0xFFFFu || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu)
            return false;
        if (static_cast<size_t>(cdOff) > offset ||
            static_cast<size_t>(cdSize) > offset - cdOff)
            return false;
        return true;
    }
    return false;
}

bool parseCentralDirectory(const uint8_t* buf, size_t len,
                           uint32_t cdOff, uint32_t cdSize, uint16_t count,
                           std::vector<CDEntry>& entries) {
    if (count > kMaxEntryCount) return false;
    if (static_cast<size_t>(cdOff) > len || cdSize > len - cdOff) return false;
    const size_t cdEnd = static_cast<size_t>(cdOff) + cdSize;
    size_t offset = cdOff;
    std::unordered_set<std::string> names;

    for (uint16_t i = 0; i < count; ++i) {
        if (offset > cdEnd || cdEnd - offset < 46u) return false;
        const uint8_t* p = buf + offset;
        if (rd32(p) != SIG_CD) return false;

        CDEntry entry;
        entry.flags = rd16(p + 8u);
        entry.method = rd16(p + 10u);
        entry.crc = rd32(p + 16u);
        entry.compSize = rd32(p + 20u);
        entry.uncompSize = rd32(p + 24u);
        entry.localOff = rd32(p + 42u);
        const uint16_t nameLen = rd16(p + 28u);
        const uint16_t extraLen = rd16(p + 30u);
        const uint16_t commentLen = rd16(p + 32u);
        const size_t entrySize = 46u + nameLen + extraLen + commentLen;
        if (entrySize > cdEnd - offset) return false;
        if ((entry.flags & 0x1u) != 0u) return false;
        if (entry.method != 0 && entry.method != 8) return false;
        if (entry.uncompSize > kMaxEntryUncompressedBytes) return false;
        if (static_cast<uint64_t>(entry.uncompSize) >
            static_cast<uint64_t>(entry.compSize) * kMaxCompressionRatio +
                kCompressionRatioAllowance)
            return false;

        entry.name.assign(reinterpret_cast<const char*>(p + 46u), nameLen);
        if (!isSafeRelativeEntryName(entry.name)) return false;
        if (!names.insert(entry.name).second) return false;
        entries.push_back(std::move(entry));
        offset += entrySize;
    }
    return offset == cdEnd;
}

bool extractEntry(const uint8_t* buf, size_t len, const CDEntry& entry,
                  const std::filesystem::path& destRoot) {
    namespace fs = std::filesystem;
    if (entry.name.back() == '/' || entry.name.back() == '\\') return true;
    if (static_cast<size_t>(entry.localOff) > len || len - entry.localOff < 30u)
        return false;

    const uint8_t* local = buf + entry.localOff;
    if (rd32(local) != SIG_LOCAL) return false;
    if (rd16(local + 6u) != entry.flags || rd16(local + 8u) != entry.method)
        return false;
    const uint16_t nameLen = rd16(local + 26u);
    const uint16_t extraLen = rd16(local + 28u);
    const size_t headerSize = 30u + nameLen + extraLen;
    if (headerSize > len - entry.localOff) return false;
    const std::string localName(reinterpret_cast<const char*>(local + 30u), nameLen);
    if (localName != entry.name) return false;

    const size_t dataOffset = static_cast<size_t>(entry.localOff) + headerSize;
    if (dataOffset > len || entry.compSize > len - dataOffset) return false;
    const uint8_t* compressed = buf + dataOffset;
    std::vector<uint8_t> decompressed;
    const uint8_t* output = compressed;

    if (entry.method == 0) {
        if (entry.compSize != entry.uncompSize) return false;
    } else {
        decompressed.resize(entry.uncompSize);
        const int result = sinflate(
            decompressed.data(), static_cast<int>(entry.uncompSize),
            compressed, static_cast<int>(entry.compSize));
        if (result < 0 || static_cast<uint32_t>(result) != entry.uncompSize)
            return false;
        output = decompressed.data();
    }
    if (crc32(output, entry.uncompSize) != entry.crc) return false;

    std::error_code ec;
    const fs::path normalisedRoot = fs::weakly_canonical(destRoot, ec);
    if (ec) return false;
    const fs::path outPath = (normalisedRoot / fs::path(entry.name)).lexically_normal();
    const fs::path relative = outPath.lexically_relative(normalisedRoot);
    const auto first = relative.begin();
    if (relative.empty() || relative.is_absolute() || first == relative.end() || *first == "..")
        return false;
    fs::create_directories(outPath.parent_path(), ec);
    if (ec) return false;

    std::ofstream file(outPath, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(reinterpret_cast<const char*>(output),
               static_cast<std::streamsize>(entry.uncompSize));
    return file.good();
}

} // namespace

bool zipExtractAll(const std::string& zipPath, const std::string& destDir) {
    namespace fs = std::filesystem;
    std::ifstream file(zipPath, std::ios::binary);
    if (!file) return false;
    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (data.empty() || data.size() > kMaxArchiveBytes) return false;

    // Decrypt in place when the archive is an encrypted .artcade container.
    // Plaintext ZIPs pass through untouched. A failed/forged tag aborts here.
    if (!artcadeDecryptArchive(data)) return false;

    uint32_t cdOff = 0;
    uint32_t cdSize = 0;
    uint16_t count = 0;
    if (!findEOCD(data.data(), data.size(), cdOff, cdSize, count)) return false;
    std::vector<CDEntry> entries;
    entries.reserve(count);
    if (!parseCentralDirectory(data.data(), data.size(), cdOff, cdSize, count, entries))
        return false;

    std::error_code ec;
    fs::create_directories(destDir, ec);
    if (ec) return false;
    const fs::path root(destDir);
    uint64_t totalUncompressed = 0;
    for (const auto& entry : entries) {
        totalUncompressed += entry.uncompSize;
        if (totalUncompressed > kMaxTotalExtractedBytes) return false;
        if (!extractEntry(data.data(), data.size(), entry, root)) return false;
    }
    return true;
}

} // namespace ArtCade
