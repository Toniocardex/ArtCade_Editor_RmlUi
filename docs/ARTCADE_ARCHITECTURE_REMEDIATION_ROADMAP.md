# ArtCade RmlUi — Architecture Remediation Roadmap

**Stato:** pianificato
**Ambito:** editor desktop nativo C++ basato su RmlUi, Raylib e runtime C++
**Scopo:** rimuovere le non conformità P0 e P1 rilevate dall'audit architetturale
**Baseline audit:** build Release riuscita; `editor_core_test`: 2.973 test superati, 0 falliti
**Ultimo aggiornamento:** 2026-07-12

## 1. Autorità e precedenza

Questa roadmap è subordinata, nell'ordine, a:

1. `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`;
2. `ARTCADE_RMLUI_ARCHITECTURE.md`;
3. `ARTCADE_RMLUI_ENGINEERING_GATES.md`;
4. `docs/RMLUI_MIGRATION_CONTRACT.md`;
5. architettura consolidata e verificata nel repository.

In caso di conflitto prevale il documento di livello superiore. La roadmap non autorizza eccezioni ai P0, doppie autorità, sincronizzazioni temporanee o refactor massivi non necessari.

## 2. Obiettivo finale

La remediation è conclusa soltanto quando:

- tutte le violazioni P0 sono chiuse;
- nessun errore lascia il `ProjectDocument` parzialmente modificato;
- file progetto e asset ostili falliscono in modo controllato;
- New, Open, Save e Replace non possono perdere il progetto corrente;
- pending edit e unsaved guard seguono il flusso canonico;
- listener, documenti RmlUi, texture e context hanno lifetime verificato;
- `artcade-editor-core` non dipende da UI, filesystem concreto o API OS;
- Edit e Play restano completamente isolati;
- test core, persistence e RmlUi sono verdi;
- un secondo audit non rileva condizioni stop-the-line.

## 3. Regole di esecuzione

- Una slice risolve un solo problema architetturale principale.
- Ogni bugfix introduce prima un regression test che fallisce sul comportamento precedente.
- Ogni slice lascia il repository compilabile e testabile.
- Nessuna P1 inizia prima del gate di riesame P0.
- Nessun test viene rimosso, disabilitato o indebolito per accettare una patch.
- Le modifiche locali preesistenti devono essere preservate e isolate dalla remediation.
- Nuovi manager, event bus, job system o framework DI sono fuori scope.
- Le astrazioni nuove devono proteggere un confine, un'invariante, un lifetime o un'operazione atomica reale.
- Ogni commit deve avere rollback comprensibile.

## 4. Stato sintetico

| ID | Priorità | Slice | Stato | Dipendenze |
|---|---|---|---|---|
| BASE-01 | Preparazione | Congelamento baseline e isolamento working tree | [x] | — |
| P0-01 | P0 | Atomicità dei Command multi-mutazione | [x] | BASE-01 |
| P0-02 | P0 | Invarianti numeriche nel core | [x] | BASE-01 |
| P0-03 | P0 | Parser JSON rigoroso ed exception-safe | [x] | P0-02 |
| P0-04 | P0 | Root confinement dei path | [x] | BASE-01 |
| P0-05 | P0 | New Project transazionale | [x] | P0-03, P0-04 |
| P0-06 | P0 | Pending edits prima dell'unsaved guard | [x] | P0-02 |
| P0-07 | P0 | Lifecycle RmlUi deterministico | [ ] | BASE-01 |
| P0-08 | P0 | Separazione core/persistence/platform | [ ] | P0-03, P0-04 |
| GATE-P0 | Gate | Riesame completo P0 | [ ] | P0-01…P0-08 |
| P1-01 | P1 | Migrazioni esplicite e sequenziali | [ ] | GATE-P0 |
| P1-02 | P1 | RML contract suite | [ ] | GATE-P0, P0-07 |
| P1-03 | P1 | Input routing focus-aware | [ ] | GATE-P0 |
| P1-04 | P1 | Limiti delle risorse | [ ] | GATE-P0 |
| P1-05 | P1 | Validazione reale degli asset | [ ] | P1-04 |
| P1-06 | P1 | Riduzione delle API mutabili | [ ] | GATE-P0 |
| P1-07 | P1 | Complessità proporzionata dei controller | [ ] | P1-02 |
| GATE-FINAL | Gate | Audit finale, stress e documentazione | [ ] | P1-01…P1-07 |

