# ArtCade RmlUi — Architecture

**Ambito:** architettura tecnica di riferimento per l’editor nativo C++ con RmlUi, Raylib, runtime C++ e Lua  
**Documento superiore:** `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md`  
**Documento operativo:** `ARTCADE_RMLUI_ENGINEERING_GATES.md`  
**Uso:** design, implementazione, refactor, code review e onboarding

---

## 1. Principio guida

Questa architettura documenta e protegge la base già consolidata di ArtCade. Non è una prescrizione a creare immediatamente ogni classe, target o infrastruttura descritta.

La priorità è:

1. unica autorità;
2. mutazioni tracciabili;
3. Edit/Play isolati;
4. persistenza sicura;
5. lifetime espliciti;
6. semplicità.

I confini logici sono obbligatori. La loro materializzazione in target CMake, interfacce, servizi o classi separate deve essere proporzionata alla complessità reale.

---

## 2. Obiettivi

ArtCade deve essere:

- resistente a doppie autorità e sincronizzazioni nascoste;
- testabile senza avviare tutta l’applicazione;
- affidabile in Undo/Redo, Save/Load e migrazioni;
- isolato tra authoring e runtime;
- deterministico nel lifecycle di RmlUi, Raylib e Lua;
- sicuro verso file, asset, script e input non affidabili;
- portabile nel runtime/export Windows e HTML5/WASM;
- estendibile senza manager onniscienti;
- comprensibile anche dopo mesi di sviluppo;
- compatibile con sviluppo assistito dall’AI senza dipendere dalla sola disciplina manuale.

### Non-obiettivi

Non è obiettivo:

- replicare un’architettura enterprise;
- introdurre CQRS, event sourcing o dependency-injection framework;
- creare un’interfaccia per ogni classe;
- rendere asincrona ogni operazione;
- separare ogni cartella in una libreria;
- prevedere plugin, cloud o networking prima che siano richiesti.

---

## 3. Vista d’insieme

```text
┌──────────────────────────────────────────────┐
│               RmlUi Presentation             │
│ RML · RCSS · controller · input · modali     │
└──────────────────────┬───────────────────────┘
                       │ EditorIntent
┌──────────────────────▼───────────────────────┐
│             Editor Application Layer         │
│ Coordinator · CommandStack · policy · change │
└──────────────────────┬───────────────────────┘
                       │ EditorCommand
┌──────────────────────▼───────────────────────┐
│                  Editor Core                 │
│ ProjectDocument · EditorState · invarianti   │
└───────────────┬──────────────────┬───────────┘
                │                  │ materializzazione
       ┌────────▼────────┐ ┌───────▼───────────┐
       │ Persistence     │ │ Runtime Snapshot  │
       │ Serializer      │ │ → PlaySession     │
       │ Migration       │ │ Lua · Physics     │
       └────────┬────────┘ └───────┬───────────┘
                │                  │
       ┌────────▼──────────────────▼───────────┐
       │           Platform Adapters           │
       │ Filesystem · Raylib · OS · optional jobs│
       └───────────────────────────────────────┘
```

La UI osserva proiezioni. Il dominio non conosce la UI. Il runtime riceve una materializzazione isolata e non il documento mutabile.

---

## 4. Autorità e ownership applicativa

### 4.1 `ProjectDocument`

È l’unica autorità persistente e contiene:

- scene;
- layer e ordine dei layer;
- Object Type/prefab;
- Entity Instance;
- componenti authoring;
- definizioni asset;
- Logic Board;
- impostazioni persistenti del progetto;
- versione schema e metadata necessari alla persistenza.

Non contiene:

- selezione;
- pannelli aperti;
- zoom/pan;
- focus;
- texture caricate;
- stato Lua runtime;
- cache;
- puntatori UI;
- dati derivati ricostruibili.

### 4.2 `EditorState`

È l’autorità dello stato semantico del workspace:

- `activeSceneId`;
- selection;
- tool attivo;
- `EditorSceneViewState` per scena;
- pan e zoom;
- grid visibility;
- snap;
- grid size;
- active layer workspace;
- eventuali altri stati semantici non persistenti.

Non contiene copie di scene, entità o componenti.

### 4.3 `EditorUiState`

Contiene esclusivamente stato visuale:

- splitter;
- tab attiva;
- pannelli aperti;
- filtri;
- ricerca;
- sezioni espanse;
- visibilità console;
- posizione di finestre flottanti;
- scroll quando utile.

`EditorUiState` non è dominio. Può restare nel modulo applicativo esistente se è puro e indipendente da RmlUi; i dettagli specifici di documenti RML, `Element*`, classi RCSS o handle UI devono restare nel modulo RmlUi.

### 4.4 `PlaySession`

