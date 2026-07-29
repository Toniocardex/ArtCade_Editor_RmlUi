# ADR-0048 — Object Type Names as the Sole Entity Display Name

**Status:** Accepted  
**Date:** 2026-07-29  
**Scope:** Object Type naming, Scene Instance presentation, Inspector,
Hierarchy, Scene View labels, Logic Board orientation, project persistence and
migration.  
**Related:** `ADR-0023-hierarchy-instance-ux.md`,
`ADR-0045-scene-logic-scope.md`,
`ADR-0047-scene-logic-board-discoverability.md`,
`ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md` §4–§6/§12–§13,
`ARTCADE_RMLUI_ENGINEERING_GATES.md` §4/§11–§14/§19/§23.

## Context

The current alpha authoring model persists two user-facing names for a placed
entity:

```text
SceneInstanceDef.instanceName  — one name for one placement
EntityDef.name                 — the display name of the shared Object Type
```

The Object Type already owns the components, Logic Board, scripts and default
variables that define behaviour. `SceneInstanceDef` owns only placement,
transform, visibility, layer and explicit instance overrides. The Logic Board
therefore correctly identifies an Object Type, while the Inspector and
Hierarchy currently foreground a separate instance name. This asks authors to
rename two independent fields merely to keep the Scene and Logic Board easy to
recognise.

Repository inspection confirms that `instanceName` is presentation-only: it is
used by the Inspector, Hierarchy, Scene View labels, Tilemap context and user
messages; it is not a Logic Board owner, an Object Type key, a runtime
behavioural input, or a stable identity. It is nonetheless persisted in the
v11 project format and passed through creation, duplication and rename
commands. Removing it is consequently a project-format and migration decision,
not a cosmetic Inspector-only change.

## Decision

### One editable authoring name

`EntityDef.name` is the sole editable, user-facing name for an Object Type.
The Inspector shows one editable row labelled **Object Type**. Editing it
renames that shared display name through the existing
`RenameObjectTypeCommand`; every instance using the type, in every scene,
updates through normal document invalidation.

`ObjectTypeId` remains a stable technical identifier and map key. Renaming the
display name never changes it, never rewrites references, and never changes a
Logic Board identity. It can be shown as secondary read-only diagnostic text,
but is not a second editable name.

`SceneInstanceDef::instanceName` is removed from the authoring model and from
the current-format project JSON. `RenameEntityCommand`, `setInstanceName`,
Hierarchy F2/double-click instance renaming, and create/duplicate parameters
whose only purpose is an instance name are removed with it.

This decision does **not** introduce instance-specific Logic. Logic remains
owned by the Object Type as defined by ADR-0045. An optional future instance
label, if a real use case appears, needs its own ADR and must not be silently
reintroduced as a second mandatory name.

### Derived placement labels

All views that need to distinguish placements use one central read-only query
owned by the existing document/read-model boundary, not independently copied
formatting in panels:

```text
base = resolved Object Type display name
rank = 1 + count of same-type instances in the same Scene with a lower EntityId
label = base + " · " + rank
```

For example, two placements of Object Type `Coin` are displayed as `Coin · 1`
and `Coin · 2`. The rank is scene-local and ordered by stable `EntityId`, so it
does not change when a layer is renamed, hidden or reordered. It is derived
only; no counter, generated label or cache is persisted.

The same query is used by Hierarchy rows, Inspector heading/breadcrumb, Scene
View labels, Tilemap editing context, selection messages and diagnostics. The
Hierarchy still searches the derived label, Object Type display name,
`ObjectTypeId`, layer and `EntityId`.

### UI contract

- The Inspector replaces the `Identity` section with **Entity** and exposes
  `Object Type` as its one naming field. It keeps instance-owned `Visible` and
  `Layer` rows in that section.
- The Hierarchy shows the derived placement label and no instance rename entry
  point. Its tooltip may show the stable instance ID and Object Type ID for
  diagnostics.
- The Logic Board header and target picker continue to show the Object Type
  display name. Because that is now the same name that anchors the Scene,
  authors rename it once rather than maintaining a parallel LB-facing label.
- The UI states clearly, adjacent to an editable Object Type name where space
  permits, that the edit affects every instance using the type. It does not
  offer a hidden per-instance rename fallback.

### Authority, mutation and Play

`ProjectDocument` remains the sole persistent authority. The derived label is
a read-only projection. RmlUi may hold a transient edit buffer for the Object
Type input but never owns a name or computes an authoritative rank.

