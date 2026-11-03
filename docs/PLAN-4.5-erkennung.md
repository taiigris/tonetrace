# Plan 4.5 – Neue Tonerkennung (lernbasiert)

Stand: 27.10.2026, Basis `main` @ 3c0d047 (Tonetrace 4.4.1).
**Ergebnis (03.11.2026): Go/No-Go verfehlt, trotzdem als optionale Engine Neural in 4.5.0 umgesetzt; Abnahme verfehlt, Classic bleibt Standard. Siehe `docs/RESULT-4.5-erkennung.md` und `docs/ACCURACY.md`.**
Umfang: etwa ein Arbeitstag. Die Schritte bauen aufeinander auf, jeder endet mit einem prüfbaren Ergebnis.

## 1. Ausgangslage

Messwerte 4.4.1 (`tools/accuracy.sh`, Details in `docs/ACCURACY.md`):

| Test | 4.4.1 | Ziel |
|---|---|---|
| Referenz-Track: F1-Bass | 89 % in 1 Stück | ≥ 80 % in ≤ 2 Stücken ✔ |
| Referenz-Track: F2 | 36 % | ≥ 70 % ✘ |
| Referenz-Track: gefundene Zeilen | 2 von 8 | ≥ 6 von 8 ✘ |
| Techno-Mixes: Fragmentierung | 1,04–1,18 | ≤ 1,2 ✔ |
| Techno-Mixes: Dauer-F1 | 0,73–0,86 | ≥ 0,85 (1 von 4 ✔) |
| Techno-Mixes: Note-F1 | 0,44–0,59 | so hoch wie möglich |
| Alte Mixes F1 | in allen 27 Zeilen ≥ 4.3 | halten ✔ |

