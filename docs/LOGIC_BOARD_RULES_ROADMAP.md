# Logic Board — Roadmap per l'ampliamento delle regole

**Stato:** in corso

## Stato di avanzamento

| Slice | Stato | Evidenza |
|---|---|---|
| 0. Baseline e contract | Completata | Limiti e validazione core coperti dalla suite nativa. |
| 2A. Catalog foundation e picker | Completata | Registry descriptor-driven, compatibilitÃ  e picker RmlUi. |
| 2B. Personaggio controllabile | Completata | Input edge/held e intent riusati dai controller esistenti. |
| 2B.1 Platformer motion events | In corso | `platformer.is_falling` + `platformer.motion_state` (Moving/Stopped, ADR-0015) shipped; Rising/Airborne/edges restano pianificati. |
| 2B.2 Run Once per Activation | Completata | `LogicExecutionMode` su regola, gate rising-edge per Trigger Level, Command/UI/persistenza e test core+editor. |
| 2C. Collisioni e `EventOther` | Completata | Enter/exit deterministici, filtro per Object Type e `Destroy Self` differito. |
| 2D. Animazione e audio | Completata | Action animazione/audio complete in Editor Play e runtime esportato; parità host verificata nativa e WASM. |
| 2D.0A. Schema e migrazione ownership | Completata | Schema v4 type-owned, override sparse, promozione deterministica, round-trip e idempotenza. |
| 2D.0B. Resolver canonico Edit/Play | Completata | Un solo `resolveSpritePresentation`; Viewport, validazione e `PlaySession` consumano gli stessi valori risolti. |
| 2D.0C. Command, Inspector e Undo/Redo | Completata | Command distinti per authority, Undo esatto, badge ownership e Reset al default. |
| 2D.0D. Invarianti e cleanup legacy | Completata | Validator type-owned, guardie Logic Board, asset delete/Undo v4 e rimozione dei Command instance-owned legacy. |
| 2D.1. Action animazione | Completata | `animation.play_clip`, `animation.stop` e `animation.set_playback_speed` descriptor-driven, validate, compilate e mutate in PlaySession. |
| 2D.2. Action audio | Completata | `audio.play_sound` usa solo `AudioAssetDef` statici; la policy core Authoring/Executable rende i draft salvabili ma non eseguibili, e la cache Raylib play-scoped viene rilasciata su Stop. |
| 2D.X. Runtime Logic Host parity | Completata | Adapter non astratto, destroy differito con scope cleanup, animazione asset-scoped, audio risolto; target game nativo/WASM verdi. |
| 2D.3. Entity utilities | Completata | `entity.translate_by` (Move By) con editor offset e `entity.is_visible` come Event predicato; host `isVisible` in PlaySession/runtime. |
| 2A.1. Metadata semantici authoring | Completata | `logic-core` possiede semantic, constraint, opzioni e policy empty; la UI è una proiezione descriptor-driven verificata da contract test. |
| 2A.2. Conditions authoring | Completata | Add/Change/Remove/Move, AND/OR/NOT, proprietà generiche e Undo/Redo attraversano Intent/Command atomici. |
| 2E. Variabili | In corso | Foundation globale e blocchi Number/Toggle completati; override iniziali per istanza e famiglie Boolean/String complete restano pianificati. |
| 2E.0. Global variable authoring | Completata | Definizioni project-owned, rename atomico dei riferimenti, delete bloccato sui ref, type/value/description e Undo/Redo. |
| 2E.1. State Number + Toggle Boolean | Completata | `state.set/add/subtract/compare/toggle` percorrono descriptor, validator, compiler, runtime e UI generica. |
| 2F. Tempo e messaggi | In corso | Timer ripetuto e Wait completati; messaggi e payload restano backlog dedicato. |
| 2F.0. Timer authoring | Completata | `event.every_seconds` e `flow.wait` hanno proprietà generiche validate e lifetime nello scope Play. |

### Aggiornamenti trasversali completati

- Play avviato dalla Logic Board ora crea prima la `PlaySession` e solo dopo passa alla Scene runtime; Stop ripristina la stessa board, tab e ricerca. Una selezione manuale del workspace durante Play annulla il ritorno automatico.
- Il gameplay keyboard focus e la navigazione Play restano responsabilitÃ  di editor/coordinator; `PlaySession` rimane indipendente dalle tab RmlUi e dall'input host.
**Ambito:** editor RmlUi nativo e `vendor/artcade-runtime`; nessun bridge WASM o codice React.
**Precedenza:** `ARTCADE_RMLUI_ARCHITECTURE_CONSTITUTION.md` → `ARTCADE_RMLUI_ARCHITECTURE.md` → `ARTCADE_RMLUI_ENGINEERING_GATES.md`.

