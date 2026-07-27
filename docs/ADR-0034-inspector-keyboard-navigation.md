# ADR-0034 — Inspector Keyboard Navigation (Dropdown Arrow Keys, Escape, Tab Order)

**Status:** Accepted
**Date:** 2026-07-27
**Scope:** Arrow-key highlight + Enter/Escape for the Inspector's in-flow
dropdowns (spike: Scene Layer picker); closing the gap where Escape does not
close an open dropdown; confirming and extending Tab order across Inspector
fields and dropdown triggers
**Related:** [ADR-0024](ADR-0024-editor-actions-shortcuts.md) (keyboard
pipeline: single-frame capture → `pumpRmlInput` → shortcut router →
`EscapeOwner`), [ADR-0029](ADR-0029-expression-text-authoring.md) (in-flow
dropdown/authoring pattern, RmlUi dispatch constraints),
[ADR-0032](ADR-0032-contextual-project-variable-creation.md) (typed picker and
panel-local draft-state precedent), Architecture Constitution (RmlUi is
presentation-only)

## Context

The Inspector's Layer / Sprite Source / Tilemap Tileset / Default Clip / Text
and Gauge binding / Game View preset pickers are one-row triggers
(`dropdownTrigger()` in `inspector_panel.cpp`) whose option list expands
in-flow via a single `std::string openDropdownId_` member on `InspectorPanel`
— never a floating popup, since RmlUi scroll regions clip absolutely
positioned children (established in ADR-0029). Each open dropdown renders a
`.drop-list` of `.drop-entry` divs with `data-action`/`data-arg`; the entry
matching the currently committed value gets a static `.selected` class and a
`●` marker. **No ephemeral "highlighted index" exists today** — there is
nothing for an arrow key to move.

Keyboard input already reaches RmlUi unconditionally every frame
(`pumpRmlInput` in `editor_input.cpp` forwards Up/Down/Enter/Escape/Tab to
`Rml::Context::Process*` regardless of focus), and no entry in the app's
shortcut catalog (`editor_action_catalog.cpp`) binds any of those four keys —
so there is no global-shortcut conflict to route around.

Two things already work, and one does not:

- **Tab already does "classic" field-to-field navigation.** RmlUi's vendored
  `ElementDocument::ProcessDefaultAction` implements native document-wide
  Tab/Shift+Tab cycling (`FindNextTabElement`), and every `<input>`/
  `<textarea>` gets `TabIndex::Auto` by default via `ElementFormControl`'s
  constructor. Nothing in this project's `.rcss` sets `tab-index`, and nothing
  intercepts Tab except the Help dialog's own scoped handler and the Script
  editor's buffer. So Tab already moves between real input fields with no
  code change needed for that part of the ask.
- **Dropdown triggers are invisible to Tab.** `.drop-trigger` is a plain
  `<div>`, and plain `<div>`s default to `tab-index: none`
  (`StyleSheetSpecification.cpp`). Tab silently skips every dropdown trigger
  and lands on the next real input, so a dropdown cannot be reached, opened,
  or driven from the keyboard at all today.
- **Escape does not close an open dropdown.** The `EscapeOwner` resolution
  (`escape_routing.cpp`, fed by `EditorUi::hasOpenContextMenu()` in
  `editor_ui.cpp`) checks `viewportContextMenuVisible_`,
  `hierarchyContextMenuVisible_`, `assetsContextMenuVisible_`,
  `logicTypeMenuVisible_`, `logicMoreMenuVisible_`, and `pendingConfirm_` —
  `inspector_.hasOpenDropdown()` is not in that list. Pressing Escape while a
  dropdown is open today falls through to `EscapeOwner::None` (global
  deselect), leaving the dropdown open.

The one directly reusable precedent in this codebase for "walk a scoped
subtree of focusable elements and cycle focus" is
`HelpDialogController::focusableElements()` / `handleTabKey()`: a DFS from a
root element, cyclic `(index ± 1 + n) % n` math, Tab intercepted in the
capture-phase document `keydown` listener
(`EditorUi::Listener::ProcessEvent`) via `StopImmediatePropagation()`, gated
by `helpDialogOpen()`. It is a trap scoped to a modal, not a pattern for
document-wide navigation — see the Tab decision below.

## Decision

### Scope of this slice (spike)

Implement against **one dropdown first: the Scene Layer picker** in
`InspectorPanel`. Verify live with a `--shot` screenshot pass (open, arrow,
Enter, Escape, Tab) before extending the same shape to Sprite Source, Tilemap
Tileset, Default Clip, Text/Gauge bindings, Game View preset, and the
per-variable type dropdown.

### A. Arrow-key highlight inside an open dropdown

New panel-local presentation state on `InspectorPanel`, same shape and
lifetime as `openDropdownId_`:

```cpp
std::optional<int> dropdownHighlightIndex_;
```

Reset at every existing site that already clears/reassigns `openDropdownId_`
(`toggleDropdown`, `closeDropdowns`, `dismissTransientMenus`, and the
selection/section-change reconciliation paths).

