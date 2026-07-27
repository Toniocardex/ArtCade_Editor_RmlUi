# ADR-0033 — Product-Wide UI Design System Contract

**Status:** Accepted  
**Implementation:** Complete  
**Date:** 2026-07-27  
**Completion evidence:** `scripts\build.bat --test` green; recursive
design-system gate reports zero failures; reviewed `gallery`, `scene`,
`logic`, `expression`, `anim`, and `tileset` references all match.  
**Scope:** every shipped native-editor RML/RCSS document under
`src/editor-native/resources/ui/`, UI markup emitted from
`src/editor-native/ui/`, the platform window chrome, and the visual-regression
harness  
**Related:** [ADR-0027](ADR-0027-ui-design-tokens.md) (main-shell colour
authority and component contracts),
`ARTCADE_RMLUI_FONT_GUIDELINES_FOR_CLAUDE.md`,
Architecture Constitution §§3–4, Architecture §§14 and 36,
Engineering Gates §§4–7, 19, 36–37, and 43

## Context

ADR-0027 established the first real design-system boundary for the main editor
shell:

- `theme.rcss` is the only top-level stylesheet allowed to declare colours;
- colour values have semantic roles rather than component-local names;
- widely shared classes have written contracts;
- a component gallery captures the important states;
- token and visual-regression checks are part of `scripts/build.bat --test`.

That work is valuable and remains authoritative for its historical decisions.
It does not yet cover the whole application, and some foundations were
deliberately left undecided.

### Measured baseline

The following inventory was measured from the shipped sources on 2026-07-27.
Comments are excluded from the counts.

| Area | Current state |
|---|---:|
| Main-shell chrome colours in `theme.rcss` | 25 distinct values |
| Script syntax/content colours | 11 distinct values |
| Colour literals in nested Sprite Animation and Tileset sheets | 114 |
| Distinct colours in those nested sheets | 45 |
| Nested colours absent from the central palette | 33 |
| Distinct `margin`/`padding`/`gap` values application-wide | 22 |
| Distinct radius values | 6 |
| Transition durations | one value (`0.1s`, 99 declarations) |

The top-level token test passes because it uses a non-recursive directory
iterator. It does not inspect:

- `ui/sprite-animation/sprite_animation_editor.rcss`;
- `ui/tileset/tileset_editor.rcss`;
- any future stylesheet added in another subdirectory.

Both overlay documents load `theme.rcss`, `controls.rcss`, and `panels.rcss`,
but then introduce local neutral, accent, selection, warning, success, and
danger shades. Examples include several independent blue ramps for selected
rows, primary buttons, informational panels, and empty-state calls to action.
The overlays therefore resemble the main shell without consuming the same
visual authority.

Spacing has the inverse problem. ADR-0027 proposed
`2 / 4 / 6 / 8 / 12 / 16 / 24dp`, then correctly withdrew enforcement after
measurement showed that it did not describe the existing product. The sources
currently use, among other values:

| Value | Occurrences |
|---|---:|
| `8dp` | 83 |
| `4dp` | 66 |
| `6dp` | 65 |
| `10dp` | 65 |
| `12dp` | 39 |
| `2dp` | 38 |
| `5dp` | 28 |
| `3dp` | 16 |
| `7dp` | 13 |
| `14dp` | 12 |
| `9dp` | 11 |
| `16dp` | 9 |
| `18dp` | 8 |

Consequently, the scale still printed in the opening comment of `theme.rcss`
is aspirational rather than true. A rule that the product itself violates is
not a design-system contract.

Typography is more regular but is not expressed as a role vocabulary. Most
text is `10dp`, `11dp`, or `12dp`; isolated `9dp`, `14dp`, `17dp`, and other
sizes have accumulated. Icon glyphs consistently use the vendored Tabler font,
but their raw private-use codepoints are repeated across RML and C++ markup
without one semantic catalog.

### Why centralisation alone is insufficient

A file containing every hexadecimal value is a palette registry. It becomes a
design system only when all of the following are true:

1. the vocabulary describes meaning rather than appearance;
2. every shipped surface consumes that vocabulary;
3. components have stable markup and state contracts;
4. spacing, type, radius, motion, and icons follow finite rules;
5. exceptions are explicit and reviewable;
6. automated checks stop the system from drifting;
7. specimens and real compositions make visual changes reviewable.

The current main shell satisfies much of this definition. The application as a
whole does not.

## Problem

Without a product-wide contract:

- a new subdirectory silently escapes the colour invariant;
- two controls with the same meaning can use different accent or neutral
  shades;
- “close enough” grays and blues grow without a new semantic need;
- spacing is selected per selector rather than from a deliberate rhythm;
- a component can be visually compatible in isolation but inconsistent in an
  overlay;
- changing a theme cannot reliably update platform chrome and every RmlUi
  document;
- visual references cover the main shell but not both standalone editor
  documents.

Review by eye is insufficient. It did not prevent the original 163-colour
palette, and it does not prevent the current nested exception.

## Decision

ArtCade adopts one product-wide UI design-system contract. ADR-0027 remains the
historical basis; this ADR extends its scope recursively, replaces its proposed
spacing rule, and defines the missing foundations and gates.

`Status` records that this decision is binding. `Implementation` records
delivery and moves independently through `Not started`, `In progress`, and
`Complete`. Existing violations are the measured migration baseline; acceptance
does not permit new UI work to add further violations while that debt is being
removed.

### 1. Authority and applicability

The contract applies to every native editor document and every stylesheet
recursively below `src/editor-native/resources/ui/`.

| Datum | Authority |
|---|---|
| RmlUi chrome colour literals and semantic colour roles | machine-readable role groups in `theme.rcss` |
| Allowed spacing, radius, typography, and motion vocabularies | this ADR, reflected in the documented header of `theme.rcss` |
| Shared component geometry and markup contract | the component's canonical RCSS class plus its contract comment |
| Tabler glyph mapping in C++-generated markup | one small `ui_icons` catalog in the RmlUi presentation module |
| Tabler glyphs in static RML | document-local references constrained by the verified Tabler allowlist |
| Syntax highlighting colours | the namespaced content-palette section of `theme.rcss` |
| Visual expected result | reviewed gallery and composition references |

Component sheets own layout and geometry, but may only use values from the
approved metric vocabularies. `theme.rcss` owns colour declarations because
RmlUi 6.1 has no custom properties or `var()`.

No project data, domain type, serializer, Intent, Command, or runtime service is
introduced. The design system is presentation-only.

#### Machine-readable colour roles

Narrative palette comments are useful to a reviewer but are not a stable
interface for enforcement. Every colour-bearing rule in `theme.rcss` must use
this exact marker immediately before the rule:

```rcss
/* ds-role(surface-base): background-color */
#console,
#status-bar {
    background-color: #131316;
}
```

The deliberately small grammar is:

```text
/* ds-role(<role-name>): <colour-property> */
<one RCSS rule containing exactly one declaration for that property>
```

Rules:

- `<role-name>` must be one of the chrome or namespaced content roles defined
  by this ADR;
- `<colour-property>` is one of `color`, `background`, `background-color`,
  `border`, `border-color`, a side-specific border shorthand, or a
  side-specific border-colour property;
- the marker applies only to the immediately following rule, ignoring
  whitespace;
- the marked rule must contain exactly one colour literal in the named
  declaration;
- a rule with two colour-bearing declarations is split into two selector rules,
  each with its own marker;
- a role may appear in multiple rules and properties, but every occurrence must
  resolve to the same literal;
- multiple roles may resolve to the same literal; this equality is the alias
  and requires no second alias registry;
- an unknown role, conflicting values for one role, a marker without a matching
  declaration, or a colour rule without a marker fails the test;
- a marker whose following rule has no selector consumer fails. A vocabulary
  role need not have a declaration until a real consumer exists; no literal is
  reserved for future use.

The test parses only this marker and the adjacent declaration. It must not
evolve into a general CSS parser.

#### Platform chrome

Operating-system APIs cannot consume RCSS. A platform adapter may mirror the
minimum colours required for the native title bar, but:

- the mirror uses named constants, never call-site literals;
- every mirrored value uses the exact marker
  `// ds-role-mirror(<role-name>)`;
- the mirrored constant stores canonical RGB (`0xRRGGBB`), and conversion to
  the platform's `COLORREF`/BGR representation happens in the adapter;
- an automated test verifies that the mirrored values still equal the
  machine-readable `theme.rcss` role values;
- a mirror naming an unknown role or a role without one resolved value fails;
- the adapter is a derived consumer, not a second palette authority.

### 2. Semantic colour palette

Components never select a shade. They select a semantic role.

The following role families are the complete application chrome vocabulary:

| Family | Roles |
|---|---|
| Surfaces | `surface-sunken`, `surface-base`, `surface-chrome`, `surface-raised`, `surface-hover`, `surface-selected`, `surface-disabled` |
| Borders | `border-subtle`, `border-strong`, `border-focus` |
| Text | `text-strong`, `text-primary`, `text-secondary`, `text-tertiary`, `text-disabled` |
| Primary action | `action`, `action-hover`, `on-action` |
| Brand | `brand`, restricted to the wordmark and genuine brand marks |
| Danger | `danger-text`, optional `danger-text-hover`, `danger-surface`, `danger-border` |
| Warning | `warning-text`, optional `warning-surface`, optional `warning-border` |
| Success | `success-text`, optional `success-surface`, optional `success-border` |

Roles may alias the same literal. A semantic role does not earn a new colour
merely by having a different name.

The palette currently documented in `theme.rcss` is frozen as the maximum
baseline for the migration. Migration phases may reuse, alias, or remove
values. They may not add a new chrome literal to accommodate an old local
shade.

#### Information is neutral

The current product has no independent informational-status semantic.
Informational panels use neutral surfaces and border roles, plus normal text or
the existing action role for an actionable link/button. “Info blue” is not a
role and must not be reconstructed while migrating the standalone editors.

If a future feature needs information to be visually distinct from neutral
guidance, that need must be demonstrated by real consumers and the normal
new-role evidence. This ADR does not reserve an `info-*` family.

#### Destructive actions

The current product uses restrained destructive actions: neutral resting
controls with danger text/border/surface feedback where appropriate. It does
not require a solid filled destructive button.

`danger-text` on `danger-surface` must not be repurposed as an ad hoc filled
button contract. If a real blocking confirmation later needs a solid danger
action, contrast must be measured and the roles
`danger-action`, `danger-action-hover`, and `on-danger-action` must be accepted
together through a successor decision. They are not added preventively.

#### Variants of the same hue

A numeric shade ramp such as `blue-400 / blue-500 / blue-600` is not exposed to
components. A same-hue variant is permitted only when it has a distinct
contract:

- normal versus hover/pressed interaction feedback;
- text versus surface versus border contrast for a semantic status;
- brand colour versus accessible filled-action colour;
- a measured accessibility requirement that cannot be met by an existing
  role.

“It looks better in this component” is not a role.

Adding a distinct chrome colour requires all of:

1. a semantic role not already represented;
2. named consumers;
3. contrast measurements on every intended background;
4. a gallery specimen in every relevant state;
5. a visual reference update reviewed as a design change;
6. an update to this ADR or a successor ADR when it expands a role family.

#### Content palette

Syntax highlighting is content, not editor chrome. It remains a separate,
namespaced section of `theme.rcss`.

The complete initial content role vocabulary is:

```text
code-text
code-keyword
code-string
code-comment
code-number
code-operator
code-error-bg
code-error-rule
code-bracket
code-bracket-on
code-caret-layer
```

Content colours:

- may only be used by the Script Editor's syntax and diagnostic selectors;
- must not be reused for buttons, selection, badges, or navigation;
- remain subject to the recursive literal and documentation tests;
- require a syntax specimen when changed.

Functional transparency such as the invisible textarea caret layer is also
documented in this section rather than treated as a chrome colour.

