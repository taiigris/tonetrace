<p align="center"><img src="docs/logo.png" alt="Tonetrace" width="600"></p>

**Tonetrace** is a macOS VST3 effect: record a sample, see it as a spectrogram note map, draw the notes yourself and drag them out as MIDI. It runs on Apple Silicon and Intel (universal), in Ableton Live 12, FL Studio for Mac and other VST3 hosts. Quick start: install from the ZIP (`./install.sh`), rescan plug-ins, load Tonetrace on the audio track, press Record, play the sample, draw notes, drag MIDI into a MIDI track. Everything below is in German.

Tonetrace 4.2.0: ein Sample aufnehmen, jeden Ton als glühenden Streifen auf seiner exakten Tonhöhe sehen, per Klick auf die Linie als Note übernehmen und als MIDI in eine Spur ziehen.

![Tonetrace](docs/screenshot.png)

## Features

- **Glühende Note-Map (4.2)**: jeder Ton ist ein Heat-Streifen mit weiß-gelbem Kern und violettem bis rotem Schein auf fast schwarzem Grund, schmale Klaviatur, flache grüne Notenbalken, kompakte Waveform-Spur (abschaltbar mit **Wave**).
- **Präzise Note-Map (4.1)**: statt Halbton-Balken zeigt die Map pro Frame nur die spektralen Peaks als dünne, scharfe Linien auf schwarzem Grund, exakt auf ihrer Tonhöhe (Auflösung besser als ±5 ct), mit Waveform und Klaviatur, live während der Aufnahme (bis 120 s). Zeilenraster wie in einer Piano Roll.
- **Click-to-grab**: ein Klick auf eine Linie legt eine Note über das ganze Liniensegment an. Ziehen zeichnet weiter von Hand.
- **Fundamentals**: Obertöne werden abgedunkelt, jede Note zeigt nur ihre Grundlinie. **Tuning / Align**: die Stimmung des Samples wird erkannt und das Halbtonraster danach ausgerichtet.
- **Snap** (Onset, 1/4 bis 1/32, Off) und Magnet (rastet auf die stärkste Linie ein), Hover-Label ("A4 −12 ct") und Ghost-Note.
- **Bearbeiten**: verschieben, Länge ändern, Lasso, duplizieren, löschen, Undo/Redo (mindestens 50 Schritte).
- **Hide drums**: zeigt nur den harmonischen Anteil, auch live während der Aufnahme.
- **Quick Grab** (optional): Startpunkt aus den stärksten Linien, danach normal editierbar.
- **Preview** mit eigenem Synth (16 Stimmen), Loop-Bereich, Sound/Mix/Solo und Sync zum Host-Transport.
- **MIDI-Export** per Drag & Drop oder **Save .mid** (SMF Typ 1, 960 PPQ, Aufnahme-Tempo).
- Sichtbare Knöpfe und Kontextmenü für Undo, Redo, Delete, Duplicate (Hosts fangen manche Tasten ab).
- **About-Overlay** (Klick auf das Logo) und **Diagnosepanel** mit Version, Host, DSP-Last, Log-Ordner und Berichtsfunktion.
- State speichert Spektrogramm, Audio, Noten und Parameter, Projekte aus Version 3 und 4.0 laden weiter (die Linien werden dabei im Hintergrund neu berechnet).
- Kein JUCE und kein Fremdcode außer dem VST3 SDK (nur die VST3-Hülle nutzt es).

## Systemanforderungen

- macOS 12 oder neuer, Apple Silicon und Intel (universal)
- VST3-fähiger Host: Ableton Live 12, FL Studio (macOS) und andere VST3-Hosts

## Installation

Aus der ZIP [`releases/Tonetrace-4.2.0-macOS.zip`](releases/Tonetrace-4.2.0-macOS.zip) (SHA-256 in [`releases/SHA256SUMS.txt`](releases/SHA256SUMS.txt); selbst gebaut legt `./build.sh --release-zip` sie nach `dist/`):

```bash
unzip Tonetrace-4.1.0-macOS.zip && cd Tonetrace-4.1.0
./install.sh
```

`install.sh` kopiert `Tonetrace.vst3` nach `~/Library/Audio/Plug-Ins/VST3`, signiert ad hoc und entfernt die Quarantäne.
Manuell: `Tonetrace.vst3` selbst nach `~/Library/Audio/Plug-Ins/VST3` kopieren.

