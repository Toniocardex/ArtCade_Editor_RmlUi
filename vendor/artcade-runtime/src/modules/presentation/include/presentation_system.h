#pragma once

#include "presentation_snapshot.h"
#include "presentation_state.h"

#include <array>
#include <cstddef>

namespace ArtCade::Presentation {

/**
 * Owns presentation state and commits one immutable snapshot per frame.
 * Consumers must read committed_snapshot() for the duration of the frame.
 */
class PresentationSystem {
public:
    static constexpr std::size_t kRevisionHistory = 3;

    PresentationState& mutable_state() { return state_; }
    const PresentationState& state() const { return state_; }

    /** Commits @c calculate_snapshot(state_) into @c committedSnapshot_. */
    void begin_frame();

    /**
     * Recomputes the draft snapshot from current state without changing the
     * committed frame. Rendering, picking, and overlays continue to read the
     * previous committed snapshot until begin_frame().
     */
    void refresh_pending_snapshot();

    const PresentationSnapshot& committed_snapshot() const {
        return committedSnapshot_;
    }

    /** Draft snapshot recalculated from state; committed on @c begin_frame(). */
    const PresentationSnapshot& pending_snapshot() const {
        return pendingSnapshot_;
    }

    /** Returns a past snapshot when still in the ring buffer, else nullptr. */
    const PresentationSnapshot* find_snapshot(uint64_t revision) const;

    /**
     * Builds a snapshot from presentation state.
     * @param revision monotonic frame revision assigned by begin_frame
     */
    static PresentationSnapshot calculate_snapshot(const PresentationState& state,
                                                   uint64_t revision);

private:
    void push_history(const PresentationSnapshot& snapshot);

    PresentationState state_{};
    PresentationSnapshot pendingSnapshot_{};
    PresentationSnapshot committedSnapshot_{};
    std::array<PresentationSnapshot, kRevisionHistory> history_{};
    std::size_t historyCount_ = 0;
    uint64_t nextRevision_ = 1;
};

} // namespace ArtCade::Presentation
