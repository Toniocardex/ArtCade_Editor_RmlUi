# ADR-0019 — Export Pipeline (Windows MVP)

**Status:** Accepted  
**Date:** 2026-07-24  
**Scope:** Application-layer Export (not Intent/Command), C++ `artcade-pack-core`,
precompiled Windows player templates, transactional sibling staging  
**Related:** Constitution (UI never mutates FS authority; Edit/Play isolation),
[ADR-0017](ADR-0017-coordinator-document-core-boundaries.md),
RU-04 Play materialize (`project.json` via `ProjectSerializer`)

## Context

`build_native.bat` / `build_wasm.bat` are developer toolchains. The Python
packer requires PyNaCl. The native player resolved `game.artcade` relative to
the process CWD. There was no File → Export path in the RmlUi editor.

## Decision

### Flow

```text
RmlUi → ExportRequest → ExportApplicationService
         ├── Coordinator: evaluateExportDomainEligibility (no path)
         ├── ProjectSessionController: Save and Export, projectRoot
         ├── ProjectPackService → ARTCADE1 game.artcade
         ├── RuntimeTemplateCatalog
         └── transactional commit
```

`ExportRequest` is not an `EditorIntent`. No Undo/Redo. No document mutation
from Export failure.

### Authorities

| Owner | Owns |
|---|---|
| EditorCoordinator | Domain eligibility only (not playing, start scene, shared runtime preflight) |
| ProjectSessionController | Saved path, Save and Export (no Discard), projectRoot |
| ExportApplicationService | Templates, destination, pack, staging, commit |

### Pack input

Allowlist only: synthetic `project.json` (serializer), `assets/**`, `scripts/**`,
optional `game.json`. Reserved: `manifest.json`, `project.json`. Deterministic
**entry selection and ordering** — not byte-identical archives (`created`
timestamp + random AEAD nonce).

### Template + key

Installed `resources/export-templates/windows-x64/` with `runtime-template.json`.
`assetKeyId` must match `artcadeAssetKeyId()` from the linked packer. Editor
Export always uses `PackEncryption::ReleaseEncrypted` (ARTCADE1 only).

### Player path

Default archive = `GetApplicationDirectory() / "game.artcade"`, not CWD.

### Deferred

Web (Fasi 4–5), `--validate` (Fase 3), PE metadata/signing (Fase 6), job threads.

## Consequences

- Users export without CMake/Emscripten/Python.
- Installer builds can require templates via `ARTCADE_REQUIRE_EXPORT_TEMPLATES`.
- Play and Export share `prepareRuntimeProjectSnapshot`.
