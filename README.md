<p align="center"><img src="docs/logo.png" alt="Tonetrace" width="600"></p>

**Tonetrace** is a macOS VST3 effect: record a sample, see it as a spectrogram note map, draw the notes yourself and drag them out as MIDI. It runs on Apple Silicon and Intel (universal), in Ableton Live 12, FL Studio for Mac and other VST3 hosts. Quick start: install from the ZIP (`./install.sh`), rescan plug-ins, load Tonetrace on the audio track, press Record, play the sample, draw notes, drag MIDI into a MIDI track. Everything below is in German.

Tonetrace 4.4.1: ein Sample aufnehmen, das volle Spektrogramm lesen (horizontale Tonbänder, vertikale Drum-Streifen), die Noten frei als dünne Balken zeichnen und als MIDI in eine Spur ziehen, in Ableton Live auch als fertige Sampler-Spur.

![Tonetrace](docs/screenshot.png)

## Features

- **Volles Spektrogramm (4.3, Standard)**: die Ansicht **Spectrum** zeigt alle Energie des Samples, nicht nur Peaks: 8 Bins pro Halbton, Heat-Farbskala dunkel, blau/violett, rot, orange, gelb, weiß (starke Bereiche sättigen zu Weiß). Gehaltene Töne sind horizontale Bänder, Drums vertikale Streifen. **Lines** bleibt als Option (die Glow-Ansicht aus 4.2).
- **Freie Noten (4.3)**: Klick oder Ziehen zeichnet immer eine neue Note, standardmäßig auf das Raster 1/16 gerastet, Magnet aus. Verschieben und Längenänderung sind nie an Linien oder Onsets gebunden. Noten sind dünne türkis-grüne Balken (5 bis 6 px).
- **Breitformat-Oberfläche (4.4)**: 1280 × 720 (Minimum 1024 × 600), Kopfzeile mit Status, links Klaviatur und große Map, schmale rechte Leiste mit Drag MIDI unten; alles ohne Scrollen, die Zeitachse füllt standardmäßig die Breite (**Fit**). Sekundäre Schalter liegen im Optionen-Popover.
- **Halte-Erkennung (4.4)**: Quick Grab liefert wenige, lange Noten statt Fragmenten: gehaltene Töne laufen durch Drum-Schläge und Sidechain-Pumpen, der Bass-Grundton wird aus langen Fenstern bestimmt, Kick-Streifen werden nicht zu Noten. Messung in [`docs/ACCURACY.md`](docs/ACCURACY.md).
- **Engine MuScriptor (4.6, optional, Apple Silicon)**: **Engine: Classic / Neural / MuScriptor** im Optionen-Popover. MuScriptor ist ein großes Transkriptionsmodell (Transformer von Kyutai/Mirelo, über muscriptor.cpp lokal auf der GPU) und erkennt ganze Mixe mit allen Instrumenten **inklusive Drums** (GM-Drum-Noten, auf 1/16 verlängert). Das Modell (620 MB) lädt das Plugin beim ersten Auswählen einmalig von Hugging Face nach `~/Library/Tonetrace/models/` (SHA256-geprüft); bis dahin läuft Classic mit Hinweis. **Die Modellgewichte sind CC BY-NC 4.0 – nur nicht-kommerzielle Nutzung.**
- **Engine Neural (4.5, optional)**: im Optionen-Popover schaltet **Engine: Classic / Neural** Quick Grab auf ein trainiertes Transkriptionsmodell (Basic Pitch von Spotify, CoreML, läuft lokal auf der CPU). Es trennt eine Oktave über einem gehaltenen Bass deutlich besser als Classic und fragmentiert weniger, verliert aber auf dichten Mixes mit Drums. Standard bleibt **Classic**; ohne Modell oder ohne gespeichertes Audio (über 60 s) fällt Neural automatisch auf Classic zurück. Messung in [`docs/ACCURACY.md`](docs/ACCURACY.md).
- **New Sampler Track (4.3)**: in Ableton Live legt die **Tonetrace Bridge** (ein MIDI Remote Script) per Knopfdruck eine MIDI-Spur mit Simpler und einem Clip mit den Noten an.
- **Glühende Note-Map (4.2, jetzt Ansicht Lines)**: jeder Ton ist ein Heat-Streifen mit weiß-gelbem Kern und violettem bis rotem Schein auf fast schwarzem Grund, schmale Klaviatur, flache grüne Notenbalken, kompakte Waveform-Spur (abschaltbar mit **Wave**).
- **Präzise Note-Map (4.1)**: statt Halbton-Balken zeigt die Map pro Frame nur die spektralen Peaks als dünne, scharfe Linien auf schwarzem Grund, exakt auf ihrer Tonhöhe (Auflösung besser als ±5 ct), mit Waveform und Klaviatur, live während der Aufnahme (bis 120 s). Zeilenraster wie in einer Piano Roll.
- **Click-to-grab** (optional, Schalter **Grab on click**, Standard aus): ein Klick genau auf eine starke Linie legt eine Note über das ganze Liniensegment an.
- **Fundamentals**: Obertöne werden abgedunkelt, jede Note zeigt nur ihre Grundlinie. **Tuning / Align**: die Stimmung des Samples wird erkannt und das Halbtonraster danach ausgerichtet.
- **Snap** (1/16 Standard, 1/4 bis 1/32, Onset, Off) und Magnet (nur beim Zeichnen, Standard aus), `Alt` kehrt beides für eine Geste um, Hover-Label ("A4 −12 ct") und Ghost-Note.
- **Bearbeiten**: verschieben, Länge ändern, Lasso, duplizieren, löschen, Undo/Redo (mindestens 50 Schritte).
- **Hide drums** (Standard aus): dunkelt Drums im Spektrum auf ca. 30 % Helligkeit ab, ohne etwas zu entfernen, auch live.
- **Quick Grab** (optional): Startpunkt aus den stärksten Linien, danach normal editierbar.
- **Preview** mit eigenem Synth (16 Stimmen), Loop-Bereich, Sound/Mix/Solo und Sync zum Host-Transport.
- **MIDI-Export** per Drag & Drop oder **Save .mid** (SMF Typ 1, 960 PPQ, Aufnahme-Tempo).
- Sichtbare Knöpfe und Kontextmenü für Undo, Redo, Delete, Duplicate (Hosts fangen manche Tasten ab).
- **About-Overlay** (Klick auf das Logo) und **Diagnosepanel** mit Version, Host, DSP-Last, Log-Ordner und Berichtsfunktion.
- State speichert Spektrogramm, Audio, Noten und Parameter, Projekte aus Version 3 und 4.0 laden weiter (die Linien werden dabei im Hintergrund neu berechnet).
- Kein JUCE; Fremdcode nur das VST3 SDK (VST3-Hülle), das Basic-Pitch-Modell (Engine Neural, Apache-2.0) und muscriptor.cpp + ggml (Engine MuScriptor, MIT; beim Build geladen, Commit fest).

