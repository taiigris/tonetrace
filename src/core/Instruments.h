// 4.7: instruments of a transcribed song. MuScriptor gives every note a GM program (or kDrumProgram); notes without one
// (kNoProgram) are the user's own or come from Classic/Neural. Names, families (for the note colours) and the
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
// The notes with the selection set to exactly those of `program` (a new vector, the input stays as it is).
std::vector<NoteEvent> withInstrumentSelected(const std::vector<NoteEvent>& notes, int program);

}  // namespace ttr
