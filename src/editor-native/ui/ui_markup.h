#pragma once

#include <string>

namespace ArtCade::EditorNative {

// Escapes user/project text before inserting it into generated RML markup.
std::string escapeRml(const std::string& text);

// Small, panel-agnostic RML markup fragments shared across panels. Nothing
// here knows about any specific panel's wrapper markup or action names —
// callers own their own wrapping (a `.prop-row`, a bare `<div>`, ...) and
// supply their own action strings.

// Tabler icon glyph span (PUA codepoint passed as an RML char reference).
std::string iconMarkup(const char* codepoint);

// One row's worth of dropdown trigger: the clickable `.drop-trigger` box
// showing the current value, with a caret. The caller emits its own option
// list markup (`.drop-list`/`.drop-entry`) separately — this only builds the
// trigger itself (in-flow, not a floating popup: a scrollable ancestor would
// clip a real popup, the same reason every dropdown in this app is built
// in-flow instead). `elementId`, when non-empty, lets a caller that DOES need
// a floating menu (positioned off this trigger's own on-screen box rather
// than rendered in-flow) look the element back up via GetElementById.
std::string dropdownTriggerMarkup(const std::string& valueText,
                                  const std::string& toggleAction,
                                  const std::string& dropdownId,
                                  bool open, bool disabled,
                                  const std::string& extraClass = {},
                                  const std::string& elementId = {});

} // namespace ArtCade::EditorNative
