#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

/** Workspace-only serial regenerate queue. Never dirty / Undo / ProjectDocument. */
enum class SfxBatchItemStatus {
    Pending,
    Generating,
    Succeeded,
    Failed,
    Skipped,
    Cancelled,
};

struct SfxBatchItem {
    std::string id;
    SfxBatchItemStatus status = SfxBatchItemStatus::Pending;
    std::string message;
};

struct SfxBatchState {
    bool active = false;
    bool cancelRequested = false;
    bool summaryVisible = false;
    std::size_t currentIndex = 0;
    std::vector<SfxBatchItem> items;
    std::string projectPathAtStart;
    std::size_t succeeded = 0;
    std::size_t failed = 0;
    std::size_t skipped = 0;
    std::size_t cancelled = 0;
};

inline void resetSfxBatch(SfxBatchState& batch) {
    batch = SfxBatchState{};
}

} // namespace ArtCade::EditorNative
