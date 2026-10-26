// State robustness: fuzzing, migration from v3, editor-state codec and a crash-free decoder for hostile input.
#include <cmath>
#include <cstring>
#include <random>

#include "TestSignals.h"
#include "TestUtil.h"
#include "analysis/Spectrogram.h"
#include "analysis/WavePeaks.h"
#include "core/NoteEdit.h"
#include "core/Snapshot.h"
#include "state/Crc32.h"
#include "state/StateCodec.h"
#include "state/UiSettings.h"

using namespace ttr;

namespace {

constexpr int kFuzzCases = 12000;
constexpr size_t kHeader = 16;

std::vector<uint8_t> sampleState(ParamValues params, bool sync) {
    constexpr double kRate = 44100.0;
    const auto sig = tsig::synth({{57, 0.0, 1.0, 0.4f}, {64, 0.5, 1.5, 0.4f}}, kRate, 1.6, true);
    Recording rec;
    rec.spectrogram = computeSpectrogram(sig.data(), sig.size(), kRate);
    PeakBuilder peaks;
    peaks.prepare(kRate, 2.0);
    peaks.advance(sig.data(), sig.size());
    rec.peaks = peaks.finish();
    rec.chroma = spectrogramChroma(*rec.spectrogram);
    auto clip = std::make_shared<AudioClip>();
    for (int i = 0; i < 4000; ++i) clip->samples.push_back(int16_t((i % 50) * 200 - 5000));
    rec.audio = clip;
    rec.lengthSec = 1.6;
    rec.bpm = 111.0;
    rec.startPpq = 3.0;
    TimeGrid grid;
    grid.bpm = 111.0;
    auto notes = withNoteAdded({}, makeNote(57, 0.0, 1.0, grid, true));
    notes = withNoteAdded(notes, makeNote(64, 0.5, 1.5, grid, true));
    return encodeState(params, makeSnapshot(notes, rec, 5).get(), sync);
}

// The v3 layout, written independently of the codec: magic, version 3, parameters, then the v4 body without the sync byte.
std::vector<uint8_t> toLegacyV3(const std::vector<uint8_t>& v4) {
    std::vector<uint8_t> out;
    auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (8 * i))); };
    put32(0x31525454);
    put32(3);
    const size_t paramsEnd = kHeader + size_t(kNumParams) * sizeof(float);
    out.insert(out.end(), v4.begin() + kHeader, v4.begin() + ptrdiff_t(paramsEnd));
    out.insert(out.end(), v4.begin() + ptrdiff_t(paramsEnd) + 1, v4.end());  // skip the sync flag byte
    return out;
}

// Rebuilds the v4 header (length + CRC) after the payload was changed, so the parser itself is exercised.
void fixHeader(std::vector<uint8_t>* b) {
    if (b->size() < kHeader) return;
    const uint32_t length = uint32_t(b->size() - kHeader), crc = crc32(b->data() + kHeader, b->size() - kHeader);
    std::memcpy(b->data() + 8, &length, 4);
    std::memcpy(b->data() + 12, &crc, 4);
}

bool snapshotIsSane(const SnapshotPtr& s) {
    if (!s) return true;
    for (const NoteEvent& n : s->notes)
        if (n.pitch < 0 || n.pitch > 127 || !std::isfinite(n.startSec) || !std::isfinite(n.endSec) || n.endSec < n.startSec) return false;
    if (s->spectrogram) {
        const Spectrogram& sp = *s->spectrogram;
        if (sp.raw.size() != size_t(sp.frames) * kNumNotes || sp.harmonic.size() != sp.raw.size() || sp.percFrac.size() != size_t(sp.frames)) return false;
    }
    return !s->peaks || s->peaks->minMax.size() % 2 == 0;
}

}  // namespace

