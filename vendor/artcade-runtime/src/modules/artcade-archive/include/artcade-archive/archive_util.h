#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ArtCade {

/// Non-secret key identifier compiled with the embedded asset key (ADR-0019).
std::string_view artcadeAssetKeyId();

/// Hex-encoded SHA-256 of `data`.
std::string sha256Hex(const uint8_t* data, size_t len);
inline std::string sha256Hex(const std::vector<uint8_t>& data) {
    return sha256Hex(data.data(), data.size());
}
std::string sha256FileHex(const std::string& path);

/// Build a ZIP (store method 0) from ordered entries. Names use forward slashes.
struct ZipWriteEntry {
    std::string name;
    std::vector<uint8_t> data;
};

bool zipWriteStore(const std::vector<ZipWriteEntry>& entries,
                   std::vector<uint8_t>& outZip);

} // namespace ArtCade
