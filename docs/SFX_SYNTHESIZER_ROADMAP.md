# ArtCade SFX Synthesizer v2 — roadmap nativa

**Stato:** in ristrutturazione — SFX-R1…SFX-R5 completate il 2026-07-18;
SFX-R6 ha chiuso implementazione, review e gate automatici, ma resta aperta
fino alla conferma della checklist manuale.

Le slice SFX-1…SFX-5 descrivono la baseline consegnata, ma non costituiscono più
la chiusura della feature. Le violazioni P0/P1 elencate sotto bloccano nuove
estensioni, release e push della baseline finché le slice SFX-R1…SFX-R6 non
raggiungono la rispettiva Definition of Done.

## Contratto architetturale

- **Autorità:** `ProjectDoc.generatedSfx` possiede recipe e nome authoring. Il
  catalogo `AudioAssetDef` possiede esclusivamente l'output runtime. L'unico
  legame autorevole è `GeneratedSfxDef.outputAssetId`; l'eventuale
  `AudioAssetDef.generatedFromSfxId` è sola provenienza derivata e non può
  diventare un secondo ownership link. Il nome non viene mantenuto come mirror
  autorevole: mentre l'output è collegato deriva da `GeneratedSfxDef.name`.
  Delete è distruttivo e rimuove insieme recipe e output collegato.
- **Identità canonica:** `generated-audio-{sfxId}` →
  `assets/audio/generated/generated-audio-{sfxId}.wav`.
  `RegisterGeneratedSfxOutputCommand` rifiuta id/path non canonici.
- **Percorso unico:** ogni semantica segue esclusivamente
  `N entry point UI → 1 azione tipizzata → 1 Intent → 1 use case applicativo →`
  `1 Command/policy di dominio → 1 adapter esterno`. Più pulsanti o menu sono
  ammessi solo se emettono la stessa azione tipizzata senza decisioni locali.
  Alias stringa, Command alternativi e writer paralleli dello stesso stato sono
  vietati.
- **Intent/Command:** create, rename, update, delete, duplicate e completamento
  della generazione passano da Intent/use case e poi da Command. Preview e Stop
  sono Intent workspace e non producono dirty. Batch e Create from Current non
  implementano una seconda generazione: invocano lo stesso use case individuale.
- **Invarianti:** versioni supportate, sample rate, durata/frame massimi, ADSR,
  frequenze/Nyquist, gain, duty, bit crusher, filtri, ID e path sono rivalidati
  nel core. Nessun buffer parziale. Nessun multi-output / seriale `-0001`.
- **Undo/Redo:** ogni Command ripristina esattamente recipe e collegamenti
  persistenti. La presenza di un file derivato deve avere una policy applicativa
  esplicita: Undo non può lasciare un WAV orfano che il preflight successivo
  interpreta come collisione irrecuperabile. Apply/Undo/Redo e compensazione
  filesystem formano un protocollo definito e testato.
- **Play:** `PlaySession` e Logic Board consumano esclusivamente
  `AudioAssetDef`; il dominio recipe non viene materializzato nel gameplay.
- **Thread/lifetime:** DSP e encoding ricevono snapshot immutabili. Ogni richiesta
  project-scoped porta `ProjectSessionId`, source revision/fingerprint e
  generation token. Il completamento obsoleto viene scartato anche dopo la
  riapertura dello stesso path. Le risorse Raylib restano sul thread proprietario.
- **Failure path:** errori strutturati arrivano alla Console; finalize file e
  registrazione documentale sono coordinati da un'unica transazione applicativa
  con rollback/compensazione. Nessun fallimento può sostituire il WAV lasciando
  invariati catalogo o fingerprint. Create and Generate dichiara una sola policy
  atomica o uno stato di recupero esplicito, mai un mezzo successo implicito.

## Slice

| Slice | Stato | Definition of Done |
|---|---|---|
| SFX-1. Core DSP | Baseline acquisita | Synth deterministico, preset, WAV atomico e 12/12 test forniti superati. |
| SFX-2. Dominio authoring | Riallineata in R1/R3 | Schema v8, authority policy e writer unico completati; Undo filesystem coordinato dalla transazione R3. |
| SFX-3. Editor RmlUi | Riallineata in R4 | Controller, ViewModel, registry azioni e catalogo preset unici completati. |
| SFX-4. Preview e Generate | Riallineata in R2/R3 | Service/session identity e transazione output/compensazione completati. |
| SFX-5. Gate finali | Obsoleta | Evidenza storica non sufficiente dopo l'audit; sostituita da SFX-R6. |