**Ursache:**
- Die heutige Erkennung entscheidet über Pegelschwellen auf dem Spektrum (Salienz, Obertonsubtraktion, Gates).
- Ein Ton eine Oktave über einem gehaltenen Bass (F2 über F1) liegt im Pegel genau dort, wo auch der 2. Teilton des Basses liegt. Mit Schwellen ist das nicht zu trennen.
- Jede strengere Regel gegen Fehlnoten entfernt auch echte kurze Bassnoten (A#1, B1, C3).
- In 4.4.0 und 4.4.1 ist das zweimal ausprobiert worden, die Grenze ist erreicht.

**Folgerung:**
- Die Tonhöhen-Entscheidung braucht ein Modell, das gelernt hat, wie Obertonreihen echter Instrumente aussehen. Das ist ein trainiertes Polyphonie-Transkriptionsmodell.
- Die bestehende Nachbearbeitung bleibt: Drum-Brücken, Beat-Raster, Mindestlänge, Hysterese.

## 2. Ansatz

**Primär: Spotify „Basic Pitch“**
- Open Source, laut Repo Apache-2.0, sehr kleines Modell, polyphon, instrumentunabhängig.
- Es gibt fertige Modellexporte; laut Repo sind CoreML, ONNX, TFLite und TF enthalten.
- Ausgaben je Frame: Noten-Aktivierung (88 Tasten), Onset-Wahrscheinlichkeit und eine feine Tonhöhenkontur (3 Bins pro Halbton).

**Integration:**
1. Das CoreML-Modell läuft im Analyse-Worker, nie im Audio-Thread.
2. Aus den Wahrscheinlichkeiten entstehen Noten mit einem eigenen Decoder in C++.
3. Darauf setzt die bestehende Tonetrace-Nachbearbeitung auf.

**Fallback, falls Basic Pitch nicht passt** (Lizenz, Größe, Qualität im Bass):
- Eigene NMF/PLCA-Zerlegung mit harmonischen Vorlagen je Tonhöhe, die pro Aufnahme angepasst werden.
- Dazu eine onset-gekoppelte Dekodierung.
- Kein Download nötig, aber deutlich mehr Eigenentwicklung (eher 2 Tage).

**Im Plugin:**
- Im Optionen-Popover ein Schalter **Engine: Neural | Classic**.
- Neural wird Standard, wenn die Abnahme in Abschnitt 4 bestanden ist.
- Classic bleibt der heutige Stand 4.4.1 und dient als Rückfallebene.

## 3. Schritte

### Schritt 0 – Klärung und Freigabe (30 min)

1. Basic Pitch prüfen: Lizenz von Code **und** Modellgewichten, verfügbare Exportformate, Eingabeformat (laut Doku 22 050 Hz mono, 2-s-Fenster mit Überlappung, ca. 86 Frames/s) und Tastenumfang (laut Doku A0–C8). Alle Angaben im Repo nachlesen, nicht aus dem Gedächtnis übernehmen.
2. **Freigabe:** Das Klonen bzw. Herunterladen fremden Codes und Modells muss ausdrücklich freigegeben werden. Der Auto-Modus hat das bei JUCE schon einmal blockiert.
   - Ablage: `external/basic-pitch/`, ist bereits per `.gitignore` ausgenommen.
   - Ins Repo kommt nur die Modelldatei selbst plus `THIRD_PARTY_LICENSES.md`.
3. Entscheidung festhalten: Basic Pitch oder Fallback NMF.

**Ergebnis:** Lizenz geklärt, Modelldatei liegt lokal.

### Schritt 1 – Referenzlauf außerhalb des Plugins (60 min)

1. Basic Pitch einmal unverändert mit dem offiziellen Python-Paket auf das Referenzfenster anwenden.
   - Datei: `~/Music/FLX4/Britney Spears - Gimme More (KAAZE Hot Teknō Remix).wav`, nur lesen.
   - Fenster: Start 156,83 s und 38,62 s, je 20 Beats bei 134 BPM.
   - In einer venv im Scratchpad, nicht im Repo.
2. Die Noten als `pitch|start|end` exportieren und mit dem bestehenden Zeilenvergleich in `tools/accuracy.cpp` auswerten (Modus `track` bzw. dieselbe Logik).
3. Ebenso die synthetischen Mixes auswerten (`accuracy mixes`; dafür die Mixes als WAV exportieren).

**Ergebnis:** Eine Tabelle „Basic Pitch roh“ gegen 4.4.1.

**Go/No-Go:** Erreicht roh F2 nicht deutlich mehr als 36 % und mindestens 4 von 8 Zeilen, lohnt die Integration nicht. Dann den Fallback NMF verfolgen.

### Schritt 2 – Modell ins Plugin (90 min)

Neue Dateien, je Datei höchstens 400 Zeilen:

| Datei | Aufgabe |
|---|---|
| `src/analysis/ml/Resampler.{h,cpp}` | Downmix plus Resampling auf 22 050 Hz (vDSP bzw. polyphaser FIR) |
| `src/analysis/ml/NeuralTranscriber.{h,mm}` | CoreML-Aufruf: Fenster schneiden, Überlappung zusammenfügen, Ausgaben als Matrizen `note[frames][88]`, `onset[frames][88]`, `contour[frames][264]` |
| `src/analysis/ml/NoteDecoder.{h,cpp}` | Wahrscheinlichkeiten → Noten, Details siehe unten |
| `src/analysis/ml/NeuralEngine.{h,cpp}` | gemeinsame Schnittstelle neben dem heutigen Weg in `QuickGrab.cpp` |

**NoteDecoder:**
- Hysterese auf der Aktivierung: an ≥ 0,5, aus < 0,3.
- Onset-Peaks starten Noten.
- Die Kontur liefert Cent und die Stimmungsschätzung.

**Weitere Änderungen:**
- CMake: Die Modelldatei bei Build mit `xcrun coremlc compile` in `Contents/Resources/` des Bundles kompilieren. Universal-Build und Signatur bleiben wie bisher.
- Fehlt das Modell oder schlägt CoreML fehl: automatisch Classic verwenden, ins Log schreiben und im Diagnose-Panel anzeigen. Nie abstürzen.
- Live-Aufnahme:
  - Die Spektrum-Anzeige bleibt wie heute.
  - Das Modell läuft blockweise alle ca. 2 s im Worker.
  - Die Endanalyse läuft nach Stop über die ganze Aufnahme.

**Ergebnis:** Quick Grab mit Engine Neural liefert Noten, alle bestehenden Tests sind grün.

### Schritt 3 – Nachbearbeitung anschließen (60 min)

Die vorhandenen Bausteine kommen hinter den NoteDecoder:
- Drum- und Sidechain-Brücken (`SustainRuns`)
- Beat-Raster und Kick-Erkennung (`BeatHits`)
- Mindestlänge eine 1/16 Note beim Host-Tempo
- Quick-Grab-Regler: steuert die Aktivierungsschwelle (0 % = 0,7, 100 % = 0,25), statt der heutigen dB-Schwelle
- Fundamentals und Hide drums wirken wie bisher

**Ergebnis:** Neural-Weg mit Tonetrace-Verhalten.

### Schritt 4 – Messen und abstimmen (60 min)

1. `tools/accuracy.cpp` um `--engine neural|classic` erweitern.
2. Alle Tabellen für beide Engines neu erzeugen: alte Mixes, Techno-Mixes, Rauschen, Referenzfenster.
3. Nur die generischen Parameter abstimmen: Schwellen, Brückenlänge, Mindestlänge. Keine Track-spezifischen Konstanten.

### Schritt 5 – Performance und Robustheit (30 min)

- Ziel Endanalyse: Echtzeitfaktor ≤ 0,05, also 120 s Audio in höchstens 6 s auf Apple Silicon. Zum Vergleich: heute 0,011.
- UI-Rahmenzeit bleibt unter 8 ms, der Audio-Thread bleibt unverändert. Der Malloc-Zähler-Test muss grün bleiben.
- Sanitizer-Lauf (ASan/UBSan) mit kurzen, stillen und verrauschten Signalen sowie Mono und Stereo.
- Neue Unit-Tests:
  - Resampler (Sinus behält Frequenz und Pegel)
  - NoteDecoder (synthetische Wahrscheinlichkeitsmatrix → erwartete Noten)
  - Rückfall auf Classic ohne Modell
  - Neuer Test: Sägezahn-Bass F1 durchgehend plus F2 mit eigenen Anschlägen → beide Noten

### Schritt 6 – Release 4.5.0 (30 min)

Wie bei den bisherigen Releases:
1. VERSION 4.5.0 setzen, README-Changelog schreiben, Drittlizenz eintragen.
2. ACCURACY.md und PERFORMANCE.md aktualisieren, Screenshots neu erzeugen.
3. Tests, Validator, Hosttest, UI-Test und Benchmarks laufen lassen.
4. `./build.sh --release-zip`, alte ZIP in `releases/` per `git rm` ersetzen, SHA256SUMS erneuern.
5. Installieren, Scan nach persönlichen Daten, pushen.

## 4. Abnahme

Neural wird Standard, wenn **alle** Punkte erfüllt sind:

| Kriterium | Ziel |
|---|---|
| Referenz-Track: F1-Bass | ≥ 80 % in ≤ 2 Stücken |
| Referenz-Track: F2 | ≥ 70 % |
| Referenz-Track: gefundene Zeilen | ≥ 6 von 8 |
| Referenz-Track: False Positives | ≤ 25 im Fenster |
| Techno-Mixes | Fragmentierung ≤ 1,2 und Dauer-F1 ≥ 0,85 in allen vier |
| Alte Mixes | F1 ≥ 4.4.1 in allen 27 Zeilen; sonst begründete Ausnahmen in ACCURACY.md |
| Rauschen | nicht mehr Noten als 4.4.1 (2 / 9 / 29) |
| Performance | Echtzeitfaktor Endanalyse ≤ 0,05, Rahmenzeit < 8 ms |

Werden die Ziele verfehlt, bleibt Classic Standard und Neural optional. Das wird ehrlich dokumentiert.

## 5. Risiken

| Risiko | Gegenmaßnahme |
|---|---|
| Modell im Sub-Bass (F1 ≈ 44 Hz) zu ungenau | Bass unter C2 weiter aus `LowBand` (lange Fenster), Neural darüber; Zusammenführung per Oktavprüfung |
| Lizenz der Gewichte unklar | Fallback NMF (Schritt 0 entscheidet) |
| CoreML im Host nicht verfügbar oder langsam | `MLComputeUnits` auf CPU-only probieren; Rückfall Classic |
| Bundle-Größe | Modell laut Repo klein (< 1 MB); prüfen |
| Live-Analyse ruckelt | Modell nur im Worker, Blöcke ≥ 2 s, Priorität Utility |

## 6. Feste Regeln

- Das Referenz-Plugin nicht laden, ausführen, disassemblieren oder analysieren. Als Vergleich dient nur das eigene Screenshot-Ergebnis.
- Referenzbilder (`/*.png` im Projektordner) nie committen.
- Musikdateien nur lesen, nichts davon ins Repo. `~/Music/Tonetrace` nicht anfassen.
- Ableton und FL Studio nicht automatisch starten.
- Vor jedem Push: Scan nach persönlichen Daten (Projekt-Arbeitsname, Nutzername, Home-Pfade, Firmen-Domain) per `git grep -nIiE` muss leer sein. Das Muster steht bewusst nicht in dieser Datei.
- Commits im Format `<type>: <desc>` mit Co-Authored-By-Zeile.
- Andere Remote Scripts in Live unverändert lassen.

## 7. Start morgen

So startest du die Arbeit in Claude Code im Projektordner:

> Setze docs/PLAN-4.5-erkennung.md um. Beginne mit Schritt 0. Ich gebe das Klonen von Basic Pitch nach `external/basic-pitch/` frei. Nach Schritt 1 die Go/No-Go-Tabelle zeigen und dann ohne Rückfrage weitermachen.

Hinweise:
- Die Auswerte-Tools liegen im Repo (`tools/accuracy.cpp`, `tools/accuracy.sh`). Die Zwischenergebnisse im Scratchpad dieser Sitzung sind morgen nicht mehr da und werden nicht gebraucht.
- Zum Sparen: die Umsetzung (Schritte 2–5) an einen Sonnet-Agenten geben, Prüfung und Release selbst machen.