È l’unica autorità dello stato runtime durante Play. Possiede:

- mondo runtime;
- entità runtime;
- stato fisico;
- istanze Lua;
- timer e subscription runtime;
- snapshot degli asset necessari.

Non modifica il documento authoring.

### 4.5 Asset

Il catalogo persistente degli asset è parte del `ProjectDocument`.

Il sottosistema asset possiede soltanto elementi derivati:

- import pipeline;
- decoding;
- resolver;
- texture cache;
- thumbnail cache;
- placeholder;
- indici ricostruibili;
- diagnostica.

Non esiste un secondo `AssetCatalog` mutabile che replichi le definizioni del documento.

---

## 5. Invarianti del dominio

Il dominio deve proteggere almeno:

### Identità

- ID univoci per namespace;
- ID non derivati dai nomi;
- nomi modificabili senza cambiare identità;
- generazione ID centralizzata;
- import e duplicazione senza collisioni.

### Scene e layer

- `SceneDef.layers` è l’unica autorità dell’ordine;
- indice 0 rappresenta il layer più arretrato e l’ultimo il più avanzato, salvo esplicita revisione del modello;
- ogni scena possiede un `defaultLayerId` valido;
- ogni `EntityInstance.layerId` appartiene alla scena corretta;
- il layer attivo editoriale è workspace state, non autorità persistente separata;
- layer nascosti non devono diventare una copia persistente della visibilità runtime.

### Entità e Object Type

- ogni Entity Instance appartiene a una scena;
- ogni riferimento a Object Type è valido o esplicitamente mancante;
- nessun Object Type/prefab con nome duplicato quando la policy ArtCade lo vieta;
- un Object Type non contiene più movement driver incompatibili;
- componenti con valori numerici rispettano finitezza e range.

### Asset

- ogni `AssetId` punta a una definizione esistente o a uno stato mancante esplicito;
- una cancellazione definisce reject, cascade, null o placeholder;
- il path non è identità;
- la rinomina o lo spostamento non cambiano l’ID.

### Workspace

- `activeSceneId` è valido o assente;
- selection è valida o vuota;
- delete, undo, redo e replace riconciliano il workspace;
- stato workspace non rende dirty il progetto.

---

## 6. Confini dei moduli

### 6.1 Confini logici obbligatori

- **Editor Core:** modello e invarianti puri.
- **Editor Application:** Intent, Command, Coordinator, revisioni, casi d’uso.
- **RmlUi Presentation:** documenti, controller, input e rendering UI.
- **Scene View:** camera, snapshot, picking e overlay editoriale.
- **Runtime:** PlaySession e gameplay.
- **Persistence/Platform:** serializer, filesystem e adapter concreti.

### 6.2 Target minimi consigliati

La separazione iniziale minima può essere:

```text
artcade_editor_core
artcade_editor_ui_rml
artcade_runtime
artcade_platform_or_persistence
artcade_editor
```

Target ulteriori come `artcade_editor_application`, `artcade_scene_view`, `artcade_assets` o `artcade_persistence` sono introdotti quando:

- impediscono una dipendenza reale;
- permettono test o riuso sostanziale;
- riducono tempi di build o coupling;
- chiariscono ownership già complessa.

Non sono obbligatori solo perché compaiono nel diagramma concettuale.

### 6.3 Dipendenze vietate

- core → RmlUi;
- core → Raylib;
- core → filesystem concreto;
- runtime → RmlUi;
- runtime → `EditorState`;
- serializer → RmlUi;
- Panel Controller → filesystem concreto;
- Render Interface → `ProjectDocument`;
- Command → `Rml::Element`;
- cache → mutazione diretta del dominio.

---

## 7. Composition Root

Il Composition Root è l’unico luogo che crea e collega implementazioni concrete.

Può essere una classe semplice, senza framework DI:

```cpp
class EditorApplicationHost final {
public:
    Result<void> initialize();
    int run();
    void shutdown() noexcept;

private:
    // L'ordine reale deve riflettere i lifetime.
    PlatformServices platform_;
    ProjectStorage project_storage_;
    EditorCoordinator coordinator_;
    RmlUiHost rml_ui_;
    PanelRegistry panels_;
    SceneViewHost scene_view_;
};
```

L’esempio va adattato all’ordine reale di costruzione e distruzione C++. In alternativa, componenti inizializzati in fasi diverse possono essere posseduti tramite RAII/`unique_ptr`.

### Responsabilità

- creare servizi;
- collegare porte e adapter;
- inizializzare nell’ordine corretto;
- eseguire il main loop;
- avviare shutdown deterministico;
- non contenere logica di dominio.

### Vietato

