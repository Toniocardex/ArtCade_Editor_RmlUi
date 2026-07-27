#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// ADR-0035: one entry a panel can arrow-key onto within whichever in-flow
// dropdown is currently open. `value` is empty for panels whose pick action
// carries the value in data-arg (the Inspector's convention); panels that
// split addressing key (data-arg) from picked value (data-value, the Logic
// Board's convention) populate it so Enter can dispatch the exact same
// (action, arg, value) triple a click on that entry already does.
struct DropdownNavEntry {
    std::string action;
    std::string arg;
    std::string value;
    bool current = false;
};

// Panel-local presentation state only (ADR-0034/0035): never serialized,
// never touches ProjectDocument. One instance per panel, shared across
// whichever single dropdown that panel currently has open — not one instance
// per dropdown, since only one dropdown is ever open at a time in either
// InspectorPanel or LogicBoardPanel.
class DropdownNavigation {
public:
    // Call at the top of every refresh(): the entry list is rebuilt fresh
    // each paint by whichever dropdown block is open, so a stale index from a
    // previously open dropdown can never mismatch this frame's list.
    void clearEntries() { entries_.clear(); }

    // Call everywhere the owning panel's openDropdownId_ is cleared or
    // reassigned to a different id — a new dropdown session invalidates the
    // old highlight index.
    void resetSession() {
        entries_.clear();
        highlight_.reset();
    }

    std::size_t push(DropdownNavEntry entry) {
        entries_.push_back(std::move(entry));
        return entries_.size() - 1;
    }

    bool isHighlighted(std::size_t index) const {
        return highlight_.has_value()
            && static_cast<std::size_t>(*highlight_) == index;
    }

    // delta is +1 (Down) or -1 (Up); wraps at the ends. The first press since
    // opening starts adjacent to the entry marked `current` (falling back to
    // the top of the list) rather than an arbitrary index.
    void move(int delta) {
        if (entries_.empty()) return;
        const int count = static_cast<int>(entries_.size());
        int base = 0;
        if (highlight_) {
            base = *highlight_;
        } else {
            for (int i = 0; i < count; ++i) {
                if (entries_[static_cast<std::size_t>(i)].current) {
                    base = i;
                    break;
                }
            }
        }
        highlight_ = ((base + delta) % count + count) % count;
    }

    // The entry Enter would dispatch — the exact (action, arg, value) triple
    // its .drop-entry click handler already uses — or nullopt when nothing is
    // highlighted or the index no longer fits this frame's list.
    std::optional<DropdownNavEntry> commit() const {
        if (!highlight_) return std::nullopt;
        const std::size_t index = static_cast<std::size_t>(*highlight_);
        if (index >= entries_.size()) return std::nullopt;
        return entries_[index];
    }

private:
    std::vector<DropdownNavEntry> entries_;
    std::optional<int> highlight_;
};

} // namespace ArtCade::EditorNative
