# Tonetrace 4.4.1 - Erkennungsgenauigkeit (4.4.0-Messung darunter, Abschnitt "4.4.1 gegen 4.4.0" zuerst)

Quick Grab arbeitet in 4.4 mit der neuen **Halte-Erkennung** (`src/analysis/SustainGrab.cpp`) statt mit der Segmentierung aus 4.3, die gehaltene Töne an jedem
Drum-Schlag und jedem Sidechain-Dip zerschnitt. Dieses Dokument misst 4.4 gegen 4.3 auf synthetischen Mixes mit Ground Truth und an einem echten Track
(nur gelesen). Alle Zahlen stammen aus `tools/accuracy.sh` (`accuracy mixes`, `accuracy scan`, `accuracy track`). Es wurde nichts auf einen Mix oder Track hin
abgestimmt: keine track-spezifischen Konstanten, keine festen Tonhöhen im Algorithmus (die Referenzzeilen stehen nur im Messwerkzeug).

## Was sich an der Erkennung geändert hat

| Maßnahme | Warum |
|---|---|
| Hysterese: ein bei T_on gestarteter Ton hält, bis er unter T_off = T_on - 7,5 dB fällt | eine einzige Schwelle zerschneidet Töne, die um die Schwelle schwanken |
| Glättung pro Tonhöhe (Median über 5 Frames = 116 ms, vor der Harmonik-Auslöschung und auf der Salienz) | Transienten (Kick, Hats) sollen weder Töne beenden noch Obertöne vortäuschen |
| Pump-Toleranz: Täler bis 20 dB beenden keinen Ton; Täler ab 6 dB nur, wenn danach ein neuer Anschlag folgt (+4 dB in 2 Frames); Lücken bis 1/8 Note (Host-BPM, 120 bis 250 ms) werden überbrückt, wenn der Pegel nicht auf Stille fällt | Sidechain-Dips von 6 bis 12 dB sind keine Notenenden, Wiederholungen derselben Note (Anschlag) schon |
| Drum-Frames sind "unbekannt": ab C3 laufen Töne durch Drum-Schläge, wenn der Rohpegel (ohne HPSS-Maske) im Loch nicht um mehr als 6 dB abfällt (bis 1 s); der Transienten-Gate aus 4.3 entfällt | Gate und harmonische Maske löschten gehaltene Töne unter jedem Schlag |
| Bass-Grundton: lange Fenster (16384 Samples = 0,74 s für C1 bis B1, 8192 = 0,37 s für C2 bis B2, `LowBand.cpp`), zeitlich begrenzt durch den harmonischen Anteil des kurzen Fensters; Oberton-Auslöschung mit voller Subtraktion und Toleranz +40 % | F1 (44 Hz) liegt im 186-ms-Fenster nur ein Bin neben F#1; Obertöne dürfen nicht als Grundton gewählt werden |
| Schwelle relativ zum Oktavband (Referenz = gehaltenes Maximum des Bandes, höchstens 12 dB unter dem globalen Maximum, nie unter der globalen Schwelle bei 100 %) | Bass und Melodie gleichzeitig vorschlagen, ohne dass Rauschen durchkommt |
| Kick-Erkennung: Beat-Raster aus dem Host-Tempo, geprüft gegen die Perkussiv-Onsets (eigenes Raster mit 0,4 bis 0,75 s Periode, wenn es klar besser passt); kurze tiefe Stücke, die mit einem Kick auf dem Beat beginnen, abklingende kurze Stücke nach einem Kick (Kick-Reste) und Stücke, deren Pegel mit dem Beat pulsiert (laut nach dem Beat, abgeklungen davor), sind Kick, kein Ton | "Keine Noten auf den Kick-Streifen" |
| Geisternoten: ein schwaches tiefes Stück neben einer mindestens 14 dB stärkeren, gleichzeitig klingenden Note (Halbton-Nachbarschaft bis eine Quinte, oder Oberton-Abstand 12 / 19 / 24 / 28 Halbtöne darüber; Ring-Zeit 0,25 s) und Stücke ab C7 neben einer solchen Note werden verworfen | Fensterleckage und Unterharmonische neben starkem Bass; Obertöne jenseits der Auslöschung |
| Rausch-Gate (Spektralflachheit): kurze Stücke (< 1 s), die weniger als 4 dB aus den Noten 3 bis 8 Halbtöne um sie herum herausragen, sind Rauschen | weißes Rauschen und Hi-Hat-Reste erzeugen keine Kurznoten mehr |
| Leckage: ein Pick mehr als 26 dB unter einem stärkeren Pick innerhalb von 6 Bins (32 Hz) ist dessen Seitenkeule | Geisternoten neben starken Bassnoten |
| Tempo-bewusste Mindestlänge: 1/16 Note bei Host-BPM (130 BPM = 115 ms, Fallback 120 ms); kurze schwache Stücke (< 150 ms unter -16 dB, < 300 ms unter -30 dB) fallen weg | wenig Kurznoten |