- `GlobalServices`;
- singleton mutabili;
- `extern EditorCoordinator*`;
- accesso globale non controllato;
- creazione di repository o runtime dentro un pannello.

---

## 8. Accesso al `ProjectDocument`

Il Coordinator e le proiezioni autorizzate possono ottenere accesso const.

Le mutazioni devono essere confinate a:

- Command;
- deserialize che costruisce un nuovo candidato;
- migrazioni su rappresentazione/candidato separato;
- operazioni interne strettamente controllate.

Le primitive di patch possono essere private e accessibili ai Command tramite API dedicate o friendship limitata.

### Regole

- nessuna collection mutabile esposta ai pannelli;
- nessun puntatore a elementi interni conservato oltre l’operazione;
- nessuna mutazione “per forzare un refresh”;
- nessun workspace state persistito per comodità;
- invarianti protette nelle primitive di dominio e nei Command.

---

## 9. Intent e Command

### 9.1 Responsabilità dell’Intent

Un Intent rappresenta l’intenzione semantica dell’utente:

```text
RenameLayerIntent
DeleteEntityIntent
MoveAssetToFolderIntent
SetComponentValueIntent
StartPlayIntent
```

Può contenere:

- ID;
- valori già parsati quando possibile;
- contesto necessario;
- nessun puntatore UI.

### 9.2 Pipeline

```text
raw input
  ↓ parse UI
validated/normalized value
  ↓ Intent
Coordinator policy + resolve IDs
  ↓ Command
precondition check
  ↓ apply atomico
CommandStack
  ↓ reconcile workspace
EditorChange
  ↓ invalidate projections
```

### 9.3 Responsabilità del Command

Un Command:

- modifica il `ProjectDocument`;
- conserva lo stato necessario a Undo;
- verifica precondizioni al momento dell’esecuzione;
- non conosce la UI;
- non conosce filesystem concreto;
- non decide come aggiornare pannelli;
- restituisce changed/no-op/error e change set.

Esempio:

```cpp
struct CommandResult {
    bool changed = false;
    EditorChangeSet changes;
};
```

### 9.4 Composite Command

Usare un Composite Command solo per mutazioni authoring realmente indivisibili, ad esempio:

- duplicazione scena complessa;
- operazione batch;
- delete asset con riparazione authoring;
- futuro Apply Runtime Changes.

Non trasformare validazione, refresh o riconciliazione in Command separati.

Se più step possono fallire, usare:

- rollback degli step applicati; oppure
- applicazione su copia/staging e commit finale.

---

## 10. CommandStack, revisioni e dirty

Ogni entry conserva:

```cpp
struct CommandHistoryEntry {
    Revision revision_before;
    Revision revision_after;
    std::unique_ptr<EditorCommand> command;
};
```

### Semantica

- una revisione identifica uno stato;
- nuove revisioni sono monotone e non riutilizzate dopo branching;
- Undo ripristina `revision_before`;
- Redo ripristina `revision_after`;
- nuovo Command dopo Undo cancella Redo;
- no-op non crea entry;
- errore non crea entry;
- `isDirty = currentRevision != savedRevision`.

### Salvataggio asincrono o differito

Se un salvataggio parte dalla revisione `R`:

```text
snapshot at R
  ↓ write
success
  ↓ savedRevision = R
```

Se durante la scrittura il progetto raggiunge `R+1`, resta dirty.

---

## 11. Riconciliazione workspace

`EditorCoordinator::reconcileWorkspace()` o equivalente normalizza dopo mutazioni strutturali:

- active scene;
- selection;
- scene view states;
- active layer workspace;
- riferimenti a scene/entità eliminate;
- capability derivate.

La riconciliazione:

- non modifica dati persistenti salvo che sia parte esplicita del Command;
- non entra nello stack come Command separato;
- non legge RmlUi;
- è deterministica e testabile.

---

## 12. Editor Change e invalidazioni

Gli eventi descrivono cosa è cambiato, non quale pannello aggiornare.

Preferire tipi specifici:

```cpp
using EditorChange = std::variant<
    EntityCreated,
    EntityDeleted,
    EntityRenamed,
    LayerChanged,
    ComponentChanged,
    AssetDefinitionChanged,
    AssetRemoved,
    SceneCreated,
    SceneDeleted,
    ProjectReplaced
>;
```

Evitare eventi troppo generici come `EverythingChanged` o `AssetChanged` quando servono invalidazioni mirate.

`ProjectReplaced` è un evento applicativo/lifecycle e può vivere nello stesso change set se il nome generale è `EditorChange`.

### Mapping

```text
EntityRenamed
  → Hierarchy row
  → Inspector header

ComponentChanged
  → Inspector section
  → Scene overlay

ProjectReplaced
  → rebuild project-scoped projections
```

