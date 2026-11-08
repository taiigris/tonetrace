// Instruments of a transcribed song. MuScriptor gives every note a GM program (or kDrumProgram). Names, families (for the note colours) and the
// per-instrument summary used by the export and the UI.
#pragma once
#include <string>
#include <vector>

#include "core/Types.h"

namespace ttr {

enum class InstrumentFamily { None, Keys, Percussion, Organ, Guitar, Bass, Strings, Voice, Brass, Reed, Winds, Synth, Drums };

// "Bass", "Drums", "Synth Pad", ...; "Notes" for kNoProgram and "Program <n>" for programs the model never picks.
std::string instrumentName(int program);
InstrumentFamily instrumentFamily(int program);
// The General MIDI sound name of a program ("Electric Bass (finger)"), "Standard Kit" for drums, empty for kNoProgram
// and anything outside 0..128. The export writes it so a GM synth (and the user) sees the exact sound after import.
std::string gmSoundName(int program);
// A program a note may carry: kNoProgram, 0..127 or kDrumProgram.
bool isValidProgram(int program);

struct InstrumentSummary {
    int program = kNoProgram;
    int noteCount = 0;
};
// Every instrument with at least one note, ascending by program (kNoProgram first, drums last).
std::vector<InstrumentSummary> instrumentsIn(const std::vector<NoteEvent>& notes);
// True when any note carries an instrument (the export then writes one track per instrument).
bool hasInstruments(const std::vector<NoteEvent>& notes);
// Three-letter chip of the sidebar strip ("BAS", "DRM", "GTR").
std::string instrumentAbbreviation(int program);
// Lowest and highest pitch of the instrument's notes (false when it has none).
bool pitchRangeOf(const std::vector<NoteEvent>& notes, int program, int* lowest, int* highest);
// "C4", "F#2".
std::string noteName(int pitch);

// ---- instrument selection (what the model listens for) ----
// The 35 instruments the model can be asked for (34 groups plus drums), one program each, ordered by family.
const std::vector<int>& selectableInstruments();
// "Keys", "Guitars", "Bass", ...
std::string instrumentFamilyName(InstrumentFamily family);
// Sorted, unique, only selectable programs; a selection of every instrument becomes empty (= all, the model's
// unrestricted path, which also finds the rarer instruments without a group).
std::vector<int> normalizedSelection(const std::vector<int>& programs);
// "All instruments", "Drums + Bass", "5 instruments".
std::string selectionSummary(const std::vector<int>& programs);
// The notes of the selected instruments (all notes for an empty selection).
std::vector<NoteEvent> notesOfInstruments(const std::vector<NoteEvent>& notes, const std::vector<int>& selection);

}  // namespace ttr
