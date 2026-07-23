# Component Alignment and Legacy Retirement Roadmap

**Status:** proposed architecture and delivery plan
**Scope:** native RmlUi editor and its `vendor/artcade-runtime` dependency
**Does not authorize:** React, WebView, Tauri, WASM-editor bridge, parallel
authoring state, compatibility synchronisation, or mechanical feature ports.

## 1. Decision

A runtime component is not an editor feature merely because its C++ type exists
or the runtime ECS can instantiate it. It becomes an authorable component only
when its complete contract is implemented in the native architecture:

```text
ProjectDocument / ProjectDoc
  -> explicit schema + validation
  -> Intent / Command + Undo / Redo
  -> targeted DomainChange and RmlUi Inspector projection
  -> one-time PlaySession materialisation
  -> isolated runtime behaviour
```

The native editor is the product. Legacy React-era code is either deleted,
replaced by a native vertical slice, or deliberately not carried forward. It is
never kept as a second authority, bridge, fallback, or temporary synchronisation
path.

## 2. Binding architectural constraints

This roadmap follows, in precedence order:

1. `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`;
2. `ARTCADE_RMLUI_ARCHITECTURE.md`;
3. `ARTCADE_RMLUI_ENGINEERING_GATES.md`;
4. `RMLUI_MIGRATION_CONTRACT.md`, only as historical porting context.

The following rules are non-negotiable for every component slice:

- `ProjectDocument`/`ProjectDoc` owns persistent authoring data.
- `EditorState` owns semantic workspace state; `EditorUiState` and RmlUi own
  only visual or temporary input state.
- Every persistent change goes through an atomic Command. A failed or no-op
  change creates neither an Undo entry nor a dirty revision.
- Play materialises independent state once. `PlaySession` neither holds a
  mutable document reference nor writes runtime changes back on Stop.
- Persistence changes use an explicit, deterministic, tested migration. An
  unsupported future schema is rejected, and a failed load leaves the open
  project unchanged.
- No component introduces a new manager, generic registry, target, or adapter
  unless it protects a concrete authority, invariant, dependency, or lifetime.

## 3. Baseline inventory

### 3.1 Already authorable in the native editor

The current Inspector can author Sprite Renderer, Sprite Animator, Box Collider
2D, Top Down Controller, Platformer Controller, Linear Mover, Tilemap, and
Script attachments. They are the reference shape for a complete editor feature;
their existing implementation is not automatically the right model for every
new component.

### 3.2 Runtime capabilities not yet authorable

The runtime `EntityDef` and entity gateway declare/support the following
capabilities, but the native editor does not yet provide their full authoring
contract:

| Capability | Initial disposition | Dependency / decision needed |
|---|---|---|
| `CollisionBody` / `Physics` | **Derived runtime only** (ADR-0014) | Not a second authoring authority beside `BoxCollider2D`. |
| `CameraTarget` | **Authorable** | Instance-owned, one target per scene; see `ADR-0003-camera-target-authority.md`. |
| `AutoDestroy` | **Authorable** | Object Type-owned lifetime; finite, non-negative seconds; `0` disables expiry; runtime elapsed time is never persisted. |
| `Text` | Candidate after rendering audit | Font asset reference, render contract, binding/value formatting. |
| `Gauge` | Candidate after rendering audit | Value binding, ranges, rendering and screen/world-space contract. |
| `MagneticItem` | Candidate after behaviour audit | Verify the runtime system that consumes it and its target/tag semantics. |
| `HordeMember` | Candidate after behaviour audit | Verify steering/runtime support and deterministic behaviour. |
| `Dialog` | Deferred | Requires a first-class dialog asset/document model and reference policy. |

No row in this table authorizes direct JSON editing or an Inspector toggle. Until
the corresponding slice closes, these fields are not native editor data and must
not be silently accepted then discarded on save.

### 3.2.1 AutoDestroy slice record — complete

- **Authority:** Object Type. Every instance materialized in Play inherits the
  authored component; there is no instance override.
- **Intent / Commands:** `AddAutoDestroy`, `RemoveAutoDestroy`, and
  `SetAutoDestroyLifespan`. Each emits a targeted Object Type component
  `DomainChange`, invalidates the Inspector only, and supports exact Undo/Redo.
