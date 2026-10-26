// Accelerate (vDSP) backed FFT. Apple platforms only.
#ifdef __APPLE__
#include <Accelerate/Accelerate.h>

#include <vector>

#include "analysis/Fft.h"

namespace ttr {

namespace {

class AccelerateFft : public RealFft {
public:
    explicit AccelerateFft(int log2n)
        : log2n_(vDSP_Length(log2n)), n_(1 << log2n), setup_(vDSP_create_fftsetup(log2n_, kFFTRadix2)),
          re_(size_t(n_ / 2)), im_(size_t(n_ / 2)) {}
    ~AccelerateFft() override { vDSP_destroy_fftsetup(setup_); }

    int size() const override { return n_; }

    void forward(const float* in, float* re, float* im) override {
        const int half = n_ / 2;
        DSPSplitComplex split{re_.data(), im_.data()};
        vDSP_ctoz(reinterpret_cast<const DSPComplex*>(in), 2, &split, 1, vDSP_Length(half));
        vDSP_fft_zrip(setup_, &split, 1, log2n_, FFT_FORWARD);
        // zrip returns 2x the DFT and packs the Nyquist bin into im[0].
        re[0] = re_[0] * 0.5f; im[0] = 0.0f;
        re[half] = im_[0] * 0.5f; im[half] = 0.0f;
        for (int k = 1; k < half; ++k) { re[k] = re_[size_t(k)] * 0.5f; im[k] = im_[size_t(k)] * 0.5f; }
    }

private:
    vDSP_Length log2n_;
    int n_;
    FFTSetup setup_;
    std::vector<float> re_, im_;
};

}  // namespace

std::unique_ptr<RealFft> createAccelerateFft(int log2n) { return std::make_unique<AccelerateFft>(log2n); }
std::unique_ptr<RealFft> createFft(int log2n) { return createAccelerateFft(log2n); }

}  // namespace ttr
#endif
