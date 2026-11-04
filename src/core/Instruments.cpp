#include "core/Instruments.h"

#include <algorithm>
#include <map>

namespace ttr {

namespace {

struct NamedProgram {
    int program;
    const char* name;
};

// The programs MuScriptor emits: one representative per instrument group of its tokenizer.
constexpr NamedProgram kNamedPrograms[] = {
    {0, "Piano"},            {2, "Electric Piano"},  {8, "Chromatic Perc."},   {16, "Organ"},
    {24, "Acoustic Guitar"}, {26, "Electric Guitar"}, {29, "Distorted Guitar"}, {32, "Acoustic Bass"},
    {33, "Bass"},            {40, "Violin"},          {41, "Viola"},            {42, "Cello"},
    {43, "Contrabass"},      {46, "Harp"},            {47, "Timpani"},          {48, "Strings"},
    {50, "Synth Strings"},   {52, "Voice"},           {55, "Orchestra Hit"},    {56, "Trumpet"},
    {57, "Trombone"},        {58, "Tuba"},            {60, "French Horn"},      {61, "Brass"},
    {64, "Alto Sax"},        {66, "Tenor Sax"},       {67, "Baritone Sax"},     {68, "Oboe"},
    {69, "English Horn"},    {70, "Bassoon"},         {71, "Clarinet"},         {72, "Flute"},
    {80, "Synth Lead"},      {88, "Synth Pad"},       {kDrumProgram, "Drums"},
};

constexpr int kProgramsPerFamily = 8;  // General MIDI groups programs in blocks of eight
constexpr int kTimpani = 47, kVoice = 52, kOrchestraHit = 55;

}  // namespace

bool isValidProgram(int program) { return program == kNoProgram || (program >= 0 && program <= kMaxProgram); }

std::string instrumentName(int program) {
    if (program == kNoProgram) return "Notes";
    for (const NamedProgram& p : kNamedPrograms)
        if (p.program == program) return p.name;
    return "Program " + std::to_string(program);
}

InstrumentFamily instrumentFamily(int program) {
    if (program == kDrumProgram) return InstrumentFamily::Drums;
    if (program < 0 || program >= kDrumProgram) return InstrumentFamily::None;
    if (program == kTimpani || program == kOrchestraHit) return InstrumentFamily::Percussion;
    if (program == kVoice) return InstrumentFamily::Voice;
    static constexpr InstrumentFamily kByBlock[] = {
        InstrumentFamily::Keys,  InstrumentFamily::Percussion, InstrumentFamily::Organ,      InstrumentFamily::Guitar,
        InstrumentFamily::Bass,  InstrumentFamily::Strings,    InstrumentFamily::Strings,    InstrumentFamily::Brass,
        InstrumentFamily::Reed,  InstrumentFamily::Winds,      InstrumentFamily::Synth,      InstrumentFamily::Synth,
        InstrumentFamily::Synth, InstrumentFamily::Percussion, InstrumentFamily::Percussion, InstrumentFamily::Percussion,
    };
    return kByBlock[program / kProgramsPerFamily];
}

std::vector<InstrumentSummary> instrumentsIn(const std::vector<NoteEvent>& notes) {
    std::map<int, int> counts;
    for (const NoteEvent& n : notes) ++counts[n.program];
    std::vector<InstrumentSummary> out;
    out.reserve(counts.size());
    for (const auto& [program, count] : counts) out.push_back({program, count});
    return out;
}

bool hasInstruments(const std::vector<NoteEvent>& notes) {
    return std::any_of(notes.begin(), notes.end(), [](const NoteEvent& n) { return n.program != kNoProgram; });
}

std::string instrumentAbbreviation(int program) {
    static const struct { int program; const char* abbreviation; } kChips[] = {
        {0, "PNO"},  {2, "EPN"},  {8, "CPR"},  {16, "ORG"}, {24, "AGT"}, {26, "GTR"}, {29, "DGT"}, {32, "ABS"}, {33, "BAS"},
        {40, "VLN"}, {41, "VLA"}, {42, "VLC"}, {43, "CBS"}, {46, "HRP"}, {47, "TMP"}, {48, "STR"}, {50, "SST"}, {52, "VOX"},
        {55, "OHT"}, {56, "TPT"}, {57, "TBN"}, {58, "TBA"}, {60, "HRN"}, {61, "BRS"}, {64, "ASX"}, {66, "TSX"}, {67, "BSX"},
        {68, "OBO"}, {69, "EHN"}, {70, "BSN"}, {71, "CLR"}, {72, "FLT"}, {80, "LED"}, {88, "PAD"}, {kDrumProgram, "DRM"},
    };
    for (const auto& c : kChips)
        if (c.program == program) return c.abbreviation;
    return program == kNoProgram ? "---" : std::to_string(program);
}

bool pitchRangeOf(const std::vector<NoteEvent>& notes, int program, int* lowest, int* highest) {
    bool found = false;
    for (const NoteEvent& n : notes) {
        if (n.program != program) continue;
        *lowest = found ? std::min(*lowest, n.pitch) : n.pitch;
        *highest = found ? std::max(*highest, n.pitch) : n.pitch;
        found = true;
    }
    return found;
}

std::string noteName(int pitch) {
    static const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return std::string(kNames[((pitch % 12) + 12) % 12]) + std::to_string(pitch / 12 - 1);
}

}  // namespace ttr
