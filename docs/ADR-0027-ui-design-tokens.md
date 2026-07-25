# ADR-0027 — UI Design Tokens and Component Contracts

**Status:** Accepted — phases 1–4 implemented (all stylesheets migrated,
invariants enforced by test, components under visual regression); phase 5 open  
**Date:** 2026-07-25  
**Scope:** `src/editor-native/resources/ui/*.rcss` (4182 lines), the colour and
spacing values they declare, the contracts of shared component classes, and the
enforcement that keeps both from drifting. No C++ authority, no domain model, no
markup restructuring.  
**Related:** Constitution §3 (AC-SIMPLE-001, minimal sufficient solution), §4
(single authority per datum), Engineering Gates §5 (new-structure
justification), §33 (performance budgets — untouched here),
[ADR-0024](ADR-0024-editor-actions-shortcuts.md) (shared catalog precedent),
`ARTCADE_RMLUI_FONT_GUIDELINES_FOR_CLAUDE.md` (typography is already
systematised — this ADR does the same for colour and spacing)

## Context

`theme.rcss` declares the product's visual intent in its opening comment:
a **zinc palette with a single blue accent**, `dp` units, Inter/JetBrains Mono
typography. It is 77 lines and defines `body`, `.panel-header`, `.accent`, the
icon classes and the scrollbar. Everything else — every panel, control, dialog,
menu and editor surface — declares its own colours inline.

A full inventory of the eight stylesheets (measured, not estimated):

| Metric | Value |
|---|---|
| Colour literals | 822 |
| Distinct colour values | 163 |
| Distinct **surface** (`background-color`) values | 63 |
| Distinct **text** (`color`) values | 68 |
| Distinct **border** values | 42 |
| Distinct spacing values (`padding`/`margin`/`gap`) | 21 |

### The declared theme is not the implemented theme

Classifying all 822 literals by hue:

| Family | Literals | Share |
|---|---|---|
| Neutral zinc (r≈g≈b) | 288 | 35% |
| **Blue-tinted grey** (b > r, low saturation) | **319** | **39%** |
| Blue accent + shades | 144 | 18% |
| Red / amber / green semantics | 50 | 6% |

