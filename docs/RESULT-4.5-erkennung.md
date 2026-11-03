# Ergebnis Plan 4.5 – Lernbasierte Tonerkennung

Stand: 03.11.2026, Basis `main` @ 3c0d047 (Tonetrace 4.4.1). Plan: `docs/PLAN-4.5-erkennung.md`.

**Entscheidung (Schritt 1):** No-Go. Weder Basic Pitch roh noch ein NMF-Prototyp erreichen am Referenzfenster die Go/No-Go-Schwelle,
und auf den Techno-Mixes ist Basic Pitch roh schlechter als Classic.

**Nachtrag:** Auf Wunsch trotzdem umgesetzt (Schritte 2 bis 6) als optionale Engine **Neural** in 4.5.0. Mit eigenem Decoder und den Tonetrace-Regeln
erreicht sie F2 72 % und 5 False Positives, verfehlt aber die Abnahme (3 von 8 Zeilen, Techno-Dauer-F1, alte Mixes). Classic bleibt Standard.
Zahlen: `docs/ACCURACY.md`, Abschnitt 4.5.0.

## Schritt 0 – Basic Pitch geprüft

| Punkt | Befund |
|---|---|
| Lizenz Code | Apache-2.0 |
| Lizenz Gewichte | keine eigene Angabe; die Modelle liegen im selben Repo (`basic_pitch/saved_models/icassp_2022/`) |
| Formate | CoreML (`nmp.mlpackage`), ONNX (230 KB), TFLite (204 KB), TF SavedModel |
| Eingabe | 22 050 Hz mono, Hop 256 (86 Frames/s), Fenster 43 844 Samples |
| Ausgabe | 88 Tasten ab A0, Kontur 264 Bins (3 pro Halbton) |

## Schritt 1 – Referenzlauf

Werkzeuge: `tools/accuracy.cpp` hat neue Modi `export` (Mixes als WAV), `score-mixes` und `score-track` (fremde Notenlisten `pitch|start|end` bewerten).
Die Python-Skripte unter `tools/ref/` liefen in einer venv außerhalb des Repos (Python 3.11, `basic-pitch[onnx]`, ONNX-Modell).

### Referenz-Track, Fenster 156,83 s (20 Beats, 134 BPM)

Überdeckung in % / Stücke. "BP roh" = Basic Pitch mit Standard-Decoder (Onset 0,5, Frame 0,3); "BP 0,2/0,2" = beste abgestimmte Einstellung.

| Zeile | Classic 4.4.1 | BP roh | BP 0,2/0,2 | NMF (Clip ×4, HPSS) |
|---|---|---|---|---|
| F1 | 89 / 1 | 50 / 17 | 82 / 38 | 93 / 5 |
| F2 | 24 / 4 | 11 / 5 | 66 / 30 | 46 / 20 |
| C2 | 0 / 0 | 0 / 0 | 0 / 0 | 97 / 13 * |
| A#1 | 17 / 1 | 0 / 0 | 0 / 0 | 100 / 4 * |
| B1 | 0 / 0 | 0 / 0 | 0 / 0 | 100 / 4 * |
| C3 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| F3 | 56 / 4 | 30 / 3 | – | 40 / 4 |
| G3/G#3 | 38 / 3 | 11 / 1 | – | 6 / 1 |
| Zeilen gefunden | 2 / 8 | 0 / 8 | 4 / 8 | 4 / 8 |
| False Positives | 11 | 3 | 15 | 118 |

\* Scheintreffer: lange, über das ganze Fenster gehaltene Stücke auf A0 bis C2 überdecken die kurzen Bass-Zeilen in der ±1-Halbton-Toleranz.

Fenster 160,41 s: Classic F2 39 % (2/8 Zeilen, FP 11), BP roh F2 11 % (1/8), BP 0,2/0,2 F2 66 % (3/8, FP 16).
Fenster 38,62 s: Classic 2/8 (FP 25), BP 0,2/0,2 1/8 (FP 23).

