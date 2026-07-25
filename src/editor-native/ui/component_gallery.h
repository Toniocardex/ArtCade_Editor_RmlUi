#pragma once

#include <string>

namespace Rml { class ElementDocument; }

namespace ArtCade::EditorNative {

/**
 * ADR-0027 phase 4 — every shared component, in every state, on one page.
 *
 * The design system's failure mode is a shared visual definition changing for
 * one consumer and silently breaking the others; both bugs fixed on
 * 2026-07-25 were exactly that, and neither the C++ suites nor the phase-3
 * token test can see it (one checks logic, the other checks where colours
 * live — not that a control still *looks* right). A rendered specimen sheet
 * diffed against a committed reference can.
 *
 * Markup only: no state, no coordinator, no authoring. It is injected into the
 * live shell so it inherits the real stylesheet cascade rather than a copy.
 */
[[nodiscard]] std::string componentGalleryMarkup();

/**
 * Applies the pseudo-classes the gallery asks for via `data-force`
 * ("hover", "focus", "active"), which a headless capture cannot produce by
 * moving a mouse. Call after the markup is injected and laid out.
 */
void applyGalleryForcedStates(Rml::ElementDocument* document);

} // namespace ArtCade::EditorNative
