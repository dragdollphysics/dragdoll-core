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

/* ---- Math types (ADR-0004: SI units, right-handed, quaternions x,y,z,w) ---------------- */
typedef struct DdVec3
{
    float x, y, z;
} DdVec3;

typedef struct DdQuat
{
    float x, y, z, w;
} DdQuat;

typedef struct DdTransform
{
    DdVec3 position;
    DdQuat rotation;
} DdTransform;

/* ---- World ----------------------------------------------------------------------------- */
/* A world owns one Jolt physics system (ADR-0002). Not thread-safe: drive a world from one
 * thread at a time (ADR-0020). */
typedef struct DdWorld DdWorld;

typedef struct DdWorldDesc
{
    uint32_t struct_size;           /* = sizeof(DdWorldDesc); lets the ABI grow safely */
    DdVec3 up;                      /* World up axis, normalised by DragDoll. Default +Y. */
    float gravity;                  /* m/s^2 along -up. Default 9.81. */
    float step_hz;                  /* Fixed simulation rate, 60..240. Default 120 (ADR-0006). */
    uint32_t max_steps_per_call;    /* Spiral-of-death guard: extra time is dropped. Default 8. */
    uint32_t worker_threads;        /* 0 = run on the calling thread only. Default 0. */
    uint32_t max_bodies;            /* Capacity. Default 16384. */
} DdWorldDesc;

/* Fill a description with the defaults above. */
DD_API void DD_CALL dragdoll_world_desc_default(DdWorldDesc* desc);

DD_API DdResult DD_CALL dragdoll_world_create(const DdWorldDesc* desc, DdWorld** out_world);

/* Destroys the world and everything in it. Safe with NULL. */
DD_API void DD_CALL dragdoll_world_destroy(DdWorld* world);

/* Advance by host time dt (seconds, any rate). Runs as many whole fixed steps as the
 * accumulated time allows (at most max_steps_per_call; the rest is dropped).
 * out_steps (optional) receives the number of fixed steps taken. */
DD_API DdResult DD_CALL dragdoll_world_step(DdWorld* world, float dt, uint32_t* out_steps);

/* Run exactly `steps` fixed steps, ignoring the accumulator (tests, lab, replays). */
DD_API DdResult DD_CALL dragdoll_world_step_fixed(DdWorld* world, uint32_t steps);

/* Fraction [0,1) of a fixed step accumulated but not yet simulated; interpolate render
 * transforms between the previous and current step with it (ADR-0006). */
DD_API float DD_CALL dragdoll_world_get_interpolation_alpha(const DdWorld* world);

/* Total fixed steps simulated since creation. */
DD_API uint64_t DD_CALL dragdoll_world_get_step_count(const DdWorld* world);

/* 64-bit hash of the full simulation state (positions, rotations, velocities of every body,
 * in a stable order). Equal hashes after equal inputs = determinism (ADR-0007). */
DD_API DdResult DD_CALL dragdoll_world_compute_state_hash(const DdWorld* world, uint64_t* out_hash);

/* ---- Proxy colliders (ADR-0002) -------------------------------------------------------- */
/* Hosts mirror their environment into the DragDoll world: static level geometry, kinematic
 * vehicles/moving objects, and optional loose dynamic props. */
typedef uint32_t DdColliderId;
#define DD_INVALID_COLLIDER ((DdColliderId)0)

typedef enum DdShapeType
{
    DD_SHAPE_SPHERE = 0,  /* radius */
    DD_SHAPE_BOX = 1,     /* half_extents */
    DD_SHAPE_CAPSULE = 2, /* radius + half_height; axis = the world's up axis in local space */
    DD_SHAPE_MESH = 3,    /* triangles; static or kinematic only (never dynamic) */
    DD_SHAPE_FORCE_32BIT = 0x7FFFFFFF
} DdShapeType;

typedef enum DdMotionType
{
    DD_MOTION_STATIC = 0,    /* Never moves (level geometry). */
    DD_MOTION_KINEMATIC = 1, /* Moved by the host each frame (vehicles); pushes, is never pushed. */
    DD_MOTION_DYNAMIC = 2,   /* Simulated by DragDoll (loose props). */
    DD_MOTION_FORCE_32BIT = 0x7FFFFFFF
} DdMotionType;

typedef struct DdColliderDesc
{
    uint32_t struct_size; /* = sizeof(DdColliderDesc) */
    DdShapeType shape;
    DdMotionType motion;
    DdTransform transform; /* Initial world transform. */

    /* Shape parameters (only the ones the shape uses are read). */
    float radius;
    float half_height;
    DdVec3 half_extents;
    const DdVec3* vertices;  /* Mesh: vertex positions (local space). */
    uint32_t vertex_count;
    const uint32_t* indices; /* Mesh: 3 per triangle; NULL = vertices are a triangle list. */
    uint32_t index_count;

    /* Material & dynamics. */
    float friction;          /* Default 0.6 */
    float restitution;       /* Default 0.0 */
    float mass;              /* Dynamic only; 0 = derive from shape at 1000 kg/m^3. */
    uint64_t user_data;      /* Opaque host value, e.g. a game object pointer. */
} DdColliderDesc;

/* Fill a description with defaults: unit sphere, static, identity transform. */
DD_API void DD_CALL dragdoll_collider_desc_default(DdColliderDesc* desc);

/* Add `count` colliders in one batch (efficient for streaming). out_ids must hold `count`
 * entries. On failure nothing is added. */
DD_API DdResult DD_CALL dragdoll_world_add_colliders(DdWorld* world, const DdColliderDesc* descs, uint32_t count,
                                                     DdColliderId* out_ids);

/* Remove `count` colliders in one batch. Unknown ids are ignored. */
DD_API DdResult DD_CALL dragdoll_world_remove_colliders(DdWorld* world, const DdColliderId* ids, uint32_t count);

/* Kinematic colliders: set where each one must be at the end of the next fixed step.
 * DragDoll derives the velocities, so contacts get the right impulses (e.g. a car hit). */
DD_API DdResult DD_CALL dragdoll_world_set_kinematic_targets(DdWorld* world, const DdColliderId* ids,
                                                             const DdTransform* targets, uint32_t count);

/* Current transform of a collider. interpolated != 0 blends the last two fixed steps with
 * the interpolation alpha (for rendering); 0 returns the latest simulated state. */
DD_API DdResult DD_CALL dragdoll_world_get_collider_transform(const DdWorld* world, DdColliderId id,
                                                              int interpolated, DdTransform* out_transform);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DRAGDOLL_H */
