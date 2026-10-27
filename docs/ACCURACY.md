# Tonetrace 4.3.0 - Erkennungsgenauigkeit gegen v3

Quick Grab und Tuning nutzen in 4.3 wieder die bewaehrte v3-Analyse: Salienz und Segmentierung auf dem harmonischen Anteil (HPSS, Drum-Filter,
harmonische Unterdrueckung). Die feine Tonhoehe aus 4.1 bestimmt nur noch die Stimmung in Cent (Align, Raster), sie entscheidet nicht mehr ueber
die Notensegmente. Neu gegenueber v3: Segmente unter 120 ms werden verworfen (keine Anschlag-Blips), und Quick Grab filtert Drum-Segmente immer,
unabhaengig vom Anzeige-Schalter *Hide drums*.

## Messverfahren

- Vergleichswerkzeug: `tools/compare_v3.sh` baut `tools/compare_v3.cpp` einmal gegen den Stand `v3-mapdraw` (lokal aus dem Backup-Bundle
  ausgecheckt, nichts heruntergeladen) und einmal gegen diesen Stand. Beide laufen auf denselben, deterministisch erzeugten Mixes (44,1 kHz, 14 s,
  Saegezahn-Toene, Ground Truth bekannt) mit denselben Einstellungen (Hide drums an, Quick Grab 0 / 50 / 100 %).
- Mixes: *melody* (Melodie, Noten 0,25 bis 0,6 s), *chords* (Dreiklaenge), *bass* (Bass, 0,4 bis 0,9 s), *vibrato* (Melodie mit +-35 ct Vibrato),
  *detune-30ct* und *detune+30ct* (Melodie um 30 ct verstimmt), *noise* (Melodie mit Rauschen), *drums+melody* (Kick, Snare, Hats plus Melodie),
  *full mix* (Melodie, Bass, Akkorde, Drums, Rauschen).
- Metriken: Note-F1 (Onset +-50 ms, Tonhoehe exakt, jede Note hoechstens einmal zugeordnet), Pitch-Genauigkeit (Anteil der onset-passenden Noten
  mit exakter Tonhoehe), Anzahl Kurznoten unter 120 ms. Es wurden keine Schwellen und keine Testsignale zugunsten von 4.3 angepasst.

## Ergebnis (F1, Pitch-Genauigkeit, Kurznoten unter 120 ms)

| Mix | Quick Grab | F1 v3 | F1 4.3 | Pitch v3 | Pitch 4.3 | Short v3 | Short 4.3 | OK |
|---|---|---|---|---|---|---|---|---|
| melody | 0 % | 0.913 | 0.913 | 1.00 | 1.00 | 0 | 0 | yes |
| melody | 50 % | 0.837 | 0.837 | 1.00 | 1.00 | 0 | 0 | yes |
| melody | 100 % | 0.837 | 0.837 | 1.00 | 1.00 | 0 | 0 | yes |
| chords | 0 % | 1.000 | 1.000 | 1.00 | 1.00 | 0 | 0 | yes |
| chords | 50 % | 1.000 | 1.000 | 1.00 | 1.00 | 0 | 0 | yes |
| chords | 100 % | 1.000 | 1.000 | 1.00 | 1.00 | 0 | 0 | yes |
| bass | 0 % | 0.938 | 0.938 | 1.00 | 1.00 | 0 | 0 | yes |
| bass | 50 % | 0.938 | 0.938 | 1.00 | 1.00 | 0 | 0 | yes |
| bass | 100 % | 0.789 | 0.789 | 1.00 | 1.00 | 0 | 0 | yes |
| vibrato | 0 % | 0.889 | 0.889 | 1.00 | 1.00 | 0 | 0 | yes |
| vibrato | 50 % | 0.837 | 0.837 | 1.00 | 1.00 | 0 | 0 | yes |
| vibrato | 100 % | 0.837 | 0.837 | 1.00 | 1.00 | 0 | 0 | yes |
| detune-30ct | 0 % | 0.889 | 0.889 | 1.00 | 1.00 | 0 | 0 | yes |
| detune-30ct | 50 % | 0.837 | 0.837 | 1.00 | 1.00 | 0 | 0 | yes |
| detune-30ct | 100 % | 0.837 | 0.837 | 1.00 | 1.00 | 0 | 0 | yes |
| detune+30ct | 0 % | 0.889 | 0.889 | 1.00 | 1.00 | 0 | 0 | yes |
| detune+30ct | 50 % | 0.837 | 0.837 | 1.00 | 1.00 | 0 | 0 | yes |
| detune+30ct | 100 % | 0.837 | 0.837 | 1.00 | 1.00 | 0 | 0 | yes |
| noise | 0 % | 0.913 | 0.913 | 1.00 | 1.00 | 0 | 0 | yes |
| noise | 50 % | 0.837 | 0.837 | 1.00 | 1.00 | 0 | 0 | yes |
| noise | 100 % | 0.667 | 0.667 | 1.00 | 1.00 | 0 | 0 | yes |
| drums+melody | 0 % | 0.913 | 0.913 | 1.00 | 1.00 | 0 | 0 | yes |
| drums+melody | 50 % | 0.692 | 0.692 | 1.00 | 1.00 | 0 | 0 | yes |
| drums+melody | 100 % | 0.567 | 0.567 | 1.00 | 1.00 | 0 | 0 | yes |
| full mix | 0 % | 0.631 | 0.667 | 1.00 | 1.00 | 14 | 0 | yes |
| full mix | 50 % | 0.690 | 0.733 | 1.00 | 1.00 | 11 | 0 | yes |
| full mix | 100 % | 0.671 | 0.711 | 1.00 | 1.00 | 11 | 0 | yes |

In jedem Mix und bei jeder Quick-Grab-Stufe gilt F1(4.3) >= F1(v3). Gleich bleibt die Genauigkeit dort, wo v3 schon sauber arbeitet; besser wird
sie im vollen Mix (Kurznoten aus Anschlaegen entfallen: 14 beziehungsweise 11 Kurznoten bei v3, keine bei 4.3).

## Realcheck (nur lokal, ohne Ground Truth)

Datei: ein lokaler Remix (erste 30 s, nur gelesen, nichts davon liegt im Repository). Notenzahl und Kurznoten (unter 120 ms), Hide drums an:

| Version | Quick Grab 0 % | Quick Grab 50 % |
|---|---|---|
| v3 | 13 Noten, 0 Kurznoten (0,0 %) | 139 Noten, 0 Kurznoten (0,0 %) |
| 4.3 | 13 Noten, 0 Kurznoten (0,0 %) | 139 Noten, 0 Kurznoten (0,0 %) |

Auf dieser Datei liefert v3 keine Segmente unter 120 ms, 4.3 dasselbe Ergebnis: der 120-ms-Filter ist hier nicht entscheidend, es gibt also auch keinen Verlust.

Neu erzeugen: `tools/compare_v3.sh <arbeitsordner> [wav-datei]`.
