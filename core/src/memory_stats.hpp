// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace dragdoll
{
struct MemoryStats
{
    int64_t currentBytes; // Live Jolt heap (includes block headers and alignment padding)
    int64_t peakBytes;    // Highest value of currentBytes since the last reset
    int64_t allocations;  // Allocations since process start
};

// Routes Jolt's allocations through a counting allocator. Idempotent; call before the first Jolt allocation.
void installCountingAllocator();
MemoryStats memoryStats();
void resetPeakMemory();
} // namespace dragdoll