**Go/No-Go (F2 deutlich über 36 % und mindestens 4 von 8 Zeilen, roh):** nicht erreicht. Basic Pitch roh liegt in beiden Hauptfenstern unter Classic.
Mit abgestimmten Schwellen sieht das Modell F2 (66 %), zerlegt die Zeile aber in 30 Stücke; die kurzen Bassnoten C2, A#1, B1 und C3 erkennt es bei keiner
Einstellung (erst bei Frame-Schwelle 0,1, dann mit 97 bis 820 False Positives). Die Abnahme (≥ 6 von 8 Zeilen, F2 ≥ 70 %) ist damit auch mit eigenem Decoder
nicht in Reichweite.

### Synthetische Mixes, Basic Pitch roh gegen Classic 4.4.1 (100 %)

| Mix | Note-F1 Classic | Note-F1 BP | Dauer-F1 Classic | Dauer-F1 BP | Fragm. Classic | Fragm. BP |
|---|---|---|---|---|---|---|
| techno-125 | 0.576 | 0.423 | 0.808 | 0.810 | 1.04 | 2.43 |
| techno-128 | 0.571 | 0.390 | 0.849 | 0.732 | 1.18 | 1.94 |
| techno-130 | 0.441 | 0.430 | 0.734 | 0.758 | 1.13 | 2.12 |
| techno-135 | 0.589 | 0.444 | 0.856 | 0.726 | 1.11 | 1.77 |
| drums+melody | 0.939 | 0.460 | 0.939 | 0.571 | 1.00 | 1.20 |
| full mix | 0.742 | 0.456 | 0.806 | 0.716 | 1.00 | 1.81 |
| vibrato | 0.980 | 0.000 | 0.980 | 0.980 | 1.00 | 1.40 |

Auf den reinen Sägezahn-Mixes (melody, chords, detune, noise) ist Basic Pitch gleich gut oder besser (F1 1,00); sobald Kick und Sidechain dazukommen, deutlich schlechter.

## Fallback NMF – Prototyp

`tools/ref/nmf_proto.py`: CQT (36 Bins/Oktave), harmonische Vorlagen je Tonhöhe (12 Teiltöne, 1/h), pro Aufnahme angepasst (KL-Updates, Abweichung von der
Vorlage begrenzt), optional HPSS und frame-relative Schwelle, Hysterese-Decoder. Ergebnis: F2 bis 88 %, aber nur mit 100 bis 185 False Positives;
bei FP ≤ 40 fällt F2 auf ≤ 44 %. Die tiefen Tonhöhen nehmen die Kick- und Sub-Energie auf und laufen über das ganze Fenster. Eine Abstimmung, die
F2 ≥ 70 % bei FP ≤ 25 liefert, wurde nicht gefunden. Der Aufwand für eine belastbare NMF-Engine (laut Plan etwa 2 Tage) ist durch den Prototyp nicht gedeckt.

## Nebenbefund

Classic misst im Fenster 156,83 s heute F2 24 % / 4 Stücke und F3 56 % / 4 Stücke; `docs/ACCURACY.md` nennt für 4.4.1 36 % / 5 und 66 % / 3.
Der Quellstand `src/` ist unverändert gegenüber 3c0d047; die Abweichung ist ungeklärt (vermutlich anderer Messlauf oder anderer Stand von `build/generated`).

## Mögliche nächste Schritte

- Basic Pitch nur als zweite Meinung für F2 über F1: Classic bleibt maßgeblich, ein Modell-Treffer eine Oktave über einem gehaltenen Bass hebt dort nur
  die Oktavsperre auf. Ungeprüft; bräuchte CoreML im Plugin (Schritt 2), aber keinen vollen Engine-Wechsel.
- Ein auf Bass und elektronische Musik trainiertes Modell statt eines instrumentunabhängigen; nur mit geklärter Lizenz.
