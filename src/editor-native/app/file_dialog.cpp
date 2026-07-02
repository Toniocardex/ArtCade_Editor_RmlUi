#include "editor-native/app/file_dialog.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>

#include <cwchar>
#endif

namespace ArtCade::EditorNative {

#if defined(_WIN32)
namespace {

// Double-null-terminated filter pairs (label, pattern). The trailing implicit
// L'\0' of the literal supplies the final terminator Win32 expects.
constexpr wchar_t kFilter[] =
    L"ArtCade Project (*.artcade-project)\0*.artcade-project\0"
    L"JSON (*.json)\0*.json\0"
    L"All Files (*.*)\0*.*\0";

constexpr wchar_t kImageFilter[] =
    L"Images (*.png;*.jpg;*.jpeg;*.webp)\0*.png;*.jpg;*.jpeg;*.webp\0"
    L"All Files (*.*)\0*.*\0";

constexpr wchar_t kAudioFilter[] =
    L"Audio (*.wav;*.ogg;*.mp3)\0*.wav;*.ogg;*.mp3\0"
    L"All Files (*.*)\0*.*\0";

constexpr wchar_t kFontFilter[] =
    L"Fonts (*.ttf;*.otf)\0*.ttf;*.otf\0"
    L"All Files (*.*)\0*.*\0";

std::optional<std::filesystem::path> openImportDialog(const wchar_t* filter,
                                                      const wchar_t* defExt) {
    wchar_t buffer[MAX_PATH] = {0};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetActiveWindow();
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = buffer;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(buffer);
}

OPENFILENAMEW makeOfn(wchar_t* buffer) {
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = GetActiveWindow();
    ofn.lpstrFilter = kFilter;
    ofn.lpstrFile   = buffer;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"artcade-project";
    return ofn;
}

} // namespace

std::optional<std::filesystem::path> openProjectFileDialog() {
    wchar_t buffer[MAX_PATH] = {0};
    OPENFILENAMEW ofn = makeOfn(buffer);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;   // cancelled
    return std::filesystem::path(buffer);
}

std::optional<std::filesystem::path> saveProjectFileDialog(
    const std::filesystem::path& suggested) {
    wchar_t buffer[MAX_PATH] = {0};
    const std::wstring start = suggested.wstring();
    if (!start.empty() && start.size() < MAX_PATH)
        std::wcscpy(buffer, start.c_str());
    OPENFILENAMEW ofn = makeOfn(buffer);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return std::nullopt;   // cancelled
    return std::filesystem::path(buffer);
}

std::optional<std::filesystem::path> openImageFileDialog() {
    return openImportDialog(kImageFilter, L"png");
}
std::optional<std::filesystem::path> openAudioFileDialog() {
    return openImportDialog(kAudioFilter, L"wav");
}
std::optional<std::filesystem::path> openFontFileDialog() {
    return openImportDialog(kFontFilter, L"ttf");
}

#else  // non-Windows: the native editor is Windows-first; no picker yet.

std::optional<std::filesystem::path> openProjectFileDialog() { return std::nullopt; }
std::optional<std::filesystem::path> saveProjectFileDialog(
    const std::filesystem::path&) { return std::nullopt; }
std::optional<std::filesystem::path> openImageFileDialog() { return std::nullopt; }
std::optional<std::filesystem::path> openAudioFileDialog() { return std::nullopt; }
std::optional<std::filesystem::path> openFontFileDialog() { return std::nullopt; }

#endif

} // namespace ArtCade::EditorNative