The keyword `transparent` is permitted only for genuine pass-through/reset
behaviour, such as the raylib scene beneath the RmlUi viewport. An alpha-bearing
hex value is still a colour and is legal only in a marked `theme.rcss` role.
Component-local translucent accent or status fills are forbidden.

### 3. Spacing vocabulary

The application-wide spacing vocabulary for `margin`, `padding`, `gap`,
`row-gap`, and `column-gap` is:

```text
0 · 2 · 4 · 6 · 8 · 10 · 12 · 16 · 24 dp
```

This scale is derived from dominant real usage and semantic inspection.
`10dp` is retained because it repeatedly represents the stable dense inset of
panels and controls; omitting it would repeat ADR-0027's original mistake.
Odd values such as `5dp`, `7dp`, and `9dp` are treated as historical optical
compensation unless a component-geometry contract proves otherwise. Frequency
alone does not create a token.

Rules:

- `0` is always allowed;
- `1px` is reserved for physical hairlines and is not a spacing token;
- negative spacing is forbidden unless it closes a demonstrated pointer dead
  zone or performs another necessary overlap;
- large positioning values are layout geometry, not spacing tokens;
- a value outside the scale must carry a valid declaration-local
  `ds-exception` marker;
- “pixel perfect”, “looks better”, or migration compatibility without a
  screenshot are not sufficient exception reasons.

#### Exception grammar

An exception uses this exact one-line form immediately before the one
declaration it exempts:

```rcss
.anim-timeline-order {
    /* ds-exception(spacing): aligns fixed frame-number column; ref=anim */
    padding-left: 14dp;
}
```

The grammar is:

```text
/* ds-exception(<category>): <reason>; ref=<visual-reference> */
<one declaration>
```

Allowed categories are `spacing`, `radius`, `type-size`, `type-weight`, and
`motion`. The marker:

- applies only to the immediately following declaration, ignoring whitespace;
- must match the declaration category;
- must contain a non-empty concrete reason;
- must reference `gallery`, `scene`, `logic`, `expression`, `anim`, or
  `tileset`, optionally followed by `/` and a specimen name;
- cannot exempt multiple declarations or an entire selector block;
- cannot use placeholder reasons such as `needed`, `legacy`, `looks better`,
  `pixel perfect`, or `todo`;
- fails when orphaned, duplicated for one declaration, or attached to an
  otherwise approved value.

The test guarantees structure, locality, and a visual review target. Reviewers
remain responsible for deciding whether the stated reason is valid.

Offsets such as `52dp`, `72dp`, and `78dp` must be classified during migration:

- if they represent indentation or column alignment, replace them with
  structural flex/grid layout where RmlUi supports it;
- if they are required geometry, keep them as a named component metric with a
  contract comment;
- they must not silently expand the spacing scale.

The migration is visual and incremental. It must not mechanically round all
values in one commit.

### 4. Radius vocabulary

The approved corner radii are:

```text
0 · 2 · 4 · 8 dp
```

`2dp` is the default control radius, `4dp` is for popovers/dialog groups, and
`8dp` is reserved for intentionally soft large surfaces.

Dots, circles, and pills may use a radius derived from their fixed height or a
percentage where supported. Such a value is geometry, must be colocated with
the width/height that defines it, must use a `ds-exception(radius)` marker, and
does not become a general radius token.

### 5. Typography vocabulary

Fonts remain:

- Inter for application UI;
- JetBrains Mono for code and fixed-column numeric/readout content;
- Tabler Icons for interface icons.

Approved weights are the vendored static faces:

```text
400 · 500 · 600 · 700
```

The initial type roles are:

| Role | Size | Intended use |
|---|---:|---|
| `type-eyebrow` | `10dp` | uppercase section labels and non-essential captions |
| `type-body` | `11dp` | default dense editor text |
| `type-label` | `12dp` | labels, menus, and normal controls |
| `type-control-strong` | `13dp` | prominent control labels |
| `type-heading-small` | `15dp` | panel and empty-state headings |
| `type-heading` | `18dp` | dialog or editor heading |
| `type-display` | `28dp` | rare product/empty-state display text |

