# ADR-0036 — Text Component Custom Font (Cache, Picker, Editor Preview)

**Status:** Accepted
**Date:** 2026-07-27
**Scope:** A lightweight, `Renderer`-independent font cache for the Editor
Scene View, so a Text component's configured font renders correctly in Edit
mode; an Inspector picker wiring `TextComponent.fontPath` to the existing
font-asset catalog, which today has no UI at all.
**Related:** [ADR-0034](ADR-0034-inspector-keyboard-navigation.md)/
[ADR-0035](ADR-0035-dropdown-keyboard-navigation-rollout.md) (the
`DropdownNavigation` picker pattern this reuses), the anchor-unification fix
(`vendor/artcade-runtime/src/core/text-anchor-math.h`) that surfaced this gap,
`docs/PLAY_RUNTIME_UNIFICATION_ROADMAP.md` RU-03/RU-04 (the closed decision
this ADR does **not** revisit — see Non-goals),
`docs/RU02_GAMEPLAY_SESSION_REFACTOR.md` (D-12/T-02 debt register, same
boundary)

## Context

Following the Text anchor fix, two things came to light:

1. **The Editor Scene View and the Play/export runtime draw Text components
   through two different, deliberately separate mechanisms** — not
   unaddressed duplication, but a closed architectural decision. The
   roadmap states this explicitly: *"RU-03 ha già stabilito che l'editor non
   usa mai `Modules::Renderer`/`CameraManager` per Play — la Scene View
   dell'editor ha un proprio pipeline di rendering completamente separato"*
   (`PLAY_RUNTIME_UNIFICATION_ROADMAP.md`). The editor owns its own raylib
   window (`InitWindow` in `editor_app.cpp`); the runtime's `Modules::Renderer`
   calls `InitWindow` again internally (`raylib_surface.cpp`) — instantiating
   one inside the already-windowed editor process is a real conflict, not
   just extra plumbing. The project's "single source of truth" principle is
   explicitly scoped to *simulation* (`GameplaySession`, shared by both
   hosts); presentation is deliberately per-host, and `GameplaySession`
   already unifies the *data* (`buildFrameSnapshot()` resolves text/binding
   formatting once for both). Only the final pixel-drawing step differs, by
   design. This ADR does not reopen that decision.

2. **There is no UI to assign a custom font to a Text component at all.**
   The Inspector's "Font" row (`inspector_panel.cpp`) is a hardcoded
   `<span class="prop-readonly">Default Font</span>` — never bound to
   `TextComponent.fontPath` or `ProjectDoc.fontAssets`. `fontPath` is a real,
   round-tripping field (`entity-json.cpp`) and the Play/export runtime
   already resolves and draws it correctly
   (`scene_entities_pass.cpp` → `Renderer::drawText` → `FontCache`). A font
   *import* pipeline already exists and works (Assets panel "Fonts" group,
   `AddFontAssetCommand`, `.ttf`/`.otf` import) — it was simply never wired to
   the component that would consume it.

So "the text renders differently in Edit vs. Play" for a custom-fonted Text
component isn't the anchor bug's kind of issue (two implementations
disagreeing about the same well-defined thing) — it's a real, separate gap:
Edit mode has never been able to render a custom font at all, because nothing
ever asked it to.

### What the runtime already does, that the editor can reuse the *shape* of

- `TextComponent::fontPath` — `std::string`, **project-relative**, empty =
  default font (`vendor/artcade-runtime/src/core/types.h:366`).
- `FontAssetDef{assetId, name, sourcePath, defaultPixelSize, glyphPreset}` —
  `sourcePath` is project-relative, matching `fontPath`'s expected shape
  exactly (`types.h:815-821`).
- `AddFontAssetCommand`/`RemoveFontAssetCommand` already mutate
  `ProjectDoc.fontAssets` (`font_asset_commands.h/.cpp`) — the import flow
  (Assets panel → `importFontAsset()` → this Command) needs no changes.
- The editor already has the exact shape of cache this needs, for images:
  `TextureCache` (`view/texture_cache.h/.cpp`, demand-load + cache by
  `AssetId`) fed by `TextureRequestCatalog` (`view/texture_request_catalog.h/
  .cpp`, a revision-cached `AssetId → resolved filesystem path` projection
  over `ProjectDocument`). `SceneView::render()` already takes a
  `TextureCache` alongside its single fixed `CanvasFont`.

## Decision

### A. `EditorFontCache` + `FontRequestCatalog` (new, editor-native only)

