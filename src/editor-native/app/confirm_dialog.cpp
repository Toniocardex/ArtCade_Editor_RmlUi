#include "editor-native/app/confirm_dialog.h"

#include <string>

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

UnsavedChoice confirmTilesetUnappliedChanges() {
    const int result = MessageBoxW(
        GetActiveWindow(),
        L"This tileset has unapplied slicing changes.\n\nApply them before closing?",
        L"ArtCade Studio",
        MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
    switch (result) {
        case IDYES: return UnsavedChoice::Save;
        case IDNO:  return UnsavedChoice::Discard;
        default:    return UnsavedChoice::Cancel;
    }
}

bool confirmTilesetResliceImpact(int removedReferencedTiles, int orphanedCells,
                                 int affectedTilemaps) {
    // Destructive default is the safe one: Cancel is the focused button.
    std::wstring message =
        L"This slicing removes " + std::to_wstring(removedReferencedTiles)
        + L" tile(s) still painted in " + std::to_wstring(affectedTilemaps)
        + L" tilemap(s).\n\n" + std::to_wstring(orphanedCells)
        + L" painted cell(s) will be cleared. This can be undone.\n\nApply anyway?";
    const int result = MessageBoxW(
        GetActiveWindow(), message.c_str(), L"ArtCade Studio",
        MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
    return result == IDOK;
}

#else  // non-Windows: abort is the safe default (never silently lose changes).

UnsavedChoice confirmUnsavedChanges() { return UnsavedChoice::Cancel; }

UnsavedChoice confirmTilesetUnappliedChanges() { return UnsavedChoice::Cancel; }

bool confirmTilesetResliceImpact(int, int, int) { return false; }

#endif

} // namespace ArtCade::EditorNative