`RenameObjectTypeCommand` remains the single authoring mutation. It validates
the non-empty, unique Object Type display-name policy in the core, is atomic,
is undoable/redoable, updates revision/dirty state, and is rejected during
Play. Undo restores the single Object Type name and therefore all derived
placement labels. Workspace selection and the displayed rank are not
persisted and do not create history entries.

No runtime format, runtime state, materialization rule or generated Logic
program gains a dependency on labels. Play continues to materialize from
Object Type IDs and instance IDs only.

### Persistence and migration

The project schema advances from v11 to v12. ArtCade is still alpha and has no
released project corpus, so this is a clean breaking format change rather than
a compatibility feature.

- A v12 writer omits `scenes[].instances[].instanceName`.
- The v12 reader and validator reject the obsolete member and pre-v12 schema
  versions. There is no v11 import path, migration warning, backup sidecar or
  hidden legacy-name map.
- Every v12 Scene Instance must resolve to a non-empty, existing Object Type.
  Catalog-less instances and an `objectTypeId` display fallback are not part of
  the v12 authoring contract.
- In-repository fixtures and test documents are updated directly to the v12
  shape as part of implementation; they are not treated as user migration
  inputs.

The native editor serializer and the canonical local runtime scene JSON reader
are updated together so saved projects and export/runtime parsing agree on the
single v12 shape.

## Invariants

- `EntityId` is the stable identity of a placement; it is never derived from a
  display name and remains valid after Object Type rename, Undo/Redo and load.
- `ObjectTypeId` is the stable identity of a shared Object Type; its display
  name is mutable without rewriting references.
- There is exactly one editable user-facing name for a normal placement: the
  resolved Object Type display name.
- A derived placement label is never serialized, used as a Logic target, or
  used to resolve a reference.
- A single display-name algorithm is used for every affected projection.
- Every persisted Scene Instance resolves to an existing Object Type; the UI
  never manufactures a fallback label or a replacement Object Type.

## Alternatives rejected

- **Keep both fields and document their different meanings:** preserves the
  current cognitive and maintenance cost without a behavioural benefit in the
  present product.
- **Synchronize instance name and Object Type name:** fails as soon as one
  Object Type has multiple placements and creates a hidden two-way sync.
- **Rename `ObjectTypeId` whenever the user renames the type:** rewrites
  references across scenes, Logic Boards and assets, making a display-name
  edit a risky identity migration.
- **Persist an automatic counter or generated instance label:** duplicates a
  value deterministically derivable from the Scene instances and `EntityId`.
- **Preserve or migrate legacy instance names:** adds compatibility, warnings
  and a second representation with no alpha-user benefit; no released project
  corpus requires it.
- **Introduce Instance Logic to justify instance naming:** changes runtime
  ownership and is unrelated to this UX simplification.

## Consequences

- Creating or duplicating an instance becomes simpler: it supplies type,
  position and layer, not a second name.
- Renaming an Object Type has a visibly broader effect and must invalidate all
  affected hierarchy, inspector, Scene View and Logic Board projections.
- v12 accepts one clean project shape only. Internal fixtures are updated
  directly; no compatibility branch or legacy storage remains to maintain.
- Existing tests and fixtures that construct `SceneInstanceDef` must use the
  derived-label helper in their presentation expectations rather than assign
  `instanceName`.

## Verification

- Object Type rename updates the Inspector, all affected Hierarchy rows,
  Scene View labels, Tilemap context and Logic Board header/picker without
  changing `ObjectTypeId`, `EntityId`, transform, layer, overrides or Logic
  Board content.
- Derived labels are deterministic for duplicate, delete, Undo/Redo, layer
  reorder and scene switch; same-type ranks are scoped to one Scene and sorted
  by `EntityId`.
- The one editable Inspector field commits through
  `RenameObjectTypeCommand`; invalid, duplicate and no-op values have the
  defined Command/dirty/Undo behaviour. Instance rename actions are absent.
- Save/load round-trips v12 projects without `instanceName`; pre-v12 schemas,
  catalog-less instances and a v12 `instanceName` member are rejected.
- Canonical runtime parsing accepts the v12 shape. Play behaviour and generated
  Logic output are unchanged by a rename or migration.
- RmlUi contract tests cover the removed controls, the shared-name edit buffer,
  focus/blur/Escape policy and long derived labels.
