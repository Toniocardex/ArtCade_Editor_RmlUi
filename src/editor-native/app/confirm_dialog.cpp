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

namespace {
std::wstring widenAsciiUtf8(const std::string& text) {
    if (text.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring out(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), count);
    return out;
}

UnsavedChoice showUnsavedPrompt(const std::wstring& detail) {
    const std::wstring message = L"There are unsaved changes:\n\n" + detail
        + L"\n\nSave them before continuing?";
    const int result = MessageBoxW(
        GetActiveWindow(), message.c_str(), L"ArtCade Studio",
        MB_YESNOCANCEL | MB_ICONWARNING | MB_DEFBUTTON1);
    switch (result) {
        case IDYES: return UnsavedChoice::Save;
        case IDNO:  return UnsavedChoice::Discard;
        default:    return UnsavedChoice::Cancel;
    }
}
} // namespace

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

UnsavedChoice confirmUnsavedChanges(const std::string& detail) {
    return showUnsavedPrompt(widenAsciiUtf8(detail));
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

bool confirmDeleteScriptAsset(const std::string& name,
                              const std::string& relativeSourcePath) {
    const std::wstring wideName = widenAsciiUtf8(name);
    const std::wstring widePath = widenAsciiUtf8(relativeSourcePath);
    const std::wstring message =
        L"Delete Script \"" + wideName + L"\" and permanently remove:\n\n"
        + widePath
        + L"\n\nUndo can restore the file contents recorded at deletion, "
          L"but this cannot recover unsaved editor drafts discarded first.\n\n"
          L"Delete Script and file?";
    const int result = MessageBoxW(
        GetActiveWindow(), message.c_str(), L"ArtCade Studio",
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    return result == IDYES;
}

#else  // non-Windows: abort is the safe default (never silently lose changes).

UnsavedChoice confirmUnsavedChanges() { return UnsavedChoice::Cancel; }
UnsavedChoice confirmUnsavedChanges(const std::string&) { return UnsavedChoice::Cancel; }

UnsavedChoice confirmTilesetUnappliedChanges() { return UnsavedChoice::Cancel; }

bool confirmTilesetResliceImpact(int, int, int) { return false; }

bool confirmDeleteScriptAsset(const std::string&, const std::string&) { return false; }

#endif

} // namespace ArtCade::EditorNative
