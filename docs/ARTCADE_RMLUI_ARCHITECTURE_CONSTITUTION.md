# ArtCade RmlUi — Architecture Constitution

**Stato:** vincolante  
**Ambito:** editor desktop nativo C++ di ArtCade basato su RmlUi, Raylib, runtime C++ e scripting Lua  
**Precedenza:** questo documento prevale su convenzioni locali, scorciatoie, pattern generici e proposte generate dall’AI  
**Documenti subordinati:** `ARTCADE_RMLUI_ARCHITECTURE.md`, `ARTCADE_RMLUI_ENGINEERING_GATES.md`

---

## 1. Scopo e criterio di interpretazione

Questa costituzione protegge i principi fondamentali già stabiliti per ArtCade RmlUi. Non introduce una nuova architettura “enterprise” e non autorizza complessità preventiva.

Una proposta è accettabile soltanto quando:

1. rispetta l’unica autorità dei dati;
2. conserva il flusso Intent/Command;
3. mantiene Edit e Play separati;
4. non introduce sincronizzazioni nascoste;
5. protegge persistenza, invarianti e lifetime;
6. risolve un problema reale con la soluzione più semplice sufficiente.

Quando una regola secondaria, un pattern o una proposta successiva entra in conflitto con i principi fondamentali, deve essere scartata.

> **Regola di precedenza:** principi fondamentali approvati → architettura consolidata nel repository → esigenza concreta → nuova proposta.

---

## 2. Severità

### P0 — Blocco immediato

Una violazione P0 blocca commit, merge, release e prosecuzione della feature.

Sono P0:

- perdita o rischio concreto di perdita dati;
- doppia autorità sullo stesso stato;
- mutazione authoring fuori da Intent/Command;
- contaminazione tra Edit e Play;
- salvataggio o sostituzione progetto non transazionali;
- migrazione incompatibile mancante;
- dipendenza vietata tra dominio e UI/piattaforma;
- ownership o lifetime non definiti;
- input, path, script o asset usati senza validazione adeguata;
- errore critico ignorato o convertito silenziosamente in stato valido;
- test indeboliti per far passare una patch;
- codice AI integrato senza comprensione e revisione del diff.

### P1 — Obbligatorio prima della chiusura della slice

La feature può essere sviluppata, ma non è conclusa finché il requisito non è soddisfatto.

### P2 — Debito pianificabile

Può essere rinviato solo se registrato con motivazione, rischio, owner, priorità e condizione di chiusura.

Non possono essere classificati P2: doppia autorità, perdita dati, vulnerabilità critica, lifetime indefinito, migrazione necessaria assente o contaminazione runtime/authoring.

---

## 3. Principio di semplicità

### AC-SIMPLE-001 — Soluzione minima sufficiente

ArtCade deve usare la soluzione più semplice che preserva correttamente:

- autorità;
- invarianti;
- Undo/Redo;
- persistenza;
- isolamento runtime;
- sicurezza;
- lifetime.

### Regole

- I confini logici sono obbligatori; nuovi framework, target, manager e livelli non lo sono automaticamente.
- Un’astrazione si introduce solo quando separa un’autorità, protegge un’invariante, isola una dipendenza esterna, controlla un lifetime, garantisce atomicità o rimuove duplicazione reale.
- Non si implementano in anticipo job system, plugin system, hot reload, transazioni generiche, cache elaborate o target CMake aggiuntivi senza un’esigenza concreta.
- Un pannello semplice non deve essere trasformato in una gerarchia di classi.
- Un’operazione semplice non deve essere spezzata in una catena artificiale di servizi o eventi.
- La robustezza non giustifica l’overengineering; l’assenza di overengineering non giustifica scorciatoie che violano i P0.

---

## 4. Autorità uniche

### AC-AUTH-001 — Matrice autorevole

