# ADR-0050 — Object Type Display Name Persistence (`name` JSON Key)

**Status:** Accepted  
**Date:** 2026-07-30  
**Scope:** Canonical project JSON load of `objectTypes[].name`, native editor
save/load round-trip, Play materialisation labels that read `EntityDef.name`.  
**Related:** `ADR-0048-object-type-names-as-entity-labels.md`,
`ADR-0049-object-types-catalog-and-instance-workspace.md`,
`ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md` §4–§6,
`ARTCADE_RMLUI_ENGINEERING_GATES.md` §4/§11–§14,
`PLAY_RUNTIME_UNIFICATION_ROADMAP.md` (RU-01 canonical `ProjectJson` load).

## Context

ADR-0048 made `EntityDef.name` the sole editable Object Type display name.
Authors rename it through `RenameObjectTypeCommand` →
`ProjectDocument::setObjectTypeName` → `EntityDef.name`. The native editor
persists that field as JSON:

```json
{ "id": "object-1", "name": "Hero Renamed", ... }
```

(`objectTypeToJson` in `src/editor-native/model/project_io.cpp`).

Current-format validation (`ProjectJson::validate_current_project_json`)
already requires a non-empty `"name"` and temporarily copies it onto a
scratch `EntityDef` while checking uniqueness. The editor fallback reader
(non-canonical path) also reads `"name"`.

After the Play/runtime unification (RU-01), projects at the current schema
version load through the **canonical** path:

```text
ProjectSerializer::deserializeCanonical
  → ProjectJson::read_object_types_map
  → ProjectJson::read_object_type
```

`read_object_type` in `vendor/artcade-runtime/src/core/entity-json.cpp`
currently does:

```cpp
out.className = typeJson.value("id", mapKey);
out.name = typeJson.value("displayName", out.className);
```

It never reads `"name"`. Renamed display names are therefore written correctly
on Save, validated on load, then **discarded** by the canonical reader, which
falls back to the stable id / `className`. Hierarchy, Inspector and derived
placement labels (`instanceDisplayName`) all show the id again after reload.
In-session rename continues to work because it never left `ProjectDocument`.

`displayName` is not an Object Type project JSON key under ADR-0048 or the
current editor writer. It is a different concept elsewhere (Logic descriptors,
recent-projects UI). Treating it as the Object Type display-name key is a
residual mismatch from an older schema assumption.

Round-trip tests that keep `name == id` (for example `"Hero"` / `"Hero"`)
cannot detect this bug. A rename where the display name differs from the
stable id must be asserted after serialize → deserialize on the canonical path.

## Decision

### Authority (unchanged)

```text
Inspector commit-type-name
  → RenameObjectTypeCommand
  → ProjectDocument::setObjectTypeName
  → EntityDef.name
  → save objectTypes[].name
  → load EntityDef.name
```

No new Command, Intent, or second display-name field. `ObjectTypeId` (map key
and JSON `"id"`) remains the stable technical identity. Rename never rewrites
ids or references (ADR-0048).

### Canonical JSON key

The Object Type display name in project JSON is **`"name"`**, matching:

- the editor writer (`objectTypeToJson`);
- current-format validation;
- ADR-0048’s `EntityDef.name` authority.

`ProjectJson::read_object_type` must load:

```cpp
out.className = typeJson.value("id", mapKey);
out.name = typeJson.value("name", out.className);
```

### Compatibility alias (read-only)

If `"name"` is absent and `"displayName"` is a non-empty string, the reader
may use `"displayName"` as a **read-only alias** so any accidental historical
payloads still load. Writers must not emit `"displayName"` for Object Types.
Validation continues to require `"name"` for current-format projects, so the
alias is not a second authoring path.

### Out of scope

- Changing Inspector or Hierarchy rename UX (already correct).
- Renaming the map key / rewriting `objectTypeId` on display rename.
- Migrating pre-current schema projects (already rejected by ADR-0048).
- Instance-specific labels (explicitly deferred by ADR-0048).

## Implementation

1. Fix `read_object_type` in `vendor/artcade-runtime/src/core/entity-json.cpp`
   to prefer `"name"`, with optional `"displayName"` fallback only when
   `"name"` is missing.
2. Add an editor-core test: `RenameObjectTypeCommand` to a display name that
   differs from the id → serialize → deserialize (canonical current format) →
   assert map key unchanged and `EntityDef.name` equals the renamed value;
   assert `instanceDisplayName` uses the restored name.
3. Do not change `RenameObjectTypeCommand`, `setObjectTypeName`, or the
   Inspector commit path.

## Invariants

- After Save/Load, `findObjectType(id)->name` equals the last successfully
  committed rename for that id.
- The objectTypes map key equals JSON `"id"` before and after rename/reload.
- Dirty/Undo behaviour of rename is unchanged (still one Command entry).
- Play and Edit both resolve labels from the loaded `EntityDef.name`.

## Alternatives rejected

1. **Write `"displayName"` from the editor to match the broken reader.**  
   Rejected: would diverge from validation (`"name"` required), ADR-0048, and
   the fallback editor reader; two keys for one authority.
2. **Bypass canonical load and keep the editor-only reader.**  
   Rejected: violates RU-01 single `ProjectJson` authority for current-format
   projects.
3. **Store the display name on each instance again.**  
   Rejected: reintroduces ADR-0048’s dual-name problem.

## Verification

- Unit: rename ≠ id → serialize → deserialize → name and id assertions above.
- Manual: rename Object Type in Inspector, Save, reopen project — Hierarchy
  catalog and instance labels show the renamed display name; technical id
  unchanged in diagnostics.
- Validator still rejects missing/empty/duplicate `"name"` values.

## Consequences

- Existing saved projects that already contain the correct `"name"` field
  start showing renamed labels after this fix with no file migration.
- Runtime Play load (same `read_object_type`) gains the same persistence
  behaviour as the editor.
- Residual `"displayName"`-only Object Type payloads, if any exist outside
  current-format validation, remain readable via the alias until retired.
