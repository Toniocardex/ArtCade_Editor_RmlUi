#include "editor-native/app/confirm_dialog.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ArtCade::EditorNative {

#if defined(_WIN32)

UnsavedChoice confirmUnsavedChanges() {
    // Yes = Save, No = Discard, Cancel = abort — the standard Windows convention
    // for an unsaved-changes prompt.
    const int result = MessageBoxW(
        GetActiveWindow(),
        L"This project has unsaved changes.\n\nSave them before continuing?",
        L"ArtCade Studio",
        MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
    switch (result) {
        case IDYES: return UnsavedChoice::Save;
        case IDNO:  return UnsavedChoice::Discard;
        default:    return UnsavedChoice::Cancel;   // IDCANCEL / dialog dismissed
    }
}

#else  // non-Windows: abort is the safe default (never silently lose changes).

UnsavedChoice confirmUnsavedChanges() { return UnsavedChoice::Cancel; }

#endif

} // namespace ArtCade::EditorNative