Mirror `TextureCache`/`TextureRequestCatalog` exactly, for fonts:

```text
view/font_request_catalog.h/.cpp   — AssetId -> resolved path, revision-cached
view/editor_font_cache.h/.cpp      — AssetId -> loaded raylib Font, demand-load + cache
```

`EditorFontCache::find(assetId)` returns `nullptr` for an unresolved,
unloaded, or failed-to-load font; every call site falls back to the existing
`CanvasFont` (Inter) exactly as today when that happens — no new "missing
font" error state, no behavior change for every Text component that doesn't
set a custom font (the overwhelming majority, today all of them).

This cache is **not** the runtime's `Modules::FontCache` reused directly —
that type lives inside `Modules::Renderer`'s ownership graph and is reached
through the full `beginFrame()/endWorldPass()` draw-queue lifecycle (see
Context). `EditorFontCache` is a small, standalone `AssetId → Font` map the
Scene View can query synchronously mid-draw, the same relationship
`TextureCache` already has to raylib `Texture2D`. Same concept
(load-once-by-asset-id, GPU resource lifetime tied to project lifetime), two
small independent implementations — not the debt this ADR is fixing (that
debt was the *anchor* logic disagreeing with itself; two hosts each owning a
lightweight resource cache for their own draw call is the existing,
unremarkable pattern already used for textures).

### B. Wiring into the Scene View draw path

`canvas_font.cpp` gains an overload taking an explicit `Font` (not just
`CanvasFont`) for `drawCanvasText`/`measureCanvasText`, so a resolved custom
font and the fallback `CanvasFont.font` share one draw/measure
implementation:

```cpp
void drawCanvasText(const Font& font, const std::string& text,
                    float x, float y, float size, Color color);
float measureCanvasText(const Font& font, const std::string& text, float size);
```

The two existing `CanvasFont`-taking overloads become thin wrappers
(`font.loaded ? font.font : GetFontDefault()`) so every other caller
(entity labels, origin marker, dimensions readout, Tile Palette/Tileset/
Sprite-Animation preview messages) is unaffected.

`SceneView::render()` gains an `EditorFontCache` parameter; its two Text
draw sites (world-space, screen-space — `scene_view.cpp:355-362,449-461`)
resolve the font once per entry:

```cpp
const Font& glyphFont = !text.fontPath.empty()
    ? (fontCache.find(text.fontPath) ? *fontCache.find(text.fontPath) : canvasFont.font)
    : canvasFont.font;
```

`editor_app.cpp` owns one `FontRequestCatalog` + `EditorFontCache` (same
lifetime tier as the existing `TextureRequestCatalog`/`TextureCache`), calls
`fontRequestCatalog.forDocument(document, assetRoot)` then
`fontCache.prepare(requests)` once per frame alongside the existing texture
prepare call, and passes the cache into `sceneView.render(...)`.

### C. `SetTextComponentFontCommand`

New authoring Command, `src/editor-native/commands/text_component_commands.h`
(existing file — the Text component's other property Commands already live
here):

```text
SetTextComponentFontCommand
    ObjectTypeId / entity address (same addressing as every other
    Text-property Command in this file)
    std::string fontPath   // "" = Default Font; otherwise a FontAssetDef.sourcePath
```

Validates the target has a Text component; no other validation (an empty
string and any non-empty string are both structurally valid — a dangling
`fontPath` pointing at a since-removed font asset degrades to the
`CanvasFont` fallback at render time, same as any other missing-asset
reference elsewhere in this codebase, not a Command-time error).

### D. Inspector picker

Replace the static Font row with an in-flow dropdown, same shape as every
other Inspector picker (`dropdownTrigger()`/`.drop-list`, ADR-0034/0035's
`DropdownNavigation` for arrow-key highlight/Enter-commit/Escape-close and
Tab-reachability — no new UI pattern, this dropdown gets the established one
for free by construction):

```text
Font   [ Default Font                    v ]
         Default Font              (current when fontPath == "")
         ─────────────────────────
         <FontAssetDef.name or assetId>   (one row per doc.fontAssets entry)
```

Action `set-text-font`, dispatching `SetTextComponentFontCommand` with the
selected entry's `sourcePath` (or `""` for "Default Font"). Disabled during
Play, matching every other Text property row.

### Authority and mutation map

