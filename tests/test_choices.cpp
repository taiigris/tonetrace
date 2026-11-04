// The user's choices for the model: its size (small, medium, large) and the instruments. Without focus the full analysis
// is kept and the selection only filters it (at once); with focus the model itself listens only for the selection
// (re-analysis). The choices in the project state, and v6 states without them.
#include <cstring>

#include "TestKernel.h"
#include "TestUtil.h"
#include "analysis/ml/MuScriptorEngine.h"
#include "core/Instruments.h"
#include "midi/ExportPlanner.h"
#include "state/Crc32.h"
#include "state/StateCodec.h"

using namespace ttr;

namespace {

std::vector<std::vector<TranscribedNote>> twoChunks() { return {{{0.10, 1.90, 45, false, 33}}, {{1.00, 1.01, 36, true, 128}}}; }

DocumentPtr waitForDoc(TonetraceKernel& k, const std::function<bool(const Document&)>& cond) {
    DocumentPtr doc;
    tk::waitFor([&] { doc = k.document(); return doc && cond(*doc); });
    return doc;
}

}  // namespace

TEST(the_model_knows_35_instruments_ordered_by_family) {
    const std::vector<int>& all = selectableInstruments();
    CHECK(all.size() == 35 && all.front() == 0 && all.back() == kDrumProgram);
    for (size_t i = 1; i < all.size(); ++i) CHECK(int(instrumentFamily(all[i - 1])) <= int(instrumentFamily(all[i])));
    CHECK(instrumentFamilyName(InstrumentFamily::Guitar) == "Guitars" && instrumentFamilyName(InstrumentFamily::Drums) == "Drums");
}

TEST(a_selection_is_normalised_and_summarised) {
    CHECK(normalizedSelection({kDrumProgram, 33, 33, 97, -1}) == std::vector<int>({33, kDrumProgram}));  // 97 has no group
    CHECK(normalizedSelection(selectableInstruments()).empty());  // every instrument = no restriction
    CHECK(selectionSummary({}) == "All instruments" && selectionSummary({kDrumProgram, 33}) == "Bass + Drums" && selectionSummary({0, 26, 33}) == "3 instruments");
}

TEST(model_sizes_have_ids_labels_and_downloads) {
    ModelSize size = ModelSize::Medium;
    CHECK(parseModelSize("small", &size) && size == ModelSize::Small && parseModelSize("large", &size) && size == ModelSize::Large);
    CHECK(!parseModelSize("huge", &size) && size == ModelSize::Large);
    CHECK(std::string(modelSizeInfo(ModelSize::Medium).label) == "Medium" && std::string(modelSizeInfo(ModelSize::Large).downloadText) == "2.7 GB");
}

TEST(choosing_another_model_size_releases_the_loaded_model_and_is_stored) {
    std::vector<ModelSize> stored;
    std::atomic<int> loads{0};
    setModelSizeStore([&stored](ModelSize s) { stored.push_back(s); });
    setModelAvailability([](ModelSize s) { return s == ModelSize::Small; });
    setTranscriberLoader([&loads](std::string*) -> std::shared_ptr<NoteTranscriber> {
        ++loads;
        return std::make_shared<tk::FakeTranscriber>(twoChunks());
    });
    CHECK(modelSize() == kDefaultModelSize && noteTranscriber() && noteTranscriber() && loads == 1);  // loaded once, then kept
    setModelSize(ModelSize::Small);
    CHECK(modelSize() == ModelSize::Small && transcriberStatus() == "not loaded yet" && stored == std::vector<ModelSize>({ModelSize::Small}));
    CHECK(noteTranscriber() && loads == 2);  // the new size is loaded on the next song
    setModelSize(ModelSize::Small);          // no change: nothing released or stored
    CHECK(loads == 2 && stored.size() == 1);
    CHECK(isModelAvailable(ModelSize::Small) && !isModelAvailable(ModelSize::Large));
    setModelSize(kDefaultModelSize);
    setModelSizeStore(nullptr);
    setModelAvailability(nullptr);
    setTranscriberLoader(nullptr);
    CHECK(!isModelAvailable(ModelSize::Small));
}

TEST(a_selection_keeps_only_those_instruments_at_once_without_a_new_analysis) {
    tk::installFakeDecoder(3.0);
    auto model = std::make_shared<tk::FakeTranscriber>(twoChunks());
    setTranscriber(model);
    TonetraceKernel k;
    k.prepare(tk::kRate, tk::kBlock);
    CHECK(k.loadSong("/tmp/Song.wav") && tk::waitIdle(k) && k.document()->notes.size() == 2);
    k.setInstrumentSelection({kDrumProgram});
    DocumentPtr doc = k.document();
    CHECK(doc && doc->notes.size() == 1 && doc->notes[0].program == kDrumProgram && doc->detectedNotes.size() == 2);
    CHECK(!k.needsReanalysis() && model->callCount() == 1 && model->programsAsked().empty());  // the model heard everything
    CHECK(parseMidiFile(k.buildMidi()).notes.size() == 1);  // the export follows the selection
    const std::vector<uint8_t> saved = k.saveState();
    k.setInstrumentSelection({});
    CHECK(k.document()->notes.size() == 2);  // back to all, still without a new analysis
    TonetraceKernel restored;
    CHECK(restored.loadState(saved.data(), saved.size()) && restored.document() && restored.document()->notes.size() == 1);
    restored.setInstrumentSelection({});
    CHECK(restored.document()->notes.size() == 2);  // the state keeps everything that was detected
    restored.deactivate();
    k.deactivate();
    setTranscriber(nullptr);
    setAudioDecoder(nullptr);
}

