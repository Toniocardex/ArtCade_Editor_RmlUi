# ADR-0035 — Dropdown Keyboard Navigation Rollout (Inspector + Logic Board)

**Status:** Accepted
**Date:** 2026-07-27
**Scope:** Generalizes ADR-0034's Layer-picker spike to every remaining
Inspector value dropdown and to the Logic Board's in-flow dropdowns (WHEN/IF/
THEN block-type catalog, execution mode, variable type, sprite clip/animation
pickers, key search results)
**Related:** [ADR-0034](ADR-0034-inspector-keyboard-navigation.md) (the spike
this generalizes — its Non-goals explicitly deferred this), Architecture
Constitution (RmlUi presentation-only)

## Context

ADR-0034 shipped arrow-key highlight, Enter-commit, and Escape-close for one
dropdown (the Inspector's Scene Layer picker) as a deliberately narrow spike,
with an explicit follow-up: "revisit generalizing once the pattern is being
copied a second and third time." That threshold has now arrived — the request
is every other Inspector dropdown plus the Logic Board's own dropdowns.

Research into the Logic Board (`LogicBoardPanel`, `logic_board_panel.cpp`)
found the same shape ADR-0034 already handled, with one added wrinkle:

- Single `openDropdownId_` (there `mutable`, since `LogicBoardPanel::refresh()`
  is `const`), same toggle-to-close semantics, same in-flow
  `.drop-trigger`/`.drop-list`/`.drop-entry` markup via the shared
  `dropdownTriggerMarkup()` — structurally identical to the Inspector.
- **Different commit contract.** The Inspector puts the picked value directly
  in `data-arg` (`set-entity-layer|layer-2`). The Logic Board always keeps
  `data-arg` as the addressing key and puts the picked value in `data-value`
  (`pick-logic-property data-arg="rule-1|t|0|target" data-value="self"`). A
  shared "commit the highlighted entry" primitive must therefore carry an
  optional value, not just `(action, arg)`.
- **Centralized rendering, not per-dropdown.** Unlike the Inspector (each of
  its ~15 dropdowns builds its `<div class="drop-entry">` markup inline at its
  own call site), the Logic Board funnels almost every dropdown's entries
  through one of three shared functions:
  - `catalogEntries()` (`logic_board_panel.cpp`) — the WHEN/IF/THEN
    block-type catalog (5 call sites: trigger, per-rule add-condition,
    per-condition retype, per-action add, per-action retype). This is the
    picker the user's screenshot shows ("On Start", "Is Visible",
    "Set Position").
  - `dropEntry()` (`logic_board_panel.cpp`, 9 call sites) — variable type,
    execution mode, sprite clip/animation pickers, key search results.
  - `entry()` (`logic_property_editor.cpp`) — per-property value pickers
    (Direction, comparison operator, etc.), reached through
    `renderLogicProperties()`'s already-large parameter list in a different
    translation unit.

  Instrumenting the first two gives keyboard navigation to every Logic Board
  dropdown call site that matters here in two edits instead of fourteen.

## Decision

### A shared, panel-owned `DropdownNavigation` value type

Extract ADR-0034's Layer-specific `dropdownHighlightIndex_` /
`navigableDropdownEntries_` / `moveDropdownHighlight()` /
`dropdownHighlightCommit()` out of `InspectorPanel` into a small reusable
value type (`src/editor-native/ui/dropdown_navigation.h`), so both panels
compose an instance instead of each hand-rolling it:

```cpp
struct DropdownNavEntry {
    std::string action;
    std::string arg;
    std::string value;   // empty for the Inspector's own dropdowns
    bool current = false;
};

class DropdownNavigation {
public:
    void clearEntries();                  // call at the top of every refresh()
    void resetSession();                  // call wherever openDropdownId_ resets
    std::size_t push(DropdownNavEntry entry);
    bool isHighlighted(std::size_t index) const;
    void move(int delta);                 // wraps; starts adjacent to `current`
    std::optional<DropdownNavEntry> commit() const;
private:
    std::vector<DropdownNavEntry> entries_;
    std::optional<int> highlight_;
};
```