| Concern | Owner | Mutation |
|---|---|---|
| `TextComponent.fontPath` | `ProjectDocument` (entity/object-type data) | `SetTextComponentFontCommand` only |
| Font *asset* catalog (`fontAssets`) | `ProjectDocument.fontAssets` | existing `AddFontAssetCommand`/`RemoveFontAssetCommand` — unchanged |
| Resolved `AssetId → Font` (Edit preview) | `EditorFontCache` (editor-native, per-process) | presentation cache only, rebuilt from `FontRequestCatalog`, never touches `ProjectDocument` |
| Resolved `fontPath → Font*` (Play/export) | `Modules::FontCache` inside `Renderer` | unchanged, out of scope |

RmlUi renders the picker and dispatches the action only; it never resolves a
font or touches the cache.

## Non-goals

- **Does not give the editor a `Modules::Renderer` instance**, and does not
  reopen RU-03/RU-04. The Editor Scene View and Play/export runtime remain
  two presentation hosts by design; this ADR adds a second, editor-owned,
  Renderer-independent font cache — it does not merge the two draw paths.
- Does not change how the standalone game/export resolves fonts
  (`Modules::FontCache`, `resolvedFontKey()`, the `AssetLoader` manifest
  resolver) — untouched.
- Does not add font *import* UI (Assets panel "Fonts" group + `.ttf`/`.otf`
  import already exists and is unchanged) — only the missing picker that
  *assigns* an already-imported font to a Text component.
- Does not add per-glyph-preset/subset preview accuracy, kerning parity with
  the runtime's own `Font` loading flags, or any other rendering-fidelity
  guarantee beyond "loads the same file with `LoadFontEx`, same as
  `CanvasFont` does today for Inter."
- Does not touch `text-anchor-math.h`'s already-documented, separate tracked
  debt (this ADR's cache closes the "measurement uses a different font"
  half of that debt for Text components specifically; it does not attempt to
  make the editor and runtime call the exact same `drawText` function).

## Verification

- `SetTextComponentFontCommand`: apply/undo/redo, targeting a Text component
  and a non-Text component (fails), a since-removed font asset path (applies
  fine — presentation-only fallback, not a Command-time error).
- `FontRequestCatalog`: revision-cache invalidation (adding/removing a font
  asset, or changing `assetRoot`, produces a fresh `Requests` map; an
  unrelated document mutation does not rebuild it).
- `EditorFontCache`: demand-load, cache hit on second `find()`, `nullptr` for
  an unresolved/missing/corrupt font file.
- Real RmlUi routing test (ADR-0034/0035 style): trigger renders
  `tab-index: auto` and is keyboard-navigable; opening lists every
  `fontAssets` entry plus "Default Font"; Enter/click commits
  `SetTextComponentFontCommand`; Escape/outside-click cancels without
  mutating; Play disables the row.
- Live `--shot` check: a Text component with a custom font selected renders
  with that font's actual glyphs in the Scene View (not Inter), while every
  other canvas-drawn text (entity labels, origin marker, dimensions readout)
  is visually unchanged.
- Full suite: `scripts\build.bat --test`.

## Definition of Done

- Editor Scene View renders a Text component's configured font when set,
  falling back to `CanvasFont`/Inter exactly as today when unset or
  unresolvable.
- Inspector's Font row is a real picker wired to `fontAssets`, using the
  established `DropdownNavigation` keyboard-nav pattern.
- No change to Play/export font resolution, no `Modules::Renderer` instance
  in the editor process, no reopening of RU-03/RU-04.
- Full test suite green, including new coverage above.

## Implementation status

Implemented on 2026-07-27, not yet committed.

- New `src/editor-native/view/editor_font_cache.h/.cpp`
  (`FontResource{font, resolvedSourcePath, loaded, error}` +
  `EditorFontCache`). Simpler than the ADR's original two-class sketch: since
  `SceneFrameText::fontPath` (new field, see below) already carries the
  project-relative path directly — matching `FontAssetDef::sourcePath`'s own
  shape — there is no `AssetId` indirection to resolve on the render side,
  so a separate `FontRequestCatalog` mirroring `TextureRequestCatalog` turned
  out unnecessary; `EditorFontCache::prepare()` resolves each frame's
  distinct `fontPath`s against `assetRoot` directly
  (`resolvePathInsideRoot`), demand-loads via `LoadFontEx`, and caches by the
  `fontPath` string itself. `fontAssets` is read only by the Inspector picker
  (to list choices), never by this cache.