`9dp`, `14dp`, and `17dp` are not new roles. Existing uses must migrate to the
nearest semantic role or document a `ds-exception(type-size)` with a visual
reason.

Relative `em` sizes are permitted only for Tabler icon glyph selectors. They
are icon geometry rather than readable-text roles and must be documented by
the icon component contract.

Line-height used to vertically centre a fixed-height RmlUi control is a
component metric, not an independent typography token. It remains equal to the
control's contracted height.

Text below `10dp` is forbidden for user-readable information. Truncation,
wrapping, long localized strings, and DPI behaviour remain part of the RmlUi
contract gate.

### 6. Motion vocabulary

The only current transition duration is:

```text
motion-fast = 0.1s
```

It remains the default for colour and border feedback. New durations require a
demonstrated interaction need and a named motion role. Decorative animation is
not introduced by this ADR.

Reduced-motion infrastructure is not added preventively while no substantial
motion exists.

### 7. Iconography contract

Tabler Icons is the sole interface icon family. Unicode punctuation and
mathematical symbols may remain text content; they must not become an
alternative icon library.

A small presentation-only `ui_icons` catalog maps semantic names such as
`Delete`, `Rename`, `Add`, `ChevronDown`, `Locked`, and `Visible` to the Tabler
character references. It is authoritative for C++-generated markup only. New
C++ markup uses the catalog instead of repeating raw PUA codepoints.

The catalog:

- contains no state or rendering logic;
- introduces no manager or runtime service;
- may return the existing character-reference strings used by RmlUi;
- centralises meaning and font coupling only;
- is verified by markup/gallery tests.

Static RML cannot consume a C++ header. It may therefore keep document-local
Tabler character references under these constraints:

- every PUA codepoint belongs to an allowlist extracted from the vendored
  Tabler font and verified by test;
- the containing element uses `.icon` or a named semantic icon class;
- a static icon-only control provides `title` text;
- contracted states appear in the gallery;
- introducing an RML preprocessor, runtime icon registry, or custom loader only
  to remove these references is explicitly out of scope.

This is a deliberate technical duplication at the static-document boundary,
not a second icon library or a second C++ glyph authority.

Every icon-only interactive control must provide an accessible textual label
through visible text or the existing RmlUi-supported tooltip/title mechanism.

### 8. Component contracts

A class is a design-system component when it is reused across documents or
when its interaction/state semantics must remain stable.

Every shared component contract states:

- accepted markup shape, including whether bare text is allowed;
- required child classes;
- normal, hover, focus, active/selected, disabled, and destructive states that
  apply;
- size and spacing metrics;
- text and icon behaviour;
- how a variant is added without changing the base contract;
- whether the component is safe in every RmlUi document or scoped to one
  editor.

Variants express meaning (`primary`, `destructive`, `selected`, `disabled`),
not location (`in-tileset`, `blue-button`) or a raw visual value.

One-off layout wrappers do not need artificial component abstractions. This
preserves Constitution AC-SIMPLE-001.

### 9. Automated enforcement

`ui_stylesheet_tokens_test` becomes recursive and scans every shipped `.rcss`
file. The existing lightweight source gates are also extended with two logical
scanners:

- a recursive static-RML inline-style scanner;
- a generated-markup scanner for `src/editor-native/ui/`.

They may live in the existing token/markup test executables. This decision does
not require another CMake target or a general C++/RCSS parser.

The RCSS scanner enforces:

1. no colour literal or named colour outside `theme.rcss`;
2. every declared theme colour belongs to a valid adjacent `ds-role` marker;
3. every `.rcss` file below the UI root is discovered;
4. spacing values are on the approved scale or have a valid `ds-exception`;
5. radii are approved or documented derived geometry;
6. font sizes and weights use the approved vocabulary or have a valid
   `ds-exception`;
7. transition durations use a documented motion role;
8. the test contains negative self-checks proving that every scanner can fail.

#### Inline-style gates

Static `.rml` files must not contain:

- an inline `style` attribute assigning `color`, `background`,
  `background-color`, `border`, `border-color`, or a side-specific border
  colour;
