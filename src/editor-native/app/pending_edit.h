#pragma once

#include <string>

namespace ArtCade::EditorNative {

// Result of resolving the one RmlUi commit field that currently owns focus.
// Resolved includes both "no pending commit field" and a valid buffer ready to
// travel through the field's normal commit action. Invalid and Incomplete must
// block the caller before any unsaved-state decision is made.
enum class PendingEditStatus { Resolved, Invalid, Incomplete };

struct PendingEditResult {
    PendingEditStatus status = PendingEditStatus::Resolved;
    std::string message;

    bool resolved() const { return status == PendingEditStatus::Resolved; }
};

// UI-free classification shared by the RmlUi gate and core regression tests.
// Numeric fields reuse parseNumberField(), while this layer distinguishes an
// unfinished editing buffer ("-", ".", "1e", "12.") from invalid input
// (NaN, infinity, overflow, or trailing suffixes).
PendingEditResult classifyPendingEdit(const std::string& action,
                                      const std::string& value);

// False when Escape (or an unchanged edit) restored exactly the value rendered
// into the field. Layer rename is excluded because its handler also owns the
// inline-rename UI state and must close that state on an unchanged commit.
bool pendingEditNeedsCommit(const std::string& action,
                            const std::string& value,
                            const std::string& renderedValue);

} // namespace ArtCade::EditorNative
