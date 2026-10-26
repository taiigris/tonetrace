#include <cmath>
#include <vector>

#include "TestUtil.h"
#include "analysis/Fft.h"

using namespace ttr;

namespace {

std::vector<float> testSignal(int n) {
    std::vector<float> x(static_cast<size_t>(n));
    uint32_t state = 7;
    for (int i = 0; i < n; ++i) {
        state = state * 1664525u + 1013904223u;
        x[size_t(i)] = 0.3f * std::sin(0.37f * float(i)) + 0.1f * float(int32_t(state)) / 2147483648.0f;
    }
    return x;
}

}  // namespace

TEST(portable_fft_matches_naive_dft) {
    const int n = 64;
    const auto x = testSignal(n);
    std::vector<float> re(n / 2 + 1), im(n / 2 + 1);
    createPortableFft(6)->forward(x.data(), re.data(), im.data());
    for (int k = 0; k <= n / 2; ++k) {
        double sr = 0, si = 0;
        for (int t = 0; t < n; ++t) {
            sr += x[size_t(t)] * std::cos(2.0 * M_PI * k * t / n);
            si -= x[size_t(t)] * std::sin(2.0 * M_PI * k * t / n);
        }
        CHECK_MSG(std::fabs(sr - re[size_t(k)]) < 1e-4 && std::fabs(si - im[size_t(k)]) < 1e-4, "bin %d", k);
    }
}

#ifdef __APPLE__
TEST(portable_fft_matches_accelerate) {
    for (int log2n : {10, 13}) {
        const int n = 1 << log2n;
        const auto x = testSignal(n);
        std::vector<float> reA(n / 2 + 1), imA(n / 2 + 1), reP(n / 2 + 1), imP(n / 2 + 1);
        createAccelerateFft(log2n)->forward(x.data(), reA.data(), imA.data());
        createPortableFft(log2n)->forward(x.data(), reP.data(), imP.data());
        double maxMag = 0, maxErr = 0;
        for (int k = 0; k <= n / 2; ++k) {
            maxMag = std::max(maxMag, double(std::hypot(reA[size_t(k)], imA[size_t(k)])));
            maxErr = std::max({maxErr, double(std::fabs(reA[size_t(k)] - reP[size_t(k)])), double(std::fabs(imA[size_t(k)] - imP[size_t(k)]))});
        }
        CHECK_MSG(maxErr < 1e-4 * maxMag, "log2n %d: max error %g vs peak %g", log2n, maxErr, maxMag);
    }
}
#endif