## Systemanforderungen

- macOS 12 oder neuer, Apple Silicon und Intel (universal)
- VST3-fähiger Host: Ableton Live 12, FL Studio (macOS) und andere VST3-Hosts

## Installation

Aus der ZIP `Tonetrace-4.4.1-macOS.zip` (selbst gebaut legt `./build.sh --release-zip` sie nach `dist/`, die fertige ZIP liegt auch unter `releases/`):

```bash
unzip Tonetrace-4.4.1-macOS.zip && cd Tonetrace-4.4.1
./install.sh
```

`install.sh` kopiert `Tonetrace.vst3` nach `~/Library/Audio/Plug-Ins/VST3`, signiert ad hoc, entfernt die Quarantäne und installiert die Live-Bridge nach `~/Music/Ableton/User Library/Remote Scripts/TonetraceBridge` (nur dieser Ordner, andere Remote Scripts bleiben unberührt).
Manuell: `Tonetrace.vst3` selbst nach `~/Library/Audio/Plug-Ins/VST3` kopieren.

**Gatekeeper:** Das Bundle ist nur ad hoc signiert und nicht notarisiert. Rechtsklick > Öffnen hilft bei Plug-ins nicht.
Stattdessen die Quarantäne-Markierung entfernen:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3
```

**Deinstallation:** `./uninstall.sh` (entfernt auch die Bridge; mit `--logs` zusätzlich den Log-Ordner), oder `~/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3` löschen.
Exportierte MIDI-Dateien in `~/Music/Tonetrace` bleiben erhalten.

## Einrichtung im Host

**Ableton Live:** Einstellungen > Plug-Ins > „VST3-Plug-In-Systemordner verwenden“ einschalten, neu scannen (Option-Taste beim
Klick erzwingt einen vollständigen Scan). Tonetrace erscheint unter Plug-Ins > VST3 und kommt auf die Audiospur mit dem Sample.
Das Audio läuft unverändert durch.

**FL Studio (macOS):** Options > Manage plugins > Find plugins (der Ordner `~/Library/Audio/Plug-Ins/VST3` wird durchsucht),
Tonetrace aktivieren und in einen Mixer-Slot laden, auf dem das Sample liegt.

## Live-Bridge einrichten (New Sampler Track)

Ein VST3-Plug-in kann in Live keine Spuren anlegen, ein MIDI Remote Script schon (auch in Live Intro). Die **Tonetrace Bridge** ist ein kleines Python-Skript
(nur Standardbibliothek). Das Plug-in schreibt Anfragen als JSON nach `~/Library/Application Support/Tonetrace/bridge/inbox/`, das Skript fragt den Ordner
etwa alle 250 ms ab, legt die Spur an und antwortet in `outbox/`. Zusätzlich schreibt es jede Sekunde `heartbeat.json`.

1. Installieren: `./install.sh` aus der ZIP (oder `./build.sh`) kopiert den Ordner `TonetraceBridge` nach `~/Music/Ableton/User Library/Remote Scripts/`.
   Von Hand: den Ordner `live-bridge/TonetraceBridge` dorthin kopieren.
2. Live **komplett beenden und neu starten** (Live liest Remote Scripts nur beim Start).
3. Live → Einstellungen → **Link, Tempo & MIDI** → in einer freien Zeile bei **Control Surface** den Eintrag **TonetraceBridge** wählen,
   **Input** und **Output** auf **None** lassen.
4. Im Plug-in links unten im Seitenpanel prüfen: **Live bridge: connected** (Heartbeat jünger als 3 s). Steht dort „not installed“, ist Schritt 2 oder 3 offen.
5. Noten zeichnen und **New Sampler Track** drücken (nimmt die gewählten Noten, sonst alle): am Ende der Spurliste entsteht eine MIDI-Spur „Tonetrace <Tonart>“
   mit **Simpler** und einem Clip im ersten Clip-Slot. Das Sample selbst in den Simpler ziehen. Mit **Sampler track on Drag** (Standard aus) passiert das
   automatisch nach jedem Drag MIDI.
6. Probleme: ohne Bridge zeigt ein Klick auf den Knopf eine Kurzanleitung. `tools/check_live_log.sh` zeigt die Zeilen von „TonetraceBridge“ aus Lives `Log.txt`.
   Fehler der Skript-Seite stehen als Banner im Plug-in. Ist Simpler nicht auffindbar, entsteht die Spur mit Clip trotzdem, mit Hinweis.

## Schnellstart

1. Tonetrace als Effekt auf die Spur mit dem Sample legen.
2. **Record** (roter Knopf) drücken und das Sample im Host abspielen. Nochmal drücken beendet die Aufnahme.
3. Helle horizontale Bänder sind Töne, vertikale Streifen sind Drums. Mit **Gain** und **Range** die Anzeige anpassen.
4. Die Noten selbst zeichnen: Klick-Ziehen entlang eines Bandes (oder ein Klick für eine Note mit der Snap-Länge).
5. **Drag MIDI** in eine MIDI-Spur ziehen (oder **Save .mid**), in Live alternativ **New Sampler Track**.

## Bedienung im Detail

- **Ansicht (4.3):** **Spectrum** (Standard) zeigt das volle Spektrogramm auf einem feinen Tonhöhenraster (8 Bins pro Halbton, Mehrfachauflösung: lange
  Fenster tief, kurze hoch), Pegel in dB, **Gain** und **Range** wirken darauf. Dezente Zeilentrennlinien, bei jedem C etwas heller. **Lines** zeigt wie in 4.2
  nur die getrackten Peaks als glühende Linien. Die Wahl liegt im Editor-State. Ohne fertige Analyse (z. B. gerade geladenes Projekt) erscheint kurz die Halbton-Heatmap.
- **Hide drums** und **Fundamentals** (beide Standard aus) dunkeln nur ab: Drums und Obertöne auf ca. 30 % Helligkeit, nichts wird entfernt.
- **Freies Zeichnen:** Klick-Ziehen auf beliebiger Fläche zeichnet eine Note in der Zeile unter dem Cursor, gerastet nach **Snap** (Standard 1/16). Ein einfacher
  Klick zeichnet eine Note mit der Snap-Länge. Noten sind dünne türkis-grüne Balken (5 bis 6 px), exakt mittig in der Zeile. **Magnet** (Standard aus) zieht
  beim Neuzeichnen die Tonhöhe auf die stärkste Linie innerhalb von ±1 Halbton; beim Verschieben wirkt er nie.
- **Grab on click** (Standard aus): nur wenn an, legt ein Klick genau auf eine starke Linie (±30 ct, höchstens 20 dB unter der stärksten) eine Note über das
  ganze Segment an. Mit Hover zeigt dann ein Label Note und Abweichung in Cent ("A4 −12 ct"), und die Ghost-Note zeigt das Segment.
- **Ghost-Note:** Vor dem Klick zeigt eine halbtransparente Vorschau mit Label ("C#4 · 1/16"), wo die Note landen würde.
- **Snap:** *1/16* (Standard) bis *1/4* und *1/32* rasten auf das Raster nach Host-BPM (auch beim MIDI-Export), *Onset* rastet neu gezeichnete Noten auf erkannte
  Onsets und Linienenden (±80 ms), *Off* ist frei. `Alt` kehrt Snap **und** Magnet für eine Geste um (Snap an → frei, Off → Raster 1/16; Magnet an ↔ aus).
  Verschieben und Längenänderung folgen nie Linien oder Onsets, nur dem Raster.
- **Tuning / Align:** Das Panel zeigt die erkannte Abweichung des Samples von A = 440 Hz ("Tuning +23 ct"). **Align** (Standard an) verschiebt das
  Halbtonraster um diesen Wert, damit die Töne mittig in den Zeilen liegen und Noten die richtige MIDI-Tonhöhe bekommen. **− / +** trimmt von
  Hand in 5-ct-Schritten (±50 ct). Der MIDI-Export und die Preview bleiben bei A = 440 Hz.
- **Bearbeiten:** Mitte ziehen verschiebt (alle gewählten Noten gemeinsam, auf jede Zeile und jede Zeit, auch ohne Energie), die Ränder (6 px) ändern die Länge, Doppelklick löscht,
  Rechtsklick öffnet das Menü. Klick wählt, `Shift`-Klick schaltet um, `Cmd`-Ziehen zieht ein Lasso.
- **Quick Grab:** Regler (0 bis 100 %) und **Grab** erzeugen Noten mit der Halte-Erkennung (siehe Changelog 4.4.0): Hysterese, Pump-Kompensation, Drum-Frames als "unbekannt", Bass-Grundton aus langen Fenstern, Mindestlänge 1/16 bei Host-BPM (sonst 120 ms), Schwelle relativ zum Oktavband; Drums werden immer herausgefiltert, unabhängig von *Hide drums*. Ohne Klick passiert nichts.
- **Wave** (Standard an): blendet die Waveform-Spur über der Map ein und aus; das Zeitlineal bleibt, die Map wächst nach oben. Der Zustand liegt im Editor-State und kommt beim nächsten Öffnen zurück.
- **Preview** (Leertaste) spielt Aufnahme und Noten ab der Startmarke im Loop. Im Waveform-Streifen setzt ein Klick den Start,
  Ziehen einen Loop-Bereich. **Sound** = Synth-Lautstärke, **Mix** = Balance Sample/Noten, **Solo** = Eingang stumm,
  **Sync** = Preview folgt dem Host-Transport.
- **MIDI:** Exportiert werden die gewählten Noten, sonst alle. Dateiname `Tonetrace <Tonart> <BPM>bpm.mid` in `~/Music/Tonetrace`,
  Velocity fest 100.
- **Reset** löscht Aufnahme und Noten (mit Rückfrage und Undo), **Clear notes** nur die Noten. **Key** und **BPM** zeigen
  Tonart und Aufnahme-Tempo. Oktaven nach wissenschaftlicher Notation (MIDI 69 = A4 = 440 Hz).

### Shortcuts

| Aktion | Eingabe |
|---|---|
| Note zeichnen | Klick-Ziehen oder Klick auf beliebiger Fläche |
| Snap und Magnet für eine Geste umkehren | `Alt` gedrückt halten |
| Note verschieben | Ziehen in der Mitte |
| Länge ändern | Ziehen am linken/rechten Rand |
| Note löschen | Doppelklick, Menü, Knopf Delete, `Backspace` / `Entf` |
| auswählen | Klick; `Shift`-Klick mehrfach; `Cmd`-Ziehen Lasso; `Esc` nichts wählen |
| alles wählen / duplizieren | `Cmd+A` / `Cmd+D` |
| Undo / Redo | `Cmd+Z` / `Cmd+Shift+Z` |
| Preview an/aus | `Leertaste` (Map fokussiert) |
| vertikal / horizontal scrollen | Mausrad / `Shift`+Mausrad, Trackpad, Scrollbalken |
| Zeitzoom (0,25x bis 8x) | `Cmd`+Mausrad, Pinch, `−`/`+` in der Seitenleiste |
| vertikaler Zoom | `Alt`+Mausrad |
| Ton vorhören | Klick auf die Klaviertaste |
| About-Overlay | Klick auf das Logo, schließen mit `Esc`, ✕ oder Klick daneben |
| Kontextmenü | Rechtsklick (oder Ctrl-Klick) |

**Hinweis zu Hosts:** Live fängt manche Tasten ab (Cmd+Z, Leertaste, Backspace). Dafür gibt es die Knöpfe Undo, Redo, Delete,
Duplicate (Optionen-Popover) und das Kontextmenü per Rechtsklick auf Note oder Map.

## Diagnose und Fehlerbehebung

- **Log:** `~/Library/Logs/Tonetrace/Tonetrace.log` (1 MB x 3 Dateien, geschrieben vom Hintergrund-Thread, nie vom Audio-Thread).
- **Diagnosepanel:** Knopf ⓘ im Panel oder im About-Overlay „Diagnostics“. Es zeigt Version/Build, Host, Sample-Rate, Blockgröße,
  DSP-Last, Overruns, Worker-Verzögerung, Aufnahmespeicher, Bildrate und den letzten Fehler. **Copy report**, **Save report…**
  und **Open log folder** helfen beim Melden von Problemen.
- **Live-Log:** `tools/check_live_log.sh` zeigt alle Tonetrace-Zeilen der neuesten Live-`Log.txt` (11/12), die Zeilen der Bridge (`TonetraceBridge`) und Warnungen.
- **Plug-in erscheint nicht:** Scan wiederholen, prüfen ob `~/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3` existiert, ggf. Quarantäne entfernen (siehe Installation).
- **Absturzberichte** liegen in `~/Library/Logs/DiagnosticReports`. `build.sh` bricht ab, wenn ein Lauf neue `tonetrace_*`-Berichte erzeugt.

## Aus dem Quellcode bauen

Voraussetzungen: Xcode (clang, codesign, iconutil) und CMake 3.20+. Das VST3 SDK (MIT, nicht im Repository) wird einmalig geklont:

```bash
mkdir -p external && git clone --depth 1 --branch v3.8.1_build_84 https://github.com/steinbergmedia/vst3sdk.git external/vst3sdk
cd external/vst3sdk
git submodule update --init --depth 1 base pluginterfaces public.sdk cmake
cd ../..
./build.sh
```

| Aufruf | Wirkung |
|---|---|
| `./build.sh` | Build, Unit-Tests, UI-Test, Benchmark, Installation, Signierung, validator, Hosttest, Screenshots, Absturzbericht-Prüfung |
| `./build.sh --no-install` | nur bauen, Unit-Tests, Bridge-Test und UI-Test |
| `./build.sh --release-zip` | wie `./build.sh`, danach `dist/Tonetrace-4.4.1-macOS.zip` |
| `tools/make_icon.sh` | rendert Logo-PNGs, `docs/logo.png` und `assets/logo/Tonetrace.icns` neu (sind eingecheckt) |

Die Version steht nur in `VERSION`, der Build-Hash (git) im About-Overlay und im Diagnosepanel.

## Projektstruktur

```
src/vst3/      VST3: Processor, Controller (Parameter, State), Entry/Factory, NSView-Editor
src/core/      Kernel (plattformneutral): Capture, Preview, Analyse-Thread, Notenmodell, Snapshot, Diagnose, Log
src/analysis/ml/  Engine MuScriptor: MuScriptorEngine (Registry, Notenumwandlung), MuScriptorTranscriber.mm (Download, Laden), muscriptor/ (C++23-Brücke arm64, Stub x86_64)
                  Engine Neural: Modell-Schnittstelle und Fensterung, Resampler, NoteDecoder, NeuralEngine, CoreML-Anbindung (NeuralTranscriber.mm)
