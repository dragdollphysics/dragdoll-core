// SPDX-License-Identifier: MIT
// C ABI: library lifetime (dragdoll.h). Every exported function is noexcept and catches
// everything at the boundary (ADR-0020).

#include <dragdoll/dragdoll.h>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>

#include <cstdio>
#include <exception>
#include <string>

namespace
{
// Plain-old-data globals only: no constructors run at DLL load (ADR-0020).
bool gInitialized = false;
thread_local std::string* tLastError = nullptr;

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

#define DD_GUARD_BEGIN                                                                                                 \
    try                                                                                                                \
    {
#define DD_GUARD_END(failValue)                                                                                        \
    }                                                                                                                  \
    catch (const std::exception& e)                                                                                    \
    {                                                                                                                  \
        setLastError(e.what());                                                                                        \
        return failValue;                                                                                              \
    }                                                                                                                  \
    catch (...)                                                                                                        \
    {                                                                                                                  \
        setLastError("unknown internal error");                                                                        \
        return failValue;                                                                                              \
    }

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
        setLastError("dragdoll_init: host was built against an incompatible dragdoll.h");
        return DD_ERROR_VERSION_MISMATCH;
    }
    if (gInitialized)
    {
        setLastError("dragdoll_init: already initialized");
        return DD_ERROR_ALREADY_INITIALIZED;
    }

    JPH::RegisterDefaultAllocator(); // Replaced by DragDoll's accounting allocator in a later task (ADR-0015)
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
    return (tLastError != nullptr) ? tLastError->c_str() : "";
}

} // extern "C"