Le righe sopra sono **baseline storica**. La roadmap può tornare `completata`
solo dopo la chiusura delle slice di ristrutturazione seguenti.

## Audit e blocchi correnti

| Severità | Difformità | Slice di chiusura |
|---|---|---|
| P0 | Job senza `ProjectSessionId` e source revision; uno stale result può rientrare dopo reopen dello stesso path. | **Chiusa — SFX-R2** |
| P0 | Undo della registrazione lascia un WAV orfano che la guard blocca alla generazione successiva. | **Chiusa — SFX-R3** |
| P0 | Load/validator non impone identità canonica, ownership 1:1 e provenienza coerente. | **Chiusa — SFX-R1** |
| P0 | Regenerate sostituisce il WAV prima del Command e non ripristina il file precedente se la registrazione fallisce. | **Chiusa — SFX-R3** |
| P1 | Più writer modificano link/nome: Command Generated SFX e Command audio generico. | **Chiusa — SFX-R1** |
| P1 | Alias e registry azioni divergenti (`generate-new-sfx-output`, doppia create, pending-edit whitelist separata). | **Chiusa — SFX-R4** |
| P1 | Catalogo preset, normalizzazione nome e validazione modale sono duplicati. | **Chiusa — policy nome SFX-R1; preset/UI SFX-R4** |
| P1 | `Ready` non distingue output fisicamente mancante e Play ignora il file mancante. | **Chiusa — SFX-R5** |
| P1 | Command SFX no-op producono revisione/Undo. | **Chiusa — SFX-R1** |
| P1 | Rendering, lifecycle, workflow e stato SFX sono concentrati in `EditorUi`/lambda locali di `EditorApp`. | **Chiusa per job/lifetime in SFX-R2 e Controller/ViewModel in SFX-R4** |

## Matrice dei percorsi canonici

| Semantica | Azione/Intent canonico target | Use case / unico writer |
|---|---|---|
| Create from preset | `CreateGeneratedSfxIntent{presetId}` | `CreateGeneratedSfxCommand` |
| Rename Generated SFX | `RenameGeneratedSfxIntent{id, name}` | `RenameGeneratedSfxCommand`; il rename audio generico rifiuta output collegati |
| Edit recipe | `UpdateGeneratedSfxRecipeIntent{id, recipe}` | `UpdateGeneratedSfxRecipeCommand` |
| Duplicate | `DuplicateGeneratedSfxIntent{sourceId, name}` | `DuplicateGeneratedSfxCommand` |
| Hard delete | `RemoveGeneratedSfxIntent{id}` | `GeneratedSfxOutputTransaction::remove` → `RemoveGeneratedSfxCommand`; elimina recipe, output collegato, riferimenti strutturati e WAV; `RemoveAudioAssetCommand` rifiuta output collegati |
| Preview | `PreviewGeneratedSfxIntent{id}` | `GeneratedSfxGenerationService::preview` |
| Generate/Regenerate | `GenerateGeneratedSfxIntent{id}` | `GeneratedSfxGenerationService::generate` → unico Command di registrazione |
| Create from Current | `CreateAndGenerateGeneratedSfxIntent{sourceId, name}` | un use case composito che riusa Duplicate e Generate senza orchestration UI |
| Regenerate All Stale | `RegenerateAllStaleSfxIntent` | coda workspace che invoca lo stesso use case Generate per ogni ID |

Regole della matrice:

- `generate-new-sfx-output` viene rimosso; esiste una sola azione Generate.
- `create-generated-sfx` riceve sempre `presetId`; l'alias
  `create-generated-sfx-from-preset` viene rimosso.
- il catalogo preset è una sola tabella `id + label + recipe factory`, consumata
  da menu, Create, Apply e riconoscimento del preset attivo.
- nome, trim, confronto case-insensitive e generazione del suffisso appartengono
  a una sola policy pura di dominio riusata da UI capability e Command.
- la rivalidazione click-time/commit-time è obbligatoria ma richiama la stessa
  policy: ripetere il controllo non significa duplicarne l'implementazione.
- il registry tipizzato dell'azione dichiara anche pending-edit, disponibilità in
  Play e mutabilità; markup e Controller non mantengono whitelist parallele.

## Slice di ristrutturazione