## 1. Decisione e risultato atteso

Il prossimo investimento è rendere la Logic Board estendibile e poi consegnare un primo set di regole che permetta un personaggio controllabile, interazioni di collisione, animazioni e suoni. Il visual polish non fa parte di questa roadmap.

Il risultato non è una nuova lista nella UI. L'unico catalogo canonico è il registry del modulo `logic-core` nel runtime. La UI lo legge per comporre i picker; `ProjectDocument` salva solo `typeId` stabile e proprietà tipizzate; il compiler produce il programma e le feature richieste; `PlaySession` e il runtime eseguono solo dati materializzati.

```text
descriptor canonico → Command → ProjectDocument → serializer/validator
       ↓                                            ↓
 picker RmlUi ← proiezione/read-only          compiler → PlaySession/runtime
```

Questa scelta elimina la duplicazione oggi presente fra il registry e i menu hard-coded del pannello, senza introdurre manager, target CMake, cache o job system aggiuntivi.

## 2. Contratti trasversali non negoziabili

| Area | Contratto |
|---|---|
| Autorità | `ProjectDocument` possiede `LogicBoardDef`; il registry è metadata statico e il runtime possiede soltanto lo stato Play. |
| Mutazione | Una selezione confermata nel picker emette un Intent e un solo `EditorCommand` atomico. Aprire, cercare, cambiare categoria o chiudere il picker non rende dirty. |
| Persistenza | Persistono esclusivamente ID e proprietà; mai label, categoria, icona, descrizione o Lua generato. |
| Compatibilità | I `typeId` pubblicati sono stabili. Una rinomina richiede alias esplicito o migration testata. |
| Validazione | Il core valida kind, proprietà, componenti richiesti, capability di contesto, asset e riferimenti prima della compilazione. Il runtime non corregge incompatibilità silenziosamente. |
| Play | La board compilata non legge `ProjectDocument`; le Action mutano solo il mondo della `PlaySession`, mai Raylib o l'authoring. |
| Determinismo | Condizioni e Action sono valutate nell'ordine persistito; le condizioni MVP sono in AND. |
| Lifecycle | Trigger, timer e subscription sono posseduti dallo scope della board e disposti su Stop, destroy dell'istanza o replace. |

### Compatibilità da fissare prima della prima nuova regola

La slice 2A non cambia il formato persistito: i board esistenti continuano a contenere `typeId + properties` e conservano lo schema corrente. Prima della 2B va documentata la policy API:

1. le feature di un programma restano la verifica puntuale primaria (`requiredFeatures`);
2. un cambio incompatibile dell'API Lua richiede aumento di `apiVersion` e migration/gestione esplicita delle board precedenti;
3. un type ID rimosso o rinominato richiede alias o migration, mai fallback a un blocco differente.

## 3. Sequenza di implementazione

### 0. Baseline e contract test

**Obiettivo:** congelare il comportamento attuale prima del refactor del catalogo.

- Inventariare i cinque type ID esistenti e aggiungere test di round-trip, compilazione e Undo/Redo per `On Start`, `Key Pressed`, `Set Visible`, `Set Position` e `Is Grounded`.
- Registrare i contratti di input: `Key Pressed` è edge-triggered; le nuove regole distingueranno esplicitamente edge e stato mantenuto.
- Verificare che `LogicProgram.requiredFeatures` sia controllato nel runtime che carica il programma e che un feature mancante impedisca l'avvio in modo diagnostico, non un'esecuzione parziale.
- Definire i limiti misurabili per board, regole, condizioni, action, callback/eventi per frame e subscription; riusare i limiti esistenti quando sono già adeguati.

**Uscita:** test verdi sul comportamento corrente e nota di compatibilità API approvata. Nessuna nuova regola utente.

### 2A. Catalog foundation e picker descriptor-driven

**Obiettivo:** ogni futuro blocco entra nel catalogo senza modificare la struttura del pannello.

**Core/runtime**

