#include "midi/MidiFileWriter.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace ttr {

namespace {

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
    putMeta(b, 0, 0x03, std::vector<uint8_t>(name.begin(), name.end()));
}

void appendChunk(std::vector<uint8_t>& file, const char* tag, const std::vector<uint8_t>& body) {
    file.insert(file.end(), tag, tag + 4);
    put32(file, uint32_t(body.size()));
    file.insert(file.end(), body.begin(), body.end());
}

struct Event { uint32_t tick; int order; uint8_t status; uint8_t pitch; uint8_t vel; };

}  // namespace

std::vector<uint8_t> writeMidiFile(const std::vector<MidiNote>& notes, double bpm, const std::string& trackName, double clipBeats) {
    std::vector<uint8_t> file;
    file.insert(file.end(), {'M', 'T', 'h', 'd'});
    put32(file, 6);
    put16(file, 1);
    put16(file, 2);
    put16(file, kMidiPpq);

    // Track 0: tempo map
    std::vector<uint8_t> tempo;
    putTrackName(tempo, trackName + " Tempo");
    const uint32_t usPerQuarter = uint32_t(std::lround(60000000.0 / std::max(1.0, bpm)));
    putMeta(tempo, 0, 0x51, {uint8_t(usPerQuarter >> 16), uint8_t(usPerQuarter >> 8), uint8_t(usPerQuarter)});
    putMeta(tempo, 0, 0x58, {4, 2, 24, 8});
    putMeta(tempo, 0, 0x2F, {});
    appendChunk(file, "MTrk", tempo);

    // Track 1: notes
    std::vector<Event> events;
    for (const MidiNote& n : notes) {
        const uint32_t on = uint32_t(std::lround(std::max(0.0, n.startBeat) * kMidiPpq));
        const uint32_t len = std::max<uint32_t>(1, uint32_t(std::lround(n.lengthBeat * kMidiPpq)));
        const uint8_t pitch = uint8_t(std::clamp(n.pitch, 0, 127));
        const uint8_t vel = uint8_t(std::clamp(n.velocity, 1, 127));
        events.push_back({on, 1, 0x90, pitch, vel});
        events.push_back({on + len, 0, 0x80, pitch, 0});
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    std::vector<uint8_t> track;
    putTrackName(track, trackName);
    uint32_t last = 0;
    for (const Event& e : events) {
        putVarLen(track, e.tick - last);
        last = e.tick;
        track.push_back(e.status);
        track.push_back(e.pitch);
        track.push_back(e.vel);
    }
    const uint32_t clipTicks = clipBeats > 0.0 ? uint32_t(std::lround(clipBeats * kMidiPpq)) : 0;
    putMeta(track, std::max(last, clipTicks) - last, 0x2F, {});  // end of track
    appendChunk(file, "MTrk", track);
    return file;
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
    std::map<int, std::pair<uint32_t, int>> open;  // pitch -> (start tick, velocity)
    for (int t = 0; t < out.tracks && r.ok; ++t) {
        if (r.be(4) != 0x4D54726B) { r.ok = false; break; }
        const uint32_t trackLen = r.be(4);
        const size_t end = r.pos + trackLen;
        uint32_t tick = 0;
        uint8_t running = 0;
        while (r.ok && r.pos < end) {
            tick += r.varLen();
            uint8_t status = r.u8();
            if (status < 0x80) { --r.pos; status = running; }
            if (status == 0xFF) {
                const uint8_t type = r.u8();
                const uint32_t len = r.varLen();
                std::vector<uint8_t> data;
                for (uint32_t i = 0; i < len; ++i) data.push_back(r.u8());
                if (type == 0x51 && len == 3) out.bpm = 60000000.0 / double((data[0] << 16) | (data[1] << 8) | data[2]);
                if (type == 0x03 && t == 1) out.trackName.assign(data.begin(), data.end());
                if (type == 0x2F && t == 1 && out.ppq > 0) out.endBeat = double(tick) / out.ppq;
                continue;
            }
            running = status;
            const uint8_t kind = status & 0xF0;
            const int pitch = r.u8();
            const int vel = (kind == 0xC0 || kind == 0xD0) ? 0 : r.u8();
            if (kind == 0x90 && vel > 0) {
                open[pitch] = {tick, vel};
            } else if (kind == 0x80 || kind == 0x90) {
                auto it = open.find(pitch);
                if (it == open.end()) continue;
                out.notes.push_back({pitch, double(it->second.first) / out.ppq,
                                     double(tick - it->second.first) / out.ppq, it->second.second});
                open.erase(it);
            }
        }
        r.pos = end;
    }
    std::sort(out.notes.begin(), out.notes.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.pitch < b.pitch;
    });
    out.valid = r.ok;
    return out;
}

}  // namespace ttr
