#include "midi/MidiFileWriter.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ttr {

namespace {

constexpr int kChannels = 16;
constexpr uint8_t kNoteOn = 0x90, kNoteOff = 0x80, kProgramChange = 0xC0, kChannelPressure = 0xD0;
constexpr uint8_t kMetaTrackName = 0x03, kMetaEndOfTrack = 0x2F, kMetaTempo = 0x51, kMetaTimeSignature = 0x58;

void put16(std::vector<uint8_t>& b, uint32_t v) { b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v)); }
void put32(std::vector<uint8_t>& b, uint32_t v) { put16(b, v >> 16); put16(b, v & 0xFFFF); }

void putVarLen(std::vector<uint8_t>& b, uint32_t v) {
    uint8_t tmp[5];
    int n = 0;
    tmp[n++] = v & 0x7F;
    while ((v >>= 7) > 0) tmp[n++] = uint8_t((v & 0x7F) | 0x80);
    while (n > 0) b.push_back(tmp[--n]);
}

void putMeta(std::vector<uint8_t>& b, uint32_t delta, uint8_t type, const std::vector<uint8_t>& data) {
    putVarLen(b, delta);
    b.push_back(0xFF);
    b.push_back(type);
    putVarLen(b, uint32_t(data.size()));
    b.insert(b.end(), data.begin(), data.end());
}

void putTrackName(std::vector<uint8_t>& b, const std::string& name) {
    putMeta(b, 0, kMetaTrackName, std::vector<uint8_t>(name.begin(), name.end()));
}

void appendChunk(std::vector<uint8_t>& file, const char* tag, const std::vector<uint8_t>& body) {
    file.insert(file.end(), tag, tag + 4);
    put32(file, uint32_t(body.size()));
    file.insert(file.end(), body.begin(), body.end());
}

struct Event { uint32_t tick; int order; uint8_t status; uint8_t pitch; uint8_t vel; };

std::vector<uint8_t> tempoTrack(double bpm, const std::string& title) {
    std::vector<uint8_t> tempo;
    putTrackName(tempo, title + " Tempo");
    const uint32_t usPerQuarter = uint32_t(std::lround(60000000.0 / std::max(1.0, bpm)));
    putMeta(tempo, 0, kMetaTempo, {uint8_t(usPerQuarter >> 16), uint8_t(usPerQuarter >> 8), uint8_t(usPerQuarter)});
    putMeta(tempo, 0, kMetaTimeSignature, {4, 2, 24, 8});
    putMeta(tempo, 0, kMetaEndOfTrack, {});
    return tempo;
}

struct TickNote { uint32_t on, off; uint8_t pitch, vel; };

// Notes in ticks, a note-off never after the next note-on of the same pitch (rounding could otherwise let a note-on
// arrive while the same key still sounds, and the following note-off would cut the new note). Repeats on one tick drop.
std::vector<TickNote> tickNotes(const std::vector<MidiNote>& notes) {
    std::vector<TickNote> ticks;
    ticks.reserve(notes.size());
    for (const MidiNote& n : notes) {
        const uint32_t on = uint32_t(std::lround(std::max(0.0, n.startBeat) * kMidiPpq));
        const uint32_t len = std::max<uint32_t>(1, uint32_t(std::lround(n.lengthBeat * kMidiPpq)));
        ticks.push_back({on, on + len, uint8_t(std::clamp(n.pitch, 0, 127)), uint8_t(std::clamp(n.velocity, 1, 127))});
    }
    std::sort(ticks.begin(), ticks.end(), [](const TickNote& a, const TickNote& b) { return a.pitch != b.pitch ? a.pitch < b.pitch : a.on < b.on; });
    std::vector<TickNote> out;
    out.reserve(ticks.size());
    for (size_t i = 0; i < ticks.size(); ++i) {
        TickNote t = ticks[i];
        const bool hasNext = i + 1 < ticks.size() && ticks[i + 1].pitch == t.pitch;
        if (hasNext && ticks[i + 1].on == t.on) continue;  // the same key twice on one tick: keep the later one
        if (hasNext) t.off = std::min(t.off, ticks[i + 1].on);
        out.push_back(t);
    }
    return out;
}

std::vector<Event> noteEvents(const std::vector<MidiNote>& notes, uint8_t channel) {
    std::vector<Event> events;
    events.reserve(notes.size() * 2);
    for (const TickNote& t : tickNotes(notes)) {
        events.push_back({t.on, 1, uint8_t(kNoteOn | channel), t.pitch, t.vel});
        events.push_back({t.off, 0, uint8_t(kNoteOff | channel), t.pitch, 0});
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    return events;
}

std::vector<uint8_t> noteTrack(const MidiTrack& t, uint32_t clipTicks) {
    const uint8_t channel = uint8_t(std::clamp(t.channel, 0, kChannels - 1));
    std::vector<uint8_t> track;
    putTrackName(track, t.name);
    if (t.program >= 0 && t.program <= 127) {
        putVarLen(track, 0);
        track.push_back(uint8_t(kProgramChange | channel));
        track.push_back(uint8_t(t.program));
    }
    uint32_t last = 0;
    for (const Event& e : noteEvents(t.notes, channel)) {
        putVarLen(track, e.tick - last);
        last = e.tick;
        track.push_back(e.status);
        track.push_back(e.pitch);
        track.push_back(e.vel);
    }
    putMeta(track, std::max(last, clipTicks) - last, kMetaEndOfTrack, {});
    return track;
}

}  // namespace

