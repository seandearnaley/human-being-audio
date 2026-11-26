// SPDX-License-Identifier: GPL-3.0-or-later

#include "AllocationCounter.h"

#include <cstdlib>
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <new>

namespace
{
thread_local int countingScopes = 0;
thread_local std::size_t allocations = 0;
thread_local void (*nextAllocation) (void*) = nullptr;
thread_local void* allocationContext = nullptr;
} // namespace

// The other forms of operator new and delete forward to these by default,
// except C++17 over-aligned allocations which call the align_val_t overloads directly.
void* operator new (std::size_t size)
{
    if (const auto callback = nextAllocation)
    {
        nextAllocation = nullptr;
        callback (allocationContext);
    }
    if (countingScopes > 0)
        ++allocations;

    if (auto* memory = std::malloc (size == 0 ? 1 : size))
        return memory;

    throw std::bad_alloc();
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    if (const auto callback = nextAllocation) {
        nextAllocation = nullptr;
        callback(allocationContext);
    }
    if (countingScopes > 0)
        ++allocations;

    const std::size_t align = static_cast<std::size_t>(alignment);
    const std::size_t effectiveAlign = align < sizeof(void*) ? sizeof(void*) : align;
    const std::size_t requested = size == 0 ? effectiveAlign : size;
    const std::size_t rounded = ((requested + effectiveAlign - 1) / effectiveAlign) * effectiveAlign;
#if defined(_WIN32)
    if (auto* memory = _aligned_malloc(rounded, effectiveAlign))
        return memory;
#else
    void* memory = nullptr;
    if (posix_memalign(&memory, effectiveAlign, rounded) == 0 && memory != nullptr)
        return memory;
#endif
    throw std::bad_alloc();
}

void operator delete (void* memory) noexcept
{
    std::free (memory);
}

void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }

void operator delete(void* memory, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

void operator delete(void* memory, std::size_t, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

namespace livecut::engine::testing
{

NextAllocation::NextAllocation (void (*callback) (void*), void* context)
{
    nextAllocation = callback;
    allocationContext = context;
}

NextAllocation::~NextAllocation() { nextAllocation = nullptr; }

AllocationCounter::AllocationCounter() : startCount (allocations)
{
    ++countingScopes;
}

AllocationCounter::~AllocationCounter()
{
    --countingScopes;
}

std::size_t AllocationCounter::count() const
{
    return allocations - startCount;
}

} // namespace livecut::engine::testing
