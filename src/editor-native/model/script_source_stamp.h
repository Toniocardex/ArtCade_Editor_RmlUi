#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ArtCade::EditorNative {

// Stable content-only identity shared by filesystem fingerprinting and the
// Play apply workflow. It is derived data: it cannot provide or replace the
// saved Lua source.
struct ScriptSourceStamp {
    std::uint64_t hash = 0;
    std::size_t size = 0;

    bool operator==(const ScriptSourceStamp& other) const {
        return hash == other.hash && size == other.size;
    }
};

inline ScriptSourceStamp scriptSourceStamp(std::string_view source) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : source) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return ScriptSourceStamp{hash, source.size()};
}

} // namespace ArtCade::EditorNative