- Estendere `LogicBlockDescriptor` con `categoryId`, `description`, proprietà, `requiredComponents`, `requiredContext`, `requiredFeature` e `requiresTick`. I valori sono metadata immutabili del registry, non una seconda copia del documento.
- Introdurre alias tipizzati minimi (`LogicBlockTypeId`, `LogicCategoryId`) e `LogicContextCapability`: `Self`, `EventOther`, `DeltaTime`, `CollisionContact`, `MessagePayload`.
- Rendere `validateBoard` capace di ricevere il contesto authoring necessario per verificare componenti e capability; mantenere i controlli su kind e proprietà nel core. Il compiler usa i descriptor per raccogliere le feature richieste, senza una seconda tabella `if` nella UI.
- Conservare il formato `typeId + properties`. Non salvare metadata o Lua.

**Editor RmlUi**

- Sostituire le righe selezionabili hard-coded con un picker riusabile per Trigger, Condition e Action: categorie, ricerca, descrizione, tastiera, stato disabled e motivo dell'incompatibilità.
- Il picker mantiene soltanto stato visuale locale (aperto, query, categoria, indice focus). I risultati sono una proiezione read-only del registry più del contesto della regola.
- Collegare la conferma ai Command esistenti o a un unico Command di replace del blocco che conservi l'Undo esatto. Non introdurre Command per refresh, filtro o validazione UI.
- Le proprietà con pochi valori restano controlli in linea (`mode-option`); solo la scelta del tipo di blocco passa dal picker.

**Test e DoD**

- Test unitari per default descriptor, type ID duplicati, metadata incoerenti, incompatibilità di componente/capability e feature raccolte.
- Test dei Command per replace/add di ogni kind, no-op, Undo/Redo e board invalida senza mutazione.
- Contract test RmlUi: attach/detach, ricerca, navigazione tastiera, entry disabled, nessun dirty all'apertura e un solo Command alla conferma.
- Build nativa e test core; nessun nuovo comportamento runtime.

**Dipendenza sbloccata:** tutte le ondate successive.

### 2B. Personaggio controllabile

**Obiettivo:** dimostrare una Logic Board utile end-to-end senza esporre velocità fisiche che aggirino il `PlatformerController`.

| Kind | Blocchi |
|---|---|
| Trigger | `input.key_released`, `input.key_held` |
| Condition | `platformer.is_grounded` completato nel runtime |
| Action | `platformer.move_horizontal`, `platformer.jump` |

- `Key Released` è edge-triggered; `While Key Held` viene eseguito una volta per tick solo per le board che lo dichiarano (`requiresTick = true`).
- `Move Horizontal` accetta un asse finito e normalizzato/range-validato; compila in `platformer.requestMove(owner, axis)`. `Jump` compila in `platformer.requestJump(owner)`. Il controller conserva accelerazione, velocità massima, collisione, gravità e decisione grounded.
- I tre blocchi platformer richiedono `Self` e `PlatformerController`; il validator li rifiuta in authoring se il tipo proprietario ne è privo.
- Fissare l'ordine runtime: campionamento input → dispatch Logic Board → consumo degli intent da parte del controller → fisica. Il test usa `dt` esplicito.

**Demo/DoD:** A/D muovono, Space salta soltanto se grounded; rilascio tasto non genera azioni spurie; una board event-only non riceve tick. Coprire validazione, compiler, host, Play/Stop ripetuti e diagnostica con entity, board e rule.

### 2C. Collisioni e contesto `EventOther`

**Obiettivo:** consentire pickup/hazard senza rendere `Other` disponibile fuori da un evento che lo produce.

| Kind | Blocchi |
|---|---|
| Trigger | `collision.enter`, `collision.exit` |
| Condition | `collision.other_is_object_type` |
| Action | `entity.destroy_self` |

- Il runtime produce eventi di transizione deterministici con uno snapshot di contatto e `EventOther`; non passa puntatori live di fisica o UI.
- `Other Is Object Type` richiede `EventOther`. La stessa capability verrà riutilizzata in seguito da `Other Has Tag` e `Destroy Other`.
- La distruzione durante dispatch è differita/sicura: l'event loop termina su uno snapshot, poi applica la mutazione del mondo runtime. Ogni scope della board dell'entità distrutta viene cancellato.

**Demo/DoD:** collisione con un tipo specifico distrugge il pickup; `Other` non è selezionabile per `On Start` o input; enter/exit non duplicano eventi; destroy durante callback, Stop e Replace Project restano sicuri.

