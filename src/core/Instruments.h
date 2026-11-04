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

}  // namespace ttr