Il dominio non emette `RefreshInspector` o `RefreshHierarchy`.

---

## 13. Read Model e proiezioni

Usare proiezioni quando proteggono il dominio o semplificano realmente la vista.

Esempi:

```cpp
struct InspectorViewModel {
    std::optional<EntityId> selected_entity;
    std::string display_name;
    std::vector<ComponentSectionViewModel> components;
    bool authoring_enabled = false;
};
```

### Regole

- valori e ID, non puntatori mutabili;
- dimensione proporzionata alla vista;
- nessun mega-snapshot dell’intero progetto;
- invalidazione mirata;
- ricostruibile;
- nessuna persistenza;
- niente duplicazione di regole di risoluzione.

### API di lettura

Evitare un’unica `EditorReadSnapshot` onnisciente se porta ogni pannello a leggere tutto.

Preferire:

```cpp
InspectorViewModel buildInspectorView() const;
HierarchyViewModel buildHierarchyView() const;
ToolbarViewModel buildToolbarView() const;
```

oppure builder/funzioni equivalenti già coerenti con il repository.

Una lista banale può essere costruita localmente senza un nuovo livello astratto.

---

## 14. Architettura dei pannelli RmlUi

La complessità del pannello determina la struttura.

### Livello 1 — Pannello semplice

Un solo controller può:

- conservare riferimenti agli elementi validi;
- aggiornare la vista;
- registrare listener;
- emettere Intent;
- gestire cleanup.

Non è necessario separare `PanelView` e `PanelController`.

### Livello 2 — Pannello medio

Controller + ViewModel dedicato quando:

- aggrega più dati;
- ha buffer editabili;
- riceve invalidazioni mirate;
- presenta stati di validazione.

### Livello 3 — Pannello complesso

Separare View, Controller e porta applicativa quando il pannello, come Logic Board o Asset Browser, ha lifecycle, interazioni e test sufficientemente complessi.

### Contratto comune opzionale

```cpp
class IEditorPanel {
public:
    virtual ~IEditorPanel() = default;
    virtual Result<void> attach(Rml::ElementDocument&) = 0;
    virtual void detach() noexcept = 0;
};
```

Aggiungere metodi comuni solo se il `PanelRegistry` li usa realmente. Evitare interfacce gonfie.

---

## 15. Sessione documentale e listener RmlUi

Un controller collegato a un documento deve avere una sessione di lifecycle chiara.

La sessione può occuparsi di:

- verifica degli elementi obbligatori;
- registrazione listener;
- scope RAII;
- Data Model;
- invalidazione handle;
- detach idempotente.

### Protocollo preferito di chiusura

```text
sospendi nuovi eventi
  ↓
detach controller
  ↓
rimuovi listener mentre gli Element sono validi
  ↓
invalida handle locali
  ↓
Close document
  ↓
Context::Update / completamento distruzione differita
```

Se un documento viene chiuso prima del detach, listener e sessione devono restare vivi finché RmlUi può ancora richiamarli. È vietato distruggere listener ancora collegati nel periodo di distruzione differita.

### Regole

- nessuna lambda cattura riferimenti locali;
- catture di `this` solo dentro lifetime garantito;
- detach idempotente;
- reload invalida tutti i vecchi handle;
- project replacement non duplica listener;
- controller distrutti prima dei documenti osservati;
- custom interfaces vive fino al completamento dello shutdown RmlUi.

---

## 16. Data Model RmlUi

Il Data Model è un adapter di presentazione.

```text
ProjectDocument + EditorState
  ↓ projection
RmlUi Data Model
  ↓ render
```

Modifica:

```text
RmlUi event
  ↓ controller
Intent
  ↓ Command
ProjectDocument
```

### Regole

- binding one-way di default;
- niente binding bidirezionale diretto al dominio;
- niente riferimenti a container che possono essere riallocati;
- aggiornamenti programmatici non generano Command ricorsivi;
- project replacement ricrea i binding project-scoped;
- `DirtyAllVariables()` non viene chiamato a ogni frame;
- errori di binding vengono diagnosticati.

---

## 17. Buffer dei campi editabili

I campi possono attraversare stati temporanei non validi.

```cpp
enum class FieldValidity {
    Clean,
    Valid,
    Incomplete,
    Invalid
};

template<typename T>
struct FieldEditBuffer {
    std::string raw_text;
    std::optional<T> parsed_value;
    FieldValidity validity = FieldValidity::Clean;
    bool modified = false;
};
```

### Policy uniforme

- `input`: aggiorna il buffer;
- Enter: commit se valido;
- Escape: rollback;
- blur: commit o rollback secondo policy dichiarata;
- cambio selection: risoluzione obbligatoria;
- Start Play/Stop/Replace: risoluzione esplicita;
- valori invalidi non producono Command;
- aggiornamenti esterni non sovrascrivono silenziosamente un buffer modificato.

