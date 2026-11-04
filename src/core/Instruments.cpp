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

std::vector<NoteEvent> withInstrumentSelected(const std::vector<NoteEvent>& notes, int program) {
    std::vector<NoteEvent> out = notes;
    for (NoteEvent& n : out) n.selected = n.program == program;
    return out;
}

}  // namespace ttr
