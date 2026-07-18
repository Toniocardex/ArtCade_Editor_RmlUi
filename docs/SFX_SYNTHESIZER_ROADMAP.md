# ArtCade SFX Synthesizer v2 — roadmap nativa

**Stato:** completata (contratto 1:1 + Create from Current)

## Contratto architetturale

- **Autorità:** `ProjectDoc.generatedSfx` possiede le recipe persistenti. Il normale
  catalogo `AudioAssetDef` possiede gli output runtime; il legame usa
  `GeneratedSfxDef.outputAssetId`. Il nome display di un output ancora collegato
  è sempre `GeneratedSfxDef.name` (via `resolveAudioAssetDisplayName`);
  `AudioAssetDef.name` porta il default (mirror) e diventa autorità solo al detach.
  Lo stato Ready/Stale è derivato da `generatedRecipeFingerprint`.
- **Identità canonica:** `generated-audio-{sfxId}` →
  `assets/audio/generated/generated-audio-{sfxId}.wav`.
  `RegisterGeneratedSfxOutputCommand` rifiuta id/path non canonici.
- **Intent/Command:** create, rename, update, delete, duplicate e completamento
  della generazione passano da Command. Apertura editor, preset non applicato,
  preview e Stop sono stato workspace e non producono dirty.
- **Invarianti:** versioni supportate, sample rate, durata/frame massimi, ADSR,
  frequenze/Nyquist, gain, duty, bit crusher, filtri, ID e path sono rivalidati
  nel core. Nessun buffer parziale. Nessun multi-output / seriale `-0001`.
- **Undo/Redo:** ogni Command ripristina esattamente recipe e collegamenti
  persistenti. I file derivati non sono l'autorità e non vengono simulati dentro
  lo stack Undo (Create and Generate = Duplicate + Register, due entry MVP).
- **Play:** `PlaySession` e Logic Board consumano esclusivamente
  `AudioAssetDef`; il dominio recipe non viene materializzato nel gameplay.
- **Thread/lifetime:** DSP e encoding ricevono snapshot immutabili. Il risultato
  rientra con ID e revisione di origine; una recipe cambiata rende obsoleto il
  completamento. Le risorse Raylib restano sul thread proprietario.
- **Failure path:** errori strutturati arrivano alla Console; un fallimento non
  muta documento o catalogo e non lascia output parziali (salvo Needs generation
  dopo Duplicate riuscito + Generate fallito).

## Slice

| Slice | Stato | Definition of Done |
|---|---|---|
| SFX-1. Core DSP | Completata | Synth deterministico, preset, WAV atomico e 12/12 test forniti superati. |
| SFX-2. Dominio authoring | Completata | Schema v5, codec, validator, Command e Undo/Redo con stale guard. |
| SFX-3. Editor RmlUi | Completata | Catalogo Generated SFX, editor completo e preset senza autorità parallela. |
| SFX-4. Preview e Generate | Completata | Job su snapshot, preview owner-thread e WAV atomico registrato come `AudioAssetDef`. |
| SFX-5. Gate finali | Completata | Build nativa, lifecycle smoke, core + Logic + 12 SFX verdi. |

## Workspace multi-asset (contratto congelato)

- Browser: ricerca, + New from Preset, Rename, **Duplicate Sound**, Delete.
- Ogni riga = un sound asset indipendente (`1 recipe ↔ 1 AudioAssetDef ↔ 1 WAV`).
- **Generate Audio Asset** (Needs generation): primo WAV canonico.
- **Regenerate Audio Asset** (Stale/Ready): stesso id/path, replace atomico.
- **Create New Sound from Current…** (menu ⋯): `DuplicateGeneratedSfxCommand`
  + Generate sul nuovo id. Dialog: Enter/CTA creano; blur solo valida;
  Escape/Cancel senza mutazioni. Preflight progetto salvato.
- L’originale non viene ripristinato (se Stale resta Stale).
- **Regenerate All Stale**: stessa operazione individuale in-place, seriale,
  workspace-only; non aumenta `audioAssets.size()`.
- Nomi univoci trim + case-insensitive (`audioDisplayNameExists`);
  `uniqueGeneratedSfxName` → `Coin`, `Coin 02`, `Coin 03`, …
- Prossima: Variation Sets — non “più output per recipe”.

## Note di integrazione

Il riferimento alla React UI nel pacchetto originale è sostituito dal prodotto
RmlUi nativo, in accordo con `AGENTS.md`. Non vengono introdotti React, Tauri,
bridge WASM, FFmpeg o polling del filesystem. Ogg Vorbis resta un adapter
opzionale finché le librerie Xiph e le relative notice non sono distribuite.
