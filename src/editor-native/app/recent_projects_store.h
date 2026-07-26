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
//
// `revision()` is an in-memory content generation counter. It increments once
// per logical mutation that changes the normalized entry list; persistence
// success/failure does not affect it.
class RecentProjectsStore {
public:
    static constexpr std::size_t kMaxEntries = 10;

    const std::vector<RecentProjectEntry>& entries() const { return entries_; }

    // In-memory content generation. Distinct from JSON schema `version`.
    std::uint64_t revision() const noexcept { return contentRevision_; }

    // Insert or move `path` to the front. Empty path is a no-op.
    // `utc` is the touch timestamp (injectable for tests).
    // Always bumps revision when the touch applies (including re-touch of the
    // front entry, which refreshes lastOpenedUtc).
    void touch(std::string path, std::int64_t utc);

    // Remove by path key. Returns true if an entry was removed (and revision
    // bumped). Missing path / empty key → false, no revision bump.
    bool remove(const std::string& path);

    // Empty list is a no-op (no revision bump).
    void clear();

    // Parse preferences JSON. Malformed / non-object root leaves the store
    // unchanged and returns false. Normalized content equivalent to the
    // current list → no revision bump; otherwise replace + bump once.
    bool fromJson(const std::string& text);

    // Serialize current entries. Always succeeds for an in-memory store.
    std::string toJson() const;

    // Comparison key used for dedupe (backslash→slash, trim trailing slash).
    static std::string normalizePathKey(std::string path);

    static std::string displayNameFromPath(const std::string& path);

private:
    static bool entriesEqual(const std::vector<RecentProjectEntry>& a,
                             const std::vector<RecentProjectEntry>& b);

    std::vector<RecentProjectEntry> entries_;
    std::uint64_t contentRevision_ = 0;
};

} // namespace ArtCade::EditorNative
