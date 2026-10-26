// Small platform-neutral real FFT interface. Apple builds use Accelerate, everything else (or tests)
// the portable radix-2 fallback. Both produce the plain unscaled DFT of a real signal.
#pragma once
#include <memory>

namespace ttr {

class RealFft {
public:
    virtual ~RealFft() = default;
    virtual int size() const = 0;
    // in: size() real samples. re/im: size()/2 + 1 bins each (bin 0 and size()/2 have im == 0).
    virtual void forward(const float* in, float* re, float* im) = 0;
};

// Best available implementation for this platform (Accelerate on Apple, portable otherwise).
std::unique_ptr<RealFft> createFft(int log2n);
// Always the portable implementation (used on non-Apple platforms and compared against Accelerate in tests).
std::unique_ptr<RealFft> createPortableFft(int log2n);
#ifdef __APPLE__
std::unique_ptr<RealFft> createAccelerateFft(int log2n);
#endif

}  // namespace ttr