### 2D. Animazione e audio

**Obiettivo:** collegare il feedback del gameplay agli asset già authoring, senza incorporare editor di asset nella Logic Board.

#### 2D.0 — Prerequisito: ownership SpriteRenderer/SpriteAnimator

La capability completa e i default appartengono all'Object Type. Una
`SceneInstanceDef` persiste soltanto delta opzionali; campo assente significa
inherit. Il resolver canonico applica default type-owned → override sparse →
validazione → proiezione Edit/Play. I progetti v3 vengono promossi in ordine
canonico di Scene ID e ordine persistito delle istanze; i casi misti conservano
esattamente l'assenza tramite il solo flag migration-only `capabilityEnabled`.

- **2D.0A completata:** schema v4, codec, migrazione pura e idempotente, nessun
  blocco legacy risalvato sulle istanze.
- **2D.0B completata:** `resolveSpritePresentation` è l'unica precedenza usata
  da viewport, validator e materializzazione Play.
- **2D.0C completata:** Command separati per default Object Type e delta istanza,
  Undo/Redo esatto, Inspector con badge `OBJECT TYPE` / `INSTANCE OVERRIDE` e
  azione `Reset to Object Type`.
- **2D.0D completata:** la rimozione Animator è bloccata quando richiesta dalla
  board; i componenti completi v3 esistono solo come input temporaneo del decoder
  e della migrazione; i vecchi Command instance-owned e i relativi verb di
  `ProjectDocument` sono rimossi. Validator, cancellazione asset e Undo/Redo
  operano esclusivamente su default dell'Object Type e override sparsi. Coperti
  i casi negativi di override orfano, velocità non positiva e clip referenziata.

| Kind | Blocchi |
|---|---|
| Action | `animation.play_clip`, `animation.stop`, `animation.set_playback_speed`, `audio.play_sound` |

- Le proprietà persistono `animationAssetId`, `clipId` e `audioAssetId`, mai nomi o path. Le liste del picker sono proiezioni read-only del catalogo asset di `ProjectDocument`.
- Validare asset e clip esistenti, appartenenza dell'asset, tipo dell'asset e componente richiesto all'owner. Materializzare le risoluzioni nel runtime; nessuna query al documento durante Play.
- Definire comportamento esplicito per clip/suono cancellati o non risolvibili: il validator blocca Play con diagnostica; nessun fallback silenzioso.

**Demo/DoD:** salto → Jump + clip + suono; round-trip di tutti gli ID; asset/clip mancanti, replace e Stop non lasciano handle o callback residue.

#### 2D.2 — Chiusura gate authoring/audio

- `logic-core` è l'unica autorità della validazione: `ValidationMode::Authoring`
  classifica come warning soltanto una selezione asset esplicitamente vuota;
  ID non vuoti ma mancanti, asset Stream e valori invalidi restano errori.
- Command, Save e Load consumano la policy Authoring senza filtrare codici
  diagnostici; compiler, Play ed export usano sempre la policy Executable.
- Le conferme dei picker Add/Change attraversano Intent semantici e un solo
  Command atomico; Undo/Redo, revision e dirty restano quelli del Command.
- Il percorso coperto è lineare: selezione senza asset → modifica volume →
  Save/Load draft → import StaticSound → scelta asset → Save → Play.
- La cache `Sound` dell'Editor Play è posseduta dall'applicazione ma ha lifetime
  play-scoped: viene svuotata sia su Start sia su Stop e allo shutdown.

#### 2D.X — Runtime Logic Host parity (completata)

- `RuntimeLogicHostAdapter` è un boundary sottile e implementa `isObjectType`,
  `requestDestroy`, le tre operazioni Animation e `playSound`; una
  `static_assert` localizza immediatamente ogni futura divergenza
  dall'interfaccia condivisa.
- Il `World` materializzato è l'autorità per esistenza/tipo, destroy differito
  e stato animation. Le clip sono identificate dalla coppia stabile
  `(animationAssetId, clipId)` e la velocità è per-entità.
- L'Audio manager mantiene una proiezione runtime di `AudioAssetDef` e rifiuta
  entità inattive, asset mancanti/stream, path vuoti e volumi fuori range.
- La rimozione strutturale cancella lo scope Logic associato e pulisce lo stato
  animation senza invalidare il dispatch corrente.