| Area | Unica autorità |
|---|---|
| Dati persistenti del progetto | `ProjectDocument` |
| Scena attiva, selezione, tool e stato semantico della Scene View | `EditorState` |
| Layout e preferenze esclusivamente visuali | `EditorUiState` o stato UI locale, mai il dominio |
| Stato durante l’esecuzione | `PlaySession` |
| Definizioni persistenti degli asset | `ProjectDocument` |
| Texture, thumbnail e risorse caricate | cache derivate dedicate |
| Persistenza su disco | `ProjectSerializer` + adapter/repository filesystem |
| Stato temporaneo di un controllo RmlUi | buffer locale del controller |
| Definizione della Logic Board | `ProjectDocument` |
| Stato eseguibile della logica | runtime C++/Lua |
| Dati mostrati dalla UI | proiezioni/ViewModel ricostruibili |

### Regole P0

- RmlUi non è autorità del progetto.
- Il Data Model RmlUi non è uno store applicativo.
- Le cache non sono autorità.
- I ViewModel non sono autorità.
- I buffer UI non sono autorità.
- La `PlaySession` non è autorità per l’authoring.
- Il filesystem non sostituisce il documento in memoria come autorità durante una sessione aperta.
- Nessun `LayerManager`, `EntityManager`, `AssetCatalog` o altro manager può duplicare dati persistenti del `ProjectDocument`.
- È vietato ricostruire dati persistenti leggendo elementi RML.
- È vietata la sincronizzazione periodica tra copie dello stesso stato.

---

## 5. Flusso delle mutazioni

### AC-MUT-001 — Unico confine authoring

```text
Evento RmlUi / interazione Scene View
    ↓
Controller
    ↓
EditorIntent
    ↓
EditorCoordinator
    ↓
Validazione / risoluzione / policy
    ↓
EditorCommand
    ↓
ProjectDocument
    ↓
DomainChange / EditorChange
    ↓
Riconciliazione workspace
    ↓
Invalidazione proiezioni
    ↓
Aggiornamento UI
```

### Regole P0

- Ogni mutazione persistente passa da un `EditorCommand`.
- Ogni mutazione workspace passa da Intent o API controllate di `EditorState`.
- Ogni mutazione puramente visuale resta in `EditorUiState` o nello stato locale del pannello.
- Nessun listener RmlUi modifica direttamente il documento.
- Nessun pannello riceve un riferimento mutabile al dominio.
- Nessun Command conosce RmlUi, elementi RML, focus, coordinate schermo o dettagli del rendering.
- Un Command è atomico: applicazione completa oppure nessuna modifica.
- Un Command fallito non entra nello stack Undo.
- Un Command no-op non produce revisione.
- La validazione UI migliora l’esperienza, ma il core rivalida sempre le invarianti.

---

## 6. Invarianti del dominio

### AC-DOM-001 — Baseline obbligatoria

Il core deve garantire almeno:

- ID univoci nel rispettivo namespace;
- riferimenti validi oppure esplicitamente nullable;
- `SceneDef.layers` come unica autorità dell’ordine dei layer;
- `SceneDef.defaultLayerId` esistente e appartenente alla scena;
- `EntityInstance.layerId` valido e appartenente alla scena dell’entità;
- `activeSceneId`, quando presente, riferito a una scena esistente;
- selection mai pendente dopo delete, replace o undo;
- nomi visuali separati dagli ID;
- rinomina senza modifica dell’identità;
- valori numerici persistenti finiti;
- dimensioni, velocità, tempi e range validi;
- un solo movement driver incompatibile per Object Type;
- nessun Object Type/prefab con nome duplicato quando la policy ArtCade lo vieta;
- cancellazioni con policy esplicita: rifiuto, cascata, null o placeholder;
- nessun riferimento asset pendente non gestito;
- nessuna entità o layer riferito a una scena differente;
- nessuna proprietà derivata duplicata come seconda autorità.

La UI non può essere l’unico punto che protegge queste invarianti.

---

## 7. Write Model e Read Model

### AC-READ-001

Il Write Model autorevole comprende:

- `ProjectDocument`;
- `EditorState`;
- `EditorCommand` e `CommandStack`;
- `PlaySession`, esclusivamente per il runtime.

