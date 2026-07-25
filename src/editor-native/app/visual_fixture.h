#pragma once

#include "core/types.h"

#include <string>

namespace ArtCade::EditorNative {

/**
 * ADR-0027 phase 4 — the project the visual-regression harness renders the
 * real panels against.
 *
 * The component gallery pins each shared control in isolation, which is what
 * caught the disabled-state and dangling-selector regressions. It cannot see
 * defects of *composition*: the key picker inside a Logic Board rule card, an
 * Inspector filled with a real entity's components, a Hierarchy with layers.
 * Those are exactly the places the last three UI bugs were reported from.
 *
 * Built in code rather than committed as an opaque .artcade blob so it stays
 * readable and regenerable (`--write-fixture <path>`), and deliberately
 * content-light: every element earns its place by making some panel render a
 * state that would otherwise go unphotographed.
 */
[[nodiscard]] ProjectDoc makeVisualFixtureProject();

/** Serializes the fixture through the real project writer. Empty on success. */
[[nodiscard]] std::string writeVisualFixtureProject(const std::string& path);

} // namespace ArtCade::EditorNative