- Verifiche: build `game` nativa e WASM; test host parity, animator, lifecycle
  destroy e smoke combinato Key Pressed → Is Grounded → Jump → Play Clip →
  Play Sound.

#### 2D.3 — Entity utilities (completata)

- `entity.is_visible` è un Event predicato (`on_update` + guard) con toggle
  Expected; riusa la feature `entity.visibility` e l'host `isVisible`.
- `entity.translate_by` (Move By) espone editor ΔX/ΔY su proprietà `offset`;
  la mutazione resta Command → `ProjectDocument` → host `translate`.
- Verifiche: compile/runtime tick, PlaySession Is Visible → Move By,
  `logic_board_editor_test` e `logic_board_test` verdi.

### 2B.1 Platformer motion events (pianificata)

**Obiettivo:** espandere la famiglia `platformer` oltre `is_grounded` con stati
verticali/orizzontali ben definiti e poche transizioni edge, senza introdurre
un generico `Is Moving` che si confonde con la caduta.

**Stato:** in corso. Shipped: `platformer.is_falling` (While) e
`platformer.motion_state` (Moving/Stopped, ADR-0015). Edge e Rising/Airborne
restano pianificati.

#### Contratti già veri oggi (non rompere)

| Fatto | Implicazione |
|---|---|
| `platformer.is_grounded` è un Event predicato (`on_update` + guard) | Ogni rule “Is Grounded → Play Sound” può sparare **ogni tick** mentre grounded. Gli edge servono per one-shot (SFX/VFX). |
| `RuntimePlatformerController` espone `grounded` + `verticalVelocity` (+Y giù) | Rising/Falling si derivano da `verticalVelocity`; non esiste ancora velocità orizzontale persistita. |
| Moto H = `axis * moveSpeed` nel frame | “Moving horizontally” va definito su **asse intent** o su **dx applicato**, con deadzone esplicita. |
| Jump gated nel controller (`jump && grounded`) | Non serve un predicato Logic solo per “puoi saltare”; i nuovi eventi sono per feedback/animazioni/rule authoring. |

#### Vocabolario congelato (naming)

| Concetto | Significato | Non significa |
|---|---|---|
| Grounded | contatto suolo questo step (`hitGround` / `grounded`) | “fermo” |
| Airborne | `!grounded` | “sta cadendo” |
| Rising | airborne e `verticalVelocity < -ε` (−Y = su) | “ha premuto Jump” |
| Falling | airborne e `verticalVelocity > +ε` | “non a terra” (include anche Rising) |
| Moving horizontally | `|axis|` o `|dx|` sopra deadzone | qualsiasi moto, inclusa caduta verticale |

**Vietato in v1:** typeId / label `Is Moving` senza qualifica. Se serve moto laterale,
il nome deve dire **Horizontally** (o “Has Horizontal Speed/Input”).

#### Tabella canonica — typeId / semantics / while-vs-edge / segnale

Priorità `P0` = prima implementazione utile; `P1` = subito dopo; `P2` = backlog
nella stessa famiglia; `—` = già shipped.

| Priorità | typeId stabile | Label UI | While / Edge | Semantics (invariante) | Segnale runtime | Note |
|---|---|---|---|---|---|---|
| — | `platformer.is_grounded` | Is Grounded | **While** | `grounded == true` | `RuntimePlatformerController::grounded` (host `isGrounded`) | Già shipped. Non rinominare il typeId. |
| — | `platformer.is_falling` | Is Falling | **While** | `!grounded && verticalVelocity > +ε` (+Y giù); export: anche `!climbing` | host `isFalling` / `World::isPlatformerFalling` | **Shipped.** ε = `0.001`. Predicato continuo (`on_update`); non confondere con Airborne. |
| — | `platformer.motion_state` | Platformer Motion | **While** | `Moving` ⇔ `|vx| > ε`; `Stopped` ⇔ negazione | host `isPlatformerMoving` / `World::isPlatformerMovingHorizontally` | **Shipped (ADR-0015).** ε = `0.01`. Un typeId, property `state` Moving\|Stopped. Walk/Idle = Motion + Is Grounded; OncePerActivation per edge clip. Non è intent input. |
| P0 | `platformer.landed` | Landed | **Edge** | transizione `!grounded → grounded` | latch per-entity: `wasGrounded` vs `grounded` post-fisica | One-shot SFX/clip atterraggio. Non duplicare con While Grounded. |
| P0 | `platformer.left_ground` | Left Ground | **Edge** | transizione `grounded → !grounded` | stesso latch | Copre salto e caduta da bordo; non è “Started Falling”. |
| P1 | `platformer.is_rising` | Is Rising | **While** | `!grounded && verticalVelocity < -ε` | idem | Clip di salita. All’apice (`|vy| ≤ ε`) nessuno dei due while è vero. |
| P1 | `platformer.is_airborne` | Is Airborne | **While** | `!grounded` | `grounded` | Umbrella. Preferire Falling/Rising quando la clip è diversa. |
| P1 | `platformer.started_falling` | Started Falling | **Edge** | entra in Falling da non-Falling | latch su predicato Falling | SFX “inizio caduta”; non sparare ogni frame di While Falling. |
| P2 | *(superseded)* | Is Moving Horizontally (intent) | — | — | — | Sostituito da `platformer.motion_state` (velocity). Un futuro blocco **Has Horizontal Input** resta distinto se serve la volontà del controller. |
| P2 | *(non in v1)* | Is Moving | — | — | — | **Esplicitamente fuori.** Troppo ambiguo vs Falling / Airborne. |

