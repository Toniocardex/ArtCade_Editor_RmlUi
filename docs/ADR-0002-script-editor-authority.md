# ADR-0002 — Script Editor authority and runtime boundary

**Status:** Accepted  
**Date:** 2026-07-15  
**Updated:** 2026-07-19 — three-surface product model; see also
`SCRIPT_EDITOR_ARCHITECTURE.md`  
**Scope:** native RmlUi editor and shared C++ runtime model

## Context

ArtCade needs a first-class Script workspace without turning RmlUi, the project
JSON, the process working directory, or the Play runtime into parallel stores of
Lua source. The existing `mainScriptPath` is a legacy runtime bootstrap field;
it cannot represent a catalog of editable source assets or type-owned script
attachments.

Scripts are the **advanced** authoring surface for the same gameplay runtime as
the Logic Board. They are not an editor for Logic-generated Lua.

## Decision

### Three product surfaces

| Surface | Authority role | Editable |
|---|---|---|
| Logic Board | Visual gameplay | Yes |
| Generated Lua | Projection of Logic Board | **No** (inspect/copy only) |
| Script Editor | Manual Lua on the same host | Yes |

Logic Board and Manual Scripts both talk to `IGameplayRuntimeHost` /
`PlaySession`. They must not rewrite each other. Bidirectional Logic ⇄ free Lua
sync is rejected.

### Four technical authorities

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
- within each lifecycle phase, generated Logic runs before manual Scripts
  (attachment order); later Script writes to the same property win predictably;
- Start Play materializes only saved source into immutable `ScriptProgram`
  snapshots; it never reads a live editor buffer or queries `ProjectDocument`
  per frame;
- automatic hot reload is not part of the initial implementation;
- `ICodeEditorSurface` is presentation/input only — never a second text
  authority.

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
  strict boundary and fails atomically.
- syntax diagnostics are derived from one exact buffer revision and are
  discarded when that revision changes; loading a chunk for diagnostics never
  executes it or opens Lua libraries.

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

Play started from Script follows the established authoring-workspace navigation
policy: it opens Scene for the runtime view and Stop returns to the same Script
workspace/buffer state. At Start, the application reads each linked saved source
once and passes those exact bytes to the shared runtime. One isolated VM is
created per entity/attachment, generated Logic callbacks run before manual
scripts, and Stop disposes every scope without changing authoring state.

Gameplay input and collision callbacks consume immutable projections produced
by the existing runtime input/collision authorities. Generated Logic consumes a
projection before manual attachments; attachments retain persisted order, and
entity destruction remains deferred until the whole dispatch phase completes.
Both languages mutate runtime state only through `IGameplayRuntimeHost`, so the
Editor Play adapter and standalone adapter implement one shared semantic
contract rather than parallel script APIs.

Saving source while Play is active never mutates or hot-reloads the current
runtime. The Coordinator compares content fingerprints against the exact source
snapshot materialized at Start and exposes one derived restart-required state
only for linked assets. `Restart & Apply` first reads and validates all enabled
saved sources at the application boundary, then materializes a replacement
session while the old one remains alive. Only a complete replacement is swapped
in. Current Scene restarts remain pinned to their original Scene ID; restarting
from Script re-arms the established Scene preview → Stop → Script navigation
without replacing editor buffers or their cursor/scroll state.

## Alternatives rejected

- Store Lua source inside project JSON: mixes metadata and text-file history.
- Edit `main.lua`/`main.luac`: conflates manual source with generated bootstrap.
- Append Logic Board output to manual scripts: destroys ownership and diagnostics.
- Make Generated Lua editable or reverse-sync it into the Logic Board.
- Bidirectional Logic Board ⇄ free Lua authoring.
- Use RmlUi form values as buffers: creates a UI authority and loses state on refresh.
- Automatic hot reload in the MVP: introduces scope/subscription lifetime before
  the base runtime is proven.
- Host Scintilla as a Win32 HWND over Raylib/RmlUi in the MVP (embedding/z-order
  conflict); prefer an in-process surface behind `ICodeEditorSurface`.

## Required verification

- serializer/migration round-trip and deterministic output;
- duplicate IDs/paths, invalid extensions and traversal rejected;
- symlink/junction escape rejected by the file service;
- atomic write preserves the previous file on failure;
- metadata Command apply/Undo/Redo and delete-reference guard;
- buffer dirty/save/undo routing and unsaved guard;
- RmlUi controller contract without rebuilding the textarea per keystroke;
- compile-only Lua 5.4 diagnostics, stale-revision rejection and strict
  saved-source validation over the authored reference set;
- shared `script-core`/`script-runtime` lifecycle, sandbox and gameplay host in
  native Editor Play and standalone export targets.
