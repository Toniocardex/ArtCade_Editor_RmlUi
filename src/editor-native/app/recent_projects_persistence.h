#pragma once

#include "editor-native/app/recent_projects_store.h"

#include <string>

namespace ArtCade::EditorNative {

struct RecentProjectsPersistResult {
    bool ok = false;
    std::string message;
};

// Best-effort load into @p store. Missing file → empty store + ok.
// Malformed JSON → clear store, ok=false with message (caller may warn).
RecentProjectsPersistResult loadRecentProjects(RecentProjectsStore& store);

// Atomic temp+rename write. Failure does not mutate @p store.
RecentProjectsPersistResult saveRecentProjects(const RecentProjectsStore& store);

} // namespace ArtCade::EditorNative