- **Invariant:** `lifespan` is a finite value in seconds and is `>= 0`.
  `0` deliberately disables automatic expiry. Add starts at the safe authored
  default of one second.
- **Persistence:** `objectTypes[].autoDestroy = { lifespan }`. The runtime
  counter `_timeAlive` is Play-owned and is neither saved nor restored. Invalid
  values are rejected on both canonical and legacy-compatible load paths.
- **Play:** materialisation creates independent runtime state with a fresh
  elapsed timer. Runtime expiry removes the Play entity only; Stop leaves the
  document and its revision unchanged.
- **Proof:** core tests cover commands, no-op/invalid mutation rejection,
  Undo/Redo, serializer round-trip, malformed input, and isolated Play expiry;
  the native RmlUi editor target compiles with the Inspector projection.

### 3.2.2 CameraTarget slice record — complete

- **Authority:** a `SceneInstanceDef`, never its Object Type. A scene has at
  most one target.
- **Intent / Commands:** `AddCameraTarget`, `RemoveCameraTarget`,
  `SetCameraTargetOffset`, and `SetCameraTargetFollowSpeed`; each is targeted
  by `(SceneId, EntityId)`, has exact Undo/Redo, and emits a component
  `DomainChange`.
- **Invariant:** offsets are finite; follow speed is finite and non-negative;
  `0` snaps.
- **Persistence:** `scenes[].instances[].cameraTarget`. Current and
  compatibility loaders reject both duplicate targets and a legacy type-level
  `EntityDef.cameraTarget` rather than guessing a migration.
- **Play:** materialisation takes the instance component. The runtime camera
  state is presentation-independent, so editor Play follows it without writing
  the editor viewport state or the authoring document.
- **Proof:** core tests cover commands, uniqueness, invalid values, Undo/Redo,
  round-trip and Play camera position; the RmlUi executable compiles.

### 3.3 Collision authority — closed by ADR-0014

`BoxCollider2D` is the **sole persistent collision authoring component**.
`CollisionBody` is a richer **session-local** runtime representation
(shapes, layers/profiles, response rules). They must not coexist as two
editable authorities.

**Decision (Accepted):**
[`ADR-0014-boxcollider2d-collision-authority.md`](ADR-0014-boxcollider2d-collision-authority.md)
chooses option 2 — retain `BoxCollider2D` as the limited authoring profile
that materialises deterministically into a derived `CollisionBody` on the
shared runtime spawn/load path (Editor Play, `game.exe`, WASM, tests).
Authoring `EntityDef.collisionBody` is retired from the current project form
(JSON key ignored on load; writers never emit it). Session-local
`CollisionBody` remains the derived runtime projection from `boxCollider2D`.
Legacy multi-shape / circle / layer profiles are technical debt — no silent
crush into BoxCollider2D.

A future ADR may still choose full `CollisionBody` authoring (former
option 1), but only via an explicit migration **from** `BoxCollider2D`,
never by dual-write or dual Inspector surfaces.

## 4. Required design record for each component

Before a component receives code, its slice specification records:

1. **Authority and ownership** — project, Object Type, instance, or asset;
   inherited/override semantics if applicable.
2. **Data contract** — field meanings, defaults, finite/range constraints,
   identifiers, missing-reference policy, and limits.
3. **Intent and Command** — named user intents and atomic Commands, including
   exact Undo data and `DomainChange` scope.
4. **Persistence** — JSON shape, schema version, migration from every
   supported version, rejection behaviour, and save/load tests.
5. **Inspector projection** — read-only values, editable buffers, disablement
   during Play, selection/replace behaviour, and RmlUi listener lifetime.
6. **Play contract** — independent materialisation, runtime initialization,
   error reporting, reset semantics, Stop teardown, and proof that authoring
   remains unchanged.
7. **Dependencies** — assets, Logic/Lua API, rendering, physics, events, or
   system modules; each dependency must already exist or have its own prior
   slice.