- an embedded style block declaring a colour;
- a literal chrome colour outside the linked theme.

C++ source under `src/editor-native/ui/` must not:

- emit a `style` attribute containing a colour-bearing property;
- pass a colour-bearing property to `Element::SetProperty`;
- set a `style` attribute containing a colour through `SetAttribute`;
- embed `#rgb`, `#rrggbb`, or `#rrggbbaa` chrome literals in generated markup;
- pass named colours as inline chrome values.

HTML/RML character references such as `&#xeb41;` are not colours and must be
excluded from the scanner. Selector-like IDs in diagnostic strings are not
style declarations and must not produce false positives.

Dynamic project colour data is not editor chrome. It may be rendered only as
escaped text or as a form-control value; it must never be interpolated into an
inline style. A static project-colour literal needed by UI presentation uses
the language-appropriate exact marker immediately before one C++ statement or
one RML element:

```cpp
// ds-content-colour(project-data): default value shown in the colour field
html += "<input value=\"#000000\"/>";
```

```rml
<!-- ds-content-colour(project-data): default value shown in the colour field -->
<input value="#000000"/>
```

The marker cannot permit a `style` attribute or `SetProperty` call. Dynamic
escaped values do not need a marker because no colour literal exists in the
source.

Both logical scanners require negative fixtures for inline RML, generated
markup strings, `SetProperty`, character-reference false positives, and
project-data values.

#### Scanner grammar

The metric scanners intentionally cover the syntax the product uses:

- spacing shorthands contain one to four tokens and every numeric token is
  checked independently;
- unitless `0` is accepted;
- `auto` is accepted only in `margin` declarations;
- `none`, `inherit`, `initial`, `unset`, percentages, and `em` are not spacing
  values and are rejected there unless a successor decision authorises a real
  use;
- border-radius shorthands contain one to four values; percentages require a
  declaration-local `ds-exception(radius)` and derived-geometry reason;
- comma-separated transition declarations are scanned for every duration;
- negative values are scanned rather than skipped;
- colour-bearing border shorthands resolve the one colour literal associated
  with their `ds-role` marker.

Layout properties such as `width`, `height`, `top`, `left`, and flex bases are
component geometry and are not spacing declarations. The test must not apply
the spacing scale to them.

Discovery must fail closed:

- at least the known shell, Sprite Animation, and Tileset stylesheets must be
  found;
- a fixture stylesheet in a nested directory must be scanned by the test;
- an unreadable stylesheet is a failure, not an empty input.

Tests inspect declarations, not narrative comment examples. They do parse only
the exact `ds-role`, `ds-role-mirror`, `ds-exception`, and
`ds-content-colour` markers defined by this ADR. Selector IDs such as
`#btn-play-project` must not be mistaken for colours.

### 10. Visual coverage

The existing gallery remains the canonical isolated specimen sheet. It is
extended when a shared component, role, or typography level is added.

The real-composition harness must cover at least:

| View | Required coverage |
|---|---|
| `gallery` | foundations, shared controls, and every contracted state |
| `scene` | shell, Hierarchy, Inspector, toolbar, tabs |
| `logic` | rule cards, inputs, toggles, menus |
| `expression` | focused completion and diagnostic states |
| `anim` | Sprite Animation standalone document |
| `tileset` | Tileset standalone document |

The existing `--shot-anim` and `--shot-tileset` paths are reused; this ADR does
not create a second screenshot framework.

Reference updates are acceptance of a visual change, not a way to make CI
green. A change description must state which roles or metrics moved and why.

### 11. Rules for new UI work

A new panel or document is complete only when:

- it imports the canonical theme and component sheets;
- it declares no local colour;
- neither its static RML nor C++-generated markup introduces inline chrome
  colour;
- it uses the spacing, radius, type, and motion vocabularies;
- it reuses a component contract where semantics match;
- any new shared contract appears in the gallery;
- a composition capture covers risks that isolated specimens cannot;
- recursive token, markup, and visual tests pass.

Copying an existing editor stylesheet and retaining its local palette is
forbidden.