### SFX-R1 — Autorità, invarianti e writer unico

**Stato:** completata il 2026-07-18.

**Obiettivo:** rendere impossibile creare due verità persistenti sullo stesso
Generated SFX o mutuarne il link attraverso Command alternativi.

- **Autorità:** recipe/nome/link in `GeneratedSfxDef`; output runtime in
  `AudioAssetDef`; provenienza audio derivata e non autorevole.
- **Intent/Command:** introdurre la matrice canonica; Command audio generici
  rifiutano rename/remove di un output ancora collegato; una sola policy pura
  applica link e hard delete senza trasferimenti o writer alternativi.
- **Invarianti:** ID/path canonici, ownership output 1:1, path univoco
  case-insensitive, provenienza coerente, nessun mirror nome autorevole.
- **Undo/Redo:** apply/undo/redo di create, rename, update, duplicate, link,
  hard delete preservano ordine e identità; no-op senza revisione.
- **Play:** nessuna variazione; continua a consumare solo `AudioAssetDef`.
- **Test:** serializer hostile/legacy, due recipe sullo stesso output, provenienza
  errata, rename/remove audio collegato rifiutati, matrice Undo/Redo/no-op.

**Evidenza:** schema v8 con migrazione v7→v8; `generated_sfx_policy` è l'unica
implementazione di nome, identità, ownership e stato; output collegati senza nome
mirror; Command audio generici rifiutano output collegati; validator canonico
1:1; build Release e suite core/Logic/DSP verdi.

### SFX-R2 — Generation Service e identità di sessione

**Stato:** completata il 2026-07-18.

**Obiettivo:** una sola autorità workspace per preview, generate, batch, job e
relativo lifetime, estratta dalle lambda locali di `EditorApp`.

- **Autorità:** `GeneratedSfxGenerationService` possiede job, generation token e
  stato operativo; `ProjectSessionController` emette un nuovo `ProjectSessionId`
  su New/Open/Replace, anche quando il path non cambia.
- **Intent/Command:** Preview e Generate entrano nel service tramite Intent;
  soltanto il completamento valido emette il Command di registrazione.
- **Invarianti:** snapshot immutabile con session ID, document/source revision,
  recipe fingerprint e asset ID; un solo job/coda secondo policy esplicita.
- **Undo/Redo:** il service non entra nello stack; solo il Command persistente lo fa.
- **Play:** Preview/Generate rifiutati durante Play; Stop/Replace cancellano o
  invalidano deterministicamente i job.
- **Test:** reopen stesso path, Replace Project, edit/delete durante render,
  completamenti fuori ordine, cancel e shutdown.

**Evidenza:** `GeneratedSfxGenerationService` è l'unico owner di future, token,
cancellazione e coda seriale; `EditorApp` non conserva più job o batch paralleli.
Ogni snapshot porta `ProjectSessionId`, revisione, asset ID, recipe e fingerprint;
il completamento viene scartato per token superato, reopen/Replace, edit recipe,
delete o qualunque revisione concorrente. `ProjectSessionController` avanza
l'identità su New/Open/Replace e su Save As verso un nuovo root, anche a path
riaperto identico. Preview/Generate/Regenerate All entrano tramite Intent tipizzati,
Play è rivalidato nel service e shutdown esegue cancel+join. Il target dedicato
`generated_sfx_generation_service_test` copre sessione same-path, out-of-order,
edit/delete, Play, cancel e shutdown; build nativa e suite core/Logic/DSP verdi.

### SFX-R3 — Transazione output, collisioni e Undo filesystem

**Stato:** completata il 2026-07-18.

**Obiettivo:** coordinare staging, finalize, registrazione e compensazione senza
mezzi successi e senza WAV orfani irrecuperabili.

- **Autorità:** documento per ownership; adapter/repository output per I/O; il
  filesystem non decide mai ownership, ma segnala conflitti esterni.
- **Intent/Command:** Generate/Regenerate usa una sola transazione applicativa;
  Replace, Generate unique name e Cancel sono esiti tipizzati dello stesso
  preflight.
- **Invarianti:** destinazione confinata, replace atomico, backup/rollback del WAV
  precedente, cleanup staging e nessuna sovrascrittura implicita.
- **Undo/Redo:** policy esplicita per file derivato; `Generate → Undo → nuovo
  Command → Generate` deve avere un esito recuperabile e testato.
