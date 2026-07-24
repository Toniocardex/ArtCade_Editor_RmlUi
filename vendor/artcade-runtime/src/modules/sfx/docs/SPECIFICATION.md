# Specifica tecnica — ArtCade SFX Synthesizer v2

## 1. Obiettivo

Fornire ad ArtCade un dominio authoring per creare effetti sonori arcade senza
introdurre una seconda autorità sugli asset audio e senza collegare il DSP puro a
UI, filesystem, Raylib o processi esterni.

Il sintetizzatore produce audio derivato da una recipe persistente. Il file WAV o
OGG non è l'autorità authoring: è un artefatto rigenerabile.

## 2. Confini dei moduli

### `SfxSynthesizer`

Responsabilità:

- validare recipe e impostazioni di rendering;
- generare PCM mono normalizzato in `float`;
- garantire determinismo per recipe, seed, generatorVersion e sample rate;
- non accedere al filesystem;
- non conoscere Asset Manager, ProjectDocument, Raylib o UI.

### `WavEncoder`

Responsabilità:

- convertire `FloatAudioBuffer` in WAV mono PCM16;
- clipping controllato;
- normalizzazione opzionale;
- dither TPDF opzionale;
- scrittura mediante file temporaneo nella stessa directory.

### `VorbisEncoder`

Responsabilità:

- convertire direttamente il buffer in Ogg Vorbis;
- usare libogg/libvorbis/libvorbisenc;
- non invocare FFmpeg o shell;
- restituire errori strutturati.

### `RaylibPreview`

Responsabilità:

- adattare il buffer a una risorsa `Sound` Raylib;
- possedere e rilasciare la preview;
- non inizializzare o chiudere il dispositivo audio globale;
- essere usato sul thread proprietario delle risorse Raylib.

### `recipe_json`

Responsabilità:

- serializzare/deserializzare `GeneratedSfxDef`;
- preservare schemaVersion, generatorVersion e seed;
- non decidere migrazioni del ProjectDocument.

## 3. Modello authoring

```text
GeneratedSfxDef
├── schemaVersion
├── id
├── name
├── recipe
│   ├── schemaVersion
│   ├── generatorVersion
│   ├── durationSeconds
│   ├── masterGain
│   ├── amplitude ADSR
│   ├── primaryVoice
│   ├── secondaryVoice
│   ├── noise
│   ├── bitCrusher
│   ├── filter
│   └── randomSeed
├── outputAssetId
└── outputPath
```

Il `ProjectDocument` di ArtCade deve possedere il `GeneratedSfxDef`. L'Asset Manager
possiede il normale `AudioAssetDef` risultante. Il legame è espresso da
`outputAssetId`, non da scansioni del filesystem.

## 4. Versionamento

### `schemaVersion`

Versiona la forma serializzata della recipe.

### `generatorVersion`

Versiona il comportamento DSP. Cambi che possono alterare il PCM richiedono un
nuovo valore, ad esempio:

- modifica dell'ADSR;
- nuovo algoritmo oscillator;
- modifica del PRNG;
- cambio del DC blocker;
- modifica della quantizzazione;
- correzione che cambia intenzionalmente il risultato sonoro.

La v2 inclusa in questo pacchetto accetta soltanto `generatorVersion == 2`.
Migrazioni future devono essere esplicite; non bisogna reinterpretare silenziosamente
una recipe precedente.

## 5. Invarianti di validazione

- sample rate: 8.000–192.000 Hz;
- durata: finita e maggiore di zero;
- frame prodotti non oltre `maximumFrames`;
- `attack + decay + release <= duration`;
- sustain compreso tra 0 e 1;
- almeno una voce o noise abilitati;
- frequenze positive e inferiori a Nyquist;
- gain compresi tra 0 e 1;
- duty Pulse compreso tra 0,05 e 0,95;
- quantizzazione 2–16 bit;
- reductionRateHz compreso tra 0 e sampleRate;
- valori non finiti sempre rifiutati;
- schema/generator version non supportati sempre rifiutati.

Una richiesta non valida non produce un buffer parziale.

## 6. Semantica DSP

### Square e Pulse

- `Square`: duty fisso al 50%; i campi duty sono ignorati.
- `Pulse`: duty interpolato tra `dutyStart` e `dutyEnd`.

### Oscillator quality

- `Raw`: mantiene aliasing aggressivo, utile come scelta chiptune.
- `BandLimited`: usa PolyBLEP per square, pulse e saw.