Stati ammessi: `[ ]` non iniziato, `[-]` in corso, `[x]` completato, `[!]` bloccato con motivazione registrata.

## 5. Fase BASE — Protezione della baseline

### BASE-01 — Congelamento baseline e isolamento working tree

**Problema**

Il working tree contiene modifiche locali preesistenti. Le fix non devono sovrascriverle né attribuirle erroneamente alla remediation.

**Attività**

- [x] Registrare `git status --short`.
- [x] Salvare e revisionare il diff completo preesistente.
- [x] Isolare il lavoro esistente in un commit o branch concordato.
- [x] Creare un branch di remediation, ad esempio `codex/architecture-remediation`.
- [x] Eseguire `git diff --check`.
- [x] Eseguire build Release e suite core.
- [x] Registrare warning e limitazioni ambientali conosciute.

**Gate di uscita**

- [x] Le modifiche preesistenti sono recuperabili e distinguibili.
- [x] La baseline compila.
- [x] Tutti i test baseline sono verdi.
- [x] Nessuna modifica funzionale è stata introdotta dalla fase BASE.

## 6. Programma P0

### Wave A — Integrità del dominio

### P0-01 — Atomicità dei Command multi-mutazione

**Problema**

Alcuni Command applicano più mutazioni fallibili in sequenza. Un failure path successivo alla prima mutazione può lasciare il documento modificato senza history coerente.

**Scope minimo**

- `SetSpriteRendererAssetCommand`;
- `SetSpriteRendererAnimationCommand`;
- `RemoveSpriteAnimationAssetCommand`;
- primitive correlate di `ProjectDocument`;
- test di Command, revision e Undo/Redo.

**Design richiesto**

- [x] Validare tutte le precondizioni prima della prima mutazione.
- [x] Rappresentare lo stato finale completo di SpriteRenderer/SpriteAnimator.
- [x] Applicare lo stato finale tramite primitive private atomiche.
- [x] Eseguire un solo `markDirty()` per operazione logica.
- [x] Eliminare ogni `return failure` successivo a una mutazione non rollbackata.
- [x] Conservare lo stato necessario per Undo esatto.
- [x] Non usare un manager o un transaction framework generico.

**Test obbligatori**

- [x] Failed Apply → documento invariato.
- [x] Failed Apply → revision, dirty e history invariati.
- [x] Apply → New State.
- [x] Apply → Undo → Original State.
- [x] Apply → Undo → Redo → New State.
- [x] Rimozione asset referenziato da più scene/istanze.
- [x] Ripristino esatto di renderer e animator.
- [x] No-op senza revisione.

**Gate di uscita**

Nessun Command può restituire `ok == false` dopo aver lasciato una mutazione osservabile nel `ProjectDocument`.

### P0-02 — Invarianti numeriche nel core

**Problema**

Alcuni valori numerici possono entrare nel dominio senza controllo di finitezza o range. La UI non può essere l'unico livello di validazione.

**Scope**

- transform delle entità;
- dimensioni e colori scena;
- componenti numerici persistenti;
- zoom, pan e valori workspace;
- validator di Load/Save.

**Design richiesto**

- [x] Introdurre helper puri e mirati per finitezza/range.
- [x] Validare nei Command prima della mutazione.
- [x] Proteggere le primitive di dominio da valori non finiti.
- [x] Validare gli stessi invarianti durante Load e Save.
- [x] Evitare duplicazioni divergenti tra UI, Command e validator.