std::vector<uint8_t> writeMidiTracks(const std::vector<MidiTrack>& tracks, double bpm, const std::string& title, double clipBeats) {
    std::vector<uint8_t> file;
    file.insert(file.end(), {'M', 'T', 'h', 'd'});
    put32(file, 6);
    put16(file, 1);
    put16(file, uint32_t(1 + tracks.size()));
    put16(file, kMidiPpq);
    appendChunk(file, "MTrk", tempoTrack(bpm, title));
    const uint32_t clipTicks = clipBeats > 0.0 ? uint32_t(std::lround(clipBeats * kMidiPpq)) : 0;
    for (const MidiTrack& t : tracks) appendChunk(file, "MTrk", noteTrack(t, clipTicks));
    return file;
}

std::vector<uint8_t> writeMidiFile(const std::vector<MidiNote>& notes, double bpm, const std::string& trackName, double clipBeats) {
    MidiTrack track;
    track.name = trackName;
    track.notes = notes;
    return writeMidiTracks({track}, bpm, trackName, clipBeats);
}

namespace {

struct Reader {
    const std::vector<uint8_t>& d;
    size_t pos = 0;
    bool ok = true;
    uint8_t u8() { if (pos >= d.size()) { ok = false; return 0; } return d[pos++]; }
    uint32_t be(int n) { uint32_t v = 0; for (int i = 0; i < n; ++i) v = (v << 8) | u8(); return v; }
    uint32_t varLen() { uint32_t v = 0; for (int i = 0; i < 4; ++i) { uint8_t b = u8(); v = (v << 7) | (b & 0x7F); if (!(b & 0x80)) break; } return v; }
};

struct OpenNote { uint32_t tick; int velocity; int program; };

struct TrackState {
    uint32_t tick = 0;
    uint8_t running = 0;
    int programs[kChannels];
    std::map<int, OpenNote> open;  // channel * 128 + pitch
    std::string name;
    TrackState() { std::fill(std::begin(programs), std::end(programs), -1); }
};

void parseMeta(Reader& r, int index, TrackState& s, ParsedMidi& out) {
    const uint8_t type = r.u8();
    const uint32_t len = r.varLen();
    std::vector<uint8_t> data;
    for (uint32_t i = 0; i < len && r.ok; ++i) data.push_back(r.u8());
    if (type == kMetaTempo && data.size() == 3) out.bpm = 60000000.0 / double((data[0] << 16) | (data[1] << 8) | data[2]);
    if (type == kMetaTrackName) s.name.assign(data.begin(), data.end());
    if (type == kMetaEndOfTrack && index >= 1) out.endBeat = std::max(out.endBeat, double(s.tick) / out.ppq);
}

void parseChannelEvent(Reader& r, uint8_t status, TrackState& s, ParsedMidi& out) {
    const uint8_t kind = status & 0xF0;
    const int channel = status & 0x0F;
    const int data1 = r.u8();
    const int data2 = (kind == kProgramChange || kind == kChannelPressure) ? 0 : r.u8();
    if (kind == kProgramChange) { s.programs[channel] = data1; return; }
    const int key = channel * 128 + data1;
    if (kind == kNoteOn && data2 > 0) {
        s.open[key] = {s.tick, data2, s.programs[channel]};
        return;
    }
    if (kind != kNoteOff && kind != kNoteOn) return;
    const auto it = s.open.find(key);
    if (it == s.open.end()) return;
    out.notes.push_back({data1, double(it->second.tick) / out.ppq, double(s.tick - it->second.tick) / out.ppq, it->second.velocity,
                         it->second.program, channel});
    s.open.erase(it);
}

// One MTrk chunk: meta data into `out`, notes appended to out.notes.
void parseTrack(Reader& r, int index, ParsedMidi& out) {
    const uint32_t trackLen = r.be(4);
    const size_t end = r.pos + trackLen;
    TrackState s;
    while (r.ok && r.pos < end) {
        s.tick += r.varLen();
        uint8_t status = r.u8();
        if (status < 0x80) { --r.pos; status = s.running; }
        if (status == 0xFF) { parseMeta(r, index, s, out); continue; }
        s.running = status;
        parseChannelEvent(r, status, s, out);
    }
    out.trackNames.push_back(s.name);
    if (index == 1) out.trackName = s.name;
    r.pos = end;
}

}  // namespace

ParsedMidi parseMidiFile(const std::vector<uint8_t>& bytes) {
    ParsedMidi out;
    Reader r{bytes};
    if (bytes.size() < 14 || std::string(bytes.begin(), bytes.begin() + 4) != "MThd") return out;
    r.pos = 4;
    r.be(4);
    out.format = int(r.be(2));
    out.tracks = int(r.be(2));
    out.ppq = int(r.be(2));
    if (out.ppq <= 0) return out;
    for (int t = 0; t < out.tracks && r.ok; ++t) {
        if (r.be(4) != 0x4D54726B) { r.ok = false; break; }
        parseTrack(r, t, out);
    }
    std::sort(out.notes.begin(), out.notes.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.pitch < b.pitch;
    });
    out.valid = r.ok;
    return out;
}

}  // namespace ttr