TEST(with_focus_the_model_listens_for_the_selected_instruments_after_a_reanalysis) {
    tk::installFakeDecoder(3.0);
    auto model = std::make_shared<tk::FakeTranscriber>(twoChunks());
    setTranscriber(model);
    TonetraceKernel k;
    k.prepare(tk::kRate, tk::kBlock);
    CHECK(!k.reanalyse() && !k.needsReanalysis());  // nothing loaded
    CHECK(k.loadSong("/tmp/Song.wav") && tk::waitIdle(k));
    CHECK(model->programsAsked().empty() && model->callCount() == 1);
    k.setInstrumentSelection({kDrumProgram, 33});
    CHECK(k.instrumentSelection() == std::vector<int>({33, kDrumProgram}) && !k.needsReanalysis());
    k.setModelFocus(true);
    CHECK(k.modelFocus() && k.needsReanalysis());
    k.play();
    CHECK(k.reanalyse());
    DocumentPtr doc = k.document();
    CHECK(doc && doc->transcription.phase == TranscriptionPhase::Running && doc->notes.empty() && doc->audio);  // starts over, keeps playing
    CHECK(k.isPlaying() && tk::waitIdle(k));
    doc = k.document();
    CHECK(doc && doc->transcription.phase == TranscriptionPhase::Done && doc->notes.size() == 2);
    CHECK(model->programsAsked() == std::vector<int>({33, kDrumProgram}) && model->callCount() == 2 && !k.needsReanalysis());
    k.setModelFocus(false);
    CHECK(k.needsReanalysis());  // back to the full analysis needs the model again
    k.pause();
    k.deactivate();
    setTranscriber(nullptr);
    setAudioDecoder(nullptr);
}

TEST(a_new_analysis_ignores_the_late_updates_of_the_one_it_replaced) {
    tk::installFakeDecoder(3.0);
    auto slow = std::make_shared<tk::FakeTranscriber>(twoChunks(), true);
    setTranscriber(slow);
    TonetraceKernel k;
    k.prepare(tk::kRate, tk::kBlock);
    CHECK(k.loadSong("/tmp/Song.wav"));
    waitForDoc(k, [](const Document& d) { return d.audio != nullptr; });
    slow->release();  // the first analysis delivers its first chunk
    CHECK(waitForDoc(k, [](const Document& d) { return d.notes.size() == 1; }));
    const uint64_t firstRun = k.document()->transcription.run;
    CHECK(k.reanalyse());
    const uint64_t secondRun = k.document()->transcription.run;
    slow->releaseAll();  // the old job finishes its chunk while it is being cancelled, the new one runs through
    CHECK(tk::waitIdle(k));
    const DocumentPtr doc = k.document();
    CHECK(secondRun != firstRun && doc && doc->transcription.run == secondRun && doc->transcription.phase == TranscriptionPhase::Done && doc->notes.size() == 2);
    k.deactivate();
    setTranscriber(nullptr);
    setAudioDecoder(nullptr);
}

TEST(the_instrument_selection_is_stored_with_the_project) {
    tk::installFakeDecoder(3.0);
    setTranscriber(std::make_shared<tk::FakeTranscriber>(twoChunks()));
    std::vector<uint8_t> saved;
    {
        TonetraceKernel k;
        k.prepare(tk::kRate, tk::kBlock);
        k.setInstrumentSelection({26, 29, 33});
        k.setModelFocus(true);
        saved = k.saveState();
        k.deactivate();
    }
    TonetraceKernel other;
    CHECK(other.loadState(saved.data(), saved.size()) && other.instrumentSelection() == std::vector<int>({26, 29, 33}) && other.modelFocus());
    other.deactivate();
    setTranscriber(nullptr);
    setAudioDecoder(nullptr);
}

TEST(a_tonetrace_5_0_state_without_a_selection_still_loads) {
    const std::vector<uint8_t> v7 = encodeState(defaultParams(), nullptr, MixerState{}, 0.0, {});
    constexpr size_t kSelectionAt = 16 + kNumParams * 4 + 8 + 2;  // header, parameters, export tempo, empty mixer
    CHECK(v7.size() > kSelectionAt + 1 && v7[kSelectionAt] == 0 && v7[kSelectionAt + 1] == 0);
    std::vector<uint8_t> v6 = v7;
    v6.erase(v6.begin() + ptrdiff_t(kSelectionAt), v6.begin() + ptrdiff_t(kSelectionAt + 2));  // no selection, no focus flag
    const uint32_t version = 6, length = uint32_t(v6.size() - 16), crc = crc32(v6.data() + 16, length);
    std::memcpy(&v6[4], &version, 4);
    std::memcpy(&v6[8], &length, 4);
    std::memcpy(&v6[12], &crc, 4);
    DecodedState out;
    std::string reason;
    CHECK_MSG(decodeState(v6.data(), v6.size(), &out, &reason), "%s", reason.c_str());
    CHECK(out.formatVersion == 6 && out.instrumentSelection.empty() && !out.isModelFocused);
    const std::vector<uint8_t> chosen = encodeState(defaultParams(), nullptr, MixerState{}, 0.0, StateChoices{{33, kDrumProgram}, true});
    CHECK(decodeState(chosen.data(), chosen.size(), &out) && out.instrumentSelection == std::vector<int>({33, kDrumProgram}) && out.isModelFocused);
}
