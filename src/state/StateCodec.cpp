#include "state/StateCodec.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "state/Crc32.h"

namespace ttr {

namespace {

constexpr uint32_t kMagic = 0x31525454;  // "TTR1"
constexpr size_t kHeaderBytes = 16;      // magic, version, payload size, CRC32
constexpr uint32_t kMaxNotesInState = 400000;
constexpr uint32_t kMaxMixEntries = 256;
constexpr size_t kMaxStringBytes = 4096;
constexpr double kMinClipRate = 8000.0, kMaxClipRate = 192000.0;
constexpr double kMaxLengthSec = 3600.0;
constexpr uint8_t kMuteFlag = 1, kSoloFlag = 2;
constexpr size_t kNoteBytes = 2 + 8 + 8 + 2;  // pitch, start, end, program

class Writer {
public:
    template <class T> void put(T v) {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    void putString(const std::string& text) {
        const size_t n = std::min(text.size(), kMaxStringBytes);
        put<uint16_t>(uint16_t(n));
        bytes.insert(bytes.end(), text.begin(), text.begin() + ptrdiff_t(n));
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
    bool getString(std::string* out) {
        const uint16_t n = get<uint16_t>();
        if (!ok_ || n > kMaxStringBytes || remaining() < n) return ok_ = false;
        out->assign(reinterpret_cast<const char*>(data_ + pos_), n);
        pos_ += n;
        return true;
    }
    bool ok() const { return ok_; }
    size_t remaining() const { return size_ - pos_; }

private:
    const uint8_t* data_;
    size_t size_, pos_ = 0;
    bool ok_ = true;
};

bool fail(std::string* reason, const char* text) {
    if (reason) *reason = text;
    return false;
}

bool validProgram(int p) { return p == kNoProgram || (p >= 0 && p <= kMaxProgram); }

// Validates the header (size and CRC) and positions `payload` at the parameter block.
bool readHeader(const uint8_t* data, size_t size, uint32_t* version, const uint8_t** payload, size_t* payloadSize, std::string* reason) {
    if (!data || size == 0) return fail(reason, "the state is empty");
    if (size < 8) return fail(reason, "the state is truncated");
    Reader head(data, size);
    if (head.get<uint32_t>() != kMagic) return fail(reason, "not a Tonetrace state (bad signature)");
    *version = head.get<uint32_t>();
    if (*version > kStateFormatVersion) return fail(reason, "the state was saved by a newer Tonetrace version");
    if (*version < kStateFormatVersion) return fail(reason, "the state was saved by Tonetrace 4 - version 5 starts with a new song (load the song again)");
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
    (*params)[kParamRecord] = 0.0f;  // never restore a running recording
    return true;
}

void putMixer(Writer& w, const MixerState& mixer) {
    const auto& entries = mixer.entries();
    const size_t n = std::min<size_t>(entries.size(), kMaxMixEntries);
    w.put<uint16_t>(uint16_t(n));
    size_t written = 0;
    for (const auto& [program, mix] : entries) {
        if (written++ == n) break;
        w.put<int16_t>(int16_t(program));
        w.put<uint8_t>(uint8_t((mix.mute ? kMuteFlag : 0) | (mix.solo ? kSoloFlag : 0)));
        w.put<float>(mix.gain);
    }
}

bool getMixer(Reader& r, MixerState* out) {
    const uint16_t n = r.get<uint16_t>();
    if (!r.ok() || n > kMaxMixEntries) return false;
    MixerState mixer;
    for (uint16_t i = 0; i < n; ++i) {
        const int program = r.get<int16_t>();
        const uint8_t flags = r.get<uint8_t>();
        const float gain = r.get<float>();
        if (!r.ok() || !validProgram(program) || !std::isfinite(gain)) return false;
        mixer = mixer.with(program, {(flags & kMuteFlag) != 0, (flags & kSoloFlag) != 0, gain});
    }
    *out = mixer;
    return true;
}

void putAudio(Writer& w, const SongAudio& audio) {
    w.put<double>(audio.sampleRate);
    w.put<uint8_t>(uint8_t(audio.channels));
    w.put<uint32_t>(uint32_t(audio.samples.size()));
    for (int16_t v : audio.samples) w.put<int16_t>(v);
}

std::shared_ptr<SongAudio> getAudio(Reader& r) {
    auto audio = std::make_shared<SongAudio>();
    audio->sampleRate = r.get<double>();
    audio->channels = r.get<uint8_t>();
    const uint32_t count = r.get<uint32_t>();
    if (!r.ok() || !(audio->sampleRate >= kMinClipRate && audio->sampleRate <= kMaxClipRate) || audio->channels < 1 || audio->channels > 2 ||
        count > kMaxStateAudioSamples || count % uint32_t(audio->channels) != 0 || r.remaining() < size_t(count) * sizeof(int16_t))
        return nullptr;
    audio->samples.resize(count);
    for (int16_t& v : audio->samples) v = r.get<int16_t>();
    return audio;
}

void putDocument(Writer& w, const Document& doc) {
    w.putString(doc.sourceName);
    w.putString(doc.sourcePath);
    w.put<uint8_t>(doc.isRecording ? 1 : 0);
    w.put<double>(doc.lengthSec);
    // A transcription that was still running is stored as cancelled (its notes so far are kept).
    const TranscriptionPhase phase = doc.transcription.isActive() ? TranscriptionPhase::Cancelled : doc.transcription.phase;
    w.put<uint8_t>(uint8_t(phase));
    w.put<double>(doc.transcription.finalizedSec);
    w.put<uint32_t>(uint32_t(std::min<size_t>(doc.notes.size(), kMaxNotesInState)));
    size_t written = 0;
    for (const NoteEvent& n : doc.notes) {
        if (written++ == kMaxNotesInState) break;
        w.put<int16_t>(int16_t(n.pitch));
        w.put<double>(n.startSec);
        w.put<double>(n.endSec);
        w.put<int16_t>(int16_t(n.program));
    }
    const bool storeAudio = doc.isRecording && doc.audio && doc.audio->samples.size() <= kMaxStateAudioSamples;
    w.put<uint8_t>(storeAudio ? 1 : 0);
    if (storeAudio) putAudio(w, *doc.audio);
}

bool getDocument(Reader& r, DocumentPtr* out, std::string* reason) {
    auto doc = std::make_shared<Document>();
    if (!r.getString(&doc->sourceName) || !r.getString(&doc->sourcePath)) return fail(reason, "invalid song name");
    doc->isRecording = r.get<uint8_t>() != 0;
    doc->lengthSec = r.get<double>();
    const uint8_t phase = r.get<uint8_t>();
    doc->transcription.finalizedSec = r.get<double>();
    if (!r.ok() || !std::isfinite(doc->lengthSec) || doc->lengthSec < 0.0 || doc->lengthSec > kMaxLengthSec ||
        phase > uint8_t(TranscriptionPhase::Cancelled) || !std::isfinite(doc->transcription.finalizedSec))
        return fail(reason, "invalid document header");
    doc->transcription.phase = TranscriptionPhase(phase);
    if (doc->transcription.isActive()) doc->transcription.phase = TranscriptionPhase::Cancelled;
    doc->transcription.progress = doc->transcription.phase == TranscriptionPhase::Done ? 1.0f : 0.0f;
    const uint32_t count = r.get<uint32_t>();
    if (!r.ok() || count > kMaxNotesInState || r.remaining() < size_t(count) * kNoteBytes) return fail(reason, "invalid note count");
    doc->notes.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        NoteEvent n;
        n.pitch = r.get<int16_t>();
        n.startSec = r.get<double>();
        n.endSec = r.get<double>();
        n.program = r.get<int16_t>();
        if (n.pitch < 0 || n.pitch > 127 || !std::isfinite(n.startSec) || !std::isfinite(n.endSec) || n.endSec < n.startSec || !validProgram(n.program))
            return fail(reason, "invalid note data");
        doc->notes.push_back(n);
    }
    if (r.get<uint8_t>() != 0 && !(doc->audio = getAudio(r))) return fail(reason, "invalid audio data");
    if (!r.ok()) return fail(reason, "the state is truncated");
    std::stable_sort(doc->notes.begin(), doc->notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.startSec < b.startSec; });
    *out = doc;
    return true;
}

}  // namespace

std::vector<uint8_t> encodeState(const ParamValues& params, const Document* document, const MixerState& mixer, double exportBpm) {
    Writer w;
    for (float p : params) w.put<float>(p);
    w.put<double>(std::isfinite(exportBpm) && exportBpm > 0.0 ? exportBpm : 0.0);
    putMixer(w, mixer);
    w.put<uint8_t>(document ? 1 : 0);
    if (document) putDocument(w, *document);
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

bool decodeState(const uint8_t* data, size_t size, DecodedState* out, std::string* reason) {
    uint32_t version = 0;
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
    if (!readHeader(data, size, &version, &payload, &payloadSize, reason)) return false;
    Reader r(payload, payloadSize);
    DecodedState state;
    state.formatVersion = version;
    if (!readParams(r, &state.params, reason)) return false;
    state.exportBpm = r.get<double>();
    if (!r.ok() || !std::isfinite(state.exportBpm) || state.exportBpm < 0.0) return fail(reason, "invalid export tempo");
    if (!getMixer(r, &state.mixer)) return fail(reason, "invalid instrument mix");
    const uint8_t hasDocument = r.get<uint8_t>();
    if (!r.ok()) return fail(reason, "the state is truncated");
    if (hasDocument != 0 && !getDocument(r, &state.document, reason)) return false;
    *out = std::move(state);
    return true;
}

}  // namespace ttr