8. **Tests** — command, invariant, serializer/migration, materialisation,
   Play/Stop, UI-contract and regression tests proportionate to the risk.

## 5. Legacy retirement protocol

Legacy is classified by outcome, not ported by default.

| Outcome | Meaning |
|---|---|
| **Delete** | Remove obsolete React/WebView/WASM bridge code, compatibility wrappers, duplicate stores, polling, and dead entry points. |
| **Rewrite** | Rebuild a useful capability through the native component contract in section 4. Do not reuse its legacy state flow. |
| **Do not carry** | Remove or leave unimplemented a feature that has no current product need or violates the architecture. |
| **Historical only** | Retain a document only when it is clearly non-authoritative and useful for provenance; otherwise archive or delete it. |

### 5.1 Baseline audit result

At the time of this roadmap, the native C++ product contains no operational
React, TypeScript UI, WebView, Tauri, or `editor-api` bridge dependency. Existing
mentions are guardrails or historical documentation. In particular,
`RMLUI_NATIVE_EDITOR_PLAN.md` and `ARTCADE_RMLUI_CLAUDE_REFACTOR_PROMPT.md` need
an explicit archival/rewrite decision before they can be cited as active plans.

The audit is repeated for every slice that touches a former migration area. A
new direct include, link dependency, or live bridge path is a stop-the-line
architecture violation.

### 5.2 Retirement acceptance criteria

When a legacy path is retired:

- its replacement has completed the relevant component slice, or the product
  explicitly decided not to retain the feature;
- call sites, build links, tests, scripts and active documentation no longer
  advertise the path as supported;
- no compatibility adapter or periodic synchronisation remains;
- deletion includes targeted regression coverage for the native path;
- the full diff shows no unrelated porting or refactoring hidden in the slice.

## 6. Delivery order

### Phase 0 — audit and decisions

- Build the component matrix from section 3 with exact runtime consumers,
  serializer coverage, render support, Logic/Lua exposure, and missing tests.
- Classify every React-era implementation/document as Delete, Rewrite, Do not
  carry, or Historical only.
- Write the collision ADR before adding any collision UI or data field.
- Establish the explicit schema migration policy for every component family.

### Phase 1 — low-dependency authoring foundation

`AutoDestroy` and `CameraTarget` are complete as independent vertical slices.
Do not batch future components into a generic
"component framework"; shared helpers are permitted only after a concrete
duplication is demonstrated.

### Phase 2 — collision model

Implement the ADR decision, its migration, the editor overlay and Play
materialisation. Delete the superseded collision path in the same programme;
do not leave two authoring representations indefinitely.

### Phase 3 — presentation components

Implement `Text` and `Gauge` only after the rendering audit establishes asset
resolution, missing-Font behaviour, safe value binding and world/screen-space
semantics.

### Phase 4 — behaviour and interaction components

Implement `MagneticItem` and `HordeMember` only where their runtime systems are
real, deterministic, and independently testable. Implement `Dialog` only after
the dialog document/asset model and its reference/deletion policy are approved.

### Phase 5 — retirement closure

Remove obsolete migration material and dead code identified in Phase 0; retain
only concise historical records that cannot be mistaken for a current contract.

## 7. Slice definition of done

A component slice is complete only when all applicable checks pass:

- one persistent authority and no mutable UI/runtime mirror;
- core validation prevents invalid state independently of the UI;
- atomic Commands have exact Undo/Redo and correct revision/dirty behaviour;
- save/load and explicit schema migration are tested, including malformed and
  future-version input;
- Project replace, selection changes, pending input, and asset deletion have
  defined outcomes;
- Play materialises once, works without RmlUi, and does not modify authoring;
- Start failure remains in Edit, Stop frees session-owned state safely;
- Inspector uses targeted invalidation and has no listener/lifetime leak;
- targeted tests and `scripts\\build.bat --test` pass;
- the legacy audit and documentation status are updated.

## 8. Out of scope

This roadmap does not authorize a generic ECS editor, a plugin system, a new UI
stack, a bidirectional runtime-sync mechanism, broad runtime refactoring, or
automatic revival of every component declared in historical code. Such work
requires a separate problem statement and, where applicable, an ADR.
