# ArtCade SFX Synthesizer v2 — roadmap nativa

**Stato:** completata

## Contratto architetturale

- **Autorità:** `ProjectDoc.generatedSfx` possiede le recipe persistenti. Il normale
  catalogo `AudioAssetDef` possiede gli output runtime; il legame usa
  `GeneratedSfxDef.outputAssetId`. Il nome display di un output ancora collegato
  è sempre `GeneratedSfxDef.name` (via `resolveAudioAssetDisplayName`);
  `AudioAssetDef.name` resta vuoto finché la recipe non viene rimossa (handoff
  ownership). Lo stato Ready/Stale è derivato da `generatedRecipeFingerprint`.
- **Intent/Command:** create, rename, update, delete e completamento della
  generazione passano da Command. Apertura editor, preset non applicato, preview
  e Stop sono stato workspace e non producono dirty.
- **Invarianti:** versioni supportate, sample rate, durata/frame massimi, ADSR,
  frequenze/Nyquist, gain, duty, bit crusher, filtri, ID e path sono rivalidati
  nel core. Nessun buffer parziale e nessuna sovrascrittura implicita.
- **Undo/Redo:** ogni Command ripristina esattamente recipe e collegamenti
  persistenti. I file derivati non sono l'autorità e non vengono simulati dentro
  lo stack Undo.
- **Play:** `PlaySession` e Logic Board consumano esclusivamente
  `AudioAssetDef`; il dominio recipe non viene materializzato nel gameplay.
- **Thread/lifetime:** DSP e encoding ricevono snapshot immutabili. Il risultato
  rientra con ID e revisione di origine; una recipe cambiata rende obsoleto il
  completamento. Le risorse Raylib restano sul thread proprietario.
- **Failure path:** errori strutturati arrivano alla Console; un fallimento non
  muta documento o catalogo e non lascia output parziali.

## Slice

| Slice | Stato | Definition of Done |
|---|---|---|
| SFX-1. Core DSP | Completata | Synth deterministico, preset, WAV atomico e 12/12 test forniti superati. |
| SFX-2. Dominio authoring | Completata | Schema v5, codec, validator, Command e Undo/Redo con stale guard. |
| SFX-3. Editor RmlUi | Completata | Catalogo Generated SFX, editor completo e preset senza autorità parallela. |
| SFX-4. Preview e Generate | Completata | Job su snapshot, preview owner-thread e WAV atomico registrato come `AudioAssetDef`. |
| SFX-5. Gate finali | Completata | Build nativa, lifecycle smoke, 3914 core + 138 Logic + 12 SFX verdi. |

## Evidenze finali

- `scripts\\build.bat --test`: build Release completata.
- `editor_core_test`: 3914 passati, 0 falliti.
- `logic_board_editor_test`: 138 passati, 0 falliti.
- `sfx_synthesizer_test`: 12/12 passati.
- `artcade-editor-native.exe --lifecycle-smoke`: binding RmlUi e teardown passati.
- Smoke visuale a 1366×768: editor Generated SFX renderizzato con form scrollabile.

## Note di integrazione

Il riferimento alla React UI nel pacchetto originale è sostituito dal prodotto
RmlUi nativo, in accordo con `AGENTS.md`. Non vengono introdotti React, Tauri,
bridge WASM, FFmpeg o polling del filesystem. Ogg Vorbis resta un adapter
opzionale finché le librerie Xiph e le relative notice non sono distribuite.

## Workspace multi-asset (Slice 1)

- Browser persistente a sinistra: ricerca, + New from Preset, Duplicate, Rename
  (focus Name), Delete; selezione = `openGeneratedSfxId` (workspace only).
- `DuplicateGeneratedSfxCommand` copia solo recipe + nome univoco; mai
  `outputAssetId` / WAV.
- Output ancora collegato non compare come secondo asset sotto Audio.
- Stato derivato: Needs generation / Stale / Ready via
  `generatedRecipeFingerprint` (modificare la recipe non scollega l'output).
- Generate New: nuovo WAV + nuovo `AudioAssetDef` (`…-0001`, `…-0002`, …);
  Command `CreateGeneratedSfxOutputCommand` (create-only); il precedente output
  resta Audio indipendente. Path derivato dall'asset id, non dal display name.
- `RegisterGeneratedSfxOutputCommand` resta per un eventuale Regenerate Current.
- Slice 2: Regenerate All Stale (coda seriale workspace-only; ogni item crea
  un nuovo output se quello precedente esiste già).
- Prossima: Variation Sets (Slice 3).