TEST(state_decoder_survives_12000_fuzzed_blobs) {
    ParamValues params = defaultParams();
    params[kParamGain] = 0.3f;
    const std::vector<uint8_t> valid = sampleState(params, true);
    const std::vector<uint8_t> legacy = toLegacyV3(valid);
    CHECK(valid.size() > 1000);
    DecodedState ok;
    CHECK(decodeState(valid.data(), valid.size(), &ok) && ok.snapshot && ok.syncToHost && ok.formatVersion == 4);

    std::mt19937 rng(0xC0FFEE);
    auto rnd = [&](size_t n) { return size_t(rng() % std::max<size_t>(1, n)); };
    int accepted = 0, rejected = 0, noReason = 0, insane = 0, exceptions = 0;
    for (int i = 0; i < kFuzzCases; ++i) {
        std::vector<uint8_t> blob;
        switch (i % 8) {
            case 0:  // pure noise
                blob.resize(rnd(400));
                for (uint8_t& b : blob) b = uint8_t(rng());
                break;
            case 1:  // noise behind a valid signature and a plausible header
                blob = {0x54, 0x54, 0x52, 0x31, uint8_t(3 + rnd(2)), 0, 0, 0};
                blob.resize(8 + rnd(600));
                for (size_t k = 8; k < blob.size(); ++k) blob[k] = uint8_t(rng());
                break;
            case 2:  // truncation of a valid state
                blob.assign(valid.begin(), valid.begin() + ptrdiff_t(rnd(valid.size())));
                break;
            case 3:  // bit flips, header untouched: the checksum must catch them
                blob = valid;
                for (size_t k = 0, n = 1 + rnd(6); k < n; ++k) blob[kHeader + rnd(blob.size() - kHeader)] ^= uint8_t(1u << rnd(8));
                break;
            case 4:  // payload mutations with a fixed-up header: the parser must cope
            case 5:
                blob = valid;
                for (size_t k = 0, n = 1 + rnd(8); k < n; ++k) blob[kHeader + rnd(blob.size() - kHeader)] = uint8_t(rng());
                fixHeader(&blob);
                break;
            case 6:  // hostile counts and sizes in the first 120 payload bytes, fixed header
                blob = valid;
                for (size_t k = 0, n = 1 + rnd(4); k < n; ++k) blob[kHeader + 52 + rnd(100)] = (rng() & 1) ? 0xFF : 0x7F;
                fixHeader(&blob);
                break;
            default:  // the legacy v3 layout, mutated and truncated
                blob = legacy;
                for (size_t k = 0, n = rnd(5); k < n; ++k) blob[8 + rnd(blob.size() - 8)] = uint8_t(rng());
                if (rnd(3) == 0) blob.resize(rnd(blob.size()));
                break;
        }
        DecodedState out;
        std::string reason;
        try {
            if (decodeState(blob.data(), blob.size(), &out, &reason)) {
                ++accepted;
                if (!snapshotIsSane(out.snapshot)) ++insane;
            } else {
                ++rejected;
                if (reason.empty()) ++noReason;
            }
            ParamValues pv;
            decodeStateParams(blob.data(), blob.size(), &pv);
        } catch (...) {
            ++exceptions;
        }
    }
    printf("    fuzz: %d accepted (mutations that stay valid), %d rejected\n", accepted, rejected);
    CHECK(exceptions == 0);
    CHECK_MSG(insane == 0, "%d accepted states broke an invariant", insane);
    CHECK_MSG(noReason == 0, "%d rejections without a reason", noReason);
    CHECK(rejected > kFuzzCases / 2);
    CHECK(accepted > 0);  // the benign mutations (bytes inside spectrogram data) still load
}

TEST(decoder_gives_clear_reasons) {
    DecodedState out;
    std::string reason;
    CHECK(!decodeState(nullptr, 0, &out, &reason) && reason.find("empty") != std::string::npos);
    const uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    CHECK(!decodeState(junk, sizeof junk, &out, &reason) && reason.find("not a Tonetrace state") != std::string::npos);
    std::vector<uint8_t> valid = sampleState(defaultParams(), false);
    std::vector<uint8_t> flipped = valid;
    flipped[kHeader + 40] ^= 1;
    CHECK(!decodeState(flipped.data(), flipped.size(), &out, &reason) && reason.find("checksum") != std::string::npos);
    CHECK(!decodeState(valid.data(), valid.size() - 5, &out, &reason) && reason.find("truncated") != std::string::npos);
    std::vector<uint8_t> newer = valid;
    newer[4] = 9;
    CHECK(!decodeState(newer.data(), newer.size(), &out, &reason) && reason.find("newer") != std::string::npos);
}

