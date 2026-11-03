#include "analysis/ml/NeuralTranscriber.h"

#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>
#include <dlfcn.h>

#include <cstdlib>
#include <mutex>
#include <vector>

namespace ttr {

namespace {

NSString* const kInputName = @"input_2";
NSString* const kNoteName = @"Identity_1";
NSString* const kOnsetName = @"Identity_2";
NSString* const kContourName = @"Identity";

// Copies a [1, frames, bins] output into a frame-major float buffer, honouring the strides. False on a shape or type mismatch.
bool copyOutput(MLMultiArray* array, int frames, int bins, float* dst) {
    if (!array || array.shape.count != 3 || array.shape[1].intValue != frames || array.shape[2].intValue != bins) return false;
    const long s1 = array.strides[1].longValue, s2 = array.strides[2].longValue;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const void* base = array.dataPointer;
#pragma clang diagnostic pop
    if (array.dataType == MLMultiArrayDataTypeFloat32) {
        const float* src = static_cast<const float*>(base);
        for (int f = 0; f < frames; ++f)
            for (int b = 0; b < bins; ++b) dst[size_t(f) * size_t(bins) + size_t(b)] = src[f * s1 + b * s2];
        return true;
    }
    if (array.dataType == MLMultiArrayDataTypeDouble) {
        const double* src = static_cast<const double*>(base);
        for (int f = 0; f < frames; ++f)
            for (int b = 0; b < bins; ++b) dst[size_t(f) * size_t(bins) + size_t(b)] = float(src[f * s1 + b * s2]);
        return true;
    }
    return false;
}

class CoreMLModel final : public NeuralModel {
public:
    explicit CoreMLModel(MLModel* model) : model_(model) {}

    bool infer(const float* window, float* note, float* onset, float* contour) override {
        std::lock_guard<std::mutex> guard(mutex_);  // one prediction at a time per model
        @autoreleasepool {
            NSError* error = nil;
            MLMultiArray* input = [[MLMultiArray alloc] initWithShape:@[ @1, @(kNeuralWindowSamples), @1 ] dataType:MLMultiArrayDataTypeFloat32 error:&error];
            if (!input) return false;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            float* dst = static_cast<float*>(input.dataPointer);
#pragma clang diagnostic pop
            const long stride = input.strides[1].longValue;
            for (int i = 0; i < kNeuralWindowSamples; ++i) dst[i * stride] = window[i];
            MLDictionaryFeatureProvider* features = [[MLDictionaryFeatureProvider alloc] initWithDictionary:@{kInputName : input} error:&error];
            if (!features) return false;
            id<MLFeatureProvider> out = [model_ predictionFromFeatures:features error:&error];
            if (!out) return false;
            return copyOutput([out featureValueForName:kNoteName].multiArrayValue, kNeuralWindowFrames, kNeuralKeys, note) &&
                   copyOutput([out featureValueForName:kOnsetName].multiArrayValue, kNeuralWindowFrames, kNeuralKeys, onset) &&
                   copyOutput([out featureValueForName:kContourName].multiArrayValue, kNeuralWindowFrames, kNeuralContourBins, contour);
        }
    }

private:
    MLModel* model_;
    std::mutex mutex_;
};

std::vector<std::string> candidatePaths() {
    std::vector<std::string> paths;
    if (const char* env = std::getenv(kNeuralModelEnv); env && *env) paths.push_back(env);
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&candidatePaths), &info) && info.dli_fname) {
        NSString* binary = [NSString stringWithUTF8String:info.dli_fname];
        NSString* dir = binary.stringByDeletingLastPathComponent;  // Contents/MacOS in a bundle
        NSString* name = [NSString stringWithUTF8String:kNeuralModelName];
        paths.push_back([[dir.stringByDeletingLastPathComponent stringByAppendingPathComponent:@"Resources"] stringByAppendingPathComponent:name].UTF8String);
        paths.push_back([dir stringByAppendingPathComponent:name].UTF8String);
    }
    return paths;
}

}  // namespace

std::shared_ptr<NeuralModel> loadCoreMLModel(const std::string& path, std::string* reason) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()] isDirectory:YES];
        if (![[NSFileManager defaultManager] fileExistsAtPath:url.path]) {
            if (reason) *reason = "model not found: " + std::string(kNeuralModelName);
            return nullptr;
        }
        MLModelConfiguration* config = [[MLModelConfiguration alloc] init];
        // CPU only: on the GPU / Neural Engine the model runs in float16 and turns digital silence into all 88 keys at once.
        config.computeUnits = MLComputeUnitsCPUOnly;
        NSError* error = nil;
        MLModel* model = [MLModel modelWithContentsOfURL:url configuration:config error:&error];
        if (!model) {
            if (reason) *reason = std::string("CoreML could not load the model: ") + (error ? error.localizedDescription.UTF8String : "unknown error");
            return nullptr;
        }
        return std::make_shared<CoreMLModel>(model);
    }
}

void installCoreMLLoader() {
    static std::once_flag once;
    std::call_once(once, [] { setNeuralLoader([](std::string* reason) -> std::shared_ptr<NeuralModel> {
        std::string why = "model not found: " + std::string(kNeuralModelName);
        for (const std::string& path : candidatePaths()) {
            if (path.empty() || ![[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:path.c_str()]]) continue;
            if (std::shared_ptr<NeuralModel> model = loadCoreMLModel(path, &why)) return model;
        }
        if (reason) *reason = why;
        return nullptr;
    }); });
}

}  // namespace ttr
