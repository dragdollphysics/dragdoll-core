/*
 * DragDoll — C ABI (ADR-0017, ADR-0020)
 *
 * Rules for everything in this header:
 *   - Plain C11: no C++ types cross this boundary.
 *   - Every function is safe to call from C, never throws, and reports errors as DdResult.
 *   - Memory returned by DragDoll is owned by DragDoll; output buffers are owned by the caller.
 *   - Explicit __cdecl calling convention for a stable 32-bit ABI.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DRAGDOLL_H
#define DRAGDOLL_H

#include <stdint.h>

/* ---- Export / calling convention ------------------------------------------------------- */
#if defined(_WIN32)
    #define DD_CALL __cdecl
    #if defined(DRAGDOLL_STATIC)
        #define DD_API
    #elif defined(DRAGDOLL_BUILDING)
        #define DD_API __declspec(dllexport)
    #else
        #define DD_API __declspec(dllimport)
    #endif
#else
    #define DD_CALL
    #if defined(DRAGDOLL_BUILDING)
        #define DD_API __attribute__((visibility("default")))
    #else
        #define DD_API
    #endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Versioning ------------------------------------------------------------------------ */
#define DD_API_VERSION_MAJOR 0
#define DD_API_VERSION_MINOR 0
#define DD_API_VERSION_PATCH 1
#define DD_MAKE_VERSION(major, minor, patch) (((uint32_t)(major) << 22) | ((uint32_t)(minor) << 12) | (uint32_t)(patch))
#define DD_API_VERSION DD_MAKE_VERSION(DD_API_VERSION_MAJOR, DD_API_VERSION_MINOR, DD_API_VERSION_PATCH)

/* ---- Results --------------------------------------------------------------------------- */
typedef enum DdResult
{
    DD_OK = 0,
    DD_ERROR_INVALID_ARGUMENT = 1,
    DD_ERROR_NOT_INITIALIZED = 2,
    DD_ERROR_ALREADY_INITIALIZED = 3,
    DD_ERROR_OUT_OF_MEMORY = 4,
    DD_ERROR_VERSION_MISMATCH = 5,
    DD_ERROR_INTERNAL = 100,
    DD_RESULT_FORCE_32BIT = 0x7FFFFFFF
} DdResult;

/* ---- Library lifetime ------------------------------------------------------------------ */

/* Version of the DragDoll binary actually loaded (compare with DD_API_VERSION at build time). */
DD_API uint32_t DD_CALL dragdoll_get_version(void);

/* Human-readable build description: version, architecture, Jolt version, CPU features.
 * The returned string is owned by DragDoll and valid for the lifetime of the process. */
DD_API const char* DD_CALL dragdoll_get_build_info(void);

/* Initialise the library. Pass DD_API_VERSION so a host built against an incompatible
 * header is rejected with DD_ERROR_VERSION_MISMATCH. Call once before anything else. */
DD_API DdResult DD_CALL dragdoll_init(uint32_t api_version);

/* Release everything created by dragdoll_init. Safe to call when not initialised. */
DD_API void DD_CALL dragdoll_shutdown(void);

/* Detail message for the last error on the calling thread ("" if none). Owned by DragDoll. */
DD_API const char* DD_CALL dragdoll_last_error_message(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DRAGDOLL_H */
