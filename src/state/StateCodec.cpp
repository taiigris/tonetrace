#include "state/StateCodec.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Snapshot.h"
#include "state/Crc32.h"

namespace ttr {

namespace {

constexpr uint32_t kMagic = 0x31525454;  // "TTR1"
constexpr uint32_t kLegacyVersion = 3;  // no checksum, no sync flag
constexpr size_t kHeaderBytes = 16;     // magic, version, payload size, CRC32 (v4)
constexpr uint32_t kMaxNotesInState = 200000;
constexpr uint32_t kMaxFramesInState = 40000;
constexpr uint32_t kMaxPeaksInState = 40000;
constexpr uint32_t kMaxAudioSamplesInState = 4u * 1024u * 1024u;
constexpr double kMinClipRate = 8000.0, kMaxClipRate = 96000.0;
constexpr float kSpectrogramFloorDb = 60.0f;
constexpr float kPeakScale = 127.0f;

class Writer {
public:
    template <class T> void put(T v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    std::vector<uint8_t> bytes;
};

class Reader {
public:
    Reader(const uint8_t* d, size_t n) : data_(d), size_(n) {}
    template <class T> T get() {
        T v{};
        if (pos_ + sizeof(T) > size_) { ok_ = false; return v; }
        std::memcpy(&v, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return v;
    }
    bool ok() const { return ok_; }
    size_t remaining() const { return size_ - pos_; }
private:
    const uint8_t* data_;
    size_t size_, pos_ = 0;
    bool ok_ = true;
};

void putNote(Writer& w, const NoteEvent& n) {
    w.put<int32_t>(n.pitch);
    w.put<double>(n.startSec);
    w.put<double>(n.endSec);
    w.put<uint8_t>(n.selected ? 1 : 0);
}

NoteEvent getNote(Reader& r) {
    NoteEvent n;
    n.pitch = r.get<int32_t>();
    n.startSec = r.get<double>();
    n.endSec = r.get<double>();
    n.selected = r.get<uint8_t>() != 0;
    return n;
}

uint8_t quantizeValue(float v) {
    if (v <= 0.0f) return 0;
    const float db = 20.0f * std::log10(v);
    return uint8_t(std::lround(std::clamp(1.0f + db / kSpectrogramFloorDb, 0.0f, 1.0f) * 255.0f));
}

float dequantizeValue(uint8_t q) {
    return q == 0 ? 0.0f : std::pow(10.0f, ((q / 255.0f) - 1.0f) * kSpectrogramFloorDb / 20.0f);
}

// Spectrogram energies are stored on a 60 dB scale relative to the loudest raw value.
uint8_t quantizeRel(float v, float ref) { return quantizeValue(ref > 0.0f ? v / ref : 0.0f); }
float dequantizeRel(uint8_t q, float ref) { return dequantizeValue(q) * ref; }
uint8_t quantizeUnit(float v) { return uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); }

size_t spectrogramBytes(const Spectrogram& s) { return size_t(s.frames) * (2 * kNumNotes + 2); }

void putSpectrogram(Writer& w, const Spectrogram& s) {
    const float ref = s.peak > 0.0f ? s.peak : *std::max_element(s.raw.begin(), s.raw.end());
    w.put<double>(s.hopSec);
    w.put<double>(s.lengthSec);
    w.put<uint32_t>(uint32_t(s.frames));
    w.put<float>(ref);
    for (float v : s.raw) w.put<uint8_t>(quantizeRel(v, ref));
    for (float v : s.harmonic) w.put<uint8_t>(quantizeRel(v, ref));
    for (float v : s.percFrac) w.put<uint8_t>(quantizeUnit(v));
    for (float v : s.flux) w.put<uint8_t>(quantizeUnit(v));
}

std::shared_ptr<Spectrogram> getSpectrogram(Reader& r) {
    auto s = std::make_shared<Spectrogram>();
    s->hopSec = r.get<double>();
    s->lengthSec = r.get<double>();
    const uint32_t frames = r.get<uint32_t>();
    const float ref = r.get<float>();
    if (!r.ok() || frames == 0 || frames > kMaxFramesInState || !(s->hopSec > 0.0) || !std::isfinite(ref) || ref <= 0.0f ||
        r.remaining() < size_t(frames) * (2 * kNumNotes + 2))
        return nullptr;
    s->frames = int(frames);
    s->raw.resize(size_t(frames) * kNumNotes);
    s->harmonic.resize(s->raw.size());
    s->percFrac.resize(frames);
    s->flux.resize(frames);
    for (float& v : s->raw) v = dequantizeRel(r.get<uint8_t>(), ref);
    for (float& v : s->harmonic) v = dequantizeRel(r.get<uint8_t>(), ref);
    for (float& v : s->percFrac) v = r.get<uint8_t>() / 255.0f;
    for (float& v : s->flux) v = r.get<uint8_t>() / 255.0f;
    s->peak = ref;
    return r.ok() ? s : nullptr;
}

constexpr size_t kNoteStateBytes = 21;  // pitch, start, end, selected

bool validNote(const NoteEvent& n) {
    return n.pitch >= 0 && n.pitch <= 127 && std::isfinite(n.startSec) && std::isfinite(n.endSec) && n.endSec >= n.startSec;
}

bool fail(std::string* reason, const char* text) {
    if (reason) *reason = text;
    return false;
}

// Validates the header (v4: size and CRC) and positions `payload` at the parameter block.
bool readHeader(const uint8_t* data, size_t size, uint32_t* version, const uint8_t** payload, size_t* payloadSize, std::string* reason) {
    if (!data || size == 0) return fail(reason, "the state is empty");
    if (size < 8) return fail(reason, "the state is truncated");
    Reader head(data, size);
    if (head.get<uint32_t>() != kMagic) return fail(reason, "not a Tonetrace state (bad signature)");
    *version = head.get<uint32_t>();
    if (*version == kLegacyVersion) {
        *payload = data + 8;
        *payloadSize = size - 8;
        return true;
    }
    if (*version > kStateFormatVersion) return fail(reason, "the state was saved by a newer Tonetrace version");
    if (*version != kStateFormatVersion) return fail(reason, "unknown state format version");
    if (size < kHeaderBytes) return fail(reason, "the state is truncated");
    const uint32_t length = head.get<uint32_t>(), crc = head.get<uint32_t>();
    if (length != size - kHeaderBytes) return fail(reason, "the state is truncated or has extra data");
    if (crc32(data + kHeaderBytes, length) != crc) return fail(reason, "checksum mismatch (the state is damaged)");
    *payload = data + kHeaderBytes;
    *payloadSize = length;
    return true;
}

bool readParams(Reader& r, ParamValues* params, std::string* reason) {
    *params = defaultParams();
    for (int i = 0; i < kNumParams; ++i) (*params)[size_t(i)] = r.get<float>();
    if (!r.ok()) return fail(reason, "the parameter block is truncated");
    for (int i = 0; i < kNumParams; ++i) {
        if (!std::isfinite((*params)[size_t(i)])) return fail(reason, "invalid parameter value");
        (*params)[size_t(i)] = std::clamp((*params)[size_t(i)], paramSpec(i).minValue, paramSpec(i).maxValue);
    }
    (*params)[kParamRecord] = 0.0f;  // never restore a running recording or preview
    (*params)[kParamPreview] = 0.0f;
    return true;
}

void putPeaks(Writer& w, const WavePeaks& peaks) {
    w.put<uint32_t>(uint32_t(peaks.minMax.size()));
    for (float v : peaks.minMax) w.put<int8_t>(int8_t(std::lround(std::clamp(v, -1.0f, 1.0f) * kPeakScale)));
}

std::shared_ptr<WavePeaks> getPeaks(Reader& r) {
    const uint32_t values = r.get<uint32_t>();
    if (!r.ok() || values % 2 != 0 || values > 2 * kMaxPeaksInState || r.remaining() < values) return nullptr;
    auto peaks = std::make_shared<WavePeaks>();
    peaks->minMax.resize(values);
    for (float& v : peaks->minMax) v = float(r.get<int8_t>()) / kPeakScale;
    return peaks;
}

void putAudio(Writer& w, const AudioClip& clip) {
    w.put<double>(clip.sampleRate);
    w.put<uint32_t>(uint32_t(clip.samples.size()));
    for (int16_t v : clip.samples) w.put<int16_t>(v);
}

std::shared_ptr<AudioClip> getAudio(Reader& r) {
    auto clip = std::make_shared<AudioClip>();
    clip->sampleRate = r.get<double>();
    const uint32_t count = r.get<uint32_t>();
    if (!r.ok() || !(clip->sampleRate >= kMinClipRate && clip->sampleRate <= kMaxClipRate) || count > kMaxAudioSamplesInState ||
        r.remaining() < size_t(count) * sizeof(int16_t))
        return nullptr;
    clip->samples.resize(count);
    for (int16_t& v : clip->samples) v = r.get<int16_t>();
    return clip;
}

}  // namespace

std::vector<uint8_t> encodeState(const ParamValues& params, const Snapshot* snap, bool syncToHost) {
    Writer w;
    for (float p : params) w.put<float>(p);
    w.put<uint8_t>(syncToHost ? 1 : 0);
    w.put<uint8_t>(snap ? 1 : 0);
    if (snap) {
        w.put<double>(snap->lengthSec);
        w.put<double>(snap->bpm);
        w.put<double>(snap->startPpq);
        w.put<uint64_t>(snap->version);
        for (float v : snap->chroma) w.put<float>(v);
        w.put<uint32_t>(uint32_t(snap->notes.size()));
        for (const NoteEvent& n : snap->notes) putNote(w, n);

        const Spectrogram* spec = snap->spectrogram.get();
        const bool storeSpectrogram = spec && spectrogramBytes(*spec) < kMaxSpectrogramStateBytes;
        w.put<uint8_t>(storeSpectrogram ? 1 : 0);
        if (storeSpectrogram) putSpectrogram(w, *spec);
        w.put<uint8_t>(snap->peaks ? 1 : 0);
        if (snap->peaks) putPeaks(w, *snap->peaks);
        w.put<uint8_t>(snap->audio ? 1 : 0);
        if (snap->audio) putAudio(w, *snap->audio);
    }
    Writer out;
    out.put<uint32_t>(kMagic);
    out.put<uint32_t>(kStateFormatVersion);
    out.put<uint32_t>(uint32_t(w.bytes.size()));
    out.put<uint32_t>(crc32(w.bytes.data(), w.bytes.size()));
    out.bytes.insert(out.bytes.end(), w.bytes.begin(), w.bytes.end());
    return out.bytes;
}

bool decodeStateParams(const uint8_t* data, size_t size, ParamValues* out, std::string* reason) {
    uint32_t version = 0;
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    if (!readHeader(data, size, &version, &payload, &payloadSize, reason)) return false;
    Reader r(payload, payloadSize);
    return readParams(r, out, reason);
}

namespace {

bool decodeSnapshot(Reader& r, SnapshotPtr* out, std::string* reason) {
    Recording rec;
    rec.lengthSec = r.get<double>();
    rec.bpm = r.get<double>();
    rec.startPpq = r.get<double>();
    const uint64_t version = r.get<uint64_t>();
    if (!r.ok() || !std::isfinite(rec.bpm) || rec.bpm < 10 || rec.bpm > 999 || !std::isfinite(rec.lengthSec) || !std::isfinite(rec.startPpq))
        return fail(reason, "invalid recording header");
    for (float& v : rec.chroma) {
        v = r.get<float>();
        if (!std::isfinite(v)) return fail(reason, "invalid key data");
    }
    const uint32_t count = r.get<uint32_t>();
    if (!r.ok() || count > kMaxNotesInState || r.remaining() < size_t(count) * kNoteStateBytes) return fail(reason, "invalid note count");
    std::vector<NoteEvent> notes;
    notes.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const NoteEvent n = getNote(r);
        if (!validNote(n)) return fail(reason, "invalid note data");
        notes.push_back(n);
    }
    if (r.get<uint8_t>() != 0 && !(rec.spectrogram = getSpectrogram(r))) return fail(reason, "invalid spectrogram data");
    if (r.get<uint8_t>() != 0 && !(rec.peaks = getPeaks(r))) return fail(reason, "invalid waveform data");
    if (r.get<uint8_t>() != 0 && !(rec.audio = getAudio(r))) return fail(reason, "invalid audio data");
    if (!r.ok()) return fail(reason, "the state is truncated");
    *out = makeSnapshot(std::move(notes), rec, version);
    return true;
}

}  // namespace

bool decodeState(const uint8_t* data, size_t size, DecodedState* out, std::string* reason) {
    uint32_t version = 0;
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    if (!readHeader(data, size, &version, &payload, &payloadSize, reason)) return false;
    Reader r(payload, payloadSize);
    DecodedState state;
    state.formatVersion = version;
    if (!readParams(r, &state.params, reason)) return false;
    if (version >= kStateFormatVersion) state.syncToHost = r.get<uint8_t>() != 0;
    const uint8_t hasSnapshot = r.get<uint8_t>();
    if (!r.ok()) return fail(reason, "the state is truncated");
    if (hasSnapshot != 0 && !decodeSnapshot(r, &state.snapshot, reason)) return false;
    *out = std::move(state);
    return true;
}

}  // namespace ttr
