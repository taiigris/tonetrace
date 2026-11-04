// 4.7: macOS audio file decoder for the song import (ExtAudioFile: wav, aiff, mp3, m4a/aac, flac, caf).
#pragma once
#include <string>

#include "core/SongImport.h"

namespace ttr {

// Reads at most `maxSeconds` of `path`, converted by Core Audio to mono float at `sampleRate`.
bool decodeWithCoreAudio(const std::string& path, double sampleRate, double maxSeconds, DecodedAudio* out, std::string* reason);
// Registers decodeWithCoreAudio with the core (idempotent).
void installAudioFileDecoder();

}  // namespace ttr