Nicht umgesetzt: das Runden von Start und Ende auf das 1/16-Raster (in der Spezifikation optional); die Noten bleiben auf dem Analyse-Raster (23 ms), Snap rundet beim
Zeichnen und beim MIDI-Export.

## Messverfahren

- Synthetische Mixes mit Ground Truth (44,1 kHz): die acht Mixes aus 4.3 (14 s, Sägezahn-Töne, `tests/TestOldMixes.h`) und vier neue **Techno-Mixes**
  (`tests/TestTechno.h`, 12 Takte, 125 / 128 / 130 / 135 BPM): Four-on-the-floor-Kick (Sinus-Sweep 150 -> 45 Hz), Sidechain-Pumpen von 8 / 10 / 12 / 9 dB auf
  Bass und Pad, Hi-Hats, gehaltener Bass (F1, C2 und Nachbarn, 1 bis 2 Takte), Dreiklang-Pad, kurze Melodie-Stabs (1/8 und 3/16).
- **Note-F1:** Onset +-50 ms, Tonhöhe exakt, jede Note einmal. **Dauer-F1:** Recall = Anteil der wahren Noten, deren Dauer zu mindestens 70 % von erkannten
  Stücken derselben Tonhöhe überdeckt ist; Precision = Anteil der erkannten Stücke, die zu mindestens 50 % in einer wahren Note derselben Tonhöhe liegen.
  **Fragmentierung:** erkannte Stücke je wahrer Note (über die gefundenen Noten). **Kurznoten:** Anteil der Stücke unter 120 ms.
- Der 4.3-Stand ist der unveränderte Quellbaum von 4.3.0, mit demselben Werkzeug gebaut (`tools/accuracy.sh <quellordner> <ausgabe>`).
  Gemessen wird mit Quick Grab 0 / 50 / 100 %, Host-BPM = Mix-BPM (alte Mixes: 120).

## Techno-Mixes (Ziele: Fragmentierung <= 1,2 und Dauer-F1 >= 0,85)

| Mix | Quick Grab | Fragm. 4.3 | Fragm. 4.4 | Dauer-F1 4.3 | Dauer-F1 4.4 | Note-F1 4.3 | Note-F1 4.4 | Kurznoten 4.4 | Ziele (Fragm. <= 1,2 / Dauer-F1 >= 0,85) |
|---|---|---|---|---|---|---|---|---|---|
| techno-125 | 0 % | 1.81 | 1.04 | 0.226 | 0.827 | 0.095 | 0.590 | 0 % | Fragm. erreicht, Dauer-F1 verfehlt |
| techno-125 | 50 % | 2.04 | 1.04 | 0.579 | 0.814 | 0.265 | 0.581 | 0 % | Fragm. erreicht, Dauer-F1 verfehlt |
| techno-125 | 100 % | 2.02 | 1.04 | 0.558 | 0.814 | 0.244 | 0.581 | 0 % | Fragm. erreicht, Dauer-F1 verfehlt |
| techno-128 | 0 % | 2.08 | 1.18 | 0.078 | 0.859 | 0.033 | 0.579 | 0 % | beide erreicht |
| techno-128 | 50 % | 1.96 | 1.18 | 0.514 | 0.854 | 0.283 | 0.575 | 0 % | beide erreicht |
| techno-128 | 100 % | 1.94 | 1.18 | 0.511 | 0.854 | 0.274 | 0.575 | 0 % | beide erreicht |
| techno-130 | 0 % | 1.75 | 1.13 | 0.248 | 0.768 | 0.086 | 0.452 | 2 % | Fragm. erreicht, Dauer-F1 verfehlt |
| techno-130 | 50 % | 2.19 | 1.13 | 0.529 | 0.739 | 0.176 | 0.444 | 2 % | Fragm. erreicht, Dauer-F1 verfehlt |
| techno-130 | 100 % | 2.20 | 1.13 | 0.522 | 0.739 | 0.169 | 0.444 | 2 % | Fragm. erreicht, Dauer-F1 verfehlt |
| techno-135 | 0 % | 1.48 | 1.11 | 0.218 | 0.871 | 0.146 | 0.606 | 0 % | beide erreicht |
| techno-135 | 50 % | 2.00 | 1.11 | 0.642 | 0.861 | 0.309 | 0.593 | 0 % | beide erreicht |
| techno-135 | 100 % | 1.93 | 1.11 | 0.641 | 0.861 | 0.300 | 0.593 | 0 % | beide erreicht |