**Gatekeeper:** Das Bundle ist nur ad hoc signiert und nicht notarisiert. Rechtsklick > Öffnen hilft bei Plug-ins nicht.
Stattdessen die Quarantäne-Markierung entfernen:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3
```

**Deinstallation:** `./uninstall.sh` (mit `--logs` auch den Log-Ordner), oder `~/Library/Audio/Plug-Ins/VST3/Tonetrace.vst3` löschen.
Exportierte MIDI-Dateien in `~/Music/Tonetrace` bleiben erhalten.

## Einrichtung im Host

**Ableton Live:** Einstellungen > Plug-Ins > „VST3-Plug-In-Systemordner verwenden“ einschalten, neu scannen (Option-Taste beim
Klick erzwingt einen vollständigen Scan). Tonetrace erscheint unter Plug-Ins > VST3 und kommt auf die Audiospur mit dem Sample.
Das Audio läuft unverändert durch.

**FL Studio (macOS):** Options > Manage plugins > Find plugins (der Ordner `~/Library/Audio/Plug-Ins/VST3` wird durchsucht),
Tonetrace aktivieren und in einen Mixer-Slot laden, auf dem das Sample liegt.

## Schnellstart

1. Tonetrace als Effekt auf die Spur mit dem Sample legen.
2. **Record** (roter Knopf) drücken und das Sample im Host abspielen. Nochmal drücken beendet die Aufnahme.
3. Helle Linien sind Töne, Obertöne sind dunkel. Mit **Gain** und **Range** die Anzeige anpassen.
4. Auf eine Linie klicken: die Note entsteht über das ganze Segment. Oder per Klick-Ziehen selbst zeichnen.
5. **Drag MIDI** in eine MIDI-Spur ziehen (oder **Save .mid**).

## Bedienung im Detail

- **Linien lesen:** Jede Linie ist ein Peak des Spektrums (Helligkeit = Pegel, **Gain** und **Range** wirken darauf). Dezente Zeilen wie in einer
  Piano Roll: Zeilen der schwarzen Tasten dunkler, C-Zeilen heller. Der Rest bleibt schwarz.
- **Hover:** Die Zeile unter dem Cursor wird über die ganze Breite hervorgehoben, die passende Taste der Klaviatur leuchtet, und ein Label am Cursor
  nennt Note und Abweichung der Linie in Cent ("A4 −12 ct"; ohne Linie nur den Zeilennamen). Über einer Linie wird das ganze zusammenhängende
  Segment umrandet.
- **Click-to-grab:** Ein einfacher Klick auf eine Linie (ohne Ziehen) legt eine Note über das Segment an: Anfang und Ende des Segments, Zeile der Linie.
  Die Linie leuchtet kurz auf, Undo nimmt es zurück. Ein zweiter Klick auf dieselbe Stelle wählt die Note aus.
- **Ghost-Note:** Vor dem Klick zeigt eine halbtransparente Vorschau mit Label ("C#4 · 1/8"), wo die Note landen würde: auf einer Linie das ganze
  Segment, sonst eine kurze Note auf der Zeile unter dem Cursor.
- **Zeichnen:** Klick-Ziehen auf leerer Fläche, die Note entsteht in der Zeile unter dem Cursor. Gezeichnete Noten sind schmaler als die Zeile und
  genau mittig. **Magnet** zieht die Tonhöhe auf die stärkste Linie innerhalb von ±1 Halbton (Grundlinien zählen voll, Obertöne wenig).
- **Snap:** *Onset* (Standard) rastet Anfang und Ende auf erkannte Onsets und Linienenden ein (±80 ms), *1/4* bis *1/32* auf das Raster nach Host-BPM
  (auch beim MIDI-Export), *Off* ist frei. `Alt` = vorübergehend ohne Snap.
- **Fundamentals** (Standard an): Peaks, die sehr wahrscheinlich Obertöne (2x bis 6x und höher, ±30 ct) einer gleichzeitig stärkeren, tieferen Linie
  sind, werden auf 20 % abgedunkelt (nicht gelöscht). Quick Grab bevorzugt Grundlinien. **Hide drums** blendet Schlagzeug-Linien aus.
- **Tuning / Align:** Das Panel zeigt die erkannte Abweichung des Samples von A = 440 Hz ("Tuning +23 ct"). **Align** (Standard an) verschiebt das
  Halbtonraster um diesen Wert, damit die Linien mittig in den Zeilen liegen und Noten die richtige MIDI-Tonhöhe bekommen. **− / +** trimmt von
  Hand in 5-ct-Schritten (±50 ct). Der MIDI-Export und die Preview bleiben bei A = 440 Hz.
- **Bearbeiten:** Mitte ziehen verschiebt (alle gewählten Noten gemeinsam), die Ränder (6 px) ändern die Länge, Doppelklick löscht,
  Rechtsklick öffnet das Menü. Klick wählt, `Shift`-Klick schaltet um, `Cmd`-Ziehen zieht ein Lasso.
- **Quick Grab:** Regler (0 bis 100 %) und **Grab** erzeugen Noten aus den stärksten Linien. Ohne Klick passiert nichts.
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
| Note zeichnen | Klick-Ziehen auf leerer Fläche |
| ohne Snap zeichnen / verschieben | `Alt` gedrückt halten |
| Note verschieben | Ziehen in der Mitte |
| Länge ändern | Ziehen am linken/rechten Rand |
| Note löschen | Doppelklick, Menü, Knopf Delete, `Backspace` / `Entf` |
| auswählen | Klick; `Shift`-Klick mehrfach; `Cmd`-Ziehen Lasso; `Esc` nichts wählen |
| alles wählen / duplizieren | `Cmd+A` / `Cmd+D` |
| Undo / Redo | `Cmd+Z` / `Cmd+Shift+Z` |
| Preview an/aus | `Leertaste` (Map fokussiert) |
| vertikal / horizontal scrollen | Mausrad / `Shift`+Mausrad, Trackpad, Scrollbalken |
| Zeitzoom (0,25x bis 8x) | `Cmd`+Mausrad, Pinch, `−`/`+` im Panel |
| vertikaler Zoom | `Alt`+Mausrad |
| Ton vorhören | Klick auf die Klaviertaste |
| About-Overlay | Klick auf das Logo, schließen mit `Esc`, ✕ oder Klick daneben |
| Kontextmenü | Rechtsklick (oder Ctrl-Klick) |

**Hinweis zu Hosts:** Live fängt manche Tasten ab (Cmd+Z, Leertaste, Backspace). Dafür gibt es die Knöpfe Undo, Redo, Delete,
Duplicate in der Statusleiste und das Kontextmenü per Rechtsklick auf Note oder Map.

## Diagnose und Fehlerbehebung

- **Log:** `~/Library/Logs/Tonetrace/Tonetrace.log` (1 MB x 3 Dateien, geschrieben vom Hintergrund-Thread, nie vom Audio-Thread).
- **Diagnosepanel:** Knopf ⓘ im Panel oder im About-Overlay „Diagnostics“. Es zeigt Version/Build, Host, Sample-Rate, Blockgröße,
  DSP-Last, Overruns, Worker-Verzögerung, Aufnahmespeicher, Bildrate und den letzten Fehler. **Copy report**, **Save report…**
  und **Open log folder** helfen beim Melden von Problemen.
- **Live-Log:** `tools/check_live_log.sh` zeigt alle Tonetrace-Zeilen der neuesten Live-12-`Log.txt` plus Warnungen.
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
| `./build.sh --no-install` | nur bauen, Unit-Tests und UI-Test |
| `./build.sh --release-zip` | wie `./build.sh`, danach `dist/Tonetrace-4.2.0-macOS.zip` |
| `tools/make_icon.sh` | rendert Logo-PNGs, `docs/logo.png` und `assets/logo/Tonetrace.icns` neu (sind eingecheckt) |

Die Version steht nur in `VERSION`, der Build-Hash (git) im About-Overlay und im Diagnosepanel.

## Projektstruktur

```
src/vst3/      VST3: Processor, Controller (Parameter, State), Entry/Factory, NSView-Editor
src/core/      Kernel (plattformneutral): Capture, Preview, Analyse-Thread, Notenmodell, Snapshot, Diagnose, Log
src/analysis/  Live-Spektrum, Pitch-Lines (Mehrfachauflösung, Tracking, Onsets, Tuning), Waveform-Peaks, HPSS/Drum-Filter, Noten-Analyse, Quick Grab, Tonart, FFT
src/capture/   vorab allokierter Aufnahmepuffer (120 s)
src/midi/      MIDI-Writer (SMF Typ 1) und Export-Planer
src/preview/   Preview-Synth, lock-freie Notenübergabe
src/state/     State-Codec (v4, liest v3), Editor-Einstellungen
src/render/    Glow-Look: Colormap-LUT und Gauss-Profile (reines C++, getestet)
src/ui/        Cocoa-Oberfläche (TTR*_v1): Map, Waveform, Piano, Controls, Logo (TTRLogo), About-Overlay, Diagnose
assets/logo/   Logo als SVG, gerenderte PNGs (png/) und Tonetrace.icns
tests/         Unit-Tests ohne Fremdbibliotheken
tools/         Hosttest, UI-Test, Benchmark, Screenshot-Tool, Logo-Renderer, check_live_log.sh, Release-Dateien
docs/          Screenshots, Logo, PERFORMANCE.md
```

## Performance

Gemessen mit `tonetrace_bench` (Release, universal, nativ), Details in `docs/PERFORMANCE.md`:

- Audio-Thread (48 kHz, 64 Frames): Idle 0,003 % Mittel, Record 0,007 %, Preview mit 16 Stimmen und Limiter 0,45 % Mittel, Spitze unter 2 % (Ziel unter 10 %). Keine Allokation, keine Locks.
- Speicher pro Instanz: leer wenige MB, 120 s Aufnahme bei 44,1 kHz ca. 38 MB (nach der Analyse ca. 91 MB), bei 192 kHz ca. 109 MB. Der Puffer wird erst beim Aufnehmen belegt.
- Analyse: live (Spektrum und Pitch-Lines) je Echtzeitfaktor 0,002, Endanalyse inklusive Pitch-Lines von 60 s Audio in ca. 0,5 s, Quick Grab in 0,01 s. Editor: Frame mit 120 s Aufnahme unter 8 ms (gemessen ca. 5 bis 6 ms, auch während der Aufnahme).

## Bekannte Einschränkungen

- Ableton Live und FL Studio wurden bei der Entwicklung nicht gestartet: geprüft sind Hosttest, UI-Test und Validator.
- Der Editor funktioniert nur, wenn Processor und Controller im selben Prozess laufen (alle gängigen Mac-Hosts).
- Die Live-Trennung bei Hide drums kennt nur die Vergangenheit und weicht leicht vom Ergebnis nach dem Stoppen ab.
- Bass unter ca. E2 (82 Hz) ist wegen der Fensterlänge nur grob aufgelöst, und tiefe Töne haben unschärfere Anfänge und Enden (Fenster bis 0,74 s, Kanten ca. ±50 ms).
- Die Linien werden nicht im State gespeichert: bei Aufnahmen über 60 s, deren Audio nicht im State liegt, zeigt die Map nach dem Laden die alte Halbton-Heatmap.
- Ein Oberton, der genau auf der Tonhöhe einer anderen Note liegt (z. B. eine Melodienote zwei Oktaven über dem Bass), wird mit Fundamentals abgedunkelt: dann Fundamentals ausschalten.
- Preview und MIDI bleiben bei A = 440 Hz, auch wenn das Raster um das erkannte Tuning verschoben ist.
- Das Default-Snap ist jetzt *Onset*: neue Instanzen starten damit, gespeicherte Projekte behalten ihren Wert.

## Changelog

### 4.2.0 – glühende Note-Map

- Töne sind leuchtende Heat-Streifen: Kern ca. 2 px (weiß-gelb), gaussförmiger Schein bis ca. 8 px und ein dunkelroter/violetter Hauch um starke Linien, additiv überblendet. Farbskala schwarz, dunkelviolett, magenta, rot, orange, gelb, weiß. Die Helligkeit folgt dem Pegel jedes Frames (mit leichter Textur). Schwache Teiltöne und Obertöne (bei *Fundamentals*) sind blasse, dünne violett-blaue Linien. Gain und Range steuern Helligkeit und Kontrast.
- Das Glow-Rendering läuft in den gecachten Kachel-Bitmaps (vorberechnete Gauss-Profile, Colormap-LUT, 4 Subpixel-Phasen); der Audio-Thread ist unverändert.
- Zeilenraster fast schwarz: keine Streifen für schwarze Tasten, nur eine dezente Linie bei C und eine sehr leise zwischen E und F. Zeilen-Hover bleibt, dezenter.
- Klaviatur schmal (14 px Tastenstreifen, C-Labels links davon); die aktive Taste leuchtet türkis. Die Map ist 16 px breiter.
- Noten sind flache grüne Balken (#2FBF5B, 5 bis 7 px, ohne Rahmen), ausgewählte orange (#FF9F43), die Ghost-Note ein halbtransparenter Balken. Der Name erscheint nur bei Hover oder Auswahl neben dem Balken. Hover-Label und Segment-Outline mit dünner heller Linie.
- Waveform-Spur kompakt (44 px) in gedämpftem Türkis; neuer Toggle **Wave** im Seitenpanel (Standard an, Editor-State v3, liest v1 und v2).
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
VST is a registered trademark of Steinberg Media Technologies GmbH.
