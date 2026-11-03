// Windowed-sinc resampling of a mono signal to the model rate (any input rate, band-limited below both Nyquist rates).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ttr {

std::vector<float> resampleMono(const float* samples, size_t count, double fromRate, double toRate);
// The stored recording (int16) as floats at `toRate`.
std::vector<float> resampleInt16(const int16_t* samples, size_t count, double fromRate, double toRate);

}  // namespace ttr
