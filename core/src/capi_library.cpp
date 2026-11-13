// SPDX-License-Identifier: MIT
// C ABI: library lifetime and error reporting (dragdoll.h). Every exported function is
// noexcept in practice and catches everything at the boundary (ADR-0020).

#include "capi_guard.hpp"
#include "memory_stats.hpp"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <cstdio>
#include <new>
#include <string>

namespace
{
// Plain-old-data globals only: no constructors run at DLL load (ADR-0020).
bool gInitialized = false;
thread_local std::string* tLastError = nullptr;

const char* cpuFeatures() noexcept
{
    return ""
#if defined(JPH_USE_AVX512)
           " avx512"
#endif
#if defined(JPH_USE_AVX2)
           " avx2"
#endif
#if defined(JPH_USE_AVX)
           " avx"
#endif
#if defined(JPH_USE_SSE4_2)
           " sse4.2"
#endif
#if defined(JPH_USE_SSE4_1)
           " sse4.1"
#endif
#if defined(JPH_USE_FMADD)
           " fma"
#endif
        ;
}
} // namespace

namespace dragdoll::detail
{
void setLastError(const char* message) noexcept
{
    try
    {
        if (tLastError == nullptr)
        {
            tLastError = new std::string();
        }
        *tLastError = message;
    }
    catch (...)
    {
        // Out of memory while reporting an error: nothing sensible left to do.
    }
}

void clearLastError() noexcept
{
    if (tLastError != nullptr)
    {
        tLastError->clear();
    }
}

const char* lastError() noexcept
{
    return (tLastError != nullptr) ? tLastError->c_str() : "";
}

bool isLibraryInitialized() noexcept
{
    return gInitialized;
}
} // namespace dragdoll::detail

using namespace dragdoll::detail;

extern "C"
{

DD_API uint32_t DD_CALL dragdoll_get_version(void)
{
    return DD_API_VERSION;
}

DD_API const char* DD_CALL dragdoll_get_build_info(void)
{
    static char sInfo[256] = {};
    if (sInfo[0] == '\0')
    {
        std::snprintf(sInfo, sizeof(sInfo), "DragDoll %d.%d.%d | %s | Jolt %d.%d.%d |%s", DD_API_VERSION_MAJOR,
                      DD_API_VERSION_MINOR, DD_API_VERSION_PATCH, sizeof(void*) == 4 ? "x86" : "x64",
                      JPH_VERSION_MAJOR, JPH_VERSION_MINOR, JPH_VERSION_PATCH, cpuFeatures());
    }
    return sInfo;
}

DD_API DdResult DD_CALL dragdoll_init(uint32_t api_version)
{
    DD_GUARD_BEGIN
    clearLastError();
    // Same major version and a header no newer than this binary.
    if ((api_version >> 22) != DD_API_VERSION_MAJOR || api_version > DD_API_VERSION)
    {
        return fail(DD_ERROR_VERSION_MISMATCH, "dragdoll_init: host was built against an incompatible dragdoll.h");
    }
    if (gInitialized)
    {
        return fail(DD_ERROR_ALREADY_INITIALIZED, "dragdoll_init: already initialized");
    }

    dragdoll::installCountingAllocator(); // Every Jolt allocation is counted (ADR-0015)
#ifdef JPH_ENABLE_ASSERTS
    // Debug builds: say which Jolt check failed before breaking (by default Jolt breaks silently).
    JPH::AssertFailed = [](const char* expression, const char* message, const char* file, JPH::uint line) {
        std::fprintf(stderr, "Jolt assert failed: %s%s%s (%s:%u)\n", expression, message != nullptr ? " - " : "",
                     message != nullptr ? message : "", file, line);
        std::fflush(stderr);
        return true; // Break into the debugger / crash, as before
    };
#endif
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    gInitialized = true;
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API void DD_CALL dragdoll_shutdown(void)
{
    try
    {
        if (!gInitialized)
        {
            return;
        }
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
        gInitialized = false;
    }
    catch (...)
    {
        setLastError("dragdoll_shutdown: unexpected internal error");
    }
}

DD_API const char* DD_CALL dragdoll_last_error_message(void)
{
    return lastError();
}

DD_API DdResult DD_CALL dragdoll_get_memory_stats(DdMemoryStats* out_stats)
{
    DD_GUARD_BEGIN
    if (out_stats == nullptr || out_stats->struct_size != sizeof(DdMemoryStats))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_get_memory_stats: out_stats with a valid struct_size is required");
    }
    const dragdoll::MemoryStats stats = dragdoll::memoryStats();
    out_stats->current_bytes = static_cast<uint64_t>(stats.currentBytes);
    out_stats->peak_bytes = static_cast<uint64_t>(stats.peakBytes);
    out_stats->allocation_count = static_cast<uint64_t>(stats.allocations);
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API void DD_CALL dragdoll_reset_peak_memory(void)
{
    dragdoll::resetPeakMemory();
}

} // extern "C"
