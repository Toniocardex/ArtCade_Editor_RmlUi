# ADR-0001 — Tilemap Ownership Model

**Stato:** Adottata
**Contesto:** Tileset/Tilemap Editor, Slice 1 completata (commit `f4b1362` /
`1851e07c`), in preparazione della Slice 4 (`TilemapComponent` e storage)

## Modello legacy esistente

`vendor/artcade-runtime/src/core/types.h` definisce (Fase D2, commento
originale: "field names mirror editor TS"):

```cpp
struct TilesetSourceRef { std::string tilesetAssetId; };

struct TilemapData {
    float tileSize = 32.f;
    int   cols = 0, rows = 0;              // 0 = nessuna tilemap
    std::vector<int> data;                 // cols*rows, row-major
    std::vector<int> sourceIndices;        // parallelo a data, indice in tilesetSources
    std::vector<TilesetSourceRef> tilesetSources;   // supporto multi-tileset già presente
    std::string tilesetAssetId;            // legacy, solo migrazione
    std::string defaultTilesetAssetId;
};
```

`SceneDef` possiede `tilemap: TilemapData` (griglia unica, legacy) e
`tilemapLayers: unordered_map<layerId, TilemapData>` (una griglia per layer) —
un modello **scene/layer-keyed**, non entity-owned.

## Provenienza e stato attuale

Proviene dal vecchio editor TypeScript (pre-pivot RmlUi), da cui il commento
"editor TS" nel codice. Verificato con grep mirato in entrambi i repository:

- **`src/editor-native/` (artcade-editor): zero riferimenti.** Nessun comando,
  nessuna UI, nessuna serializzazione tocca `TilemapData`/`tilemapLayers` nel
  nuovo editor nativo. Da qui, non è un secondo sistema *ancora* attivo nel
  percorso authoring nuovo.
- **`ArtCade-Studio_V2/runtime-cpp` (il runtime condiviso): non morto —
  attivo lato runtime.** `World::activeTilemap_` (src/world/src/world.cpp)
  è un `TilemapData` vivo, usato per costruire il collision world e per il
  rendering (`src/app/render/tilemap-renderer.cpp`,
  `scene_frame_builder.cpp`, `scene_background_pass.cpp`); la
  serializzazione esiste in `src/core/scene-json.cpp`; `editor-api.cpp`
  espone l'integrazione per il vecchio editor.

**Conclusione della classificazione:** il modello legacy non è "codice morto
da rimuovere", è un **formato runtime funzionante senza più un produttore
authoring** nel nuovo editor. Non esiste oggi alcuna via per generarlo da
`artcade-editor`; esiste solo la via per consumarlo (world/rendering) e per
crearlo dal vecchio editor TS, ritirato.

## Dipendenze residue e presenza in vecchi file progetto

Non sono stati trovati file `.artcade-project`/JSON di esempio in nessuno dei
due repository con `tilemapLayers` popolato — la ricerca è stata limitata al
codice sorgente dei due repository sotto controllo; progetti reali degli
utenti (se esistenti altrove) non sono stati verificabili da qui e restano
un rischio di migrazione da riconfermare prima di rimuovere qualunque campo.

## Decisione

**Adottato: modello entity-owned.** Una `Tilemap Entity` possiede un
`TilemapComponent` (nuovo, Slice 4), che referenzia un `TilesetAssetId` (lo
`AssetId` già introdotto nella Slice 1). Un tileset per componente nell'MVP;
per background/gameplay/foreground si creano tre entità, ciascuna sul
proprio scene layer esistente — nessun secondo sistema di layer:

```text
BackgroundTilemap Entity → layer Background
GameplayTilemap Entity   → layer Gameplay
ForegroundTilemap Entity → layer Foreground
```

Motivazione: il modello Entity/Component è l'unico coerente con Hierarchy,
selezione, Inspector, Command/Intent, duplicazione (già presente per le
istanze via `CloneInstanceCommand`) e PlaySession del nuovo editor nativo.
Il modello legacy non viene scelto per **non** avere oggi un proprietario
operativo nel workflow authoring — non perché sia tecnicamente sbagliato.

**Non-decisione, esplicitamente aperta per la Slice 4/8:** se il runtime
compili `TilemapComponent` in un `TilemapData` (riusando
`tilemap-renderer.cpp`/`World::activeTilemap_` così come sono, coerente col
principio "formato authoring e formato runtime non devono coincidere",
sezione 20.3 della spec Tileset/Tilemap) oppure se il rendering runtime
venga esteso a leggere `TilemapComponent` direttamente. La Slice 1 e questo
ADR non impegnano quella scelta; la registrano come dipendenza nota per non
essere riscoperta a freddo.

## Strategia di migrazione

1. **Slice 4** introduce `TilemapComponent` (authoring-only, nessuna
   generazione automatica dal legacy).
2. Prima di rimuovere `tilemap`/`tilemapLayers` da `SceneDef`: riconfermare
   con l'utente se esistono progetti reali (non nei due repository) che li
   popolano. Se sì, serve una migrazione esplicita verso entità con
   `TilemapComponent`, non una rimozione diretta.
3. Se non esistono progetti da preservare: deprecare `tilemap`/
   `tilemapLayers`/`TilemapData`/`TilesetSourceRef` come campi legacy-only
   (commento `// legacy, rimuovere in schema vX`) e rimuoverli in una
   versione di schema successiva, non nella stessa slice che introduce
   `TilemapComponent`.
4. **Non mantenere due modelli attivi contemporaneamente** oltre il tempo
   strettamente necessario alla migrazione. Se `TilemapComponent` diventa
   authoritative e il vecchio modello resta solo come target di
   compilazione runtime (vedi non-decisione sopra), questo non conta come
   "due modelli attivi" nel senso vietato dai paletti — è un authoring
   singolo con un formato runtime derivato, esattamente come Tileset/Tile
   già distinguono dato authoring da dato runtime.

## Perché questo rispetta i paletti di ArtCade

Evita esattamente l'anti-pattern:

```text
SceneDef.tilemapLayers
+
Entity.TilemapComponent
```

come due ownership, due percorsi di serializzazione e due entry point
concorrenti per la stessa informazione — vietato da `RMLUI_MIGRATION_CONTRACT.md`
e dai principi 4.6 della spec Tileset/Tilemap ("nessuna sincronizzazione
nascosta", "se una rappresentazione può essere derivata, deve essere
ricostruita").
