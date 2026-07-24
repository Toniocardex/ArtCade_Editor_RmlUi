#include "editor-native/app/export/export_types.h"

#include <algorithm>
#include <cctype>

namespace ArtCade::EditorNative {

bool destinationIsInsideProjectRoot(const std::filesystem::path& destination,
                                    const std::filesystem::path& projectRoot) {
    if (destination.empty() || projectRoot.empty()) return false;
    std::error_code ec;
    const auto destAbs = std::filesystem::weakly_canonical(destination, ec);
    if (ec) return false;
    const auto rootAbs = std::filesystem::weakly_canonical(projectRoot, ec);
    if (ec) return false;
    auto destIt = destAbs.begin();
    auto rootIt = rootAbs.begin();
    for (; rootIt != rootAbs.end(); ++rootIt, ++destIt) {
        if (destIt == destAbs.end()) return false;
        if (*destIt != *rootIt) return false;
    }
    return true;
}

std::string normalizeProductFileName(std::string productName, bool* changed) {
    const std::string original = productName;
    static const char* reserved[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
    };
    for (char& c : productName) {
        if (c < 32 || std::string("<>:\"/\\|?*").find(c) != std::string::npos) c = '_';
    }
    while (!productName.empty() && (productName.back() == ' ' || productName.back() == '.'))
        productName.pop_back();
    if (productName.empty()) productName = "Game";
    std::string upper = productName;
    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    for (const char* name : reserved) {
        if (upper == name) {
            productName += "_Game";
            break;
        }
    }
    if (changed) *changed = (productName != original);
    return productName;
}

} // namespace ArtCade::EditorNative
