// Portable iterative radix-2 FFT (no platform APIs).
#include <cmath>
#include <vector>

#include "analysis/Fft.h"

namespace ttr {

namespace {

class PortableFft : public RealFft {
public:
    explicit PortableFft(int log2n)
        : n_(1 << log2n), rev_(size_t(n_)), cosT_(size_t(n_ / 2)), sinT_(size_t(n_ / 2)), re_(size_t(n_)), im_(size_t(n_)) {
        for (int i = 0; i < n_; ++i) {
            int r = 0;
            for (int b = 0; b < log2n; ++b) r |= ((i >> b) & 1) << (log2n - 1 - b);
            rev_[size_t(i)] = r;
        }
        for (int k = 0; k < n_ / 2; ++k) {
            cosT_[size_t(k)] = std::cos(-2.0 * M_PI * k / n_);
            sinT_[size_t(k)] = std::sin(-2.0 * M_PI * k / n_);
        }
    }

    int size() const override { return n_; }

    void forward(const float* in, float* re, float* im) override {
        for (int i = 0; i < n_; ++i) { re_[size_t(rev_[size_t(i)])] = in[i]; im_[size_t(rev_[size_t(i)])] = 0.0; }
        for (int len = 2; len <= n_; len <<= 1) {
            const int half = len / 2, step = n_ / len;
            for (int base = 0; base < n_; base += len) {
                for (int k = 0; k < half; ++k) {
                    const double wr = cosT_[size_t(k * step)], wi = sinT_[size_t(k * step)];
                    const size_t a = size_t(base + k), b = a + size_t(half);
                    const double tr = re_[b] * wr - im_[b] * wi, ti = re_[b] * wi + im_[b] * wr;
                    re_[b] = re_[a] - tr; im_[b] = im_[a] - ti;
                    re_[a] += tr; im_[a] += ti;
                }
            }
        }
        for (int k = 0; k <= n_ / 2; ++k) { re[k] = float(re_[size_t(k)]); im[k] = float(im_[size_t(k)]); }
    }

private:
    int n_;
    std::vector<int> rev_;
    std::vector<double> cosT_, sinT_, re_, im_;
};

}  // namespace

std::unique_ptr<RealFft> createPortableFft(int log2n) { return std::make_unique<PortableFft>(log2n); }

#ifndef __APPLE__
std::unique_ptr<RealFft> createFft(int log2n) { return createPortableFft(log2n); }
#endif

}  // namespace ttr
