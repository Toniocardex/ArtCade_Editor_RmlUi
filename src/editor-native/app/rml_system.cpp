#include "editor-native/app/rml_system.h"

#include <raylib.h>

#include <cstdio>

namespace ArtCade::EditorNative {

double RmlSystem::GetElapsedTime() {
    return GetTime();   // seconds since InitWindow
}

bool RmlSystem::LogMessage(Rml::Log::Type type, const Rml::String& message) {
    const char* tag = "RmlUi";
    switch (type) {
        case Rml::Log::LT_ERROR:
        case Rml::Log::LT_ASSERT:   tag = "RmlUi ERROR";   break;
        case Rml::Log::LT_WARNING:  tag = "RmlUi WARN";    break;
        default:                    tag = "RmlUi";         break;
    }
    std::fprintf(stderr, "[%s] %s\n", tag, message.c_str());
    return true;
}

void RmlSystem::SetClipboardText(const Rml::String& text) {
    ::SetClipboardText(text.c_str());   // raylib global
}

void RmlSystem::GetClipboardText(Rml::String& text) {
    if (const char* clip = ::GetClipboardText()) text = clip;
}

} // namespace ArtCade::EditorNative