src/analysis/  Live-Spektrum, Pitch-Lines (Mehrfachauflösung, Tracking, Onsets, Tuning), Waveform-Peaks, HPSS/Drum-Filter, Noten-Analyse, Quick Grab (Halte-Erkennung: SustainGrab, SustainRuns, LowBand, BeatHits), Tonart, FFT
src/capture/   vorab allokierter Aufnahmepuffer (120 s)
src/midi/      MIDI-Writer (SMF Typ 1) und Export-Planer
src/bridge/    Client der Live-Bridge: Anfrage-JSON, atomares Schreiben, Heartbeat, Antworten
live-bridge/   TonetraceBridge: das MIDI Remote Script für Ableton Live (Python, nur Standardbibliothek)
src/preview/   Preview-Synth, lock-freie Notenübergabe
src/state/     State-Codec (v4, liest v3), Editor-Einstellungen
src/render/    Glow-Look: Colormap-LUT und Gauss-Profile (reines C++, getestet)
src/ui/        Cocoa-Oberfläche (TTR*_v1): Map, Waveform, Piano, Controls, Logo (TTRLogo), About-Overlay, Diagnose
assets/ml/     Basic-Pitch-Modell (CoreML-Paket, wird beim Bauen zu nmp.mlmodelc kompiliert)
assets/logo/   Logo als SVG, gerenderte PNGs (png/) und Tonetrace.icns
tests/         Unit-Tests ohne Fremdbibliotheken, tests/bridge: Python-Test des Remote Scripts mit Mock der Live-API
tools/         Hosttest, UI-Test, Benchmark, Screenshot-Tool, Logo-Renderer, compare_v3 und accuracy (Genauigkeit), check_live_log.sh, Release-Dateien
docs/          Screenshots, Logo, PERFORMANCE.md, ACCURACY.md
```

## Performance

Gemessen mit `tonetrace_bench` (Release, universal, nativ), Details in `docs/PERFORMANCE.md`:

- Audio-Thread (48 kHz, 64 Frames): Idle 0,003 % Mittel, Record 0,007 %, Preview mit 16 Stimmen und Limiter 0,45 % Mittel, Spitze unter 2 % (Ziel unter 10 %). Keine Allokation, keine Locks.
- Speicher pro Instanz: leer wenige MB, 120 s Aufnahme bei 44,1 kHz ca. 40 MB (nach der Analyse ca. 103 MB, davon ca. 3,5 MB für das feine Spektrum), bei 192 kHz ca. 109 MB. Der Puffer wird erst beim Aufnehmen belegt.
- Analyse: live (Spektrum und Pitch-Lines) je Echtzeitfaktor 0,002, Endanalyse inklusive Pitch-Lines und feinem Spektrum von 60 s Audio in ca. 0,6 s, Quick Grab in unter 0,1 s. Editor: Frame mit 120 s Aufnahme unter 8 ms (gemessen ca. 5,5 ms, während der Aufnahme 6,4 ms; Spectrum-Ansicht, Fenster 1280 × 720).

## Bekannte Einschränkungen

- Ableton Live und FL Studio wurden bei der Entwicklung nicht gestartet: geprüft sind Hosttest, UI-Test und Validator.
- Der Editor funktioniert nur, wenn Processor und Controller im selben Prozess laufen (alle gängigen Mac-Hosts).
- Die Live-Trennung bei Hide drums kennt nur die Vergangenheit und weicht leicht vom Ergebnis nach dem Stoppen ab.
- Quick Grab trennt Halbtöne im Bass (C1 bis B2) mit langen Fenstern (bis 0,74 s); die Kanten tiefer Töne sind deshalb bis ca. 100 ms unschärfer. Bei Aufnahmen aus einem geladenen State ohne Audio (über 60 s) fehlt die lange Analyse, Quick Grab fällt im Bass auf die Auflösung von 4.3 zurück.
- Die Halte-Erkennung erreicht auf dichten echten Tracks nicht das Niveau des synthetischen Tests (Dauer-F1 der Techno-Mixes 0,74 bis 0,86, Ziel 0,85 in zwei von vier Mixes erreicht; Referenz-Track: F1-Bass in einem Stück, F2 und kurze Bassnoten fehlen): Pad-Töne unter Bass-Obertönen gehen verloren, Bass-Onsets kommen durch die langen Fenster bis ca. 100 ms zu spät. Reines weißes Rauschen erzeugt weiterhin einzelne Kurznoten (weniger als in 4.3). Details und Zahlen in `docs/ACCURACY.md`.
- Engine MuScriptor: nur Apple Silicon, nur gespeichertes Audio (bis 60 s) nach dem Stopp, braucht einmalig Internet für den Modell-Download (620 MB) und Speicher für das Modell (GPU). Die Modellgewichte sind CC BY-NC 4.0, also nur für nicht-kommerzielle Nutzung. Auf rein synthetischen Klängen (nackter Sägezahn, Sinus) ist das Modell unzuverlässig (Oktavfehler, Zerstückelung); auf echten Tracks liefert es auch Pad-Akkorde und Drums, die man ggf. löschen muss. Die kurzen Bassnoten des Referenz-Tracks (C2, B1) findet es nicht. Der erste Grab nach dem Start des Hosts dauert länger (Laden, Metal-Shader).
- Engine Neural: findet auf dem Referenz-Track 3 von 8 Zeilen (Abnahme 6), kurze Bassnoten (C2, A#1, B1) gar nicht; auf Mixes mit Drums (drums+melody, full mix) und bei Quick Grab 100 % liegt das F1 unter Classic. Sie analysiert nur das gespeicherte Audio (bis 60 s) und erst nach dem Stopp, nicht live. CoreML läuft bewusst nur auf der CPU (auf GPU / Neural Engine rechnet das Modell in float16 und meldet bei digitaler Stille alle Tasten).
- Die Linien werden nicht im State gespeichert: bei Aufnahmen über 60 s, deren Audio nicht im State liegt, zeigt die Map nach dem Laden die alte Halbton-Heatmap.
- Ein Oberton, der genau auf der Tonhöhe einer anderen Note liegt (z. B. eine Melodienote zwei Oktaven über dem Bass), wird mit Fundamentals abgedunkelt: dann Fundamentals ausschalten.
- Preview und MIDI bleiben bei A = 440 Hz, auch wenn das Raster um das erkannte Tuning verschoben ist.
- Neue Instanzen starten mit Snap 1/16, Magnet aus, Hide drums aus und der Ansicht Spectrum; gespeicherte Projekte aus 4.2 behalten ihre Werte (außer der Ansicht: sie öffnen im Spectrum).
- Die Bridge ist nur gegen einen Mock der Live-API getestet (Python-Unittest), nicht gegen ein laufendes Live: Namen im Live-Browser (Simpler), der Zeitpunkt des Gerätladens und die Note-API (`add_new_notes`, sonst `set_notes`) können sich je nach Live-Version unterscheiden.
- Im Spectrum sind Töne unter ca. 125 Hz durch das lange Fenster zeitlich verschmiert (wie ein Bassanschlag in jedem Spektrogramm).

## Changelog

### 4.6.0 – Engine MuScriptor (optional)

- **Erkennung:** dritte Quick-Grab-Engine **MuScriptor** (muscriptor.cpp, Modell `medium`): alle Instrumente eines Mixes inklusive Drums, Drum-Treffer als 1/16-Noten (nie in den nächsten Treffer derselben Trommel). Der Quick-Grab-Regler wirkt bei MuScriptor nicht (das Modell hat keine Schwelle). Am Referenzfenster 156,83 s: 4 von 8 Zeilen, F2 99 % (Classic 2/8, Neural 3/8); Messung in `docs/RESULT-4.5-erkennung.md`.
- **Modell:** Download (620 MB) beim ersten Auswählen der Engine im Hintergrund, SHA256-geprüft, nach `~/Library/Tonetrace/models/`; Fortschritt im Hinweis-Banner und im Diagnosepanel („MuScriptor engine“). Gewichte CC BY-NC 4.0 (nur nicht-kommerziell), nicht im Plugin enthalten.
- **Build:** muscriptor.cpp + ggml als arm64-Unterbuild (festes Commit), per `lipo` mit einem x86_64-Stub vereint; auf Intel-Macs meldet die Engine „needs Apple Silicon“ und Classic läuft. Neuer Test `tonetrace_mstest` (echtes Modell auf dem Testausschnitt von muscriptor.cpp: Plugin-Weg = direkter Aufruf, schneller als Echtzeit, Stille), übersprungen solange das Modell fehlt.
- **Robustheit:** Die Modellgewichte werden vor dem Abbau der Metal-Geräte von ggml freigegeben (sonst Absturz beim Beenden des Hosts). Editor-State bleibt v5, das Engine-Byte nimmt jetzt 0/1/2 auf.


### 4.5.0 – Engine Neural (optional)

- **Erkennung:** zweite Quick-Grab-Engine **Neural** mit dem Modell Basic Pitch (Spotify, Apache-2.0) über CoreML, im Optionen-Popover umschaltbar (**Engine: Classic / Neural**, Editor-State v5, ältere States laden mit Classic). Eigener Decoder (Hysterese, Onset-Spitzen, Brücken bis 1/8 Note), danach die Tonetrace-Regeln: Mindestlänge 1/16 bei Host-BPM, keine kurzen tiefen Noten auf dem Kick, der Quick-Grab-Regler setzt die Aktivierungsschwelle (0 % = 0,6, 100 % = 0,35).
- **Messung:** Referenz-Track F2 72 % statt 24 %, F1 in einem Stück, False Positives 5 statt 11; Techno-Mixes Note-F1 0,61 bis 0,75 statt 0,44 bis 0,60 und Fragmentierung höchstens 1,04; weißes Rauschen weniger Noten. Die Abnahme für den Standard (6 von 8 Zeilen, Dauer-F1 >= 0,85 in allen Techno-Mixes, alte Mixes >= 4.4.1) ist verfehlt, deshalb bleibt **Classic Standard**. Details in `docs/ACCURACY.md`.
- **Robustheit:** ohne Modell, bei CoreML-Fehlern oder ohne gespeichertes Audio läuft Classic, mit Hinweis-Banner und Eintrag im Log; das Diagnosepanel zeigt den Status der Engine. Das Modell lädt erst beim ersten Neural-Grab im Hintergrund-Thread, der Audio-Thread ist unverändert.
- **Tests:** 126 Unit-Tests (neu: Resampler, Decoder, Fensterung, Tempo- und Kick-Regeln, Rückfall auf Classic, Editor-State v5) und ein Modelltest mit dem echten Modell (`tonetrace_mltest`: F1 gehalten plus F2 mit eigenen Anschlägen, Stille, Echtzeitfaktor 0,015).

### 4.4.1 – Oktavnoten mit eigenen Anschlägen

- **Erkennung:** Ein Kandidat im Oberton-Abstand (Oktave, Quinte darüber usw.) zu einem stärkeren tieferen Ton wird nicht mehr automatisch als Oberton verworfen: setzt er nicht gemeinsam mit diesem ein und aus (Onset und Ende beide innerhalb von 60 ms), bleibt er eine eigene Note (Quick Grab, zweiter Durchlauf mit exakter Oberton-Subtraktion). Im Referenz-Track steigt die F2-Überdeckung von ca. 24 auf 36 %, das Ziel 70 % bleibt verfehlt. Gates, Rauschen und alte Mixes unverändert (F1 weiter >= 4.3 in allen Zeilen). Details in `docs/ACCURACY.md`.
- **Tests:** 115 Unit-Tests (neu: Oktavnote mit eigenen Anschlägen neben gehaltenem Bass).

### 4.4.0 – Halte-Erkennung, Breitformat-Oberfläche

- **Erkennung (Quick Grab):** neue Halte-Erkennung (`src/analysis/SustainGrab.cpp`).
  - *Hysterese* (aus bei T_on − 7,5 dB statt einer Schwelle) und *Median über 116 ms*: Töne flackern nicht mehr um die Schwelle.
  - *Pump-Kompensation*: Täler bis 20 dB beenden keinen Ton, ab 6 dB nur bei neuem Anschlag; Lücken bis 1/8 Note bei Host-BPM werden überbrückt (Sidechain-Dips von 6 bis 12 dB).
  - *Drum-Frames als unbekannt*: der Transienten-Gate und die HPSS-Maske schneiden gehaltene Töne nicht mehr an jedem Schlag ab; die Kick (Beat-Raster, Pulsprüfung) wird erkannt und nicht zur Note.
  - *Bass-Grundton* aus langen Fenstern (0,74 s für C1 bis B1, 0,37 s für C2 bis B2) und Oberton-Auslöschung mit Toleranz.
  - *Geisternoten- und Rausch-Filter*: schwache Stücke neben deutlich stärkeren Noten (Leckage, Unterharmonische) und Stücke ohne Spektralspitze (Rauschen, Hi-Hat-Reste) werden verworfen.
  - *Schwelle relativ zum Oktavband* (global begrenzt), *Mindestlänge* 1/16 Note bei Host-BPM (Fallback 120 ms).
  - Warum: 4.3 verlor gehaltene Bassnoten und zerschnitt Pads im Techno; Zahlen (Fragmentierung, Dauer-F1, alte Mixes, Referenz-Track) in `docs/ACCURACY.md`.
- **Oberfläche:** Breitformat 1280 × 720 (Minimum 1024 × 600), Kopfzeile (Logo, Status Key · BPM · Tuning · Notenzahl, Hilfe, Snap, Ansicht, Optionen, Info), links Klaviatur und große Map über die volle Höhe, rechts eine 200-px-Leiste (Gain, Range, Speed, Quick Grab, Record, Reset, Preview, Drag MIDI hervorgehoben, Save .mid, New Sampler Track, Undo/Redo). Sekundäre Schalter im Optionen-Popover. Standard-Zoom **Fit** (die ganze Aufnahme füllt die Breite), keine Statusleiste. Ruhige Optik: Graupalette und eine Akzentfarbe (Türkis), flache Buttons, 8-px-Raster.
- **State:** unverändert (Format und Parameter-IDs stabil, Projekte aus 4.2 und 4.3 laden); ältere Fenstergrößen unter dem neuen Minimum werden angehoben.
- **Tests:** 114 Unit-Tests (neu: Techno-Mixes, Fragmentierung, Bass-Auflösung, Hysterese, Kick-Puls, Mindestlänge, stabile Parameter-IDs), 12 Python-Tests, UI-Test mit Layout-Prüfung bei 1280 × 720 und 1024 × 600 (keine Überlappung, nichts außerhalb) und Popover.
- **Werkzeuge:** `tools/accuracy.sh` und `tools/accuracy.cpp` (Mixes, Fensterscan und Referenzvergleich).

### 4.3.0 – volles Spektrogramm, freie Noten, Sampler-Spur in Live

- **Ansicht Spectrum (Standard) / Lines:** volles, fein aufgelöstes Spektrogramm (8 Bins pro Halbton, Mehrfachauflösung, Interpolation, Heat-Skala bis Weiß), gekachelt und gecacht; Lines ist die Ansicht aus 4.2. Hide drums und Fundamentals sind in der Spectrum-Ansicht standardmäßig aus und dunkeln nur ab (ca. 30 %).
- **Freie Noten:** Standard-Snap 1/16 (Onset bleibt Option), Magnet standardmäßig aus und nur beim Neuzeichnen aktiv, `Alt` kehrt Snap und Magnet um. Verschieben und Längenänderung sind nie an Linien oder Onsets gebunden. Klick und Ziehen zeichnen immer eine Note; Click-to-grab nur mit dem neuen Schalter **Grab on click** (Standard aus). Notenbalken dünn (5 bis 6 px) und türkis-grün.
- **Erkennung wie v3:** Quick Grab nutzt die v3-Analyse (Salienz und Segmentierung auf dem harmonischen Anteil), Segmente ab 120 ms; die feine Tonhöhe dient nur der Cent-Bestimmung. Messbarer Vergleich mit v3 in `docs/ACCURACY.md` (F1 in jedem Mix mindestens so gut), Werkzeug `tools/compare_v3.sh`.
- **Live-Bridge:** neuer Ordner `live-bridge/TonetraceBridge` (MIDI Remote Script), Knopf **New Sampler Track**, Schalter **Sampler track on Drag**, Statusanzeige **Live bridge**, Datei-Protokoll in `~/Library/Application Support/Tonetrace/bridge`. `build.sh` und `install.sh` installieren die Bridge, `uninstall.sh` entfernt sie.
- **State:** Editor-State v4 (liest v1 bis v3), Projekte aus 4.2 laden unverändert (Spectrum-Ansicht, Onset und Magnet bleiben wie gespeichert).
- **Tests:** 106 Unit-Tests (neu: Standardwerte, freies Zeichnen und Verschieben in energielose Zeilen, 4.2-State, feines Spektrum, 120-ms-Filter, Bridge-Client), 12 Python-Tests für das Remote Script, UI-Test mit freiem Zeichnen und Verschieben in energielose Zeilen.

### 4.2.0 – glühende Note-Map

- Töne sind leuchtende Heat-Streifen: Kern ca. 2 px (weiß-gelb), gaussförmiger Schein bis ca. 8 px und ein dunkelroter/violetter Hauch um starke Linien, additiv überblendet. Farbskala schwarz, dunkelviolett, magenta, rot, orange, gelb, weiß. Die Helligkeit folgt dem Pegel jedes Frames (mit leichter Textur). Schwache Teiltöne und Obertöne (bei *Fundamentals*) sind blasse, dünne violett-blaue Linien. Gain und Range steuern Helligkeit und Kontrast.
- Das Glow-Rendering läuft in den gecachten Kachel-Bitmaps (vorberechnete Gauss-Profile, Colormap-LUT, 4 Subpixel-Phasen); der Audio-Thread ist unverändert.
- Zeilenraster fast schwarz: keine Streifen für schwarze Tasten, nur eine dezente Linie bei C und eine sehr leise zwischen E und F. Zeilen-Hover bleibt, dezenter.
- Klaviatur schmal (14 px Tastenstreifen, C-Labels links davon); die aktive Taste leuchtet türkis. Die Map ist 16 px breiter.
- Noten sind flache grüne Balken (#2FBF5B, 5 bis 7 px, ohne Rahmen), ausgewählte orange (#FF9F43), die Ghost-Note ein halbtransparenter Balken. Der Name erscheint nur bei Hover oder Auswahl neben dem Balken. Hover-Label und Segment-Outline mit dünner heller Linie.
- Waveform-Spur kompakt (44 px) in gedämpftem Türkis; neuer Toggle **Wave** im Optionen-Popover (Standard an, Editor-State v3, liest v1 und v2).
- Tests: 90 Unit-Tests (neu: Colormap-LUT, Glow-Profil, Wave-Toggle-State); Versionsprüfungen der Tools folgen der VERSION-Datei.

### 4.1.0 – präzise Note-Map

- Die Map zeigt dünne, scharfe Peak-Linien auf der exakten Tonhöhe statt ganzer Halbton-Balken (Peak-Map, Mehrfachauflösung, parabolische Interpolation, Peak-Tracking mit ±40 ct und Lücken bis 2 Frames). Die alte Heatmap bleibt als Rückfall für Aufnahmen ohne Linien.
- Neu: Click-to-grab, Ghost-Note, Hover-Label mit Cent-Abweichung, Zeilen-Hervorhebung und Tasten-Leuchten, Segment-Outline, kurzes Aufleuchten der Linie nach dem Anlegen.
- Neu: **Fundamentals** (Obertöne abdunkeln), **Tuning-Erkennung** mit **Align** und manuellem Trim (±50 ct), Snap-Modus **Onset** (Standard) mit eigener Onset-Erkennung (Spectral Flux, 6 ms Raster).
- Magnet und Quick Grab nutzen die Linien statt der Zeilenenergie. Gezeichnete Noten sind schmaler und mittig in der Zeile.
- Piano-Roll-Raster, neue Bedienelemente (Fundamentals, Align, Tuning) mit Tooltips und Statuszeile; Snap-Liste um *Onset* erweitert.
- State: Format unverändert (v4), Editor-State v2 (liest v1), Projekte aus 4.0 laden weiter.
- Tests: 84 Unit-Tests (neu: Tuning ±3 ct, Peak-Genauigkeit ≤ 5 ct, Halbton-Trennung E2/F2 und C5/C#5, Obertöne ≤ 25 %, Vibrato, Click-to-grab ±30 ms, Onset-Snap ±15 ms, Migration), UI-Test mit Hover-Label, Ghost und Click-to-grab.

### 4.0.0

Map & Draw: Live-Spektrogramm, Noten zeichnen, MIDI-Export, Preview, Diagnose (siehe oben).

## Lizenzhinweise

Das VST3 SDK von Steinberg steht unter der MIT-Lizenz und wird beim Bauen aus `external/vst3sdk` eingebunden.
Die Neural-Engine nutzt das Modell Basic Pitch von Spotify (Apache-2.0, `assets/ml/nmp.mlpackage`); Lizenztext und NOTICE stehen in `THIRD_PARTY_LICENSES.md`.
Die MuScriptor-Engine linkt muscriptor.cpp und ggml (MIT) sowie pffft (FFTPACK-Lizenz); die Modellgewichte (Kyutai/Mirelo) stehen unter CC BY-NC 4.0, werden nicht mitgeliefert, sondern beim ersten Gebrauch geladen und dürfen nur nicht-kommerziell genutzt werden. Details in `THIRD_PARTY_LICENSES.md`.
VST is a registered trademark of Steinberg Media Technologies GmbH.
