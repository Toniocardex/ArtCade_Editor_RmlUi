#include "../include/presentation_system.h"

#include "../include/presentation_solver.h"

#include <algorithm>

namespace ArtCade::Presentation {

PresentationSnapshot PresentationSystem::calculate_snapshot(
    const PresentationState& state,
    uint64_t revision) {
    return solve_presentation_snapshot(state, revision);
}

void PresentationSystem::push_history(const PresentationSnapshot& snapshot) {
    for (std::size_t index = kRevisionHistory - 1; index > 0; --index)
        history_[index] = history_[index - 1];
    history_[0] = snapshot;
    historyCount_ = std::min(historyCount_ + 1, kRevisionHistory);
}

void PresentationSystem::begin_frame() {
    const uint64_t revision = nextRevision_++;
    committedSnapshot_ = calculate_snapshot(state_, revision);
    pendingSnapshot_ = committedSnapshot_;
    push_history(committedSnapshot_);
}

void PresentationSystem::refresh_pending_snapshot() {
    pendingSnapshot_ = calculate_snapshot(state_, nextRevision_);
}

const PresentationSnapshot* PresentationSystem::find_snapshot(
    uint64_t revision) const {
    for (std::size_t index = 0; index < historyCount_; ++index) {
        if (history_[index].revision == revision)
            return &history_[index];
    }
    return nullptr;
}

} // namespace ArtCade::Presentation
