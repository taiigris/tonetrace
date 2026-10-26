// Flush-to-zero / denormals-are-zero for the scope of one audio block (restores the caller's mode).
#pragma once
#include <cstdint>

#if defined(__aarch64__)
#elif defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace ttr {

class ScopedDenormalGuard {
public:
    ScopedDenormalGuard() {
#if defined(__aarch64__)
        uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        asm volatile("msr fpcr, %0" : : "r"(fpcr | kFlushToZero));
#elif defined(__x86_64__) || defined(__i386__)
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u);  // FTZ | DAZ
#endif
    }
    ~ScopedDenormalGuard() {
#if defined(__aarch64__)
        asm volatile("msr fpcr, %0" : : "r"(saved_));
#elif defined(__x86_64__) || defined(__i386__)
        _mm_setcsr(static_cast<unsigned>(saved_));
#endif
    }
    ScopedDenormalGuard(const ScopedDenormalGuard&) = delete;
    ScopedDenormalGuard& operator=(const ScopedDenormalGuard&) = delete;

private:
    static constexpr uint64_t kFlushToZero = 1ull << 24;  // FPCR.FZ
    uint64_t saved_ = 0;
};

}  // namespace ttr
