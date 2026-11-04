#include "core/SongImport.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <mutex>

namespace ttr {

namespace {

struct DecoderSlot {
    std::mutex mutex;
    AudioDecoder decoder;
};

DecoderSlot& slot() {
    static DecoderSlot s;
    return s;
}

std::string lowerExtension(const std::string& path) {
    const std::string name = fileNameOf(path);
    const size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return {};
    std::string ext = name.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

bool isUsable(const DecodedAudio& audio) {
    if (audio.samples.empty() || audio.channels < 1 || audio.channels > 2 || !(audio.sampleRate > 0.0)) return false;
    return audio.samples.size() % size_t(audio.channels) == 0;
}

}  // namespace

void setAudioDecoder(AudioDecoder decoder) {
    std::lock_guard<std::mutex> guard(slot().mutex);
    slot().decoder = std::move(decoder);
}

bool decodeAudioFile(const std::string& path, DecodedAudio* out, std::string* reason) {
    AudioDecoder decoder;
    {
        std::lock_guard<std::mutex> guard(slot().mutex);
        decoder = slot().decoder;
    }
    std::string why;
    DecodedAudio audio;
    if (!isSongFile(path)) {
        why = "unsupported file type (use wav, aiff, mp3, m4a, flac or caf)";
    } else if (!decoder) {
        why = "no audio decoder in this build";
    } else if (!decoder(path, kSongDecodeRate, kMaxSongSeconds, &audio, &why)) {
        if (why.empty()) why = "the file could not be read";
    } else if (!isUsable(audio)) {
        why = "the file contains no audio";
    }
    if (!why.empty()) {
        if (reason) *reason = why;
        return false;
    }
    *out = std::move(audio);
    return true;
}

const std::vector<std::string>& songFileExtensions() {
    static const std::vector<std::string> kExtensions = {"wav", "wave", "aif", "aiff", "aifc", "mp3", "m4a", "aac", "mp4", "flac", "caf"};
    return kExtensions;
}

bool isSongFile(const std::string& path) {
    const std::string ext = lowerExtension(path);
    const auto& all = songFileExtensions();
    return !ext.empty() && std::find(all.begin(), all.end(), ext) != all.end();
}

std::string fileNameOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace ttr
