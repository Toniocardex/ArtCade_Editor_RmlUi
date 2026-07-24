# ADR-0023 — Hierarchy Instance UX & Duplicate Naming

**Status:** Accepted  
**Date:** 2026-07-24  
**Scope:** native RmlUi Hierarchy presentation; Duplicate Instance command;
instance display naming; search / layer chrome  
**Related:** Constitution (ProjectDocument authority; UI presentation-only),
[Engineering Gates](ARTCADE_RMLUI_ENGINEERING_GATES.md)

## Decision

- Persistent authority unchanged: `SceneInstanceDef` fields only. No clone
  provenance fields.
- User-facing verb: **Duplicate Instance** (not Clone). Create Instance of Type
  remains a separate path.
- Auto names: `Name`, then `Name (2)`, `Name (3)`, … parsing only terminal
  `" (" + integer >= 2 + ")"`. Shared by Duplicate and Create Instance of Type.
- Duplicate inserts at `sourceIndex + 1` via `insertInstance`, snapshot on first
  apply for deterministic Undo/Redo.
- Hierarchy rows: instance name primary; raw `objectTypeId` only in tooltip;
  override dot + shared-type glyph when relevant; layer count + local collapse.
- Search matches instance name, type display name, type id, layer name, entity id
  (case-insensitive).
- Rename: F2 / menu / double-click; draft panel-local; Rename no-op when unchanged.

## Out of scope

Drag reorder, multi-select, advanced search syntax, persistent clone lineage,
schema changes, runtime-cpp.