While a dropdown is open (`openDropdownId_` non-empty):

- **ArrowDown / ArrowUp** move `dropdownHighlightIndex_` by ±1, wrapping at
  the ends (matching `HelpDialogController`'s cyclic convention) over the
  current dropdown's entry count. The first arrow press after opening starts
  from the currently committed entry's index if there is one, else index 0.
- **Enter** commits the highlighted entry by dispatching the exact same
  `data-action`/`data-arg` pair its `.drop-entry` click handler already uses —
  no second commit code path.
- **Escape** closes without committing (`closeDropdowns()`), returning focus
  to the trigger.
- Keyboard highlight is a distinct visual state from the static `.selected`
  marker; add `.drop-entry.highlighted` (using existing ADR-0027 tokens for
  the focus ring/background) rather than repurposing `.selected`, so "the
  current value" and "the keyboard cursor" can both be visible at once.

### B. Escape closes an open dropdown (gap fix)

Add `inspector_.hasOpenDropdown()` alongside the existing checks inside
`EditorUi::hasOpenContextMenu()` (or as an equivalent early branch in the
`EscapeOwner` resolution), at the same precedence tier as the other
transient-menu-open cases. Escape closes the dropdown instead of falling
through to `routeGlobalEscape`.

### C. Tab / Shift+Tab field order

RmlUi's native document-wide tab-cycling already satisfies "classic Tab
between fields" for real inputs; this slice does not add a competing
Inspector-scoped tab trap. The only markup change needed:

- Give `.drop-trigger` `tabindex="0"` (or this codebase's RML/RCSS
  equivalent) so it joins the tab order like any other control, and make
  Enter/Space on a focused trigger open it — reusing the exact same open path
  a click already uses.
- **Tab while a dropdown is open closes it and advances focus normally**
  (does not trap inside the option list). Dropdown-internal traversal is
  arrow-keys-only. This matches how a native `<select>` behaves (Tab
  commits/closes and moves on) and avoids adding a second, dropdown-scoped tab
  trap on top of RmlUi's own document-wide one.

### Authority and mutation map

| Concern | Owner | Mutation |
|---|---|---|
| Highlight index / open id | `InspectorPanel` (`dropdownHighlightIndex_`, `openDropdownId_`) | presentation only, no Command |
| Actual value commit | existing `data-action` pick handlers (`set-entity-layer`, etc.) | existing Commands, unchanged |
| Escape-owns-dropdown precedence | `EscapeOwner` resolution (`escape_routing.cpp` / `editor_ui.cpp`) | routing only, no document mutation |
| Tab focus movement | RmlUi `Context` (native) + new `tabindex` on trigger markup | no new C++ traversal code for the base case |

RmlUi remains presentation-only throughout: every mutation this slice touches
is either transient `InspectorPanel` state or a re-dispatch into an
**existing** Command-backed action; no new `ProjectDocument` field, Command,
Intent, or Undo entry is introduced.

### Alternatives rejected

- **A generic cross-dropdown "enumerate current entries" abstraction now.**
  Rejected for this slice — every dropdown already builds its own option list
  inline (the established style per ADR-0032), and generalizing before a
  third caller exists risks guessing the wrong shape. Revisit once the Layer
  picker's pattern is being copied a second and third time.
- **Scoping/trapping Tab within the Inspector panel** (the
  `HelpDialogController` pattern). Rejected — the ask is "classic Tab
  behavior," which is document-wide by default and already works; trapping it
  would be a regression in a non-modal context for no user-facing benefit.
- **RmlUi's native CSS `nav-up/-down/-left/-right` spatial navigation**
  (implemented in vendored RmlUi, unused in this project's `.rcss`). Rejected
  — it is grid/spatial navigation, not "Up/Down cycles a list of options in
  document order," and adopting it here would be the first use of a
  previously-inert mechanism for a semantics mismatch.

### Non-goals (this slice)

- Extending arrow-key navigation to every other Inspector dropdown (Sprite
  Source, Tileset, Default Clip, text/gauge bindings, Game View preset,
  per-variable type) — planned follow-up once the Layer-picker spike is
  verified live.
- Any change to the global/app-level shortcut catalog or its bindings.
- Any `ProjectDocument`/Command/Undo/schema change — this is entirely
  presentation state.
- The Logic Board panel's own pickers (separate panel, separate controller,
  out of scope here).

## Verification

- Live `--shot` pass on the Layer picker: open, ArrowDown/Up moves the
  highlight, Enter commits, Escape closes without committing, Tab reaches and
  Enter/Space-activates the trigger, Tab while open closes the dropdown and
  advances focus.
- Existing suites touching this area (`inspector-object-variables-routing-test`,
  `logic-board-editor-test`) unaffected — no shared code changes outside
  `InspectorPanel` and the Escape-routing path.
- New RmlUi routing test coverage for the Layer picker's open/highlight/
  commit/cancel cycle, analogous to ADR-0032's Escape/Enter/discard coverage.

## Definition of Done

- Layer picker: ArrowUp/Down highlight, Enter commit, Escape close-without-
  commit, trigger is tab-reachable and Enter/Space-activatable, Tab-while-open
  closes and advances — all verified live via `--shot`.
- No `ProjectDocument`/Command/Undo/schema change introduced.
- Escape precedence wired without regressing any existing `EscapeOwner` case
  (modal, context menus, script editor, help dialog, etc.).
- Implementation status recorded in this document before extending the
  pattern to any other Inspector dropdown.

## Implementation status

Implemented on 2026-07-27, not yet committed.

- `InspectorPanel` gained `dropdownHighlightIndex_` and
  `navigableDropdownEntries_` (rebuilt every `refresh()`, cleared at its top),
  `moveDropdownHighlight()`, and `dropdownHighlightCommit()`. Both are reset
  everywhere `openDropdownId_` already was (`toggleDropdown`, `closeDropdowns`,
  `dismissTransientMenus`, `setOpenDropdown`, `toggleSection`,
  `reconcileOpenDropdownForScene/Entity`, the Play/selection-change resets in
  `refresh()`).
- The Layer picker's render loop (`inspector_panel.cpp`) now pushes one
  navigable entry per pickable `.drop-entry` in the exact order rendered
  (`targetLocked` entries excluded, matching their existing click-does-nothing
  behaviour), and adds `.highlighted` when the index matches.
- `dropdownTrigger()` gained an optional `extraClass` parameter; only the
  Layer trigger passes `"kbd-nav"`. `controls.rcss` scopes
  `tab-index: auto` to `.drop-trigger.kbd-nav` — every other dropdown trigger
  is unaffected and stays exactly as before. `theme.rcss` folds
  `.drop-entry.highlighted` into the existing `surface-hover` `ds-role` group
  (not a new token), keeping `ui-stylesheet-tokens-test` green.
- `EditorUi::Listener::ProcessEvent` intercepts Up/Down/Enter/Escape on the
  Layer trigger's own keydown only while its dropdown is open
  (`action == "toggle-inspector-dropdown" && arg == "layer" && openDropdownId()
  == arg`), deferring all four to `processFrame()` via
  `pendingDropdownHighlightMove_` / `pendingDropdownHighlightCommit_` /
  `pendingDropdownClose_` — the same reason every other field in this listener
  defers its ending: RmlUi is still using the focused trigger element when the
  dispatch returns, and `moveDropdownHighlight`/`closeDropdowns` rebuild the
  Inspector's markup. Tab is recorded for close but never
  `StopPropagation()`'d, so RmlUi's own native document-wide tab-cycling still
  runs in the same frame and moves focus on.
- Escape gap fix: `EditorUi::hasOpenContextMenu()` now includes
  `inspector_.hasOpenDropdown()`, and `editor_action_dispatcher.cpp`'s
  `EscapeOwner::ContextMenu` case calls `ui_.dismissInspectorTransientMenus()`
  alongside the existing `ui_.hideContextMenus()`.
- New test `tests/inspector-layer-dropdown-keyboard-test.cpp` (registered in
  `tests/CMakeLists.txt` and `scripts/build.bat --test`): starts from the real
  rendered trigger/entries, crosses the real `EditorUi` listener, and
  re-fetches every element after each `frame()` (each dropdown/highlight
  change rebuilds `#inspector-body`, invalidating prior pointers — this bit
  the test's first draft). Covers: trigger has `kbd-nav`;
  `hasOpenContextMenu()` false→true→false across open/Escape;
  ArrowDown highlights the non-current entry without touching the document
  (`revision()` unchanged); ArrowUp wraps the highlight back onto the current
  (`.selected`) entry; Escape closes without committing (layer unchanged,
  list collapsed); reopen → ArrowDown → Enter commits `set-entity-layer`
  (`revision()` changes, `canUndo()` true, dropdown closes) and the mutation
  is a normal one-step Undo.
- Full suite green: `scripts\build.bat --test` (all existing suites
  unaffected, including `ui-stylesheet-tokens-test`), plus the new
  `inspector-layer-dropdown-keyboard-test: 32 passed, 0 failed`.
- **Not done / known gap:** no live `--shot` screenshot was taken. The
  documented harness needs a real `.artcade-project` fixture
  (`--shot-project <path>`), and no such fixture exists in this repository
  (the `demo-assets/Untitled/...` path referenced in prior session notes is
  no longer present, and `tests/reference/` holds no `.artcade-project`
  either). The automated test above exercises the real markup, real
  click/keydown dispatch through the real `EditorUi` listener, and the real
  Command/Undo path, which is a stronger check of the new *behaviour* than a
  static screenshot would be; the new `.highlighted` style itself carries no
  independent visual risk since it reuses the already-shipped `surface-hover`
  background rather than a new color. A live check remains worth doing the
  next time a real project is open in the editor, before extending this
  pattern to any other dropdown.
- Not yet extended beyond the Layer picker (by design — see Non-goals).