`ε` e deadzone orizzontale sono costanti di policy del modulo platformer/logic
(non proprietà authoring in v1), coperte da test con `dt` esplicito.

#### Ordine di dispatch (invariante Play)

```text
input sample → Logic Board dispatch (edge/while su stato *precedente* o latch
               aggiornato post-fisica del frame precedente)
             → consumo intent platformer
             → fisica / grounded / verticalVelocity
             → aggiorna latch edge per il frame successivo
```

Gli edge `landed` / `left_ground` / `started_falling` si valutano sul confronto
stato precedente vs stato post-fisica; i while usano lo stato corrente del
controller. Nessun reverse-sync verso `ProjectDocument`.

#### Requisiti trasversali (quando si implementa)

- Registry `logic-core` = unica autorità descriptor; UI solo proiezione.
- Capability: tutti richiedono `Self` + `PlatformerController` (come `is_grounded`).
- Host: eventuali `isFalling` / `isRising` / `isAirborne` / `isPlatformerMoving`
  restano query pure su stato materializzato; gli edge possono vivere come
  eventi emessi dal tick platformer, non come polling UI.
- Validator/compiler/PlaySession/export parity come le altre famiglie.
- Test DoD minimi: (1) Landed spara una volta all’atterraggio; (2) Is Falling
  falso durante Rising; (3) Left Ground su jump; (4) nessun typeId `is_moving`;
  (5) board senza PlatformerController rifiutata in authoring;
  (6) Platformer Motion OncePerActivation: Walk una volta su Stopped→Moving,
  Idle una volta su Moving→Stopped (ADR-0015).

#### Fuori scope di 2B.1

- Coyote time / jump buffer come blocchi Logic.
- “Is Jumping” basato sul tasto (è input, non stato moto).
- Rami OR, variabili, timer (restano 2E/2F).
- Esporre `verticalVelocity` grezza come property authoring.

### 2E. Variabili

**Obiettivo:** introdurre stato gameplay mutabile con ownership e scope espliciti. È una slice autonoma, non un'aggiunta al catalogo precedente.

**Ownership e policy approvate**

- `ProjectDocument` possiede le definizioni globali (`VariableId`, tipo, valore
  iniziale, descrizione). Il drawer mantiene soltanto lo stato visuale aperto/chiuso.
- `VariableId` è il riferimento stabile persistito dai blocchi. Rename aggiorna
  definizione e tutti i `LogicVariableReference` in un solo Command; Delete è
  bloccato finché esiste almeno un riferimento.
- Il tipo della definizione deve restare compatibile con ogni blocco referenziante.
  Un cambio tipo valido resetta il valore iniziale al default deterministico del
  nuovo tipo ed è interamente reversibile con Undo/Redo.
- `PlaySession` materializza una copia dei valori iniziali ed è l'unica autorità
  sui valori mutabili durante Play. Stop distrugge quella copia; non esiste
  reverse-sync verso l'authoring.
- Le definizioni sono project-global. Override iniziali per Entity instance non
  sono stati introdotti e richiedono una slice/schema dedicati.