TEST(v3_states_migrate_to_v4) {
    ParamValues params = defaultParams();
    params[kParamGain] = 0.8f;
    params[kParamSnap] = 2.0f;
    params[kParamSoundVolume] = 0.25f;
    params[kParamHideDrums] = 0.0f;
    const std::vector<uint8_t> v4 = sampleState(params, true);
    const std::vector<uint8_t> v3 = toLegacyV3(v4);

    DecodedState fromV3, fromV4;
    std::string reason;
    CHECK_MSG(decodeState(v3.data(), v3.size(), &fromV3, &reason), "v3 state rejected: %s", reason.c_str());
    CHECK(decodeState(v4.data(), v4.size(), &fromV4));
    CHECK(fromV3.formatVersion == 3 && fromV4.formatVersion == 4);
    CHECK(fromV3.params == fromV4.params && fromV3.params[kParamGain] == 0.8f && fromV3.params[kParamSnap] == 2.0f && fromV3.params[kParamHideDrums] == 0.0f);
    CHECK(!fromV3.syncToHost && fromV4.syncToHost);  // new in v4: defaults to off for migrated projects
    CHECK(fromV3.snapshot && fromV4.snapshot && fromV3.snapshot->notes.size() == 2 && fromV3.snapshot->bpm == 111.0 && fromV3.snapshot->startPpq == 3.0);
    CHECK(fromV3.snapshot->spectrogram && fromV3.snapshot->spectrogram->frames == fromV4.snapshot->spectrogram->frames);
    CHECK(fromV3.snapshot->audio && fromV3.snapshot->audio->samples == fromV4.snapshot->audio->samples);

    // A hand-built v3 file without any recording: header, 13 parameters, "no snapshot".
    std::vector<uint8_t> tiny = {0x54, 0x54, 0x52, 0x31, 3, 0, 0, 0};
    for (int i = 0; i < kNumParams; ++i) {
        const float v = paramSpec(i).defaultValue;
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        tiny.insert(tiny.end(), p, p + 4);
    }
    tiny.push_back(0);
    DecodedState t;
    CHECK(decodeState(tiny.data(), tiny.size(), &t) && !t.snapshot && t.params == defaultParams());
    ParamValues onlyParams;
    CHECK(decodeStateParams(v3.data(), v3.size(), &onlyParams) && onlyParams[kParamGain] == 0.8f);

    // Saving a migrated project writes v4 and loads again.
    const std::vector<uint8_t> resaved = encodeState(fromV3.params, fromV3.snapshot.get(), fromV3.syncToHost);
    DecodedState again;
    CHECK(decodeState(resaved.data(), resaved.size(), &again) && again.formatVersion == 4 && again.snapshot && again.snapshot->notes.size() == 2);
}

TEST(editor_state_roundtrip_defaults_and_hostile_input) {
    UiSettings s;
    s.gain = 0.2f; s.range = 0.9f; s.speed = 3.5f; s.quickGrab = 0.4f; s.snap = 1;
    s.hideDrums = false; s.magnet = false; s.syncToHost = true;
    s.scrollSec = 12.5; s.scrollY = 321.0; s.width = 1300; s.height = 777;
    const std::vector<uint8_t> bytes = encodeUiSettings(s);
    UiSettings back;
    CHECK(decodeUiSettings(bytes.data(), bytes.size(), &back) && back == s);

    ParamValues p = defaultParams();
    applyViewParams(s, &p);
    CHECK(p[kParamGain] == 0.2f && p[kParamSpeed] == 3.5f && p[kParamSnap] == 1.0f && p[kParamHideDrums] == 0.0f && p[kParamMagnet] == 0.0f);
    CHECK(p[kParamSolo] == defaultParams()[kParamSolo] && p[kParamMix] == defaultParams()[kParamMix]);  // automatable ones are not touched
    CHECK(uiSettingsFromParams(p, true).gain == 0.2f && uiSettingsFromParams(p, true).syncToHost);

    UiSettings wild;
    wild.gain = NAN; wild.speed = 1e9f; wild.snap = -7; wild.scrollSec = -4; wild.scrollY = INFINITY; wild.width = 3; wild.height = 99999999;
    const UiSettings clean = sanitized(wild);
    CHECK(clean.gain == 0.5f && clean.speed == kSpeedMax && clean.snap == 0 && clean.scrollSec == 0.0 && clean.scrollY == -1.0);
    CHECK(clean.width == kMinWindowWidth && clean.height == kMaxWindowSize);

    std::mt19937 rng(7);
    int accepted = 0;
    for (int i = 0; i < 4000; ++i) {
        std::vector<uint8_t> blob = bytes;
        if (i % 3 == 0) blob.resize(rng() % (bytes.size() + 4));
        else for (size_t k = 0, n = 1 + rng() % 4; k < n; ++k) blob[rng() % blob.size()] = uint8_t(rng());
        UiSettings out;
        std::string reason;
        if (decodeUiSettings(blob.data(), blob.size(), &out, &reason)) { ++accepted; CHECK(sanitized(out) == out); }
        else CHECK(!reason.empty());
    }
    CHECK(accepted < 2000);
}