**Ergebnis:** Die Fragmentierung sinkt von rund 2,0 auf 1,04 bis 1,18 bei 100 % (Ziel erreicht in allen vier Mixes). Das Dauer-F1 steigt von 0,51 bis 0,64 auf 0,74 bis 0,86:
**erreicht in zwei von vier Mixes (128 und 135 BPM), verfehlt in 125 BPM (0,81) und 130 BPM (0,74)**. Das Note-F1 steigt von 0,17 bis 0,30 auf 0,44 bis 0,59. Verbleibende Fehler: Pad-Töne gehen
verloren, wenn ihre Frequenz auf einem Oberton von Bass oder Kick liegt (die Auslöschung nimmt sie mit), und die Bass-Onsets liegen durch die langen Fenster oft 50 bis 120 ms zu spät
(deshalb ist das Note-F1 niedriger als das Dauer-F1). Versuche, die Kanten der Bassnoten aus dem kurzen Fenster nachzuziehen (halber Pegel, harmonischer Anteil, lockerer Kantenschnitt),
machten das Note-F1 schlechter und sind nicht enthalten.

## Bisherige Mixes (Ziel: F1 mindestens wie 4.3)

| Mix | Quick Grab | Note-F1 4.3 | Note-F1 4.4 | Dauer-F1 4.3 | Dauer-F1 4.4 | Fragm. 4.3 | Fragm. 4.4 | Stücke 4.3 | Stücke 4.4 | F1 gehalten |
|---|---|---|---|---|---|---|---|---|---|---|
| melody | 0 % | 0.913 | 0.936 | 0.870 | 0.894 | 1.00 | 1.00 | 21 | 22 | ja |
| melody | 50 % | 0.837 | 0.936 | 0.791 | 0.894 | 1.00 | 1.00 | 18 | 22 | ja |
| melody | 100 % | 0.837 | 0.936 | 0.791 | 0.894 | 1.00 | 1.00 | 18 | 22 | ja |
| chords | 0 % | 1.000 | 1.000 | 1.000 | 1.000 | 1.00 | 1.00 | 18 | 18 | ja |
| chords | 50 % | 1.000 | 1.000 | 1.000 | 1.000 | 1.00 | 1.00 | 18 | 18 | ja |
| chords | 100 % | 1.000 | 1.000 | 1.000 | 1.000 | 1.00 | 1.00 | 18 | 18 | ja |
| bass | 0 % | 0.938 | 1.000 | 0.938 | 1.000 | 1.00 | 1.00 | 15 | 17 | ja |
| bass | 50 % | 0.938 | 1.000 | 0.938 | 1.000 | 1.00 | 1.00 | 15 | 17 | ja |
| bass | 100 % | 0.789 | 1.000 | 0.789 | 1.000 | 1.00 | 1.00 | 21 | 17 | ja |
| vibrato | 0 % | 0.889 | 0.980 | 0.844 | 0.980 | 1.00 | 1.00 | 20 | 24 | ja |
| vibrato | 50 % | 0.837 | 0.980 | 0.791 | 0.980 | 1.00 | 1.00 | 18 | 24 | ja |
| vibrato | 100 % | 0.837 | 0.980 | 0.791 | 0.980 | 1.00 | 1.00 | 18 | 24 | ja |
| detune-30ct | 0 % | 0.889 | 0.936 | 0.844 | 0.894 | 1.00 | 1.00 | 20 | 22 | ja |
| detune-30ct | 50 % | 0.837 | 0.936 | 0.791 | 0.894 | 1.00 | 1.00 | 18 | 22 | ja |
| detune-30ct | 100 % | 0.837 | 0.936 | 0.791 | 0.894 | 1.00 | 1.00 | 18 | 22 | ja |
| detune+30ct | 0 % | 0.889 | 0.958 | 0.844 | 0.980 | 1.00 | 1.00 | 20 | 23 | ja |
| detune+30ct | 50 % | 0.837 | 0.958 | 0.791 | 0.980 | 1.00 | 1.00 | 18 | 23 | ja |
| detune+30ct | 100 % | 0.837 | 0.958 | 0.791 | 0.980 | 1.00 | 1.00 | 18 | 23 | ja |
| noise | 0 % | 0.913 | 0.980 | 0.870 | 0.980 | 1.00 | 1.00 | 21 | 24 | ja |
| noise | 50 % | 0.837 | 0.960 | 0.791 | 0.960 | 1.00 | 1.00 | 18 | 25 | ja |
| noise | 100 % | 0.667 | 0.873 | 0.630 | 0.873 | 1.00 | 1.00 | 29 | 30 | ja |
| drums+melody | 0 % | 0.913 | 0.939 | 0.913 | 0.939 | 1.00 | 1.00 | 21 | 24 | ja |
| drums+melody | 50 % | 0.692 | 0.939 | 0.673 | 0.939 | 1.00 | 1.00 | 27 | 24 | ja |
| drums+melody | 100 % | 0.567 | 0.939 | 0.552 | 0.939 | 1.00 | 1.00 | 35 | 24 | ja |
| full mix | 0 % | 0.667 | 0.754 | 0.798 | 0.820 | 1.39 | 1.00 | 75 | 62 | ja |
| full mix | 50 % | 0.733 | 0.742 | 0.776 | 0.806 | 1.18 | 1.00 | 71 | 64 | ja |
| full mix | 100 % | 0.711 | 0.742 | 0.756 | 0.806 | 1.18 | 1.00 | 75 | 64 | ja |

