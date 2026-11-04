#include "analysis/ml/AudioFileDecoder.h"

#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>

namespace ttr {

namespace {

constexpr UInt32 kReadFrames = 32768;  // frames per ExtAudioFileRead
constexpr UInt32 kMaxChannels = 64;

std::string statusText(const char* what, OSStatus status) {
    char text[96];
    std::snprintf(text, sizeof text, "%s (Core Audio error %d)", what, int(status));
    return text;
}

// Closes the file on every path out of the decoder.
struct FileCloser {
    ExtAudioFileRef file = nullptr;
    ~FileCloser() { if (file) ExtAudioFileDispose(file); }
};

struct FileInfo {
    UInt32 channels = 0;
    double rate = 0.0;
    SInt64 frames = -1;
};

// Interleaved 16 bit at the target rate with the file's own channel count; Core Audio converts format and rate.
bool setClientFormat(ExtAudioFileRef file, double sampleRate, FileInfo* info, std::string* reason) {
    AudioStreamBasicDescription fileFormat{};
    UInt32 size = sizeof fileFormat;
    OSStatus st = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &fileFormat);
    if (st != noErr) { *reason = statusText("unreadable audio format", st); return false; }
    if (fileFormat.mChannelsPerFrame == 0 || fileFormat.mChannelsPerFrame > kMaxChannels || !(fileFormat.mSampleRate > 0.0)) {
        *reason = "unsupported channel layout";
        return false;
    }
    size = sizeof info->frames;
    if (ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &size, &info->frames) != noErr) info->frames = -1;
    info->channels = fileFormat.mChannelsPerFrame;
    info->rate = fileFormat.mSampleRate;
    AudioStreamBasicDescription client{};
    client.mSampleRate = sampleRate;
    client.mFormatID = kAudioFormatLinearPCM;
    client.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    client.mChannelsPerFrame = info->channels;
    client.mBitsPerChannel = 16;
    client.mBytesPerFrame = 2 * info->channels;
    client.mFramesPerPacket = 1;
    client.mBytesPerPacket = client.mBytesPerFrame;
    st = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof client, &client);
    if (st != noErr) { *reason = statusText("cannot convert this audio format", st); return false; }
    return true;
}

// Reads until the end or `limit` frames, keeping the first one or two channels; false with a reason on a read error.
bool readFrames(ExtAudioFileRef file, UInt32 channels, size_t limit, std::vector<int16_t>* out, bool* truncated, std::string* reason) {
    const UInt32 kept = std::min<UInt32>(channels, 2);
    std::vector<int16_t> block(size_t(kReadFrames) * channels);
    size_t frames = 0;
    for (;;) {
        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = channels;
        list.mBuffers[0].mDataByteSize = UInt32(block.size() * sizeof(int16_t));
        list.mBuffers[0].mData = block.data();
        UInt32 got = kReadFrames;
        const OSStatus st = ExtAudioFileRead(file, &got, &list);
        if (st != noErr) { *reason = statusText("the file could not be decoded", st); return false; }
        if (got == 0) return true;
        for (UInt32 f = 0; f < got; ++f) {
            if (frames == limit) { *truncated = true; return true; }
            for (UInt32 c = 0; c < kept; ++c) out->push_back(block[size_t(f) * channels + c]);
            ++frames;
        }
    }
}

}  // namespace

bool decodeWithCoreAudio(const std::string& path, double sampleRate, double maxSeconds, DecodedAudio* out, std::string* reason) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        FileCloser closer;
        const OSStatus st = ExtAudioFileOpenURL((__bridge CFURLRef)url, &closer.file);
        if (st != noErr) { *reason = statusText("the file could not be opened", st); return false; }
        FileInfo info;
        if (!setClientFormat(closer.file, sampleRate, &info, reason)) return false;
        const size_t limit = size_t(maxSeconds * sampleRate);
        const int kept = int(std::min<UInt32>(info.channels, 2));
        std::vector<int16_t> samples;
        if (info.frames > 0) samples.reserve(std::min(limit, size_t(double(info.frames) * sampleRate / info.rate) + 1) * size_t(kept));
        bool truncated = false;
        if (!readFrames(closer.file, info.channels, limit, &samples, &truncated, reason)) return false;
        out->samples = std::move(samples);
        out->channels = kept;
        out->sampleRate = sampleRate;
        out->fileSeconds = info.frames > 0 ? double(info.frames) / info.rate : double(out->samples.size() / size_t(kept)) / sampleRate;
        out->truncated = truncated;
        return true;
    }
}

void installAudioFileDecoder() {
    static std::once_flag once;
    std::call_once(once, [] { setAudioDecoder(decodeWithCoreAudio); });
}

}  // namespace ttr
