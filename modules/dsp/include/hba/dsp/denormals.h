// SPDX-License-Identifier: GPL-3.0-or-later
// Flush denormals to zero for the lifetime of the object, restoring the caller's
// floating-point mode afterwards.
#pragma once

#include <hba/dsp/nonblocking.h>

#include <cstdint>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace hba::dsp {

class ScopedFlushDenormals {
public:
    ScopedFlushDenormals() noexcept HBA_NONBLOCKING {
#if defined(__aarch64__) && defined(__clang__)
        saved_ = __builtin_arm_rsr64("fpcr");
        __builtin_arm_wsr64("fpcr", saved_ | (std::uint64_t { 1 } << 24));  // FZ
#elif defined(__aarch64__) && defined(__GNUC__)
        asm volatile("mrs %0, fpcr" : "=r"(saved_));
        const std::uint64_t next = saved_ | (std::uint64_t{1} << 24);
        asm volatile("msr fpcr, %0" : : "r"(next));
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        saved_ = _mm_getcsr();
        _mm_setcsr(static_cast<unsigned>(saved_) | 0x8040u);  // FTZ | DAZ
#endif
    }
    ~ScopedFlushDenormals() noexcept HBA_NONBLOCKING {
#if defined(__aarch64__) && defined(__clang__)
        __builtin_arm_wsr64("fpcr", saved_);
#elif defined(__aarch64__) && defined(__GNUC__)
        asm volatile("msr fpcr, %0" : : "r"(saved_));
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        _mm_setcsr(static_cast<unsigned>(saved_));
#endif
    }
    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
    std::uint64_t saved_ = 0;
};

}  // namespace hba::dsp