**Ergebnis:** In allen 27 Zeilen ist F1(4.4) >= F1(4.3). Deutlich besser sind melody, vibrato, detune, noise, bass und drums+melody (Wiederholungen derselben Note werden getrennt,
Drums und Rauschen erzeugen weniger Fehlnoten). Reines weißes Rauschen (Kurztest, 44,1 kHz): 4.4 liefert 2 / 9 / 29 Noten bei 0,3 / 1 / 2,5 s, 4.3 liefert 5 / 11 / 33 bis 41.

## 4.4.1 gegen 4.4.0

**Änderung:** Ein zweiter Durchlauf der Halte-Erkennung mit exakter Oberton-Subtraktion (ohne die +40-%-Toleranz) liefert Kandidaten, die der tolerante Durchlauf als Oberton eines stärkeren,
tieferen Tons verworfen hat. Ein solcher Kandidat (mindestens 0,25 s, im Abstand 12 / 19 / 24 / 28 / 31 Halbtöne über dem tieferen Ton) bleibt als eigene Note, wenn er nicht gemeinsam mit diesem
ein- **und** aussetzt (Onset und Ende beide innerhalb von 60 ms); bleibt er mit ihm deckungsgleich, ist er ein Oberton. Test: `octave_note_with_its_own_onsets_stays_a_note_beside_a_held_bass`.
**Nicht gelungen:** die Geister- und Rausch-Gates auf ganz kurze Schnipsel (< 150 ms) zu begrenzen. Mit gelockerten Gates (Prominenz 1,1 statt 1,5 und Geister-Verhältnis 25 statt 14 dB für Stücke ab 150 ms) kamen A#1 und C3 im Referenz-Fenster 38,62 s teilweise zurück, aber das Rauschen stieg von 2 / 9 / 29 auf 5 / 20 / 57 Noten und die
Dauer-F1 der Techno-Mixes fiel um 0,01 bis 0,04 (bei Prominenz 1,3: Rauschen 3 / 14 / 44 und F1 im full mix 50 % unter 4.3). Die Gates bleiben deshalb wie in 4.4.0.