- **Play:** non vede output parziali; il vecchio WAV resta utilizzabile fino al
  commit riuscito del nuovo.
- **Test:** collisione preflight e race, encode/finalize/register failure,
  rollback Regenerate, orphan dopo Undo, Redo e Create-and-Generate failure.

**Evidenza:** `GeneratedSfxOutputTransaction` è l'unico coordinatore fra staging,
repository output e `RegisterGeneratedSfxOutputCommand`; i Command non dipendono
dal filesystem concreto. Generate e Regenerate usano move no-replace, backup del
WAV precedente e compensazione del finalize/Command. Il side effect appartiene
alla stessa entry di history e rende esatti file e catalogo su Undo/Redo; la
caduta del ramo Redo pulisce gli artefatti non più raggiungibili. Il preflight ha
un esito tipizzato (`Blocked`, `GenerateNew`, `ReplaceOwned`) e le collisioni
esterne non vengono mai sovrascritte. Create from Current mantiene esplicitamente
il duplicato in `NeedsGeneration` se il finalize fallisce, rendendo disponibile
lo stesso Retry Generate canonico. Save As è bloccato durante un job e usa una
ribasatura history a due fasi (validazione completa, poi commit) per evitare root
parzialmente divergenti. Il test dedicato copre Generate/Undo/Redo, caduta del
ramo Redo, Regenerate rollback, stale recipe, finalize failure, collision race,
Retry e Save As; build Release, core, Logic Board, 12/12 DSP e lifecycle smoke
sono verdi.

### SFX-R4 — Controller RmlUi e azioni tipizzate

**Stato:** completata il 2026-07-18.

**Obiettivo:** lasciare alla View solo rendering/raccolta input e rimuovere alias,
whitelist e orchestration duplicate.

- **Autorità:** `GeneratedSfxEditorController` possiede lo stato workspace del
  pannello e produce un ViewModel derivato; nessun authoring state nella View.
- **Intent/Command:** un registry tipizzato mappa ogni entry point alla matrice
  canonica; Create from Current non esegue Command direttamente dalla UI.
- **Invarianti:** ogni azione dichiara pending-edit, Play policy e capability;
  nessun identificatore morto o alias semantico.
- **Undo/Redo:** il Controller non conserva copie authoring; dopo Undo/Redo
  riconcilia selezione, modale e focus dal documento.
- **Play:** il ViewModel disabilita azioni authoring dalla stessa policy usata dal
  dispatcher, senza una seconda lista.
- **Test:** tutti i `data-action` risolvono una sola azione tipizzata, nessun alias,
  Generate/Preset/Delete con campo pending valido o invalido, modal lifetime e
  entry point browser/context-menu convergenti.

**Evidenza:** `GeneratedSfxEditorController` possiede selezione, filtri, modali,
menu, focus one-shot, macro drag e stato batch workspace; `EditorUi` conserva
soltanto rendering RmlUi, raccolta input e refresh differito. Il Controller
produce un `GeneratedSfxEditorViewModel` e calcola le capability Play dalla
stessa tabella descriptor usata dal dispatcher. Il registry tipizzato dichiara
per ogni azione pending-edit, Play e mutabilità; la whitelist SFX parallela è
stata rimossa. `generate-new-sfx-output` e
`create-generated-sfx-from-preset` non sono più risolti né emessi; ogni Create
porta sempre il `presetId`, inclusi Assets e lifecycle smoke. Il solo
`generated_sfx_preset_catalog` alimenta menu Create, pulsanti Apply,
riconoscimento del preset e recipe factory. Create from Current è un use case
composito del Controller: la View non esegue Command e lo stato recuperabile
`NeedsGeneration` resta esplicito. Il test dedicato verifica unicità registry e
preset, risoluzione dei `data-action` SFX nel markup/source, assenza alias,
pending valido/invalido, Create/Duplicate/Generate callback, riconciliazione
Undo/Redo e policy Play condivisa. Build Release, core, Logic Board, 12/12 DSP,
test R3/R4 e lifecycle smoke sono verdi.

### SFX-R5 — Stati derivati, diagnostica e batch

**Stato: completata il 2026-07-18.**

**Obiettivo:** rendere osservabile ogni stato senza introdurre polling o una
seconda autorità.

- **Autorità:** stato derivato da documento + snapshot immutabile del service:
  `UpToDate`, `RecipeModified`, `MissingOutput`, `Generating`,
  `GenerationFailed`, `Collision`.
