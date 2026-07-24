# Integrazione in ArtCade

## Flusso autorevole

```text
RmlUi editor UI
  ↓ Intent/Command
ProjectDocument.GeneratedSfxDef
  ↓ snapshot immutabile della recipe
Sfx generation job
  ├─ SfxSynthesizer.render()
  ├─ preview temporanea, oppure
  └─ WavEncoder/VorbisEncoder
        ↓ risultato esplicito
Asset import/registration Command
        ↓
AudioAssetDef normale
        ↓
AudioManager / Logic Board
```

## Regola fondamentale

La UI non scrive direttamente file e non aggiorna direttamente il ProjectDocument.
Il job di export non possiede il documento e riceve una copia immutabile della recipe.

## Bozza di request applicativa

```cpp
struct GenerateSfxAssetRequest {
    artcade::sfx::GeneratedSfxDef definitionSnapshot;
    std::filesystem::path absoluteDestination;
    enum class Format { Wav, OggVorbis } format;
};

struct GenerateSfxAssetResult {
    std::string generatedSfxId;
    std::string outputAssetId;
    std::filesystem::path outputPath;
    bool success = false;
    std::string error;
};
```

Il completamento deve verificare che la recipe/revision da cui è partito il job sia
ancora quella attuale. Se è cambiata nel frattempo, il file può essere scartato o
marcato stale; non deve sovrascrivere lo stato più recente.

## Preview

- debounce consigliato: 80–120 ms;
- cancellare/ignorare risultati obsoleti mediante generation token;
- una sola preview posseduta dal `SfxPreviewService`;
- nessun `Sound` duplicato dentro componenti UI;
- Stop/Play sono intent workspace e non producono dirty.

## Persistenza

La recipe è persistente nel progetto. Stato pannello, preset selezionato, zoom della
waveform e preview corrente restano workspace/editor state.

## Dirty e Undo/Redo

Producono dirty e Undo/Redo:

- creazione/eliminazione GeneratedSfxDef;
- modifica recipe;
- rinomina;
- cambio del collegamento output asset.

Non producono dirty:

- preview;
- play/stop;
- selezione preset non applicata;
- zoom waveform;
- stato espanso delle sezioni UI.

## Output stale

Dopo `UpdateGeneratedSfxRecipeCommand`, l'output esistente è stale. Non rigenerarlo
silenziosamente. Mostrare un'azione esplicita `Regenerate` o generare nell'ambito di
un comando utente chiaramente identificato.

## Distribuzione OGG

Il package non incorpora binari Xiph. Se ArtCade distribuisce libogg/libvorbis:

- conservare i testi di licenza delle versioni effettivamente distribuite;
- inserirli in `THIRD_PARTY_NOTICES` o cartella equivalente;
- evitare di attribuire a Xiph endorsement del prodotto.