Techno-Mixes bei 100 % (Ziele Fragmentierung <= 1,2 und Dauer-F1 >= 0,85):

| Mix | Fragm. 4.4.0 | Fragm. 4.4.1 | Dauer-F1 4.4.0 | Dauer-F1 4.4.1 | Note-F1 4.4.0 | Note-F1 4.4.1 | Ziele |
|---|---|---|---|---|---|---|---|
| techno-125 (100 %) | 1.04 | 1.04 | 0.814 | 0.808 | 0.581 | 0.576 | Fragm. erreicht, Dauer-F1 verfehlt |
| techno-128 (100 %) | 1.18 | 1.18 | 0.854 | 0.849 | 0.575 | 0.571 | Fragm. erreicht, Dauer-F1 verfehlt |
| techno-130 (100 %) | 1.13 | 1.13 | 0.739 | 0.734 | 0.444 | 0.441 | Fragm. erreicht, Dauer-F1 verfehlt |
| techno-135 (100 %) | 1.11 | 1.11 | 0.861 | 0.856 | 0.593 | 0.589 | beide erreicht |

Alte Mixes: F1(4.4.1) >= F1(4.3) in allen 27 Zeilen (ja). Weißes Rauschen (0,3 / 1 / 2,5 s): 2 / 9 / 29 Noten wie in 4.4.0 (4.3: 5 / 11 / 33 bis 41).

Referenz-Track, Fenster 156,83 s (beste Übereinstimmung 0.40, 134 BPM), 4.4.0 gegen 4.4.1:

| Referenzzeile | 4.4.0 gefunden / Überdeckung / Stücke | 4.4.1 gefunden / Überdeckung / Stücke |
|---|---|---|
| F1 | ja / 89 % / 1 | ja / 89 % / 1 |
| F2 | nein / 24 % / 4 | nein / 36 % / 5 |
| C2 | nein / 13 % / 2 | nein / 13 % / 2 |
| A#1 | nein / 0 % / 0 | nein / 0 % / 0 |
| B1 | nein / 0 % / 0 | nein / 0 % / 0 |
| C3 | nein / 0 % / 0 | nein / 0 % / 0 |
| F3 | ja / 66 % / 3 | ja / 66 % / 3 |
| G3/G#3 | nein / 43 % / 3 | nein / 43 % / 3 |
| False Positives | 11 | 11 |

Zweites Fenster 160,41 s (Übereinstimmung 0,40):

| Referenzzeile | 4.4.0 gefunden / Überdeckung / Stücke | 4.4.1 gefunden / Überdeckung / Stücke |
|---|---|---|
| F1 | ja / 98 % / 1 | ja / 98 % / 1 |
| F2 | nein / 22 % / 5 | nein / 35 % / 7 |
| C2 | nein / 6 % / 1 | nein / 6 % / 1 |
| A#1 | nein / 0 % / 0 | nein / 0 % / 0 |
| B1 | nein / 0 % / 0 | nein / 0 % / 0 |
| C3 | nein / 0 % / 0 | nein / 0 % / 0 |
| F3 | ja / 74 % / 3 | ja / 74 % / 3 |
| G3/G#3 | nein / 19 % / 4 | nein / 19 % / 4 |
| False Positives | 7 | 7 |

**Ziele:** F1-Bass >= 80 % in <= 2 Stücken bleibt **erreicht** (89 % / 98 % in je 1 Stück). F2 >= 70 %: **verfehlt** (36 % bzw. 35 %, vorher 24 % / 22 %): zwischen den Lücken liegt F2 unterhalb von
F1 nur als Oberton im Pegel (der Rest der F2-Zeile ist von F1s 2. Teilton nicht zu trennen, wenn F2 dort ohne eigene Anschläge und Enden läuft). Mindestens 6 von 8 Zeilen: **verfehlt** (2 von 8).
False Positives: 11 (Grenze: die 47 aus dem ersten 4.4-Durchgang, eingehalten). A#1, B1 und C3 fehlen weiter (siehe oben: die Gates).