**Completato:** modello/serializer, validator, materializzazione, host runtime,
Condition Number `state.compare`, Actions Number `state.set`, `state.add`,
`state.subtract`, Action Boolean `state.toggle`, picker filtrato per tipo,
property editor generico, Command e test.

**Rimane:** Condition Boolean/String e Action String complete, eventuali limiti
tipizzati e la decisione/schema per override iniziali di istanza. Nessuna modifica
runtime torna nell'authoring senza un futuro Command esplicito di Apply Runtime Changes.

**Demo/DoD corrente:** un blocco decrementa una variabile Number nella
PlaySession; Undo/Redo agisce sulle definizioni authoring, non sui valori runtime;
riferimenti mancanti e tipi incompatibili falliscono con diagnostica.

### 2F. Tempo e messaggi

| Kind | Blocchi |
|---|---|
| Trigger completato | `event.every_seconds` |
| Action completata | `flow.wait` |
| Backlog | `message.on_message`, `message.send_message` |

I timer completati usano `dt` del runtime e appartengono allo scope della board:
Stop, destroy e replace cancellano callback e continuazioni. Intervalli non finiti
o non positivi sono invalidi; la UI committa il valore soltanto tramite Command.
Messaggi, payload, ordering fra sender/receiver e limiti subscription richiedono
ancora un mini-contract separato.

### Policy Conditions e property editor (completata)

- Ogni Condition persiste come clausola ordinata con `joinBefore` e `negated`;
  la prima clausola usa sempre AND. NOT si applica alla singola clausola e AND
  ha precedenza su OR nella compilazione.
- Add/Change/Remove/Move e le modifiche AND/OR/NOT sono Command atomici con
  snapshot esatto per Undo/Redo. Dopo remove/move la prima clausola viene
  normalizzata ad AND.
- `logic-core` possiede kind, semantic, constraint numerici, opzioni enum e
  policy `allowEmpty`. RmlUi usa questi metadata per Bool, Number/Integer,
  String/reference, Vec2, enum e Key; i metadata non vengono persistiti.
- I target Self nascosti e le coppie atomiche Animation asset/clip non diventano
  authority UI: restano rispettivamente metadata core e Command dedicato.

## 4. Backlog esplicitamente non incluso nelle prime ondate

Restano fuori fino a una slice dedicata: `Other Has Tag`, `Destroy Other`,
raggruppamenti espliciti oltre la precedenza AND/OR già supportata, variabili
collection, messaggi con payload complesso e qualsiasi scripting utente. Possono
riusare foundation e capability già introdotte, ma non devono entrare come
blocchi solo visuali.

## 5. Gate obbligatori per ciascuna ondata

Prima di iniziare una slice, registrare: autorità, Intent/Command, invarianti, Undo/Redo, dirty/revision, schema/migration, failure path, owner/lifetime, comportamento Edit/Play, proiezioni RmlUi e test. La chiusura richiede:

- test unitari di registry, validation, compiler e runtime host;
- test Command/Undo/Redo, round-trip e invalid-data per ogni proprietà nuova;
- test di integrazione PlaySession e runtime materialization;
- contract/lifecycle del pannello e del picker RmlUi;
- test negativi su feature mancante, componenti/context incompatibili, asset/reference mancanti e callback/timer invalidati;
- build nativa e suite core pertinenti verdi.

Una famiglia è considerata completata solo quando percorre integralmente descriptor → authoring → Command → persistenza → validazione → compiler → runtime host → PlaySession → UI → test. Non si avvia la famiglia successiva con TODO o fallback nel percorso precedente.

## 6. Ordine operativo

```text
0 Baseline/compatibilità
        ↓
2A Catalog foundation + picker
        ↓
2B Input + platformer              → prima demo giocabile
        ↓
2C Collisioni + EventOther         → pickup/hazard
        ↓
2D Animazione + audio + parity     → feedback gameplay coerente con export
        ↓
2D.3 Entity utilities              → Translate / Is Visible
        ↓
2B.1 Platformer motion events      → Landed / Falling / … (dopo tabella approvata)
        ↓
2E Variabili                        → stato gameplay
        ↓
2F Tempo + messaggi                 → solo dopo contract dedicato
```

La priorità pratica resta foundation-first: nessun nuovo blocco platformer oltre
`is_grounded` finché 2B.1 non è approvata come tabella sopra. 2E/2F restano
dopo, salvo riprioritizzazione esplicita.