This is presentation-only, non-serialized, per-panel state — the same
category as `openDropdownId_` itself (Architecture Constitution: RmlUi/panel
controllers never own document data). `InspectorPanel` holds a plain member;
`LogicBoardPanel` holds a `mutable` member (its `refresh()` is `const`).
`clearEntries()` runs once at the top of every `refresh()` (matching how
`navigableDropdownEntries_.clear()` already worked); `resetSession()` runs at
every existing site that already clears `openDropdownId_`.

### Generalizing the keydown routing (`EditorUi::Listener::ProcessEvent`)

ADR-0034's branch was hard-scoped to `arg == "layer"`. Two changes:

1. Drop that literal — `action == "toggle-inspector-dropdown" && type ==
   "keydown" && ui_.inspector_.openDropdownId() == arg` already scopes
   correctly to whichever dropdown is actually open, for any `arg`.
2. Add the mirror branch for the Logic Board: `action ==
   "toggle-logic-dropdown" && type == "keydown" &&
   ui_.logicBoardEditor_.openDropdownId() == arg`, deferring to its own
   `pendingLogicDropdownHighlightMove_` / `*Commit_` / `*Close_` trio
   (separate from the Inspector's, since the two panels' commits differ in
   shape and there is no shared dispatcher object to key off of).