- **Intent/Command:** retry e batch riusano Generate; dismiss errore è workspace-only.
- **Invarianti:** nessun `Ready` se l'output è noto mancante; nessun errore audio
  critico convertito in `continue` silenzioso.
- **Undo/Redo:** gli stati vengono ricalcolati, non registrati nello stack.
- **Play:** asset richiesto ma non leggibile produce diagnostica esplicita secondo
  policy, senza mutare authoring.
- **Test:** file rimosso, output non leggibile, batch con success/failure/skip/
  cancel e assenza di duplicazioni nel catalogo.

**Evidenza:** `GeneratedSfxStatusProjection` è l'unica proiezione applicativa
consumata dall'editor e dal pannello Assets; combina documento, repository e
snapshot immutabile del service senza persistere stati o aggiungere history.
Espone `UpToDate`, `RecipeModified`, `MissingOutput`, `Generating`,
`GenerationFailed` e `Collision`; Retry e batch attraversano ancora lo stesso
`GenerateGeneratedSfxIntent`. Il dismiss è una singola azione tipizzata
workspace-only. La guard e la proiezione condividono lo stesso output repository.
Play Sound non scarta più in silenzio path/file/decoder/device non validi e
deduplica la diagnostica nella sola sessione Play. `scripts\build.bat --test`
include ora anche i test service/controller: 4.669 core, 264 Logic Board, 12/12
DSP, test R3-R5/controller a zero failure, build Release e lifecycle smoke RmlUi
1/1 verdi.

### SFX-R6 — Gate finali e riallineamento documentale

**Stato: gate SFX automatici completati il 2026-07-18; gate repository da
riconfermare per un crash dello shard Script; QA manuale pendente.**

**Obiettivo:** chiudere la ristrutturazione soltanto con evidenza completa.

- review del diff contro Costituzione, Architettura e Engineering Gates;
- ricerca repository-wide di alias, writer e stringhe canoniche duplicate;
- build Release, `editor_core_test`, `logic_board_editor_test`, 12 test DSP e
  lifecycle smoke RmlUi;
- test manuali Generate, Regenerate, Create from Current, Undo/Redo, reopen stesso
  path, collisione e audio mancante;
- aggiornamento della roadmap a `completata` solo dopo chiusura di ogni P0/P1.

**Review R6:** l'audit ha rilevato e chiuso una difformità P1 residua: il
Controller aveva un solo writer ma saltava l'Intent esplicito e orchestrava
Create from Current localmente. Il percorso è ora `azione tipizzata → Intent →
EditorCoordinator → Command`; Create from Current usa una sola porta applicativa
che rivalida il preflight, applica Duplicate e richiama l'unico use case
Generate. Identità/path e allocazione ID sono nella sola
`generated_sfx_policy`; i Command SFX vengono costruiti esclusivamente dal
Coordinator, salvo `RegisterGeneratedSfxOutputCommand`, posseduto dalla singola
transazione file/documento.

**Evidenza automatica:** nessun alias legacy sotto `src/`; nessun accesso
filesystem concreto dal Controller/pannello; una sola costruzione per ciascun
Command authoring e una sola chiamata applicativa a `requestGenerate`; identità
canonica presente in una sola policy; `git diff --check` pulito. Build Release,
4.669 core, 264 Logic Board, 12/12 DSP, service/controller Generated SFX a zero
failure e stress lifecycle RmlUi 20/20 sono verdi.

**Regressione cestino (2026-07-18):** build applicazione, test Controller e
lifecycle RmlUi con click reale sull'ultimo cestino sono verdi; restano verdi
anche core 2.516, Sprite 171, Tileset/Tilemap 1.189, modello SFX 483, Logic Board
264 e DSP 12/12. Il gate repository complessivo non viene dichiarato chiuso:
`script_asset_test` termina con BEX64 `0xc0000409` nello shard appena separato,
senza asserzioni o dipendenze dal diff del cestino. Il crash è registrato come
debito di test stop-the-line da diagnosticare prima di commit/push.

**Checklist manuale di chiusura:**

- [ ] Generate crea un solo WAV e un solo AudioAsset canonico.
- [ ] Regenerate sostituisce lo stesso WAV senza aumentare la cardinalità.
- [ ] Create from Current crea una sola recipe indipendente e avvia Generate.
- [ ] Undo/Redo ripristina documento e WAV; nuovo Command dopo Undo elimina Redo.
- [ ] Riapertura dello stesso path non accetta completamenti della sessione precedente.
- [ ] Collisione esterna blocca prima del job e Retry funziona dopo la rimozione.
- [ ] WAV mancante/non leggibile è visibile nell'editor/Assets e Play produce diagnostica.

