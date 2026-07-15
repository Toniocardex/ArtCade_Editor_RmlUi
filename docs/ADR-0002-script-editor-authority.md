# ADR-0002 — Script Editor authority and runtime boundary

**Status:** Accepted  
**Date:** 2026-07-15  
**Scope:** native RmlUi editor and shared C++ runtime model

## Context

ArtCade needs a first-class Script workspace without turning RmlUi, the project
JSON, the process working directory, or the Play runtime into parallel stores of
Lua source. The existing `mainScriptPath` is a legacy runtime bootstrap field;
it cannot represent a catalog of editable source assets or type-owned script
attachments.

## Decision

The Script Editor uses four deliberately separate authorities:

| Concern | Authority | Lifetime |
|---|---|---|
| Script identity, display name and relative source path | `ProjectDocument::scriptAssets` | project session |
| Saved Lua source | `.lua` file below the project root | filesystem |
| Unsaved text, cursor, scroll and local text history | `ScriptEditorBuffer` in `EditorState` | editor workspace |
| Executing code | immutable `ScriptProgram` snapshot | one Play session |

Consequences:

- persistent metadata mutations use `EditorCommand` and project Undo/Redo;
- character editing never creates an `EditorCommand`;
- file writes go only through `ProjectScriptFileService`, are UTF-8, normalized
  to LF, project-root confined and atomically replaced;
- script references use `AssetId`, never display name or path;
- `ScriptComponent` is type-owned, ordered and has stable attachment IDs;
- Logic Board programs and manual scripts remain separate Lua programs and
  separate scopes;
- Start Play will eventually materialize only saved source; it will never read
  a live editor buffer or query `ProjectDocument` per frame;
- automatic hot reload is not part of the initial implementation.

`CenterWorkspaceMode::Script` is a first-class workspace mode, but not a second
persistent project authority. RmlUi remains presentation only.

## Invariants

- `ScriptAssetDef::assetId` is non-empty and unique.
- `sourcePath` is a safe project-relative `.lua` path without traversal.
- no two script assets own the same normalized source path.
- every attachment ID is non-empty and unique within its Object Type.
- every attachment references an existing script asset.
- instance-owned script overrides are not representable.
- removing a referenced script asset is rejected atomically.
- a syntactically incomplete buffer may be saved; Play/export validation is a
  later strict boundary and must fail atomically.

## Intent and Command boundary

UI actions emit semantic intents. The Coordinator resolves project/workspace
context and dispatches one typed Command for metadata or attachment mutations.
The script file service never mutates `ProjectDocument`; create/import workflows
roll back a newly written file if the metadata Command fails.

## Undo and unsaved changes

Project Undo/Redo covers metadata and attachments. Script-buffer Undo/Redo covers
text only. Routing depends on code-editor focus. New/Open/Exit guard both the
project dirty flag and dirty script buffers; Save All must succeed completely
before a destructive action may continue.

## Play behaviour

Script 1 and Script 2 do not execute scripts. Play started from Script follows
the established authoring-workspace navigation policy: it opens Scene for the
runtime view and Stop returns to the same Script workspace/buffer state. The
later runtime slice will snapshot saved sources at Start, isolate runtime state
from authoring and dispose all scopes at Stop.

## Alternatives rejected

- Store Lua source inside project JSON: mixes metadata and text-file history.
- Edit `main.lua`/`main.luac`: conflates manual source with generated bootstrap.
- Append Logic Board output to manual scripts: destroys ownership and diagnostics.
- Use RmlUi form values as buffers: creates a UI authority and loses state on refresh.
- Automatic hot reload in the MVP: introduces scope/subscription lifetime before
  the base runtime is proven.

## Required verification

- serializer/migration round-trip and deterministic output;
- duplicate IDs/paths, invalid extensions and traversal rejected;
- symlink/junction escape rejected by the file service;
- atomic write preserves the previous file on failure;
- metadata Command apply/Undo/Redo and delete-reference guard;
- buffer dirty/save/undo routing and unsaved guard;
- RmlUi controller contract without rebuilding the textarea per keystroke;
- later runtime parity on native and WASM targets.