Both branches' Enter/commit path calls `handleAction(entry.action, entry.arg,
entry.value)` — `handleAction`'s existing `value` parameter already exists
for exactly this purpose (the generic click dispatch already reads
`data-value` when present), so the Inspector's own dropdowns (which leave
`value` empty) and the Logic Board's (which need it) are handled by the same
call shape.

### Generalizing focusability (RCSS)

ADR-0034 scoped `tab-index: auto` to a `.kbd-nav` opt-in class on the one
Layer trigger it touched. Now that every dropdown trigger gets the same
treatment, that opt-in class is removed and `tab-index: auto` moves onto the
base `.drop-trigger` rule in `controls.rcss` — every trigger in both panels
becomes tab-reachable, and RmlUi's native "Enter/Space clicks a focused
`tab-index: auto` element" continues to make Tab-then-Enter/Space open any of
them, with zero additional C++.

### Generalizing the Escape gap fix

`EditorUi::hasOpenContextMenu()` already includes `inspector_.hasOpenDropdown()`
(ADR-0034). This adds `|| logicBoardEditor_.hasOpenDropdown()` (new accessor,
mirroring `InspectorPanel`'s). The `EscapeOwner::ContextMenu` case in
`editor_action_dispatcher.cpp` gains a call to a new
`ui_.dismissLogicBoardTransientMenus()` (closes the Logic Board's open
dropdown + repaints), alongside the existing `hideContextMenus()` and
`dismissInspectorTransientMenus()` calls — all three are unconditionally safe
to call together, each a no-op when its own thing isn't open.

### Rendering: where entries get pushed

- **Inspector** (`inspector_panel.cpp`): every one of its ~15 dropdown render
  blocks gets the same three-line addition already proven on the Layer
  picker — push a `DropdownNavEntry` for each pickable row (skipping
  `.disabled`/no-`data-action` rows and, for Sprite Source, its
  `asset-group-title` header rows, which were never entries to begin with),
  and add `.highlighted` when `dropdownNav_.isHighlighted(index)`.
- **Logic Board** (`logic_board_panel.cpp`): `catalogEntries()` and
  `dropEntry()` each gain a `DropdownNavigation& nav` parameter (threaded from
  every call site, all within this one file) and push/highlight the same way.
  `catalogEntries()` already skips incompatible (`!availability.compatible`)
  entries from getting a `data-action` — those are excluded from navigation
  the same way Inspector's locked layers are.

### Explicit scope boundary: `logic_property_editor.cpp`'s `entry()`

**Deferred, not done in this pass.** `entry()` is reached through
`renderLogicProperties()` in a different translation unit, itself already a
9-parameter function with no context struct to fold a `DropdownNavigation&`
into without a wider signature change than this rollout's other, self-contained
edits. It covers per-property value pickers (Direction, comparison operator,
and similar small fixed-choice fields *within* an already-selected
condition/action), not the block-type selection itself — lower-traffic than
the WHEN/IF/THEN catalog this rollout does cover. Tracked as a follow-up; those
triggers still gain Tab-reachability and Escape-close today via the universal
CSS/routing changes above, they just don't get arrow-key highlight yet.

### Non-goals (this rollout)

- `logic_property_editor.cpp`'s `entry()` (see above — explicit follow-up).
- The Logic Board's Object Type picker — it is a floating menu outside the
  scrollable rules list (a different mechanism from every dropdown covered
  here), and the Hierarchy/Assets/viewport context menus — also floating,
  already covered by their own existing Escape-close path.
- Any `ProjectDocument`/Command/Undo/schema change — unchanged from ADR-0034,
  still 100% presentation state.

## Verification

- `inspector-layer-dropdown-keyboard-test.cpp` (ADR-0034) continues to pass
  unmodified after `InspectorPanel` is refactored onto the shared
  `DropdownNavigation` type — proves the extraction is behavior-preserving.
- New coverage added for: a second Inspector dropdown with a disabled
  placeholder row (Text/Gauge Variable's "No compatible variables"); the
  Sprite Source dropdown's grouped `asset-group-title` shape; the Logic
  Board's WHEN/IF/THEN catalog (open → highlight → commit changes
  `rule.trigger`/`.actions`/`.conditions` typeId, one Undo entry); the Logic
  Board's Escape gap fix via `hasOpenContextMenu()`.
- Full suite: `scripts\build.bat --test`.

## Definition of Done

- Every Inspector dropdown and every Logic Board dropdown reached through
  `catalogEntries()`/`dropEntry()` supports Up/Down highlight, Enter commit,
  Escape close-without-commit, and is Tab-reachable/Enter-Space-openable.
- `logic_property_editor.cpp`'s `entry()` explicitly flagged as follow-up, not
  silently missed.
- No `ProjectDocument`/Command/Undo/schema change.
- Full test suite green, including new coverage above.

## Implementation status

Implemented on 2026-07-27, not yet committed.

- New `src/editor-native/ui/dropdown_navigation.h`: `DropdownNavEntry{action,
  arg, value, current}` + `DropdownNavigation` (`clearEntries`/`resetSession`/
  `push`/`isHighlighted`/`move`/`commit`), lifted out of `InspectorPanel`'s
  original Layer-only fields with no behavior change (proved by
  `inspector-layer-dropdown-keyboard-test.cpp` passing unmodified after the
  extraction).
- `InspectorPanel` composes one `DropdownNavigation` member; all 15 dropdown
  render blocks (Layer, Game View preset, Sprite Source [grouped, asset-group
  headers excluded], Sprite/Animator Default Clip, Tilemap Tileset, Script
  Attach, Text Binding/Variable/Format/Align, Gauge Binding/Variable/
  Direction, per-variable Object Variable type) now push navigable entries and
  render `.highlighted`. `dropdownHighlightCommit()`'s public return type
  changed from `optional<pair<string,string>>` to `optional<DropdownNavEntry>`
  to carry a `value` uniformly with the Logic Board.
- `LogicBoardPanel` composes a `mutable DropdownNavigation` (its `refresh()`
  is `const`); gained `openDropdownId()`/`hasOpenDropdown()`/
  `moveDropdownHighlight()`/`dropdownHighlightCommit()` mirroring
  `InspectorPanel`, forwarded through `LogicBoardEditorController`. All 11
  existing `openDropdownId_` reset sites gained a paired
  `dropdownNav_.resetSession()`.
- `catalogEntries()` (the WHEN/IF/THEN block-type catalog — trigger, add/
  retype condition, add/retype action; 5 call sites) and `dropEntry()`
  (variable type, execution mode, sprite clip/animation, audio asset; 9 call
  sites across `logic_board_panel.cpp`, including the `variablesDrawer()`
  helper) each gained a `DropdownNavigation&` parameter and now push/highlight
  the same way. Incompatible catalog entries (`!availability.compatible`) are
  excluded from navigation, matching the Inspector's locked-row precedent.
- `EditorUi::Listener::ProcessEvent`'s keydown branch generalized: the
  Inspector's is no longer scoped to `arg == "layer"` (any open dropdown's own
  trigger matches via `openDropdownId() == arg`); a mirror branch handles
  `toggle-logic-dropdown` with its own pending trio
  (`pendingLogicDropdownHighlightMove_`/`*Commit_`/`*Close_`, kept separate
  since the two panels' commits differ in shape). Enter's commit path calls
  `handleAction(entry.action, entry.arg, entry.value)` uniformly for both.
- `controls.rcss`: `tab-index: auto` moved from the `.kbd-nav` opt-in class
  onto the base `.drop-trigger` rule — every dropdown trigger in both panels
  is now Tab-reachable and Enter/Space-openable via RmlUi's native handling,
  no per-call-site markup change needed beyond removing the now-unnecessary
  `"kbd-nav"` argument from the Layer trigger's call site.
- Escape gap fix extended: `EditorUi::hasOpenContextMenu()` also checks
  `logicBoardEditor_.hasOpenDropdown()`; a new
  `EditorUi::dismissLogicBoardTransientMenus()` (closes the dropdown +
  repaints) is called alongside the existing two in
  `EscapeOwner::ContextMenu`.
- New test `tests/logic-board-dropdown-keyboard-test.cpp` (registered in
  `tests/CMakeLists.txt` and `scripts/build.bat --test`), mirroring
  ADR-0034's Inspector test exactly: real trigger/catalog-entry elements,
  real `EditorUi` listener, re-fetches every element after each `frame()`.
  Covers the WHEN trigger-type catalog's open/highlight/commit/Escape/Undo
  cycle and the Logic Board's `hasOpenContextMenu()` gap fix. Two fixture
  bugs surfaced and were fixed in the test itself, not the feature: the
  `LogicBoardDef.id` field (`"logic:" + objectTypeId`, unrelated to the
  object type's own key) must be set or every Command rejects with
  `LB_BOARD_ID: Invalid board id`; and `Logic::makeDefaultRule()`'s default
  action can make some trigger swaps invalid for unrelated compatibility
  reasons, so the fixture rule keeps no actions to isolate the picker's
  keyboard-nav plumbing from the compiler's own trigger/action compatibility
  rules.
- `inspector-layer-dropdown-keyboard-test.cpp` updated for the CSS
  generalization: the `kbd-nav`-class assertion became a computed-style check
  (`GetComputedValues().tab_index() == Rml::Style::TabIndex::Auto`), since
  tab-index no longer comes from a per-element marker class.
- Full suite green: `scripts\build.bat --test`, including
  `ui-stylesheet-tokens-test` (1295/1295 — the `.drop-entry.highlighted`
  addition to `theme.rcss`'s existing `surface-hover` group and the
  `.drop-trigger` structural change in `controls.rcss` both respect the
  design-token gate), `inspector-layer-dropdown-keyboard-test` (32/32,
  unmodified behavior after the `DropdownNavigation` extraction),
  `logic-board-dropdown-keyboard-test` (27/27, new).
- **Known pre-existing flake, unrelated to this work**: `check_ui_gallery.py`'s
  `[expression]` (and, once, `[logic]`) reference-image diff is nondeterministic on
  this machine — confirmed by reproducing it against the last clean commit
  *before* any ADR-0035 change, with a different pixel count on each run
  (likely a blinking-caret or animation-phase capture race, matching the
  script's own "machine-specific" comment). Not caused by and not fixed by
  this change.
- Explicitly not done: `logic_property_editor.cpp`'s `entry()` (per-property
  value pickers — Direction, comparison operator, etc.) — see the dedicated
  scope-boundary section above.