## Architecture gates for this change

This ADR classifies the implementation as **RmlUi-only UI refactor**, with a
small platform-presentation mirror for the native title bar.

| Gate | Decision |
|---|---|
| Data authority | machine-readable `theme.rcss` roles for RmlUi colour; tested derived mirror for platform chrome |
| Persistent state | none |
| Workspace/local state | none |
| Intent/Command | N/A; visual styles do not mutate authoring state |
| Invariants | recursive coverage, semantic-only colour usage, finite metric vocabularies |
| Undo/Redo | unaffected |
| Dirty/revision | unaffected |
| Edit/Play | the same presentation contract applies; no runtime state is changed |
| Save/Load/migration | N/A; project format is untouched |
| Failure path | parser/load errors remain explicit; token and visual drift fail tests |
| Owner/lifetime | existing RmlUi host and static UI resources |
| Native/WASM | native RmlUi editor only; no WASM/editor-api work |
| Security | user/project text remains escaped; project colour data never becomes inline style |
| Rollback | per migration slice and per stylesheet; no data rollback required |

## Migration plan

The migration is intentionally split. A single application-wide visual rewrite
would violate the slice and review gates.

The first implementation slice changes the header field to
`Implementation: In progress`. Partial phase completion is recorded in this
section or its implementation PRs without changing `Status`.

### Phase 1 — Close the enforcement hole

1. add `ds-role` markers to every colour declaration in `theme.rcss`;
2. make stylesheet discovery recursive;
3. add nested positive and negative scanner fixtures;
4. add the RML inline-style and C++ generated-markup scanners;
5. migrate Sprite Animation colours into semantic role groups in `theme.rcss`;
6. add the `anim` visual reference;
7. keep the build green before proceeding.

Phase 1 is complete when Sprite Animation contains zero colour declarations
and the recursive test would fail if one is reintroduced.

### Phase 2 — Migrate Tileset and platform chrome

1. migrate Tileset colours without adding local shades;
2. add the `tileset` visual reference;
3. centralise the native title-bar mirror and add its equality test;
4. verify both standalone documents at supported DPI.

Phase 2 is complete when zero colour declarations exist outside `theme.rcss`
anywhere below the UI root, no static RML or generated C++ markup introduces
inline chrome colour, and every platform mirror resolves to a real `ds-role`.

### Phase 3 — Adopt the real metric vocabularies

Migrate one surface at a time:

1. shared controls and gallery;
2. shell layout and panels;
3. Logic Board and expression field;
4. Script and SFX editors;
5. Sprite Animation and Tileset.

For each surface:

- classify every off-scale value as spacing, component geometry, or dead
  compensation;
- replace it with a token or documented exception;
- capture before and after;
- review density, alignment, text clipping, small-window, and DPI behaviour;
- land the enforcement for that property only after its migration is clean.

### Phase 4 — Typography, radius, motion, and icons

1. map existing type sizes to named roles;
2. remove unreadable `9dp` UI text or justify the exceptional case;
3. normalise general radii and document derived circles/pills;
4. enforce the existing `0.1s` motion role;
5. introduce the small icon catalog and migrate repeated semantic glyphs
   incrementally;
6. verify static-RML Tabler references against the allowlist;
7. extend gallery specimens for affected contracts.

### Phase 5 — Design-system completion gate

1. run the complete build and test suite;
2. run all six visual views;
3. inspect Windows title chrome and all standalone documents;
4. reconcile `theme.rcss`, ADR-0027, and font guidance so none states a
   superseded scale;
5. change `Implementation` to `Complete` only when every required invariant is
   enforced; `Status` remains `Accepted`.

An alternate light or high-contrast theme is not required to complete these
phases. A second theme should be added only for a real product or accessibility
need, using the same semantic roles.

## Alternatives considered

### Keep the current main-shell boundary

Rejected. It labels the tested subset a design system while allowing new
subdirectories and standalone documents to escape it.

### Forbid every same-hue variant

Rejected. Hover feedback and accessible text/surface/border combinations often
need different luminance. The contract forbids arbitrary shades, not necessary
semantic variants.