**Test obbligatori**

- [x] `NaN`.
- [x] `+infinity` e `-infinity`.
- [x] Zero e valori negativi dove vietati.
- [x] Estremi fuori range.
- [x] Documento ostile contenente numeri invalidi.
- [x] Failed Command senza revisione.

**Gate di uscita**

Nessun valore numerico persistente non finito può entrare nel `ProjectDocument`; nessun workspace state non finito può compromettere camera, picking o rendering.

### Wave B — Trust boundary e persistenza

### P0-03 — Parser JSON rigoroso ed exception-safe

**Problema**

Campi JSON con tipo errato possono produrre eccezioni non intercettate o fallback silenziosi.

**Design richiesto**

- [x] Distinguere campo assente da campo presente ma malformato.
- [x] Usare lettori tipizzati con contesto del campo.
- [x] Intercettare eccezioni JSON al confine pubblico del deserializer.
- [x] Restituire errori espliciti e mostrabili.
- [x] Non costruire un progetto parzialmente valido da input corrotto.
- [x] Lasciare intatto il progetto corrente in ogni failure path.

**Test obbligatori**

- [x] `formatVersion` con tipo stringa, null, array e oggetto.
- [x] Oggetti transform sostituiti da scalari.
- [x] Array con elementi di tipo errato.
- [x] Numeri fuori rappresentazione.
- [x] JSON troncato e malformato.
- [x] Unicode valido.
- [x] Campi opzionali realmente assenti.
- [x] Eccezioni interne convertite in errore esplicito.

**Gate di uscita**

Nessun file progetto può causare un'eccezione non intercettata, un crash o una conversione silenziosa di corruzione in stato valido.

### P0-04 — Root confinement dei path

**Problema**

Un path relativo può contenere traversal e risolversi fuori dalla root del progetto.

**Design richiesto**

- [x] Creare un'unica funzione `resolvePathInsideRoot` o equivalente.
- [x] Rifiutare path assoluti e root differenti.
- [x] Normalizzare i componenti del path.
- [x] Canonicalizzare la root e il parent esistente.
- [x] Verificare containment per componenti, non per prefisso stringa.
- [x] Gestire symlink, junction e reparse point.
- [x] Restituire `Result<path>` con errore e remediation.
- [x] Riutilizzare la funzione in Edit, Play, texture, import e Save As.

**Test obbligatori**

- [x] `../secret.png`.
- [x] Separatori Windows/Unix misti.
- [x] Path assoluto e drive differente.
- [x] Root con nome avente lo stesso prefisso di un'altra directory.
- [x] Symlink/junction verso l'esterno.
- [x] Path interno valido.
- [x] Path Unicode.

**Gate di uscita**

Nessun dato controllato dal progetto può causare letture o scritture fuori dalla root autorizzata.

### Wave C — Workflow transazionali

### P0-05 — New Project transazionale

**Problema**

Il documento corrente può essere sostituito prima di sapere se il primo salvataggio del nuovo progetto è riuscito.

**Flusso autorevole**

```text
costruisci candidato
  ↓
valida candidato
  ↓
serializza e salva atomicamente
  ↓
commitReplaceProject
  ↓
aggiorna path, cache, titolo e capability
```

**Attività**

- [x] Estrarre il salvataggio di un `ProjectDocument` candidato dal mutamento del Coordinator.
- [x] Non chiamare `replaceProject` durante Prepare.
- [x] Committare soltanto dopo scrittura riuscita.
- [x] Aggiornare `currentProjectPath` soltanto dopo successo.
- [x] Definire cleanup di directory/file temporanei.

**Test obbligatori**

- [x] Annullamento Save As.
- [x] Creazione directory fallita.
- [x] Validazione o serializzazione fallita.
- [x] Scrittura temporanea fallita.
- [x] Atomic replace fallito.
- [x] Progetto corrente, history, selection, cache e path invariati su errore.
- [x] Successo → candidato attivo e clean.

