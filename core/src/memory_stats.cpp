// SPDX-License-Identifier: MIT
// Counting allocator for Jolt (ADR-0015: guest-process memory budget). Every Jolt allocation - bodies, broadphase,
// contact buffers, the temp allocator - goes through here so DragDoll can report what it really uses.
#include "memory_stats.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Memory.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

namespace dragdoll
{
namespace
{
// Plain-old-data atomics: no constructors run at DLL load (ADR-0020).
std::atomic<int64_t> gCurrentBytes{0};
std::atomic<int64_t> gPeakBytes{0};
std::atomic<int64_t> gAllocations{0};
bool gInstalled = false;

// Every block is preceded by a 16-byte header remembering its size and the alignment it was allocated with.
struct Header
{
    size_t size;
    size_t alignment;
};
static_assert(sizeof(Header) <= 16, "header must fit in 16 bytes");
constexpr size_t kHeaderBytes = 16;

void account(int64_t delta)
{
    const int64_t now = gCurrentBytes.fetch_add(delta, std::memory_order_relaxed) + delta;
    int64_t peak = gPeakBytes.load(std::memory_order_relaxed);
    while (now > peak && !gPeakBytes.compare_exchange_weak(peak, now, std::memory_order_relaxed))
    {
    }
    if (delta > 0)
    {
        gAllocations.fetch_add(1, std::memory_order_relaxed);
    }
}

void* alignedAlloc(size_t size, size_t alignment)
{
    if (alignment < 16)
    {
        alignment = 16;
    }
    // The header occupies the last 16 bytes of the first `alignment` bytes, so the user pointer stays aligned.
    void* raw = _aligned_malloc(size + alignment, alignment);
    if (raw == nullptr)
    {
        return nullptr;
    }
    auto* user = static_cast<unsigned char*>(raw) + alignment;
    auto* header = reinterpret_cast<Header*>(user - kHeaderBytes);
    header->size = size;
    header->alignment = alignment;
    account(static_cast<int64_t>(size + alignment));
    return user;
}

void alignedFree(void* block)
{
    if (block == nullptr)
    {
        return;
    }
    auto* user = static_cast<unsigned char*>(block);
    const Header header = *reinterpret_cast<Header*>(user - kHeaderBytes);
    account(-static_cast<int64_t>(header.size + header.alignment));
    _aligned_free(user - header.alignment);
}

void* allocate(size_t size)
{
    return alignedAlloc(size, 16);
}

void* reallocate(void* block, size_t oldSize, size_t newSize)
{
    if (block == nullptr)
    {
        return alignedAlloc(newSize, 16);
    }
    if (newSize == 0)
    {
        alignedFree(block);
        return nullptr;
    }
    void* fresh = alignedAlloc(newSize, 16);
    if (fresh == nullptr)
    {
        return nullptr;
    }
    std::memcpy(fresh, block, oldSize < newSize ? oldSize : newSize);
    alignedFree(block);
    return fresh;
}
} // namespace

void installCountingAllocator()
{
    if (gInstalled)
    {
        return; // Installed once for the life of the process: blocks must be freed by the allocator that made them.
    }
    JPH::Allocate = &allocate;
    JPH::Reallocate = &reallocate;
    JPH::Free = &alignedFree;
    JPH::AlignedAllocate = &alignedAlloc;
    JPH::AlignedFree = &alignedFree;
    gInstalled = true;
}

MemoryStats memoryStats()
{
    return MemoryStats{gCurrentBytes.load(), gPeakBytes.load(), gAllocations.load()};
}

void resetPeakMemory()
{
    gPeakBytes.store(gCurrentBytes.load());
}

} // namespace dragdoll
