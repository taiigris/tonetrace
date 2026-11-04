// 4.7: loading a whole song from a file (NeuralNote-style "drop a file" instead of recording it). The core only knows the
// decoder interface; the macOS implementation (ExtAudioFile: wav, aiff, mp3, m4a, flac, caf) registers itself from
// tonetrace_ml, tests register a fake.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace ttr {

constexpr double kMaxSongSeconds = 600.0;     // longer files are cut here (the rest is ignored, with a notice)
constexpr double kSongDecodeRate = 22050.0;   // the analysis works at this rate anyway; halves the memory of 44.1 kHz

struct DecodedAudio {
    std::vector<float> mono;  // -1..1, all channels averaged
    double sampleRate = 0.0;
    double fileSeconds = 0.0;  // length of the whole file (more than mono covers when it was cut)
    bool truncated = false;
};

// Reads at most `maxSeconds` of `path` as mono at `sampleRate`. False with a readable reason on failure.
using AudioDecoder = std::function<bool(const std::string& path, double sampleRate, double maxSeconds, DecodedAudio* out, std::string* reason)>;

void setAudioDecoder(AudioDecoder decoder);
bool decodeAudioFile(const std::string& path, DecodedAudio* out, std::string* reason);

// File extensions the import accepts (lower case, without the dot).
const std::vector<std::string>& songFileExtensions();
bool isSongFile(const std::string& path);
// "My Song.mp3" for "/Users/x/Music/My Song.mp3".
std::string fileNameOf(const std::string& path);

}  // namespace ttr