**Gate di uscita**

Nessun errore nel flusso New può sostituire o perdere il progetto corrente.

### P0-06 — Pending edits prima dell'unsaved guard

**Problema**

Un campo ancora focalizzato può contenere un valore non committato quando New, Open, Close o Save interrogano il dirty state.

**Flusso autorevole**

```text
resolvePendingEdits
  ↓
resolveUnsavedGuard
  ↓
prepare
  ↓
commit
```

**Design richiesto**

- [x] Aggiungere un risultato tipizzato per `Resolved`, `Invalid` e `Incomplete`.
- [x] Riutilizzare lo stesso parsing e lo stesso percorso di commit dei campi.
- [x] Non generare Command per input invalido/incompleto.
- [x] Riportare focus e diagnostica sul campo problematico.
- [x] Applicare il gate a New, Open, Close, uscita finestra, Save e Start Play quando pertinente.

**Test obbligatori**

- [x] Valore valido non ancora sfocato.
- [x] `-`, `.`, `1e`, `12.`.
- [x] `NaN`, infinito e suffissi.
- [x] Escape/rollback.
- [x] Chiusura finestra con campo focalizzato.
- [x] Save riuscito e fallito.
- [x] Discard e Cancel.
- [x] Cambio selection durante editing.

**Gate di uscita**

Nessun pending edit viene ignorato, sovrascritto o scartato silenziosamente.

### Wave D — Lifetime e confini

### P0-07 — Lifecycle RmlUi deterministico

**Problema**

I listener sono registrati su più documenti senza detach esplicito prima dello shutdown RmlUi.

**Design richiesto**

- [ ] Rendere `bind()` sicuro rispetto a una seconda chiamata.
- [ ] Implementare `detach()` idempotente.
- [ ] Rimuovere ogni listener mentre i documenti sono validi.
- [ ] Invalidare i puntatori ai documenti dopo detach.
- [ ] Distruggere controller e listener prima dei documenti osservati.
- [ ] Chiudere i documenti prima del context.
- [ ] Mantenere le custom interface vive fino al completamento di `Rml::Shutdown()`.

**Ordine di shutdown**

```text
sospendi input
  ↓
EditorUi::detach
  ↓
distruggi EditorUi/controller/listener
  ↓
chiudi documenti
  ↓
completa distruzione differita
  ↓
Rml::Shutdown
  ↓
chiudi Raylib
```

**Test obbligatori**

- [ ] Bind e detach normali.
- [ ] Doppio detach.
- [ ] Secondo bind.
- [ ] Documento mancante.
- [ ] Nessuna callback dopo detach.
- [ ] Close con pannelli aperti.
- [ ] Stress open/close.
- [ ] Shutdown senza leak o use-after-free.

**Gate di uscita**

Prima della distruzione dei documenti non rimane alcun listener applicativo registrato.

### P0-08 — Separazione core/persistence/platform

**Problema**

Il target `artcade-editor-core` compila attualmente anche filesystem concreto e codice Win32.

**Target minimi**

```text
artcade-editor-core
artcade-editor-persistence
artcade-editor-native
```

**Responsabilità**

`artcade-editor-core`:

- `ProjectDocument` e invarianti;
- `EditorState` e workspace semantico;
- Command, CommandStack e Coordinator;
- proiezioni e materializzazioni pure.

`artcade-editor-persistence`:

- serializer;
- migration;
- validator di progetto;
- load/save;
- filesystem e atomic replace.

`artcade-editor-native`:

- composition root;
- dialog OS;
- import orchestration;
- RmlUi e Raylib.

**Controlli**

- [ ] Core senza `windows.h`, RmlUi e Raylib.
- [ ] Core senza I/O filesystem concreto.
- [ ] Persistence senza RmlUi.
- [ ] Nessun ciclo tra target.
- [ ] Visibilità CMake minima.
- [ ] Test core collegati soltanto al core.
- [ ] Test Save/Load collegati a core e persistence.

