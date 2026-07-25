# ADR-0027 — UI Design Tokens and Component Contracts

**Status:** Proposed — awaiting approval before any RCSS change  
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
| 3 | Enforcement test | Suite green with zero literals outside the token layer |
| 4 | Component gallery + reference capture | Reference image reviewed and committed |
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

## Verification

- Per phase: live `--shot` screenshots of every migrated surface, compared
  against the pre-migration capture.
- Phase 3: the enforcement test itself (negative test — a deliberately
  introduced literal must fail it).
- Phase 4: the gallery reference image; the `805d596` regression re-introduced
  locally must make the diff fail.
- Existing C++ suites are unaffected (no C++ changes) and must stay green.
