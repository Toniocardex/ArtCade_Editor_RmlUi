#pragma once

namespace ArtCade::EditorNative {

// The user's answer to the unsaved-changes prompt before a destructive action.
enum class UnsavedChoice { Save, Discard, Cancel };

enum class GuardOutcome { Proceed, Abort };

// Pure decision for the unsaved-changes guard, isolated so the Save/Discard/
// Cancel semantics are unit-testable without the native dialog. `saveSucceeded`
// is only meaningful when `choice == Save`:
//
//   not dirty            -> Proceed   (nothing to lose)
//   Cancel               -> Abort     (do not run the destructive action)
//   Discard              -> Proceed   (run it, dropping changes)
//   Save + save ok       -> Proceed
//   Save + save failed   -> Abort     (keep the project loaded and dirty)
inline GuardOutcome resolveUnsavedGuard(bool dirty, UnsavedChoice choice,
                                        bool saveSucceeded) {
    if (!dirty) return GuardOutcome::Proceed;
    switch (choice) {
        case UnsavedChoice::Cancel:  return GuardOutcome::Abort;
        case UnsavedChoice::Discard: return GuardOutcome::Proceed;
        case UnsavedChoice::Save:    return saveSucceeded ? GuardOutcome::Proceed
                                                          : GuardOutcome::Abort;
    }
    return GuardOutcome::Abort;
}

} // namespace ArtCade::EditorNative