**Gate di uscita**

Il build graph rende reali i confini dichiarati senza introdurre target o interfacce ulteriori non necessari.

## 7. GATE-P0 — Riesame obbligatorio

Il programma P1 non può iniziare finché tutti i punti seguenti non sono verdi:

- [ ] P0-01…P0-08 completati.
- [ ] Build pulita da zero.
- [ ] Test core verdi.
- [ ] Test persistence verdi.
- [ ] Regression test negativi verdi.
- [ ] Nessun failed Command lascia stato parziale.
- [ ] Save/Load/New/Open/Replace verificati.
- [ ] Edit/Play isolati.
- [ ] Pending edits verificati.
- [ ] Path traversal e symlink escape bloccati.
- [ ] Parser ostile non causa crash.
- [ ] Listener count stabile e shutdown deterministico.
- [ ] Nessun test disabilitato o indebolito.
- [ ] `git diff --check` verde.
- [ ] Diff completo letto e compreso.
- [ ] Secondo audit P0 senza stop-the-line.

## 8. Programma P1

### P1-01 — Migrazioni esplicite e sequenziali

- [ ] Definire una rappresentazione/candidato separato.
- [ ] Implementare passi espliciti come `migrateV0ToV1` e `migrateV1ToV2`.
- [ ] Aggiornare `formatVersion` a ogni passo.
- [ ] Rimuovere migrazioni nascoste dal parser generico.
- [ ] Aggiungere fixture reali per ogni versione supportata.
- [ ] Rifiutare schema futuro.
- [ ] Lasciare intatto il file originale su errore.

**Gate:** una migrazione è deterministica, testata, osservabile e indipendente da RmlUi.

### P1-02 — RML contract suite

Per ogni documento significativo:

- [ ] caricamento e parser errors;
- [ ] ID obbligatori;
- [ ] assenza di ID duplicati;
- [ ] tipo degli elementi rilevanti;
- [ ] attach e refresh;
- [ ] detach e close;
- [ ] callback residue;
- [ ] RML, RCSS e font mancanti;
- [ ] finestra piccola, DPI elevato e testi lunghi.

**Gate:** una violazione del contratto RML produce diagnostica esplicita, non un return silenzioso.

### P1-03 — Input routing focus-aware

- [ ] Usare il consumo reale degli eventi RmlUi.
- [ ] Bloccare viewport e runtime con popup/modali.
- [ ] Inviare input Play soltanto quando il viewport è autorizzato.
- [ ] Rilasciare drag capture su perdita focus.
- [ ] Evitare doppio input per frame.
- [ ] Testare typing, Delete, shortcut, menu, drag e Play.

**Gate:** un evento fisico viene tradotto una volta e raggiunge un solo consumer autorizzato.

### P1-04 — Limiti delle risorse

Definire e testare limiti per le superfici presenti:

- [ ] dimensione del file progetto;
- [ ] scene, entità, layer e Object Type;
- [ ] asset e dimensione delle stringhe;
- [ ] texture e risoluzioni;
- [ ] frame di animazione;
- [ ] tileset, tilemap, chunk e celle;
- [ ] elementi RML dinamici e listener;
- [ ] console log.

**Gate:** il superamento di un limite produce un errore controllato, mai crash, freeze o allocazione incontrollata.

### P1-05 — Validazione reale degli asset

- [ ] Verificare magic bytes/formato reale.
- [ ] Eseguire decode controllato prima dell'accettazione.
- [ ] Applicare limiti di dimensione e risoluzione.
- [ ] Gestire file troncati e payload ostili.
- [ ] Eseguire rollback completo della copia su errore.
- [ ] Fornire placeholder e diagnostica per asset mancanti.

**Gate:** l'estensione non è mai l'unico controllo di validità di un asset.