Esempi incompleti ammessi solo nel buffer:

- `-`;
- `.`;
- `12.`.

`NaN`, infinito e stringhe con suffissi non validi non entrano nel dominio.

---

## 18. Stato operativo e capability

### Stati minimi

```cpp
enum class EditorMode {
    NoProject,
    Edit,
    Play,
    Closing
};
```

Aggiungere `LoadingProject`, `StartingPlay` o `StoppingPlay` solo se l’operazione è asincrona, cancellabile o osservabile.

### Capability globali

```cpp
struct EditorCapabilities {
    bool can_edit_project = false;
    bool can_undo = false;
    bool can_redo = false;
    bool can_save = false;
    bool can_start_play = false;
    bool can_stop_play = false;
};
```

Per operazioni specifiche usare una policy centrale:

```cpp
bool canExecute(const EditorIntent&) const;
```

Toolbar, menu, shortcut, menu contestuali e DnD devono usare la stessa policy.

---

## 19. Input routing

Deve esistere una pipeline unica, anche se implementata con una classe semplice.

Responsabilità:

- raccogliere input fisico;
- inoltrarlo a RmlUi;
- rispettare focus e consumo;
- inoltrare alla Scene View quando appropriato;
- inoltrare al runtime durante Play secondo focus;
- gestire shortcut;
- evitare doppia elaborazione.

### Regole

- Delete non elimina entità durante l’editing di testo;
- Space e shortcut non attivano tool quando il campo ha focus;
- modali bloccano input sottostante;
- drag capture viene rilasciato su perdita focus;
- coordinate considerano DPI e scaling;
- un evento fisico non viene applicato due volte.

---

## 20. Scene View

La Scene View usa:

- una sola `Camera2D`/rappresentazione equivalente;
- una sola conversione world↔screen;
- una sola definizione condivisa di griglia;
- una sola funzione di snap;
- snapshot immutabili;
- stessa camera per render, picking, drag e overlay.

```text
ProjectDocument
  ↓ projection
SceneViewSnapshot
  ↓ render/picking
SceneViewController
  ↓ Intent
Coordinator
```

### Stato

- pan, zoom, grid, snap e grid size sono workspace state per scena;
- non rendono dirty il progetto;
- la griglia non viene renderizzata in Play;
- Snap può restare attivo anche con griglia nascosta;
- il Command riceve la posizione finale, non conosce la griglia.

### Regole

- draw non modifica il dominio;
- picking non usa una camera differente;
- cache scene-scoped ricostruibili;
- nessuna scansione completa del documento per frame se evitabile;
- overlay e clipping condividono la stessa trasformazione.

---

## 21. Render backend RmlUi + Raylib

Responsabilità:

- geometria;
- texture handle;
- clip/scissor;
- trasformazioni;
- stato grafico;
- cleanup.

### Regole

- distinguere texture possedute dalla UI e texture esterne;
- nessun double unload;
- handle validati;
- stato Raylib ripristinato dopo RmlUi;
- DPI, resize e clipping corretti;
- nessun accesso al `ProjectDocument`;
- nessuna logica di pannello;
- allocazioni per batch ridotte solo quando misurato necessario.

### Pipeline frame consigliata

```text
poll OS input
feed input to RmlUi/InputRouter
dispatch intents
update editor/runtime
integrate completed jobs
update projections / mark Data Model dirty
RmlUi Context::Update
BeginDrawing
render Scene View
restore graphics state
RmlUi Context::Render
EndDrawing
```

Le modifiche UI da visualizzare nello stesso frame devono avvenire prima di `Context::Update()`.

---

## 22. Drag and drop

Il payload è semantico e tipizzato.

Non contiene:

- puntatori;
- `Element*`;
- riferimenti a memoria temporanea;
- copie autorevoli.

Il drop rivalida:

- tipo;
- sorgente;
- destinazione;
- capability;
- esistenza degli ID;
- policy move/copy;
- stato corrente del progetto.

Menu contestuale e DnD chiamano la stessa operazione semantica. Il drag modifica soltanto preview temporanea finché il drop non produce Intent/Command.

---

## 23. Modali e pending edits

Le modali producono decisioni, non eseguono direttamente operazioni di dominio.

```cpp
enum class UnsavedDecision {
    Save,
    Discard,
    Cancel
};
```

### Pending edits

Prima di Open/New/Close:

1. risolvere i buffer;
2. se validi, committarli tramite Command secondo policy;
3. se invalidi/incompleti, bloccare, correggere o chiedere decisione;
4. solo dopo eseguire l’unsaved guard.

### Unsaved guard

