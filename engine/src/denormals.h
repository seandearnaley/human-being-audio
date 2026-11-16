// Flush denormals to zero for the lifetime of the object, restoring the caller's
// floating-point mode afterwards. Recursive filters decaying towards zero would
// otherwise hit the slow denormal path.
#pragma once

#include "tf/nonblocking.h"

#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace tf::dsp {

class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() noexcept TF_NONBLOCKING {
#if defined(__aarch64__)
        saved_ = __builtin_arm_rsr64("fpcr");
        __builtin_arm_wsr64("fpcr", saved_ | (std::uint64_t { 1 } << 24));  // FZ
#elif defined(__x86_64__) || defined(__i386__)
        saved_ = _mm_getcsr();
        _mm_setcsr(static_cast<unsigned>(saved_) | 0x8040u);  // FTZ | DAZ
#endif
    }
    ~ScopedFlushDenormals() noexcept TF_NONBLOCKING {
#if defined(__aarch64__)
        __builtin_arm_wsr64("fpcr", saved_);
#elif defined(__x86_64__) || defined(__i386__)
        _mm_setcsr(static_cast<unsigned>(saved_));
#endif
    }
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
    std::uint64_t saved_ = 0;
};

}