## Ordine e stop-the-line

1. SFX-R1 — invarianti e writer unico.
2. SFX-R2 — service e session identity.
3. SFX-R3 — transazione file/documento.
4. SFX-R4 — Controller e azioni tipizzate.
5. SFX-R5 — stati/diagnostica/batch.
6. SFX-R6 — gate finali.

SFX-R1, SFX-R2 e SFX-R3 hanno chiuso le violazioni P0. Variation Sets e nuove
funzionalità audio restano comunque sospese fino ai gate finali SFX-R6.

## Workspace multi-asset attuale (UX congelata durante il refactor)

- Browser: ricerca, + New from Preset, Rename, **Duplicate Sound**, Delete.
- Ogni riga = un sound asset indipendente (`1 recipe ↔ 1 AudioAssetDef ↔ 1 WAV`).
- **Generate Audio Asset** (Needs generation): primo WAV canonico.
- **Regenerate Audio Asset** (Stale/Ready): stesso id/path, replace atomico.
- **Create New Sound from Current…** (menu ⋯): `DuplicateGeneratedSfxCommand`
  + Generate sul nuovo id. Dialog: Enter/CTA creano; blur solo valida;
  Escape/Cancel senza mutazioni. Preflight progetto salvato.
- **Hardening lifetime (2026-07-18):** Cancel, Enter e Create accodano il rebuild
  finale a `EditorUi::processFrame()`. Il blur valida solo lo stato del
  Controller e non ricostruisce la modale: pressione e rilascio possono cadere
  su frame diversi e il bottone premuto deve restare vivo fino a `click`. Lo
  smoke lifecycle simula `blur → processFrame → click`, verifica l'identità
  stabile della CTA e la singola duplicazione.
- **Hard delete/workspace (2026-07-18):** apertura del workspace e selezione
  sono due stati distinti nel solo `GeneratedSfxEditorController`. Tutti gli
  entry point del cestino emettono `RemoveGeneratedSfxIntent` e convergono su
  `GeneratedSfxOutputTransaction::remove`: la transazione elimina realmente il
  WAV canonico, poi un solo Command elimina `GeneratedSfxDef`, `AudioAssetDef` e
  ogni `LogicAssetReference` strutturata verso quell'audio. Il WAV isolato viene
  conservato esclusivamente in memoria nella entry Undo; nessun backup nascosto
  resta sul disco. Undo ripristina esattamente documento, riferimenti e byte;
  Redo li elimina nuovamente e rifiuta file modificati esternamente. In Play la
  cancellazione è rifiutata prima di ogni mutazione. Eliminando l'ultima recipe
  la selezione si azzera ma l'Audio Editor resta aperto sull'empty state; solo
  `close-generated-sfx` chiude il workspace. Build Release e test dedicati
  coprono click, file fisico assente, Undo/Redo, riferimenti Logic Board e Play.
- **Preflight output (2026-07-18):** una sola guard applicativa rivalida identità
  canonica, path confinato e collisioni reali sul filesystem. La UI usa la stessa
  capability per disabilitare Generate/Create; il click la rivalida prima del
  worker e, per Create from Current, prima del `DuplicateGeneratedSfxCommand`.
- L’originale non viene ripristinato (se Stale resta Stale).
- **Regenerate All Stale**: stessa operazione individuale in-place, seriale,
  workspace-only; non aumenta `audioAssets.size()`.
- Nomi univoci trim + case-insensitive (`audioDisplayNameExists`);
  `uniqueGeneratedSfxName` → `Coin`, `Coin 02`, `Coin 03`, …
- Variation Sets resta sospesa fino alla chiusura di SFX-R6 e non può introdurre
  “più output per recipe” né aggirare la matrice dei percorsi canonici.

## Note di integrazione

Il riferimento alla React UI nel pacchetto originale è sostituito dal prodotto
RmlUi nativo, in accordo con `AGENTS.md`. Non vengono introdotti React, Tauri,
bridge WASM, FFmpeg o polling del filesystem. Ogg Vorbis resta un adapter
opzionale finché le librerie Xiph e le relative notice non sono distribuite.