- Save riuscito → continua;
- Save fallito → operazione annullata;
- Discard → continua senza salvare;
- Cancel → nessuna modifica.

La modale non chiama direttamente `replaceProject`.

---

## 24. Load, Open e Replace Project

### 24.1 Caso d’uso Open/New

```text
resolvePendingEdits
  ↓
resolveUnsavedGuard
  ↓
load candidate
  ↓
parse / schema check / migrate / validate
  ↓
prepare candidate resources
  ↓
commitReplaceProject
```

### 24.2 Prepare

Può fallire e non modifica il progetto corrente:

- lettura;
- parsing;
- verifica schema;
- migrazione;
- costruzione `ProjectDocument` candidato;
- validazione;
- eventuale preparazione di risorse in staging.

### 24.3 Commit

Breve e progettato per non fallire:

- blocca nuovi ingressi;
- ferma Play se necessario;
- incrementa/genera `ProjectSessionId`;
- invalida job e sessioni legate al vecchio progetto;
- scambia il documento;
- pulisce CommandStack;
- normalizza `EditorState`;
- riconcilia selection e scene view state;
- invalida cache project-scoped;
- emette `ProjectReplaced`.

### 24.4 Post-commit

- ricostruisce proiezioni;
- ricrea Data Model project-scoped;
- aggiorna pannelli;
- riabilita capability e input.

Errori UI post-commit devono produrre fallback/diagnostica, non una seconda transazione implicita sul dominio.

### 24.5 `replaceProject`

`replaceProject(ProjectDocument)` è un confine interno. Riceve un documento già valido. Non:

- mostra modali;
- legge filesystem;
- decide Save/Discard/Cancel;
- costruisce il candidato in modo incrementale sul documento corrente.

---

## 25. Save

```text
capture document snapshot at revision R
  ↓ validate
serialize in memory
  ↓ write secure temp
flush / verify
  ↓ atomic replace
success → savedRevision = R
```

### Regole

- snapshot coerente;
- file originale intatto fino al replace;
- fallimento non cambia `savedRevision`;
- modifiche successive a `R` restano dirty;
- nessun dato runtime serializzato;
- nessun dato letto dai widget;
- errori con contesto e remediation.

---

## 26. Runtime materialization

Il confine authoring/runtime deve essere esplicito ma semplice.

Tipi possibili:

```cpp
struct RuntimeProjectSnapshot;
struct RuntimeSceneSnapshot;
struct RuntimeEntityDef;
```

Funzione o componente:

```cpp
Result<RuntimeSceneSnapshot> materializeRuntime(
    const ProjectDocument& project,
    SceneId target_scene);
```

### Responsabilità

- risolvere componenti authoring;
- risolvere asset necessari;
- creare strutture adatte al runtime;
- validare prerequisiti;
- non conservare puntatori mutabili al documento.

Non è obbligatorio creare un nuovo target `game_model` se pochi tipi condivisi bastano. Il confine deve però essere visibile e testato.

---

## 27. PlaySession

### Start

```text
validate canPlay
  ↓ materialize runtime snapshot
create PlaySession
  ↓ enter Play
```

Se materializzazione o creazione falliscono, l’editor resta in Edit.

### Durante Play

- runtime usa solo strutture runtime;
- authoring Command bloccati;
- workspace può navigare senza influenzare la sessione;
- input runtime dipende dal focus viewport;
- errori Lua isolati e riportati.

### Stop

- blocca nuovi callback runtime;
- dispone timer/subscription;
- distrugge script e mondo runtime;
- pulisce cache play-scoped;
- ritorna in Edit mantenendo lo stato workspace corrente valido.

Stop durante callback deve essere gestito tramite una transizione sicura, non con distruzione immediata di oggetti ancora in uso.

---

## 28. Cache architecture

### Application-scoped

- font editor;
- icone integrate;
- risorse UI statiche.

### Project-scoped

- texture asset;
- thumbnail;
- resolver;
- proiezioni costose;
- compilazioni Logic Board cacheabili.

### Scene-scoped

- snapshot Scene View;
- picking acceleration;
- tilemap render cache;
- overlay cache.

### Play-scoped

- runtime asset snapshot;
- entity lookup;
- fisica;
- Lua instances.

Ogni cache dichiara autorità di origine, invalidazioni, cleanup e ricostruzione. Una cache viene introdotta quando esiste una ragione concreta, non per completezza teorica.

---

## 29. Thread model e job opzionali

### Main thread

- `ProjectDocument`;
- `EditorState`;
- CommandStack;
- RmlUi;
- Raylib graphics;
- integrazione finale dei risultati.

### Worker candidati

Solo quando misurato necessario:

- decoding CPU;
- thumbnail;
- scansione directory;
- export;
- compressione;
- compilazione Logic Board pesante;
- analisi progetto.

### Risultato asincrono

```cpp
struct ProjectJobStamp {
    ProjectSessionId project_session;
    Revision source_revision;
    JobId job_id;
};
```

Al completamento:

- scartare se `project_session` non coincide;
- scartare se la sorgente rilevante è cambiata;
- integrare sul main thread;
- usare Command per mutazioni persistenti;
- aggiornare direttamente solo cache derivate.

Il termine “controlled integration” non deve diventare una scappatoia per mutare il dominio fuori dai Command.

---

## 30. Error model

Per operazioni fallibili significative:

```cpp
enum class EditorErrorCode {
    InvalidInput,
    NotFound,
    Conflict,
    ProjectCorrupted,
    UnsupportedSchema,
    IoFailure,
    PermissionDenied,
    RuntimeFailure,
    UiContractViolation,
    InternalInvariantViolation
};

struct EditorError {
    EditorErrorCode code;
    std::string message;
    std::string technical_details;
    std::optional<std::string> remediation;
};
```

Usare un `Result<T>` equivalente per:

- I/O;
- parsing;
- migrazioni;
- Command fallibili;
- import;
- materializzazione runtime;
- caricamento RML.

Usare valori diretti o `optional` per getter semplici quando appropriato.

Separare messaggio user-facing e dettagli diagnostici, mantenendo un legame tra i due.

---

## 31. Security architecture

### Trust boundary

| Fonte | Fiducia |
|---|---|
| RML/RCSS integrati nell’app | affidabili come risorse del prodotto |
| RML/RCSS dal progetto | non consentiti nell’MVP |
| File progetto | non affidabile |
| Asset/font | non affidabile |
| Lua/Logic Board | non affidabile |
| Clipboard | non affidabile |
| Payload DnD | non affidabile |
| Plugin futuro | non affidabile |

### Misure

- path canonicalization e root confinement;
- parsing rigoroso;
- limiti dimensionali;
- decode controllato;
- escaping del testo;
- nessuna shell interpolation;
- nessuna esecuzione automatica di import;
- segreti fuori dal repository e dai log;
- protezioni specifiche attivate quando esiste la superficie: archivi, plugin, updater, rete.

---

## 32. Lua e Logic Runtime

```text
Logic Board definition
  ↓ compiler deterministico
Lua generated source
  ↓ sandboxed runtime
Logic API allowlist
  ↓ Runtime World
```

### Contratti

- `apiVersion` obbligatoria;
- `context.owner` preferito agli ID hard-coded;
- scope per board;
- token cancellabili;
- callback isolate;
- dispatch deterministico;
- snapshot dispatch quando i listener possono mutare;
- tick solo se richiesto;
- helper feature-gated;
- codice generato read-only.

### Budget anti-DoS

Definire limiti proporzionati al runtime:

- istruzioni o tempo per callback/tick;
- memoria Lua;
- profondità callback/eventi;
- eventi per frame;
- timer e subscription per board;
- spawn di entità per frame;
- ricorsione.

Il superamento disabilita o interrompe in modo controllato la board/callback e produce diagnostica, senza bloccare l’editor.

---

## 33. Ownership matrix

| Risorsa | Owner | Observer | Distruzione/invalidazione |
|---|---|---|---|
| `ProjectDocument` | `EditorCoordinator` | proiezioni const | close/replace |
| `CommandStack` | `EditorCoordinator` | capability | close/replace |
| `EditorState` | `EditorCoordinator` | UI/read models | sessione editor |
| `PlaySession` | `EditorCoordinator` | runtime view | Stop/replace/shutdown |
| `Rml::Context` | `RmlUiHost` | panel host | shutdown RmlUi |
| `ElementDocument` | document/panel host | controller | panel close/reload |
| Panel controller | panel registry/host | nessuno | detach prima del documento |
| UI listener | subscription scope | RmlUi | detach/distruzione sicura |
| Asset definitions | `ProjectDocument` | asset views/resolver | replace/Command |
| Texture asset | texture cache | snapshots | cache invalidation |
| Texture UI | render interface | RmlUi | shutdown UI |
| Lua state | `PlaySession`/runtime | bindings | Stop/shutdown |
| Background job | job owner | completion queue | cancel/join/discard stale |

La tabella va aggiornata quando si introduce un sottosistema con lifetime non ovvio.

---

## 34. Struttura cartelle consigliata

La struttura può essere adottata gradualmente:

```text
src/
├── app/
│   ├── composition_root.*
│   └── main.cpp
├── editor/
│   ├── core/
│   │   ├── project_document.*
│   │   ├── editor_state.*
│   │   ├── model/
│   │   ├── ids/
│   │   └── invariants/
│   ├── application/
│   │   ├── editor_coordinator.*
│   │   ├── intents/
│   │   ├── commands/
│   │   ├── changes/
│   │   └── read_models/
│   ├── ui_rml/
│   │   ├── rml_ui_host.*
│   │   ├── panels/
│   │   ├── document_session.*
│   │   └── input_router.*
│   └── scene_view/
├── runtime/
├── persistence/
├── assets/
├── platform/
└── shared/

resources/
├── rml/
├── rcss/
├── fonts/
└── icons/
```

Non spostare file solo per imitare la struttura se il refactor non produce un beneficio concreto.

---

## 35. CMake e controlli architetturali

### Regole

- dipendenze dichiarate con visibilità minima;
- header pubblici minimali;
- include path che impediscano accessi accidentali;
- core senza link RmlUi/Raylib/OS;
- runtime senza RmlUi;
- persistence senza RmlUi;
- nessun ciclo tra target.

### Controlli possibili

- target graph CMake;
- script CI sugli include;
- clang-tidy;
- dependency graph;
- grep strutturali mirati.

Separare un nuovo target solo se il controllo del confine giustifica il costo di build e manutenzione.

---

## 36. Regole di estensione

Prima del codice, una nuova feature definisce:

- autorità;
- dati persistenti e workspace;
- invarianti;
- Intent e Command;
- Undo/Redo;
- dirty/revision;
- persistenza/migrazione;
- comportamento Play;
- change set;
- proiezione UI necessaria;
- lifecycle;
- error path;
- test;
- sicurezza e limiti pertinenti.

### Test di necessità delle astrazioni

Prima di aggiungere un manager, service, target, cache, job o interfaccia chiedere:

1. quale problema concreto risolve;
2. quale rischio impedisce;
3. perché una funzione o classe già esistente non basta;
4. chi lo possiede;
5. se crea una copia dello stato;
6. come viene rimosso se non serve più.

---

## 37. ADR

Serve un ADR per decisioni che cambiano realmente la base:

- nuova autorità;
- modifica del formato progetto;
- nuova policy Play;
- plugin system;
- hot reload RML/Lua come feature supportata;
- multi-threading del core;
- più contesti RmlUi;
- telemetria;
- updater;
- sistema temi estendibile da progetti;
- Apply Runtime Changes;
- nuova dipendenza core sostanziale.

Non serve un ADR per ogni piccola classe, funzione o refactor locale.

---

## 38. Anti-pattern vietati

- `UIManager` onnisciente;
- `GlobalServices`;
- `SyncManager`;
- doppio catalogo asset;
- `LayerManager` che duplica `SceneDef.layers`;
- binding bidirezionale al dominio;
- polling del documento;
- serializer usato per change detection;
- `RefreshAll()` come soluzione ordinaria;
- mega-snapshot dell’intero editor;
- runtime che legge continuamente il documento;
- `Element*` usato dopo lifecycle invalido;
- worker che modifica il dominio;
- path concatenati e non validati;
- RML costruito con input non escapato;
- transazioni e servizi creati per operazioni banali;
- interfacce senza un confine o consumer reale;
- stati transitori non osservabili;
- job system introdotto senza blocchi misurati.

---

## 39. Glossario autorevole

- **ProjectDocument:** autorità persistente authoring.
- **EditorState:** stato semantico del workspace.
- **EditorUiState:** stato esclusivamente visuale e non persistente nel progetto.
- **EditorIntent:** richiesta semantica proveniente dalla UI o Scene View.
- **EditorCommand:** mutazione authoring atomica e annullabile.
- **EditorChange:** descrizione tipizzata di un cambiamento applicativo/domain.
- **Read Model/ViewModel:** proiezione ricostruibile destinata a una vista.
- **PlaySession:** stato runtime isolato.
- **Runtime Snapshot:** materializzazione immutabile o indipendente del progetto per il runtime.
- **Cache:** dato derivato ricostruibile.
- **Pending Edit:** valore UI non ancora committato.
- **ProjectSessionId:** identità della sessione di progetto aperta, usata per scartare risultati asincroni obsoleti.

---

## 40. Criterio finale

L’architettura è corretta quando:

- il `ProjectDocument` resta l’unica autorità persistente;
- ogni mutazione authoring è rintracciabile in un Command;
- il runtime non può contaminare l’editor;
- Save/Load/Replace non possono perdere il progetto corrente;
- cache, ViewModel e Data Model possono essere ricostruiti;
- chiudere o ricaricare pannelli non lascia listener o texture residue;
- il dominio può essere testato senza RmlUi e Raylib;
- una feature semplice rimane semplice;
- una feature complessa aggiunge struttura soltanto nella misura necessaria.