### Pitch sweep

- `LinearHz`: interpolazione lineare in Hz.
- `Exponential`: interpolazione moltiplicativa, più naturale in ottave/semitoni.

### Noise

Il noise ha un clock indipendente. Il valore casuale cambia quando la fase del clock
supera un periodo; `clock.startHz` e `clock.endHz` controllano quindi realmente il
timbro e lo sweep dell'esplosione/hit.

### Bit crusher

`reductionRateHz` esprime la frequenza di aggiornamento del sample-and-hold, non un
numero di frame. La resa resta comparabile a 44,1 e 48 kHz.

### Catena di processamento

```text
Primary voice ─┐
Secondary voice├─ mix → ADSR/master gain → bit crusher → low-pass → DC blocker
Noise layer ───┘                                         → de-click → clamp
```

Il de-click finale porta esplicitamente primo e ultimo campione a zero.

## 7. Determinismo

A parità di:

- recipe completa;
- `randomSeed`;
- `generatorVersion`;
- sample rate;
- piattaforma floating-point equivalente;

il buffer prodotto deve essere identico.

Il PRNG è locale al render. Non usa stato globale, orologio, thread id o random_device.

## 8. Threading

Il core DSP non conserva stato tra chiamate ed è riutilizzabile da worker thread.

Raccomandazione ArtCade:

- render breve per preview: worker con debounce UI 80–120 ms;
- encoding e scrittura: job asincrono;
- commit nel ProjectDocument: soltanto tramite Command sul thread autorevole;
- creazione/distruzione `Sound` Raylib: thread richiesto dall'integrazione Raylib.

Il job non deve mutare direttamente il ProjectDocument.

## 9. Stato di generazione nell'editor

Stati suggeriti, derivati e non autorità parallela:

```text
UpToDate
RecipeModified
MissingOutput
Generating
GenerationFailed
```

Non introdurre polling del filesystem. Lo stato cambia in risposta a Command, esito
del job o DomainChange esplicito.

## 10. Command/Intent suggeriti

```text
CreateGeneratedSfxCommand
RenameGeneratedSfxCommand
UpdateGeneratedSfxRecipeCommand
DeleteGeneratedSfxCommand
RegenerateSfxAssetCommand
```

Il preview non modifica il documento. Può usare un intent/editor service:

```text
PreviewGeneratedSfxIntent
StopSfxPreviewIntent
```

`RegenerateSfxAssetCommand` può preparare la richiesta, ma l'encoding deve essere
eseguito come job. Il completamento rientra attraverso un risultato esplicito che
aggiorna output metadata/revision secondo il protocollo dell'engine.

## 11. Asset e naming

- `GeneratedSfxDef.name` deve rispettare la policy di nomi unici scelta da ArtCade;
- conflitti di path devono essere verificati case-insensitive su Windows;
- non sovrascrivere automaticamente un altro asset;
- opzioni UI: Replace, Generate unique name, Cancel;
- il normale audio runtime vede soltanto `AudioAssetDef`/`AudioResource`;
- Logic Board non deve conoscere il concetto di synth recipe.

## 12. Formati

### WAV

Formato di debug e preview persistente, nessuna dipendenza.

### Ogg Vorbis

Formato compresso predefinito consigliato per SFX finali. L'adapter usa direttamente
le librerie Xiph e resta fuori dal core DSP.

### MP3

Non incluso nella v2. Motivi:

- padding/ritardo meno adatti a SFX molto brevi;
- dipendenza e licenza encoder separate;
- nessun motivo per accoppiare MP3 al dominio di sintesi.

Un eventuale `Mp3Encoder` futuro deve implementare la stessa separazione degli altri
encoder e non cambiare `SfxSynthesizer`.

## 13. Test di accettazione inclusi

- determinismo con stesso seed;
- seed diverso modifica il noise;
- primo e ultimo campione a zero;
- ADSR incompatibile rifiutato;
- Square ignora duty;
- Pulse usa duty;
- DC blocker riduce offset medio;
- clock noise modifica il rate di aggiornamento;
- bit crusher comparabile tra sample rate;
- nessun NaN/infinito nei preset;
- limite massimo frame applicato;
- header WAV RIFF/WAVE valido.

## 14. Non-obiettivi della v2

- sintetizzatore musicale generale;
- grafo modulare;
- streaming audio runtime;
- generazione procedurale durante il gameplay;
- automazione timeline complessa;
- multi-canale/stereo;
- MP3 integrato nel core.
