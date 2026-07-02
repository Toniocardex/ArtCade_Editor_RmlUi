#pragma once

#include <RmlUi/Core/SystemInterface.h>

namespace ArtCade::EditorNative {

// =============================================================================
// RmlSystem — Rml::SystemInterface over raylib (time, logging, clipboard).
// The only RmlUi-specific system glue; no editor or domain logic here.
// =============================================================================
class RmlSystem final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override;
    bool   LogMessage(Rml::Log::Type type, const Rml::String& message) override;
    void   SetClipboardText(const Rml::String& text) override;
    void   GetClipboardText(Rml::String& text) override;
};

} // namespace ArtCade::EditorNative