Il Read Model comprende proiezioni ricostruibili, ad esempio:

- `HierarchyViewModel`;
- `InspectorViewModel`;
- `ToolbarViewModel`;
- `SceneViewSnapshot`;
- proiezioni dell’Asset Browser e della Logic Board.

### Regole

- I ViewModel contengono valori e ID, non puntatori mutabili al dominio.
- Non è obbligatorio creare un ViewModel complesso per una vista banale.
- È vietato creare un mega-snapshot dell’intero editor come seconda copia quasi completa del progetto.
- La perdita di una proiezione o cache non deve causare perdita di dati.

---

## 8. RmlUi è esclusivamente presentazione

### AC-RML-001

RmlUi può:

- mostrare dati;
- raccogliere input;
- mantenere buffer temporanei;
- gestire focus, menu, modali e layout;
- emettere Intent;
- consumare proiezioni;
- applicare RML e RCSS.

RmlUi non può:

- possedere il progetto;
- decidere invarianti;
- mutare direttamente il dominio;
- gestire lo stack Undo/Redo;
- decidere il dirty state;
- eseguire migrazioni;
- possedere `PlaySession`;
- serializzare il dominio;
- usare binding bidirezionale automatico sui dati persistenti;
- ricostruire il progetto dalla struttura degli elementi.

---

## 9. Confini dei moduli

### AC-MOD-001 — Confini logici obbligatori

Devono restare separati almeno:

- dominio/editor core;
- applicazione/coordinamento;
- presentazione RmlUi;
- runtime/PlaySession;
- persistenza e piattaforma concreta.

### Regole

- Il core non dipende da RmlUi, Raylib, filesystem concreto o API OS.
- Il runtime non dipende da RmlUi o `EditorState`.
- Il serializer non dipende dalla UI.
- Il render backend non dipende dal `ProjectDocument`.
- La UI non accede direttamente al filesystem concreto.
- Il numero di target CMake separati deve essere il minimo utile a rendere reali i confini; cartelle e namespace sono sufficienti quando non esiste un beneficio concreto nel separare il target.
- Una dipendenza vietata deve essere impedita dalla build o dalla CI quando possibile.

---

## 10. Composition Root

### AC-COMP-001

Deve esistere un unico punto applicativo autorizzato a creare e collegare implementazioni concrete.

Il Composition Root può conoscere contemporaneamente:

- RmlUi;
- Raylib;
- filesystem;
- `EditorCoordinator`;
- repository;
- cache;
- controller;
- runtime;
- eventuali job e dialog di sistema.

### Regole P0

- Nessun service locator globale.
- Nessun singleton mutabile per servizi applicativi.
- Nessun puntatore globale al Coordinator.
- Nessuna costruzione ad hoc di dipendenze nei pannelli.
- L’ordine reale dei membri owner deve riflettere costruzione e distruzione C++.
- Lo shutdown deve essere deterministico e inverso rispetto all’inizializzazione.

---

## 11. Edit e Play

### AC-PLAY-001

- `Start Play` materializza uno stato runtime separato.
- Il confine è una struttura runtime dedicata, ad esempio `RuntimeProjectSnapshot`/`RuntimeSceneSnapshot`, costruita dal `ProjectDocument`.
- `PlaySession` non conserva riferimenti mutabili al documento authoring.
- Il runtime non interroga il documento ogni frame.
- Le modifiche runtime non tornano automaticamente nell’authoring.
- Durante Play i Command authoring sono bloccati secondo Policy A.
- Selezione e navigazione workspace possono cambiare senza alterare la sessione runtime.
- `Stop` distrugge il runtime senza ripristinare copie obsolete dello stato editor.
- Il salvataggio non include dati della `PlaySession`.
- Un eventuale “Apply Runtime Changes” deve essere esplicito, selettivo, validato, convertito in Command e annullabile.

---

## 12. Undo, Redo, revision e dirty state

### AC-REV-001