### P1-06 — Riduzione delle API mutabili

- [ ] Rendere `ProjectDocument::replace` non pubblico.
- [ ] Confinare `markProjectSaved` al salvataggio riuscito.
- [ ] Riesaminare tutte le friendship di `ProjectDocument`.
- [ ] Impedire nuovi accessi mutabili dai pannelli tramite build/CI.
- [ ] Mantenere API const per proiezioni e renderer.

**Gate:** nessun consumer esterno al percorso autorizzato può mutare o sostituire il documento.

### P1-07 — Complessità proporzionata dei controller

Questa slice è subordinata alla RML contract suite e deve essere eseguita solo se il beneficio è concreto.

- [ ] Valutare l'estrazione di Sprite Animation da `EditorUi`.
- [ ] Valutare l'estrazione di Tileset Editor da `EditorUi`.
- [ ] Mantenere semplici Hierarchy, Console e Assets.
- [ ] Documentare owner, observer, attach, detach e cleanup dei controller estratti.
- [ ] Non introdurre manager, event bus o mega-ViewModel.

**Gate:** la separazione riduce un rischio misurabile di lifecycle o coupling e non aggiunge soltanto file.

## 9. GATE-FINAL — Accettazione della remediation

### Build e test

- [ ] Build Release pulita.
- [ ] Suite core verde.
- [ ] Suite persistence verde.
- [ ] RML contract suite verde.
- [ ] Test positivi e negativi verdi.
- [ ] Regression test per ogni finding originario.
- [ ] Stress lifecycle pertinente verde.
- [ ] Nessun warning critico ignorato.

### Architettura

- [ ] Autorità unica del `ProjectDocument`.
- [ ] Mutazioni authoring soltanto tramite Command.
- [ ] Command atomici.
- [ ] Undo/Redo e revision corretti.
- [ ] Save/Load/New/Open/Replace transazionali.
- [ ] Pending edits risolti prima dell'unsaved guard.
- [ ] Edit/Play isolati.
- [ ] Runtime snapshot indipendente.
- [ ] Cache ricostruibili.
- [ ] Lifetime RmlUi espliciti.
- [ ] Nessuna dipendenza vietata.

### Sicurezza

- [ ] Parser rigoroso.
- [ ] Path confinati alla root.
- [ ] Asset validati realmente.
- [ ] Limiti delle risorse applicati.
- [ ] Nessun segreto nel repository o nei log.
- [ ] Nessun fallback silenzioso su errore critico.

### Review

- [ ] Diff completo letto e compreso.
- [ ] Nessuna API inventata o astrazione non necessaria.
- [ ] Nessun TODO nascosto.
- [ ] Nessun test indebolito.
- [ ] Documentazione e ADR aggiornati quando richiesto.
- [ ] Audit finale senza P0 o P1 aperte.

## 10. Strategia di commit raccomandata

```text
test(core): cover partial sprite command failures
fix(core): make sprite source commands atomic

test(core): cover non-finite domain values
fix(core): enforce numeric invariants

test(persistence): cover hostile json types
fix(persistence): make project parsing strict and exception-safe

test(security): cover project-root path escape
fix(security): confine asset paths to project root

test(app): cover failed new-project preparation
fix(app): make new-project workflow transactional

test(ui): cover pending edits before destructive actions
fix(ui): resolve pending edits before unsaved guard

test(rml): cover bind-detach lifecycle
fix(rml): add deterministic document-session cleanup

refactor(build): separate core from persistence and platform

test(rml): add document contract suite
fix(input): enforce focus-aware input routing
fix(assets): add resource limits and format validation
refactor(core): restrict document mutation surface
```

Non è obbligatorio usare esattamente questi titoli, ma ogni commit deve restare monotematico.

## 11. Template di chiusura per ogni slice

