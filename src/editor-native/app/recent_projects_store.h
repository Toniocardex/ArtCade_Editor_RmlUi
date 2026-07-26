#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// One MRU row. Identity is `path` (application-normalized project file path).
// `displayName` is presentation-only; `lastOpenedUtc` drives ordering.
struct RecentProjectEntry {
    std::string path;
    std::string displayName;
    std::int64_t lastOpenedUtc = 0;
};

// App-local recent-projects list (ADR-0030). Pure in-memory + JSON string
// roundtrip — no filesystem, RmlUi, ProjectDocument, dirty, or Undo.
class RecentProjectsStore {
public:
    static constexpr std::size_t kMaxEntries = 10;

    const std::vector<RecentProjectEntry>& entries() const { return entries_; }

    // Insert or move `path` to the front. Empty path is a no-op.
    // `utc` is the touch timestamp (injectable for tests).
    void touch(std::string path, std::int64_t utc);

    // Remove by path key. Missing path is a no-op.
    void remove(const std::string& path);

    void clear();

    // Parse preferences JSON. Malformed / non-object root → empty store
    // (returns false). Invalid individual entries are skipped.
    bool fromJson(const std::string& text);

    // Serialize current entries. Always succeeds for an in-memory store.
    std::string toJson() const;

    // Comparison key used for dedupe (backslash→slash, trim trailing slash).
    static std::string normalizePathKey(std::string path);

    static std::string displayNameFromPath(const std::string& path);

private:
    std::vector<RecentProjectEntry> entries_;
};

} // namespace ArtCade::EditorNative