- Ogni Command riuscito registra `revisionBefore` e `revisionAfter`.
- Una revisione identifica uno stato, non soltanto il numero di operazioni.
- Undo ripristina `revisionBefore`.
- Redo ripristina `revisionAfter`.
- Un nuovo Command dopo Undo crea una revisione mai riutilizzata e cancella Redo.
- `isDirty()` deriva da `currentRevision != savedRevision`.
- `savedRevision` viene aggiornato soltanto al completamento riuscito del salvataggio dello snapshot corrispondente.
- Workspace e UI state non rendono dirty il progetto.
- Delete/Undo preservano ordine, indice e dati necessari al ripristino esatto.
- Undo non usa euristiche.

---

## 13. Persistenza

### AC-PERSIST-001

- Ogni file progetto contiene `schemaVersion`.
- Versioni future non supportate vengono rifiutate.
- Le migrazioni sono esplicite, sequenziali, deterministiche e testate.
- Il load costruisce un candidato separato; il progetto corrente resta intatto fino al commit.
- Un errore di parsing non produce un progetto vuoto.
- Il salvataggio è atomico: snapshot → serializzazione → file temporaneo → flush/verifica → replace.
- Un salvataggio fallito non modifica `savedRevision`.
- Una migrazione fallita non modifica il file originale.
- RmlUi non partecipa alla serializzazione.
- Errori I/O e di validazione non vengono nascosti.

---

## 14. Unsaved guard e sostituzione progetto

### AC-REPLACE-001

L’unsaved guard appartiene al caso d’uso superiore (`requestOpenProject`, `requestNewProject`, `requestCloseProject`), non a `replaceProject`.

Flusso autorevole:

```text
resolvePendingEdits
    ↓
resolveUnsavedGuard
    ↓
load / migrate / validate candidate
    ↓
prepare candidate
    ↓
commitReplaceProject
```

### Regole

- I buffer validi vengono committati tramite Command prima dell’unsaved guard, se la policy lo prevede.
- I buffer invalidi o incompleti non vengono scartati silenziosamente.
- `replaceProject` riceve un documento già valido e non apre modali.
- La sostituzione distingue:
  - **Prepare:** può fallire e non modifica il progetto corrente.
  - **Commit:** breve, deterministico e progettato per non fallire.
  - **Post-commit:** ricostruisce proiezioni e UI; eventuali errori sono recuperabili senza ripristinare un dominio parzialmente sostituito.
- Il commit pulisce CommandStack, normalizza `EditorState`, riconcilia selection e scene view, invalida cache project-scoped ed emette `ProjectReplaced`.

---

## 15. Asset

### AC-ASSET-001

- Le definizioni authoring degli asset sono nel `ProjectDocument`.
- `AssetId`, `sourcePath` e metadata persistenti non sono duplicati in un catalogo mutabile separato.
- Importer, resolver, texture cache, thumbnail cache e indici sono derivati e ricostruibili.
- Cancellazione e rinomina asset devono rispettare i riferimenti.
- Il menu contestuale, il drag and drop e altre UI devono invocare la stessa operazione semantica.

---

## 16. Ownership e lifecycle

### AC-LIFE-001

Per ogni risorsa devono essere noti:

- creatore;
- owner;
- observer;
- momento di invalidazione;
- momento di distruzione;
- comportamento su close, replace e shutdown.

### Regole P0

- Preferire RAII.
- Un raw pointer è soltanto observer con lifetime garantito.
- Nessun callback cattura riferimenti locali o `this` senza scope sicuro.
- I listener RmlUi hanno un owner e cleanup deterministico.
- Gli `Element*` diventano invalidi dopo detach/close/unload/reload secondo il protocollo documentale.
- I listener ancora registrati non vengono distrutti mentre il documento può ancora richiamarli.
- I controller vengono staccati prima della distruzione dei documenti che osservano.
- I documenti vengono chiusi prima del contesto RmlUi.
- Le custom interfaces RmlUi restano vive fino al completamento dello shutdown RmlUi.
- Le risorse grafiche vengono rilasciate prima del contesto grafico da cui dipendono.
- Le callback Lua vengono rimosse prima di chiudere lo stato Lua.