```text
ID:
Problema risolto:
Autorità coinvolta:
File e target modificati:
Invarianti protette:
Intent/Command coinvolti:
Undo/Redo:
Persistenza/migrazione:
Comportamento Edit/Play:
Failure path e rollback:
Test aggiunti:
Build/test eseguiti:
Rischi residui:
Debito registrato:
```

## 12. Diario di avanzamento

Usare questa sezione per decisioni e risultati sintetici, senza sostituire commit, test o ADR.

| Data | ID | Stato | Evidenza / nota |
|---|---|---|---|
| 2026-07-12 | ROADMAP | Creato | Roadmap iniziale derivata dall'audit architetturale. |
| 2026-07-12 | P0-01 | Completato e pubblicato | Command sprite e delete-cascade convertiti a commit atomico; anche CreateScene e CreateEntityWithDefaultType ricondotti a una sola revisione. Build Release verde, `editor_core_test`: 3.367 passed, 0 failed. Incluso nel commit `010684b`, ora integrato e pubblicato su `origin/master`. |
| 2026-07-12 | P0-02 | Completato e pubblicato | Aggiunti helper numerici condivisi, guardie in Command/primitivi di dominio/workspace e validazione Load/Save; coperti NaN, ±Inf, range, documenti ostili e atomicità dei failure. Build Release verde, `editor_core_test`: 3.415 passed, 0 failed. Incluso nel commit `010684b`, ora integrato e pubblicato su `origin/master`. |
| 2026-07-12 | P0-03 | Completato e pubblicato | Deserializer convertito a reader tipizzati: i default valgono solo per campi assenti, mentre tipi errati, interi/float fuori rappresentazione, collezioni corrotte e JSON/Unicode malformati producono errori contestualizzati e exception-safe. Verificata l'immutabilità del coordinator su failure. Build Release verde, `editor_core_test`: 3.478 passed, 0 failed. Incluso nel commit `010684b`, ora integrato e pubblicato su `origin/master`. |
| 2026-07-12 | P0-04 | Completato e pubblicato | Introdotta `resolvePathInsideRoot` con normalizzazione portabile, canonicalizzazione del parent esistente e containment per componenti; applicata a validator/primitivi asset, texture Edit/Play, import e copia Save As con pre-scan dell'albero. Coperti traversal, separatori misti, path assoluti/drive, sibling-prefix, Unicode e symlink quando consentiti dalla piattaforma. Build Release verde, `editor_core_test`: 3.509 passed, 0 failed. Integrato e pubblicato su `origin/master`. |
| 2026-07-12 | P0-05 | Completato e pubblicato | Estratto il salvataggio atomico di candidati indipendenti dal coordinator e introdotta una transazione New prepare/save/commit con snapshot e rollback di destinazione/directory. Path, cache e titolo cambiano solo dopo `ProjectReplaced`. Coperti cancel, directory, validation, serialization, temp write, atomic replace, commit rollback e successo clean. Build Release verde, `editor_core_test`: 3.616 passed, 0 failed. Integrato e pubblicato su `origin/master`. |
| 2026-07-12 | BASE-01 | Completato e riallineato | Diff preesistenti revisionati e preservati, remediation integrata con merge `73b3004`, build Release e 3.616 test verdi. Il workspace principale e `origin/HEAD` puntano a `master`; il branch temporaneo è stato eliminato localmente e sul remoto. |
| 2026-07-12 | P0-06 | Completato | Introdotti `PendingEditStatus` (`Resolved`, `Invalid`, `Incomplete`) e classificazione UI-free che riusa `parseNumberField`; blur/Invio trattengono focus e diagnostica per buffer incompleti o invalidi, Escape ripristinato è un no-op. `resolvePendingEdits()` precede unsaved guard, Save/New/Open/Close/Play e navigazione di selezione, riusando il normale commit su blur. Build Release isolata linkata con successo mentre l'eseguibile principale era aperto; `editor_core_test`: 3.676 passed, 0 failed. |