## Referenz-Track (nur gelesen)

Datei: `~/Music/FLX4/Britney Spears - Gimme More (KAAZE Hot Teknō Remix).wav` (3:46, 44,1 kHz). Tempo aus der Kick-Hüllkurve geschätzt: **134 BPM** (erster Beat bei 0,111 s).
Das Stück wurde in 20-Beat-Fenstern (8,96 s) im Abstand von einem Beat gescannt (`accuracy scan`); die Übereinstimmung eines Fensters ist das F1 aus der mittleren
Pitch-Überdeckung der acht Referenzzeilen und dem Anteil der Stücke, die auf Referenzzeilen liegen. Die Zeilen (Pitch +-1 Halbton, Zeitbereiche als Anteil des Fensters)
stehen in `tools/accuracy.cpp`. Quick Grab 100 %, Host-BPM 134, Beat-Position 0. Eine Zeile gilt ab 50 % Überdeckung als gefunden.

**Gewähltes Fenster (beste Übereinstimmung 0,39): Start 156,83 s, 134 BPM.** 2 von 8 Zeilen gefunden, 11 zusätzliche Stücke (False Positives), 24 Stücke insgesamt:

| Referenzzeile | gefunden | Überdeckung | Stücke |
|---|---|---|---|
| F1 | ja | 89 % | 1 |
| F2 | nein | 24 % | 4 |
| C2 | nein | 13 % | 2 |
| A#1 | nein | 0 % | 0 |
| B1 | nein | 0 % | 0 |
| C3 | nein | 0 % | 0 |
| F3 | ja | 66 % | 3 |
| G3 / G#3 | nein | 43 % | 3 |

Zum Vergleich das zweitbeste Fenster (Start 160,41 s, 134 BPM, Übereinstimmung 0,38): 2 von 8 Zeilen, 7 False Positives, 21 Stücke insgesamt:

| Referenzzeile | gefunden | Überdeckung | Stücke |
|---|---|---|---|
| F1 | ja | 98 % | 1 |
| F2 | nein | 22 % | 5 |
| C2 | nein | 6 % | 1 |
| A#1 | nein | 0 % | 0 |
| B1 | nein | 0 % | 0 |
| C3 | nein | 0 % | 0 |
| F3 | ja | 74 % | 3 |
| G3 / G#3 | nein | 19 % | 4 |

**Ziele:** F1-Bass >= 80 % in höchstens 2 Stücken: **erreicht** (89 % in 1 Stück, im zweiten Fenster 98 % in 1 Stück). Ursache der früheren Zerstückelung war ein Sidechain-Loch von bis zu
26 dB direkt am Kick (der harmonische Anteil fällt dort auf fast null, der Rohpegel nur um etwa 11 dB): der Anschlag danach wurde als neue Note gewertet. Jetzt gilt in der Bassregion ein Loch, das auf dem
Beat beginnt und nicht auf Stille fällt, als maskierter Ton (Raster aus Host-Tempo oder eigenem Beat-Raster). F2 >= 70 %: **verfehlt** (24 % bzw. 22 %; F2 ist die Oktave von F1 und liegt im
Pegel innerhalb der Toleranz für den 2. Oberton, die Grundtonwahl entscheidet sich also für F1 allein). Mindestens 6 von 8 Zeilen gefunden: **verfehlt** (2 von 8). Der Rausch- und Geister-Filter
entfernt auf diesem Track viele Kurzstücke (24 statt 72 Stücke im Fenster); dabei fallen auch die kurzen Zeilen A#1, B1 und C3 weg (0 %). Es wurde nicht getrickst: nichts im Algorithmus kennt diesen
Track, seine Tonart oder die Referenzzeilen. Wegen der Pitch-Toleranz von +-1 Halbton und der Fensterwahl über die eigene Erkennung ist der Vergleich nur ein grobes Maß.

Neu erzeugen: `tools/accuracy.sh src build/accuracy`, dann `build/accuracy mixes`, `build/accuracy scan <wav> 134` und `build/accuracy track <wav> <startSec> 20 134`.