### AC-LIFE-002 — Process-global resource rule (P0)

Ogni risorsa globale del backend — audio device, window, graphics context, input backend — possiede **un solo owner host per processo**. Le `GameplaySession`/`PlaySession` possono consumare tali risorse esclusivamente tramite servizi borrowed e non possono inizializzarle, reinizializzarle o distruggerle (nessuna `InitAudioDevice`/`CloseAudioDevice`/`InitWindow`/`CloseWindow` dentro la simulazione o i suoi moduli quando l'host ha già aperto la risorsa). L'ownership delle risorse *caricate* dalla sessione (Sound, Music, texture di sessione, cache runtime) resta invece locale alla sessione e viene sempre rilasciata allo Stop, indipendentemente dall'ownership del device sottostante.

Origine: incidente 2026-07-21 — doppia `InitAudioDevice()` al Start Play reinizializzava il singleton miniaudio sotto il worker thread WASAPI vivo (crash sistematico; vedi RU02 D-23).

### AC-LIFE-003 — Lifetime rule (P0)

Nessun modulo può conservare reference o pointer verso oggetti stack-locali o con lifetime più breve del modulo stesso. Ogni dipendenza conservata (es. `GameAPI` → `const EngineContext&`) deve avere ownership o lifetime esplicitamente garantiti dal composition root, e l'ordine di distruzione deve garantire che i consumatori vengano distrutti prima del provider (membri dichiarati in ordine tale che il context sia distrutto per ultimo, o shutdown esplicito prima del reset).

---

## 17. Stato operativo e capability

### AC-MODE-001

Lo stato dell’editor deve essere coerente e centralizzato.

Stati minimi:

- `NoProject`;
- `Edit`;
- `Play`;
- `Closing` quando necessario.

Stati transitori come `LoadingProject`, `StartingPlay` o `StoppingPlay` si introducono solo se l’operazione è asincrona, cancellabile o osservabile per più di una transizione atomica.

Le capability globali minime sono centralizzate. Le decisioni fini possono usare `canExecute(intent)` invece di moltiplicare booleani.

La UI disabilitata rappresenta la policy; il Coordinator deve comunque rifiutare operazioni vietate.

---

## 18. Concorrenza e job

### AC-THREAD-001

- `ProjectDocument`, `EditorState`, `CommandStack`, RmlUi e grafica Raylib vivono nel main thread.
- Nessun worker modifica il dominio.
- Nessun worker accede a `Rml::Element`.
- I worker restituiscono risultati immutabili.
- Un risultato che modifica dati persistenti viene convertito in Command sul main thread.
- Un risultato che aggiorna solo cache derivate può essere integrato senza Command, ma mai come nuova autorità.
- Ogni job project-scoped include almeno `ProjectSessionId` e versione/revisione della sorgente.
- Risultati obsoleti vengono scartati.
- Il job system si introduce solo quando esiste un carico reale che blocca il main thread.

---

## 19. Errori

### AC-ERR-001

- Operazioni realmente fallibili usano un risultato esplicito con codice, messaggio, contesto e remediation quando possibile.
- Non è obbligatorio usare `Result` per getter o operazioni infallibili.
- È vietato usare `bool`, `nullptr` o stringhe isolate per nascondere il motivo di un fallimento complesso.
- È vietato catturare tutto e continuare silenziosamente.
- Nessun errore lascia stato parziale.
- Le invarianti producono assert in debug e gestione sicura in release.
- `if (!x) return;` è ammesso solo quando l’assenza è prevista e documentata; non deve nascondere corruzione o violazioni di contratto.

---

## 20. Sicurezza

### AC-SEC-001

Sono non affidabili:

- file progetto;
- asset e font;
- path;
- clipboard;
- payload drag and drop;
- Lua e Logic Board;
- dati generati dall’AI;
- plugin futuri;
- eventuali archivi o contenuti esterni.

### Regole

- Validazione al confine e nel core.
- Rifiuto di `NaN`, infinito, valori fuori range e strutture oltre i limiti.
- Path canonicalizzati prima della verifica e confinati alla root prevista.
- Nessuna shell costruita con input utente.
- RML/RCSS forniti dal progetto non sono consentiti nell’MVP.
- Testo utente inserito come testo, non come markup arbitrario.
- Protezioni come Zip Slip o archive bomb diventano P0 quando la relativa superficie di import esiste; altrimenti il gate è N/A, non un’infrastruttura da implementare in anticipo.

---

## 21. Lua e Logic Board

### AC-LUA-001

- Lua è sandboxata tramite allowlist.
- Nessun accesso implicito a filesystem, shell, processi, rete, variabili ambiente o librerie native.
- Ogni board dichiara `apiVersion`.
- Versioni non supportate falliscono esplicitamente.
- Timer, subscription e callback hanno owner e token cancellabili.
- Ogni board possiede uno scope.
- Il dispatch è deterministico e usa snapshot quando la collezione può mutare.
- Gli errori di una callback non corrompono il dispatcher.
- Le board event-only non ricevono tick inutili.
- Il codice generato è deterministico e read-only.
- Il pannello Logic Board modifica definizioni authoring tramite Command.
- Devono esistere budget/limiti per istruzioni o tempo, memoria, ricorsione, eventi, timer, subscription e creazione entità, proporzionati alle capacità effettive del runtime.

---

## 22. Cache

### AC-CACHE-001

Ogni cache dichiara:

- autorità di origine;
- lifetime: application, project, scene o play;
- eventi di invalidazione;
- cleanup;
- modalità di ricostruzione.

### Regole

- Nessuna cache contiene l’unica copia di dati persistenti.
- Project replacement invalida cache project-scoped.
- Cambio scena invalida cache scene-scoped quando necessario.
- Stop Play invalida cache play-scoped.
- Una cache si introduce solo quando esiste un beneficio misurabile o un costo di ricostruzione reale.

---

## 23. Codice generato dall’AI

### AC-AI-001

Il codice AI è non affidabile finché non compreso, revisionato, compilato e testato.

Prima dell’integrazione devono essere verificati:

- autorità;
- mutazioni;
- invarianti;
- dipendenze;
- lifetime;
- Undo/Redo;
- Save/Load;
- Play/Stop;
- error path;
- test negativi;
- assenza di polling e sincronizzazioni nascoste;
- semplicità della soluzione.

È vietato accettare:

- API inventate;
- manager generici;
- binding bidirezionale al dominio;
- listener senza cleanup;
- fallback silenziosi;
- TODO nascosti;
- refactor massivi non necessari;
- test indeboliti;
- codice non compreso dal revisore.

---

## 24. Stop-the-line

Fermare immediatamente il lavoro quando appare:

1. mutazione diretta del documento dalla UI;
2. seconda autorità o copia sincronizzata;
3. Data Model RmlUi usato come store;
4. polling per allineare UI e core;
5. runtime che modifica l’authoring;
6. salvataggio non atomico;
7. migrazione incompatibile assente;
8. progetto corrotto convertito in progetto vuoto;
9. listener o callback senza owner;
10. uso di `Element*` dopo invalidazione;
11. raw pointer con ownership ambigua;
12. path non validato;
13. Lua non sandboxata;
14. errore critico ignorato;
15. operazione parzialmente applicata;
16. cache non ricostruibile;
17. worker che modifica il dominio;
18. risultato asincrono obsoleto applicato;
19. test rimossi o indeboliti;
20. complessità introdotta senza problema reale;
21. codice AI integrato senza review.

---

## 25. Regola definitiva

> In ArtCade RmlUi, il `ProjectDocument` è l’unica verità persistente; il `EditorCoordinator` orchestra le operazioni; i Command sono l’unico confine di mutazione authoring; la `PlaySession` è isolata; RmlUi è soltanto presentazione; ogni risorsa ha un lifetime esplicito; ogni nuova astrazione deve dimostrare di essere necessaria.

Nessuna scorciatoia locale e nessuna proposta successiva possono violare questa regola.