### Expose a full numeric colour ramp

Rejected. `blue-400` or `zinc-700` tells a component which colour it wants, not
why. It recreates local visual choice under token-shaped names.

### Add a token compiler or third-party design-system framework

Rejected for now. RmlUi has no custom properties, the current selector-group
technique works, and an additional build-time language or framework is not
required to protect the measured invariants.

### Generate semantic icons into static RML

Rejected for now. It would remove a small technical duplication but require an
RML preprocessor, loader, or runtime registry. The verified Tabler allowlist and
semantic-class/title contract protect the real risk with less machinery.

### Put all UI styles in C++

Rejected. It couples presentation to controllers, makes visual review harder,
and violates the existing RML/RCSS presentation boundary.

### Normalise all metrics mechanically in one pass

Rejected. Spacing changes density and composition. Each surface needs a
reviewable visual slice.

## Consequences

### Positive

- “Product-wide” becomes mechanically true rather than a documentation claim.
- New UI documents cannot create a private palette.
- Components choose meaning; the theme chooses colour.
- Spacing and typography become predictable without erasing necessary compact
  editor density.
- Visual changes are reviewed in isolation and in real compositions.
- The existing RmlUi stack and screenshot harness are reused.
- The policy remains simple enough to grep, test, and explain.

### Costs

- `theme.rcss` selector groups grow when overlay selectors are migrated.
- Metric migration intentionally changes some spacing and requires visual
  review.
- Recursive checks make previously invisible debt fail until its migration
  phase is complete.
- The icon catalog adds one small presentation abstraction.
- GPU references remain platform/DPI sensitive and must keep the existing
  tolerance policy.

### Risks and mitigations

| Risk | Mitigation |
|---|---|
| Broad visual churn | one surface and one foundation per slice |
| Blind reference regeneration | require role/metric rationale in review |
| Theme selector groups become hard to navigate | role sections, stable ordering, and dead-selector checks |
| Exceptions become loopholes | structured `ds-exception` reason plus test |
| Test parser misses RCSS syntax | negative self-tests and nested fixtures |
| Compact UI becomes too spacious | scale derived from current dominant usage, including `10dp` |

## Invariants

After implementation, all of the following are release gates:

1. recursively, no shipped RCSS file except `theme.rcss` declares a colour;
2. every theme colour resolves through a valid machine-readable semantic or
   content `ds-role`;
3. static RML and generated C++ markup introduce no inline editor-chrome colour;
4. no component consumes a numeric colour ramp;
5. all spacing, radius, type, weight, and motion values are approved or carry a
   reviewable exception;
6. every shared interactive component has a written markup/state contract;
7. every shared state is present in the gallery;
8. every standalone RmlUi document has a real-composition visual reference;
9. platform colour mirrors are named, minimal, and tested against theme roles;
10. C++-generated icons use `ui_icons`, while static-RML icons pass the Tabler
    allowlist and labelling contract;
11. the checks prove they fail on deliberate nested and inline violations;
12. no implementation step changes `ProjectDocument`, Undo/Redo, dirty state,
    Save/Load, or Play semantics.

## Verification

Minimum verification for the completed ADR:

- `scripts\build.bat --test`;
- recursive colour scanner negative test in a nested directory;
- `ds-role` conflict, orphan, unknown-role, and missing-consumer negative tests;
- inline static-RML and generated-C++ colour negative tests;
- spacing/radius/type/motion negative tests;
- `ds-exception` category, adjacency, reason, and reference negative tests;
- platform mirror equality test;
- C++ icon-catalog and static-RML Tabler allowlist checks;
- `gallery`, `scene`, `logic`, `expression`, `anim`, and `tileset` visual
  comparisons;
- small-window capture;
- Windows DPI checks at 100%, 125%, and 150% for changed surfaces;
- review confirming zero RmlUi warnings and no clipped required text.

The ADR is not complete merely because the application looks consistent on one
machine. Its authority, vocabulary, component contracts, recursive enforcement,
and visual coverage must all be present.