- `SceneFrameText` (`scene_frame_snapshot.h`) gained a `fontPath` field,
  populated at both existing construction sites in `scene_frame_snapshot.cpp`
  (Edit-mode instance projection and in-editor Play's `RenderableEntitySnapshot`
  path) from `TextComponent::fontPath`.
- `canvas_font.h/.cpp` gained `drawCanvasText(const Font&, ...)` /
  `measureCanvasText(const Font&, ...)` overloads; the existing `CanvasFont`-
  taking overloads became one-line wrappers
  (`canvasFont.loaded ? canvasFont.font : GetFontDefault()`) — behavior-
  preserving, since raylib's own `DrawText`/`MeasureText` already resolve to
  `GetFontDefault()` internally. `text_visual_layout.h/.cpp` gained a
  matching `layoutSceneFrameText(text, const Font&)` overload so measurement
  and drawing always agree on which font was actually used.
- `SceneView::render()` gained an `EditorFontCache` parameter; both Text
  draw sites in `scene_view.cpp` (world-space, screen-space/HUD) resolve a
  `resolveTextFont()` helper (non-empty `fontPath` + cache hit + loaded →
  that `Font`; anything else → `CanvasFont`/Inter) before measuring and
  drawing, instead of always using `canvasFont`.
- `editor_app.cpp` owns one `EditorFontCache` (`textFontCache`) alongside the
  existing `textureCache`; `textFontCache.prepare(snapshot.texts, assetRoot)`
  runs once per frame next to the existing `textureCache.prepare(...)` call,
  reusing the same already-computed `assetRoot`.
- **No new Command.** `SetObjectTypeTextComponentCommand` already exists and
  already atomically replaces the whole `TextComponent` — every other Text
  property (`align`, `format`, `bindKey`, ...) already goes through one
  shared `TextComponent next = *type->text; next.<field> = ...;
  setTextComponent(coordinator_, std::move(next));` block in
  `editor_ui.cpp`. `"set-text-font"` is one more `else if` in that same
  block (`next.fontPath = arg;`), added to both the `inspectorDropdownPick`
  allowlist and the dropdown-collapse/refresh condition alongside
  `set-text-align`/`set-text-format`. The ADR's planned
  `SetTextComponentFontCommand` was unnecessary once the existing generic
  command was found.
- Inspector's static `<span class="prop-readonly">Default Font</span>` row
  (`inspector_panel.cpp`) replaced with a `dropdownTrigger("Font",
  "text-font", ...)` + `.drop-list` built from `coordinator.document().data().fontAssets`,
  with an always-first "Default Font" entry (`fontPath == ""`). Follows the
  exact same `dropdownNav_.push(...)`/`isHighlighted(...)` shape as every
  other Inspector dropdown (ADR-0034/0035) — Tab-reachability, arrow-key
  highlight, Enter-commit, and Escape-close all come from the existing
  shared plumbing with no new C++ needed for those. `"text-font"` added to
  `isEntityDropdown()`'s known-id list for reconciliation. A `fontPath` that
  no longer matches any current `fontAssets` entry shows the raw path as its
  label (same "(missing)"-style honesty as Sprite Source/Tileset).
- New test `tests/inspector-text-font-picker-test.cpp` (registered in
  `tests/CMakeLists.txt` and `scripts/build.bat --test`): real
  trigger/entries, real `EditorUi` listener, re-fetches elements after each
  `frame()`. Covers: opening lists "Default Font" + both fixture font
  assets; picking one sets `TextComponent.fontPath` and is one Undo step;
  picking "Default Font" again clears it; Play makes the trigger
  unreachable by `data-action` (the same no-`data-action`-when-disabled
  convention `dropdownTriggerMarkup()` already uses for every other
  disabled picker, confirmed against `"text-align"` in the same frame as a
  cross-check, not a new/different behavior for this row).
- Full suite green: `scripts\build.bat --test`, including the new
  `inspector-text-font-picker-test: 24 passed, 0 failed` and both ADR-0034/
  0035 dropdown-keyboard tests unmodified (proving the picker's reuse of
  `DropdownNavigation` didn't disturb anything).
- **Not independently visually verified** with a real custom `.ttf` glyph
  render (the shared `visual-fixture.artcade` project currently has zero
  imported font assets, and building a throwaway scratch project with a
  real font file was out of proportion for this pass); the code path is
  covered by the trace above plus the full test suite, but an actual
  glyph-rendering screenshot with an imported font remains worth doing the
  next time a project with a font asset is available.