Two complete grey families coexist, and the larger one is not the declared
palette. This is not a per-editor split (e.g. "the SFX editor has its own
look"): `controls.rcss` and `panels.rcss` each use **both** families, so the
same control kind can be zinc in one state and blue-grey in another.

### The same role has several values

Reading the selectors that consume each value, the roles are unambiguous — the
values are not:

| Role (from actual usage) | Values in use |
|---|---|
| Hover surface (`.menu-entry:hover`, `.tool-btn:hover`, …) | `#222226`, `#1e2026` |
| Strong border (menus, popovers) | `#323236`, `#30333b` |
| Emphasis text | `#e4e4e7`, `#f4f4f5`, `#ffffff` |
| Accent (solid) | `#3b82f6`, `#2f74e6`, `#2563eb`, `#4d8dff`, `#60a0ff`, `#60a5fa`, `#7aa2e8`, `#93b4f5` |
| Accent (translucent fill) | `#3b82f626`, `#3b82f61c`, `#3b82f61a`, `#3b82f618`, `#4d8dff12` |

And the inverse hazard — two **unrelated** roles separated by one unit of blue,
a difference no one can see and nothing records as intentional:

| Value | Role | Consumers |
|---|---|---|
| `#161619` | deep chrome | `#console`, `#status-bar`, `#center-modebar`, `#tile-palette-dock`, `.console-toolbar` |
| `#161618` | disabled control | `.drop-trigger.disabled`, `.prop-input:disabled`, `.asset-option.disabled`, `#btn-grid-size.disabled` |

Anyone "tidying up" one into the other would silently fuse the console
background with the disabled-field background forever.

Spacing shows the same pattern: 21 distinct `dp` values where `8/4/6/10/2/12`
account for the vast majority, with `5`, `7`, `9`, `13`, `14`, `18`, `26`, `28`
filling gaps arbitrarily.

### Why this is a defect, not a matter of taste

Two bugs shipped and were fixed on 2026-07-25, both caused by the absence of
this system rather than by a coding mistake:

1. **`805d596`** — `.menu-entry` was switched from `display: block` to
   `display: flex` so that one row (Help ▸ Keyboard Shortcuts) could get a
   right-aligned shortcut column. Every other consumer of that shared base —
   File/Edit/View menus, the Hierarchy Create dropdown, the Object Types
   catalog, context menus — renders its label as a bare text node, which RmlUi
   flex containers do not lay out. All of them silently lost their labels.
   There was no written contract stating what `.menu-entry` guarantees or how a
   variant should be added, and no test that would notice.
2. **`e4093c8`** — `.context-submenu` carried `margin-left: -4dp`, pulling the
   flyout back over its parent menu's padding so the two borders fused into one
   surface. With no spacing scale, a negative 4 looked as plausible as a
   positive 6.

Both are the same failure mode: **a shared visual definition changed for one
consumer, with nothing to state the contract and nothing to detect the
breakage.** As panels keep being added (Logic Board, SFX, Script, Tileset
editors already, more to come), that mode recurs by construction.

### Technical constraint

RCSS has **no CSS variables**. The at-rules RmlUi 6.1 supports are
`@decorator`, `@media`, `@spritesheet` and `@keyframes`; `var()` and custom
properties do not exist. Verified in the vendored RmlUi sources. Any token
system must therefore work by **grouping consumers** rather than by
substitution.

Two facts make theming cheap once tokens exist, and both are verified:
stylesheets are linked declaratively in `editor_shell.rml` in cascade order,
and `ElementDocument` exposes `SetStyleSheetContainer()` / `ReloadStyleSheet()`.

## Decision

### 1. One authority for every design value

`theme.rcss` becomes the **only** file allowed to declare a colour literal or a
spacing value. Component stylesheets keep everything structural — layout, flex,
sizes, transitions, geometry — and declare no colours.

Because RCSS cannot substitute values, a role is expressed by **grouping its
consumers in the token layer**:

```rcss
/* token layer — the only place #1c1c1f exists */
.menu-dropdown, .create-dropdown, .context-menu, .drop-list, .tool-btn {
    background-color: #1c1c1f;
}
```

This is the pre-variables CSS technique. It is greppable, auditable, and it
makes a theme a single swappable file.

### 2. The role vocabulary

Derived from the selectors that already consume each value — the names describe
**purpose**, never appearance, so a light theme can invert the values without
renaming anything.

**Surfaces**
| Role | Current dominant value | Used by |
|---|---|---|
| `surface-chrome-deep` | `#161619` | console, status bar, mode bar, tile-palette dock |
| `surface-chrome` | `#18181b` | menubar, toolbar, tabs, panel headers |
| `surface-raised` | `#1c1c1f` | menus, dropdowns, popovers, buttons |
| `surface-sunken` | `#111113` | text inputs, code areas, value fields |
| `surface-hover` | `#222226` | any hovered interactive row |
| `surface-disabled` | `#161618` | disabled controls |

There is deliberately **no** `surface-app` role: `body`, `#workspace` and
`#viewport` are `transparent` because raylib draws the scene beneath RmlUi.
That is existing architecture — the viewport is a transparent RmlUi element so
the raylib-drawn scene shows through, see
[`RMLUI_NATIVE_EDITOR_REPORT.md`](RMLUI_NATIVE_EDITOR_REPORT.md) §"Raylib /
OpenGL integration" — and this ADR does not touch it.

**Text**
| Role | Current dominant value |
|---|---|
| `text-strong` | `#e4e4e7` (emphasis, titles) |
| `text-primary` | `#d4d4d8` (body default) |
| `text-secondary` | `#c4c4c8` (interactive labels) |
| `text-muted` | `#8a8a93` (status, readouts) |
| `text-eyebrow` | `#71717a` (section/eyebrow labels) |
| `text-hint` | `#52525b` (shortcuts, carets) |
| `text-disabled` | `#3f3f46` |

**Borders**: `border-subtle` `#27272a`, `border-strong` `#323236`,
`border-accent` `#3b82f6`.

**Accent and semantics**: one `accent` (`#3b82f6`) with exactly one hover and
one pressed shade, plus one translucent fill; `danger` (`#e5706b`), `warning`
(`#d8b44a`), `success` (`#4f9b68`). The eight accent shades and five
translucent fills collapse into this set.

The blue-tinted grey family is **retired**: its 319 literals map onto the roles
above. Where a blue-grey value is doing genuine work that a neutral cannot
(e.g. a deliberately cool code-editor gutter), it becomes a named role of its
own rather than an undocumented sibling.

### 3. The spacing and radius scale

Spacing: **2 · 4 · 6 · 8 · 12 · 16 · 24 dp**. Radius: **2 · 4 · 8 dp**
(2 stays the default — it is already 53 of 77 declarations). Values outside the
scale require a comment stating why, exactly like a magic number in C++.

### 4. Component contracts

Every shared class in the token layer carries a short comment stating what it
guarantees and how variants are added — the missing piece behind `805d596`.
For `.menu-entry`, that contract is: *block layout, accepts a bare text node as
its label; a row needing extra columns opts into `.with-shortcut` and wraps its
text in spans.* Contracts are required for the classes with many consumers — defined once,
applied from RML and from the C++ panels that build markup:

| Class | Occurrences | Files |
|---|---|---|
| `.panel-btn` | 65 | 12 |
| `.tool-btn` | 30 | 1 |
| `.menu-entry` | 28 | 3 |
| `.context-entry` | 19 | 2 |
| `.drop-entry` | 13 | 3 |
| `.prop-input` | 12 | 3 |

Single-use classes need no contract.

### 5. Enforcement

A test fails the build when a colour literal or an off-scale spacing value
appears outside `theme.rcss`. Without it this ADR is a one-off cleanup that
re-rots at the next panel; with it, the single authority is an invariant in the
same sense as the domain invariants the Constitution already enforces.

### 6. Visual regression on the existing harness

A gallery document renders every shared component in every state (normal,
hover, disabled, selected, with-shortcut) and is captured with the existing
`--shot` harness into a committed reference image, diffed on change. This is
the check that turns "shared style changed" from a user-reported bug into a
failing test — the editor currently has **no** UI test coverage at all, and
this class of defect is invisible to the C++ suites.

## Staging

Deliberately not one commit. Each phase is independently valuable and
independently revertible.

| Phase | Content | Gate to proceed |
|---|---|---|
| 1 | Role vocabulary + scale into `theme.rcss`; migrate one stylesheet (`controls.rcss`, the largest and the source of both bugs) | Live screenshots match pre-migration for every touched surface |
| 2 | Migrate the remaining sheets, one commit each | Same, per sheet |
| 3 | Enforcement test | Suite green with zero literals outside the token layer — **done**; spacing enforcement withdrawn, see below |
| 4 | Component gallery + reference capture | Reference image reviewed and committed — **done** |
| 5 | Second theme (light or high-contrast) | Only when a real need exists — not preventive |

Phases 1–4 are justified by defects already shipped. Phase 5 is explicitly
**not** authorised by this ADR; it is listed to show that the design does not
have to change to accommodate it later.

## Consequences

- A visual value has one home; changing a hover colour is one edit, not a
  grep across 4182 lines.
- The declared theme and the implemented theme converge — today they disagree
  on 39% of the literals.
- Contrast can be audited once per role instead of per literal, which is what
  makes an accessibility claim defensible for a commercial product.
- Theming becomes a file swap, using API that already exists.
- Cost: a broad-touch migration of stylesheets. This is the risk the
  Constitution flags as "refactor massivo"; it is accepted here only because
  the defect it prevents has already occurred twice, and it is mitigated by
  per-sheet commits with visual verification at each step.
- Not addressed by this ADR: icon sizing, motion/transition durations, and the
  RML markup structure. They stay as they are.

## Phase 1 — implemented

`theme.rcss` (77 → 567 lines) is now the sole holder of colour, and
`controls.rcss` (1212 → 1064 lines) declares **zero** colour literals: the 225
declarations it carried became 34 role groups. 21 distinct values are declared
where controls.rcss alone had used ~80.

Migration was scripted rather than hand-edited, and the script **aborts on any
colour it cannot classify** instead of guessing — it stopped twice, on 9 and
then 4 unmapped values, each of which was classified deliberately. Two
verifications back the result: no rule lost a non-colour property (checked
programmatically against the pre-migration file — 77 rules disappeared, all of
them colour-only state rules), and RmlUi parses the sheets with no warnings.

### Deviation from this ADR: the accent value

The ADR named `#3b82f6` as the accent. Measuring it during implementation
showed **white text on `#3b82f6` is 3.68:1 — below the 4.5 AA threshold**, and
the primary CTA is exactly where that matters. The CTA fill therefore moved one
step down the blue ramp:

| | fill | white text on it |
|---|---|---|
| was | `#3b82f6` | 3.68 ✗ |
| now | `#2563eb` | 5.17 ✓ |
| hover | `#1d4ed8` | 6.70 ✓ |

`#3b82f6` is retained in the palette as `brand`, reserved for the wordmark and
brand marks (its only remaining consumer, `.about-brand`, migrates in phase 2).
This is a brand-adjacent change and is called out so it can be reverted by
decision rather than by accident.

### Accent policy as implemented

Blue survives on `#btn-play-project` and `.tool-btn.primary` only. Everything
that used to be blue — selected rows, active tabs and tools, focus rings, the
selection marker, open dropdown triggers — is now `surface-selected`,
`border-focus` or `text-strong`. Focus rings measure 3.4–4.1:1 against the
surfaces they sit on, meeting the 3:1 that WCAG 1.4.11 asks of control
boundaries.

Resting control borders (`border-subtle`, 1.1:1 on raised) deliberately do not
meet 3:1: those controls are identified by their surface fill — inputs sit on
`surface-sunken`, two steps below their container — and the border is
decorative. Recorded here rather than left as an implicit claim.

## Phase 2 — implemented

The remaining six sheets are migrated. **Zero colour literals now exist outside
`theme.rcss`** — the invariant is absolute, which is what makes phase 3's
enforcement test possible at all.

Three things the phase-1 script had not had to face:

- **`background` shorthand.** `sfx_editor.rcss` writes `background: #xxx`, not
  `background-color`. The property was in neither of the script's lists, so 33
  values passed through *silently* — the one failure mode the abort-on-unknown
  design existed to prevent. Fixed by treating `background` as a colour
  property; the same pass also normalises the named colour `white`, which the
  hex-only regex would have missed.
- **Content vs chrome.** The Script Editor carries a syntax-highlighting
  palette (Material Palenight: keyword, string, comment, number, operator,
  identifier). Those hues encode Lua token classes; flattening them to zinc
  would destroy code readability. They move into `theme.rcss` under a separate
  CONTENT PALETTE section rather than into the chrome roles, so the "no colour
  outside theme.rcss" rule stays absolute without lying about what they are.
  The invisible textarea's `color: #00000000` moves with them: that
  transparency is functional, not decorative.
- **One value, two roles.** `#181d25` served both section backgrounds and a
  hover in `sfx_editor.rcss`. Mapping by value alone would have erased the
  hover feedback, so that one selector carries an explicit override.

### Cleanup

- 81 whitespace-only lines left by the phase-1 strip, removed.
- Three genuinely dead selectors deleted (`.asset-option`, `.asset-options`,
  `.row-indicator-spacer` — no reference in any RML or C++ file). The phase-1
  migration had scattered `.asset-option`'s states across 12 role groups, so
  dead code had been promoted into the design system; it is gone from both
  files now.

### A regression this caught

Rebuilding `theme.rcss` split it on the first `=====*/` marker, which matched
the doc comment rather than the role-group banner — so the base element styles,
including `body { font-family: Inter; … }`, were dropped. RmlUi reported it as
"No font face defined" on a console button. Found by diffing warnings against
the committed phase-1 build, and fixed by reconstructing the file from its
three real parts. Both phases were re-verified afterwards: no rule in any sheet
lost a non-colour property, and the app builds and renders with zero RmlUi
warnings.

## Phase 3 — implemented

`tests/ui-stylesheet-tokens-test.cpp` (CTest target `ui_stylesheet_tokens_test`,
also wired into `scripts\build.bat --test`) enforces two invariants against the
shipped `.rcss` sources:

1. no colour of any kind outside `theme.rcss` — hex **and** named colours, so
   `color: white` cannot slip past a hex-only scan the way it nearly did in
   phase 2;
2. every colour `theme.rcss` declares must appear in one of its palette
   comments, so growing the palette costs a line of documentation and a
   reviewer sees it.

It found a real gap on first run: `warning-border` (`#6b5420`) had been
introduced during phase 2 without being added to the documented palette. Now
documented.

The scanner is checked against the shapes real sheets use, including the one it
must *not* flag — an RCSS id selector (`#btn-play-project`) is not a colour —
and the whole thing is verified end to end by introducing a deliberate
violation and confirming it fails. A test that cannot fail is not enforcement.

### Correction: spacing is not enforced

This ADR's §Scale proposed 2/4/6/8/12/16/24dp. Measured against the UI as
built, that scale is wrong: `10dp` alone appears 52 times, and 152 declarations
across 14 values sit off the proposed scale. The colour work could be a
faithful migration because roles were derived from real usage; the spacing
scale was not, and adopting it now would change the density of every panel.

Spacing therefore stays unenforced, and the scale above should be treated as
**not yet decided** rather than as a rule the code is failing. Fixing it is its
own phase, with its own visual verification — the same discipline the colour
phases used.

## Phase 4 — implemented

`componentGalleryMarkup()` (`src/editor-native/ui/component_gallery.cpp`)
renders every contracted component in every state on one page: the surface
elevation ramp, the text roles, and `tool-btn` / `panel-btn` / `menu-entry` /
`context-entry` / `drop-entry` / `prop-input` in normal, hover, active,
selected, primary and disabled. `--shot-gallery` injects it into the live shell
— not into a copy — so it exercises the real cascade, and
`applyGalleryForcedStates()` drives `SetPseudoClass` so hover and focus appear
in a capture that has no mouse.

`scripts/check_ui_gallery.py` renders it and diffs against
`tests/reference/ui-gallery.png` (`--update` to accept a change). Each specimen
carries a caption, so a failure's bounding box names the component that moved
rather than pointing at a coordinate.

**It detects the exact defect class this ADR exists for.** Re-introducing the
migration's own bug — dropping `.tool-btn.disabled` from its role group —
produces `FAIL 1363 pixels differ, bounding box y 51–346`, covering the toolbar
and the tool-btn row. The phase-3 token test cannot see that: it checks where
colours live, not that a selector kept the one it had.

Wired into `scripts\build.bat --test`. The capture is a real GPU render, so the
reference is machine- and DPI-specific; a tolerance of 200 pixels absorbs
driver quantisation, and the reference must be regenerated when the editor is
built on different hardware.

### `.panel-btn.primary` promoted, and what that exposed

`.primary` now reaches the accent on every `.panel-btn`, not only inside
`.editor-modal-actions`: Generate Audio Asset, Create Generated SFX and the
sprite-animation Confirm/Slice actions render as CTAs, which is what the class
name always claimed. `.panel-btn.primary:hover` is spelled out explicitly
because `.panel-btn:hover` has equal specificity and, sitting in a later sheet,
would otherwise drag a primary button back to the grey hover.

Verifying it surfaced a second defect: a **disabled** primary kept its accent
border and white label while losing its fill — half-enabled. Cause is a
three-way tie at (0,2,0): `.panel-btn.disabled` (controls.rcss, later sheet)
won the background, while `.panel-btn.primary` (theme.rcss, later *rule*) won
border and text. Resolved with `.panel-btn.primary.disabled` at (0,3,0), and
pinned in the gallery as its own specimen so it cannot drift back.

Worth recording for the next migration: `background-color: transparent` never
moved to the token layer, because the migration matched colour *literals* and
`transparent` is a keyword. Those leftovers are what create these ties.

### A gap the gallery found immediately

`.panel-btn.primary` renders as a plain button: the accent only applies to
`.editor-modal-actions .panel-btn.primary`, so the class is inert at its six
other call sites (Generate Audio Asset, Create Generated SFX, the
sprite-animation Confirm/Slice actions). Pre-existing, not caused by the
migration — but this ADR had documented the contract as "`.primary` promotes it
to the accent CTA", which was simply untrue. The contract now states the gap.
Promoting it globally would turn six buttons blue: a visual decision, left
deliberate rather than folded into tooling work.

### Not yet done

Phase 5 (a second theme), and the spacing scale, which §Correction above
withdrew.

## Verification

- Per phase: live `--shot` screenshots of every migrated surface, compared
  against the pre-migration capture.
- Phase 3: the enforcement test itself (negative test — a deliberately
  introduced literal must fail it).
- Phase 4: the gallery reference image; the `805d596` regression re-introduced
  locally must make the diff fail.
- Existing C++ suites are unaffected (no C++ changes) and must stay green.
