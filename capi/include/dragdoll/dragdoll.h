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
    DD_ERROR_UNSUPPORTED = 6, /* e.g. debug drawing in a shipping build */
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

/* ---- Memory budget (ADR-0015) ---------------------------------------------------------- */
/* DragDoll runs inside other programs (32-bit San Andreas has little address space), so it reports what it uses.
 * Counts every Jolt allocation made through DragDoll's allocator (installed by dragdoll_init). Process-wide. */
typedef struct DdMemoryStats
{
    uint32_t struct_size;       /* = sizeof(DdMemoryStats) */
    uint64_t current_bytes;     /* Live heap right now */
    uint64_t peak_bytes;        /* Highest live heap since init or the last reset */
    uint64_t allocation_count;  /* Allocations since process start */
} DdMemoryStats;

DD_API DdResult DD_CALL dragdoll_get_memory_stats(DdMemoryStats* out_stats);

/* Resets peak_bytes to the current value (to measure one phase of a game session). */
DD_API void DD_CALL dragdoll_reset_peak_memory(void);

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
    const char* record_path;        /* Non-NULL: record every call on this world to this .ddrec file (see
                                       "Recording & replay"). Default NULL. */
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

/* Kinematic colliders (vehicles): set where each one is at the END of the host time covered by
 * the next dragdoll_world_step call (i.e. this frame's pose). DragDoll time-stamps the targets
 * and samples the host's trajectory at the exact time of every fixed step, so velocities - and
 * therefore impact impulses - are correct at any host frame rate (30 / 60 / 144 fps, unlocked),
 * with at most one fixed step of latency. Without new targets a collider holds its last pose.
 * Note: a large jump between targets (a teleport) produces a large velocity; remove and re-add
 * the collider to teleport it. */
DD_API DdResult DD_CALL dragdoll_world_set_kinematic_targets(DdWorld* world, const DdColliderId* ids,
                                                             const DdTransform* targets, uint32_t count);

/* Current transform of a collider. interpolated != 0 blends the last two fixed steps with
 * the interpolation alpha (for rendering); 0 returns the latest simulated state. */
DD_API DdResult DD_CALL dragdoll_world_get_collider_transform(const DdWorld* world, DdColliderId id,
                                                              int interpolated, DdTransform* out_transform);

/* ---- Characters (ragdolls built from rigs, ADR-0010) ----------------------------------- */
/* A character is owned by its world: destroying the world destroys its characters. */
typedef struct DdCharacter DdCharacter;

/* Characters currently in a world, in creation order (for tools, replays, viewers). */
DD_API uint32_t DD_CALL dragdoll_world_get_character_count(const DdWorld* world);
DD_API DdCharacter* DD_CALL dragdoll_world_get_character(DdWorld* world, uint32_t index); /* NULL if out of range */

/* Rig JSON compiled into the library (e.g. "humanoid16"), or NULL if unknown. Owned by DragDoll. */
DD_API const char* DD_CALL dragdoll_get_builtin_rig(const char* name);

typedef struct DdCharacterDesc
{
    uint32_t struct_size;  /* = sizeof(DdCharacterDesc) */
    const char* rig_json;  /* NUL-terminated rig JSON (see packs/rigs/), e.g. dragdoll_get_builtin_rig("humanoid16") */
    DdTransform transform; /* Where the rig origin (ground point between the feet) goes. The rig's +Y is first aligned
                              with the world up axis, then `rotation` is applied (e.g. a heading about up). */
    float mass_scale;      /* Multiplies the rig mass. <= 0 means 1. */
    uint64_t user_data;    /* Opaque host value. */
} DdCharacterDesc;

/* Defaults: no rig, identity transform, mass_scale 1. */
DD_API void DD_CALL dragdoll_character_desc_default(DdCharacterDesc* desc);

/* Builds the ragdoll and adds it to the world, limp (no muscles yet). Rig errors are reported in detail by
 * dragdoll_last_error_message(). */
DD_API DdResult DD_CALL dragdoll_character_create(DdWorld* world, const DdCharacterDesc* desc, DdCharacter** out_character);

/* Removes the character from its world. Safe with NULL. */
DD_API void DD_CALL dragdoll_character_destroy(DdCharacter* character);

/* Number of bodies (16 for humanoid16); body indices follow the rig's order. */
DD_API uint32_t DD_CALL dragdoll_character_get_body_count(const DdCharacter* character);

/* Name of a body ("pelvis", "shin_l", ...), or NULL if the index is out of range. Owned by DragDoll. */
DD_API const char* DD_CALL dragdoll_character_get_body_name(const DdCharacter* character, uint32_t body);

/* Index of the body with this name, or -1. */
DD_API int32_t DD_CALL dragdoll_character_find_body(const DdCharacter* character, const char* name);

/* Total mass in kg actually simulated. */
DD_API float DD_CALL dragdoll_character_get_mass(const DdCharacter* character);

/* World transforms of all bodies (out must hold body_count entries). interpolated != 0 blends the last two fixed steps
 * for rendering. Capsule bodies: local +Y runs along the bone (towards the child), local +X is the character's left. */
DD_API DdResult DD_CALL dragdoll_character_get_body_transforms(const DdCharacter* character, int interpolated,
                                                               DdTransform* out, uint32_t capacity);

/* Instant impulse (N*s) on one body, at world_point (NULL = centre of mass). */
DD_API DdResult DD_CALL dragdoll_character_apply_impulse(DdCharacter* character, uint32_t body, DdVec3 impulse,
                                                         const DdVec3* world_point);

/* Mass of one body in kg as simulated (0 for an invalid index). */
DD_API float DD_CALL dragdoll_character_get_body_mass(const DdCharacter* character, uint32_t body);

/* Parent body index (-1 for the root or an invalid index). Bodies follow the rig's order; parents come first. */
DD_API int32_t DD_CALL dragdoll_character_get_body_parent(const DdCharacter* character, uint32_t body);

/* ---- Muscles (Phase 3) -------------------------------------------------------------------
 * Every joint with a rig "strength" has motors that pull it towards the target pose (for now: the rig's rest pose).
 * A tension profile sets how hard (stiffness as frequency + damping ratio, ADR-0009 — applied to each joint's real
 * effective inertia, so a whole arm swings at the stated frequency — and the share of the joint's strength). Internal torques only (ADR-0003). New characters start LIMP. Note: an active motor replaces the joint's
 * passive friction, so profiles must be stiffer than that friction to add any hold. */
typedef enum DdTension
{
    DD_TENSION_LIMP = 0,    /* Motors off: only passive joint friction. */
    DD_TENSION_RELAXED = 1, /* Low tone: 15% strength, 1.2 Hz, critically damped; loose but keeps some shape. */
    DD_TENSION_TENSE = 2,   /* Braced: full strength, 8 Hz, over-damped: reaches a pose and locks there. */
    DD_TENSION_STUNNED = 3, /* Dazed: 30% strength, 0.6 Hz, under-damped; folds slowly and wobbles. */
    DD_TENSION_COUNT = 4,
    DD_TENSION_FORCE_32BIT = 0x7FFFFFFF
} DdTension;

/* Changes the tension profile, blending from the current muscle state over blend_seconds (0 = at once). */
DD_API DdResult DD_CALL dragdoll_character_set_tension(DdCharacter* character, DdTension tension, float blend_seconds);
/* The profile last set (the blend towards it may still be running). */
DD_API DdTension DD_CALL dragdoll_character_get_tension(const DdCharacter* character);

/* ---- Pose input / output: the host contract (ADR-0017 / D17) ------------------------------
 * A pose is one JOINT ROTATION per body (index = body index; entry 0, the root, is ignored on input and identity on
 * output): the body's rotation relative to its parent, as an offset from the rig's rest pose, expressed in the rig's
 * rest axes (+Y up, +Z forward, +X = character's left). Identity everywhere = the rest pose. Local like animation
 * bones: a child keeps its offset when its parent moves. Example: knee flexion = rotation of the shin about +X.
 * Hosts translate their skeleton to/from this form (e.g. SA's 32-bone ped skeleton, Phase 3). */

/* Sets the pose the muscles pull towards (they only act with a tension other than LIMP). `count` must equal the body
 * count; NULL resets the target to the rest pose. Hinge joints use only the rotation about their axis; every joint is
 * clamped to its limits by the solver. */
DD_API DdResult DD_CALL dragdoll_character_set_target_pose(DdCharacter* character, const DdQuat* joint_rotations,
                                                           uint32_t count);

/* The simulated pose in the same form, plus the root body's world transform (either output may be NULL). */
DD_API DdResult DD_CALL dragdoll_character_get_pose(const DdCharacter* character, int interpolated, DdQuat* joint_rotations,
                                                    uint32_t capacity, DdTransform* root);

/* Activation from motion (ADR-0017): places every body at the CURRENT pose (root transform = the root body's world
 * transform, as get_pose returns it, plus joint rotations) and gives it the velocity of the motion from the PREVIOUS
 * pose over `dt` seconds, so a walking or falling character keeps its momentum when physics takes over. Pass NULL as
 * previous_root to start at rest; NULL joint arrays mean the rest pose. `count` = body count (when arrays are given).
 * Also sets the target pose to the current pose, so muscles hold the handed-over pose until the host sets another. */
DD_API DdResult DD_CALL dragdoll_character_activate(DdCharacter* character, const DdTransform* previous_root,
                                                    const DdQuat* previous_joints, const DdTransform* current_root,
                                                    const DdQuat* current_joints, uint32_t count, float dt);

/* ---- Skeleton maps: translate a host skeleton to/from DragDoll poses (Phase 3) ------------
 * A map (packs/skeletons/*.json) names, for each rig body, the host bone that drives it, plus the host's model axes.
 * The host supplies its bones once (id, parent, bind transform in model space); the map measures how the host's bind
 * pose differs from the rig's rest pose (e.g. a T-pose vs arms down) and compensates both ways. Host bones without a
 * body (fingers, toes, jaw...) follow their parent rigidly. Rotations are in the host's MODEL space (relative to the
 * character root, e.g. SA's ped matrix); `model_to_world` places the model. */
typedef struct DdSkeletonMap DdSkeletonMap;

typedef struct DdHostBone
{
    int32_t id;        /* Host bone id (e.g. SA RpHAnim id) */
    int32_t parent;    /* Index of the parent in the array, -1 for the root. Parents come first. */
    DdTransform bind;  /* Bind (reference) transform in model space */
} DdHostBone;

/* Built-in maps compiled into the library (packs/skeletons/*.json), e.g. "sa_ped". NULL if unknown. */
DD_API const char* DD_CALL dragdoll_get_builtin_skeleton_map(const char* name);

/* Builds a map for `character`'s rig. Every body named in the map must find its bone id among `bones`. */
DD_API DdResult DD_CALL dragdoll_skeleton_map_create(const char* map_json, const DdCharacter* character, const DdHostBone* bones,
                                                     uint32_t bone_count, DdSkeletonMap** out_map);
DD_API void DD_CALL dragdoll_skeleton_map_destroy(DdSkeletonMap* map);

/* Host pose -> DragDoll pose. bone_rotations: one per host bone (model space). root_position: the root body's mapped
 * bone position in model space (NULL = its bind position). Outputs the root transform and joint rotations, ready for
 * dragdoll_character_activate / set_target_pose. */
DD_API DdResult DD_CALL dragdoll_skeleton_map_to_pose(const DdSkeletonMap* map, const DdQuat* bone_rotations, uint32_t bone_count,
                                                      const DdVec3* root_position, const DdTransform* model_to_world,
                                                      DdTransform* out_root, DdQuat* out_joints, uint32_t joint_capacity);

/* DragDoll pose (e.g. from dragdoll_character_get_pose) -> host bone rotations in model space, one per host bone. */
DD_API DdResult DD_CALL dragdoll_skeleton_map_from_pose(const DdSkeletonMap* map, const DdTransform* root, const DdQuat* joints,
                                                        uint32_t joint_count, const DdTransform* model_to_world,
                                                        DdQuat* out_bone_rotations, uint32_t bone_capacity);

typedef enum DdCharacterState
{
    DD_STATE_ACTIVE = 0,     /* Moving under physics. */
    DD_STATE_RECOVERING = 1, /* Reserved: getting up / regaining balance (later phases). */
    DD_STATE_SETTLED = 2,    /* Every body has been nearly still for a while: the host may hand back (get up / die). */
    DD_STATE_DEAD = 3,       /* Reserved: set from the host's outcome (Phase 6). */
    DD_STATE_FAILED = 4,     /* Instability detected: the character was taken out of the simulation (see failure).
                              * Its last good pose stays readable; other calls on it fail. The host should destroy it
                              * and fall back to its own animation. DragDoll never takes the host down with it. */
    DD_STATE_FORCE_32BIT = 0x7FFFFFFF
} DdCharacterState;

typedef enum DdFailure
{
    DD_FAILURE_NONE = 0,
    DD_FAILURE_NOT_FINITE = 1, /* A body position or rotation became NaN / infinite. */
    DD_FAILURE_TOO_FAST = 2,   /* A body moved faster than 100 m/s (an energy explosion). */
    DD_FAILURE_TORN_APART = 3, /* A joint's two ends separated by more than 0.25 m. */
    DD_FAILURE_FORCE_32BIT = 0x7FFFFFFF
} DdFailure;

typedef struct DdCharacterInfo
{
    uint32_t struct_size;   /* = sizeof(DdCharacterInfo) */
    DdCharacterState state;
    float still_seconds;    /* How long every body has been nearly still (0 while moving). */
    int32_t facing;         /* Recovery hint: +1 lying face up, -1 face down, 0 neither (e.g. on the side, standing). */
    DdVec3 head_direction;  /* Recovery hint: horizontal unit vector from the pelvis towards the head (0 if upright). */
    DdFailure failure;      /* Why the state is FAILED (NONE otherwise). */
} DdCharacterInfo;

DD_API DdResult DD_CALL dragdoll_character_get_info(const DdCharacter* character, DdCharacterInfo* out);

/* Test hook (dev builds only; DD_ERROR_UNSUPPORTED otherwise): on the next step the character's state is corrupted
 * with NaN exactly where a real numerical blow-up would surface, to exercise the FAILED path. */
DD_API DdResult DD_CALL dragdoll_debug_corrupt_character(DdCharacter* character);

/* Per-body blend weight for the host: 1 = show the physics pose, 0 = keep the host's own animation. All 1 for now;
 * behaviours will lower them (e.g. a hand reaching for a rail stays physics, the rest blends). */
DD_API DdResult DD_CALL dragdoll_character_get_blend_weights(const DdCharacter* character, float* weights, uint32_t capacity);

/* ---- Recording & replay (ADR-0007, ADR-0017) ------------------------------------------- */
/* A world created with DdWorldDesc.record_path writes every call that changes it (steps, colliders, kinematic targets,
 * characters, impulses) to a .ddrec file, plus the state hash after every step. A replay rebuilds the world from the
 * file and re-applies the calls host frame by host frame, so any host session (e.g. a San Andreas play session) can be
 * reproduced in the workbench without the host. Bit-identical with the same DragDoll binary; with another build the
 * replay still runs and reports where the state hashes first diverge. */

/* Adds a human note (e.g. "foot slides here") at the current point of the recording. No-op when not recording. */
DD_API DdResult DD_CALL dragdoll_world_record_note(DdWorld* world, const char* text);

/* 1 if this world is being recorded. */
DD_API int DD_CALL dragdoll_world_is_recording(const DdWorld* world);

typedef struct DdReplay DdReplay;

typedef struct DdReplayInfo
{
    uint32_t struct_size;       /* = sizeof(DdReplayInfo) */
    uint64_t frame_count;       /* Host frames (step calls) in the file */
    uint32_t recorded_api_version;
    const char* recorded_build;  /* dragdoll_get_build_info() of the recording binary; owned by the replay */
    int same_build;              /* 1 if recorded by a binary with identical build info (bit-identical expected) */
    DdVec3 up;                   /* The recorded world's up axis */
} DdReplayInfo;

typedef struct DdReplayFrame
{
    uint32_t struct_size;       /* = sizeof(DdReplayFrame) */
    uint64_t frame;             /* 1-based index of the host frame just replayed */
    uint32_t steps;             /* Fixed steps it ran */
    float host_dt;              /* Host time this frame covered (seconds): play frames back at this pace */
    int hash_matches;           /* 1 = state identical to the recording, 0 = diverged */
    uint64_t first_divergence;  /* First diverging frame so far, 0 = none */
    const char* note;           /* Note recorded in this frame, or NULL. Valid until the next advance. */
    int at_end;                 /* 1 = nothing left to replay */
} DdReplayFrame;

/* Opens a recording and rebuilds its world (without recording). */
DD_API DdResult DD_CALL dragdoll_replay_open(const char* path, DdReplay** out_replay);

/* Information about the recording. */
DD_API DdResult DD_CALL dragdoll_replay_get_info(const DdReplay* replay, DdReplayInfo* out_info);

/* Re-applies the next host frame: every recorded call up to and including the next step. */
DD_API DdResult DD_CALL dragdoll_replay_advance(DdReplay* replay, DdReplayFrame* out_frame);

/* The replayed world (owned by the replay): read transforms, draw it, query hashes. Do not change it. */
DD_API DdWorld* DD_CALL dragdoll_replay_get_world(DdReplay* replay);

/* Closes the replay and destroys its world. Safe with NULL. */
DD_API void DD_CALL dragdoll_replay_close(DdReplay* replay);

/* ---- Debug drawing (development builds only) ------------------------------------------ */
/* Visualise every body of a world as lines and triangles in world space: for the workbench,
 * the live viewer and in-game overlays. Colours are 0xAABBGGRR (little-endian RGBA bytes).
 * Shipping builds (DRAGDOLL_DEV_TOOLS=OFF) return DD_ERROR_UNSUPPORTED. Callbacks run on the
 * calling thread, inside dragdoll_world_debug_draw. */
#define DD_DRAW_SHAPES 0x1u            /* Body shapes (solid). */
#define DD_DRAW_CONSTRAINTS 0x2u       /* Joint frames. */
#define DD_DRAW_CONSTRAINT_LIMITS 0x4u /* Joint limits: where each joint may bend. */
#define DD_DRAW_SKIP_STATIC 0x8u       /* Leave out static bodies (proxy level geometry the host already renders). */

typedef struct DdDebugDrawCallbacks
{
    uint32_t struct_size; /* = sizeof(DdDebugDrawCallbacks) */
    void* user;           /* Passed back to every callback. */
    void(DD_CALL* line)(void* user, DdVec3 from, DdVec3 to, uint32_t color);
    void(DD_CALL* triangle)(void* user, DdVec3 a, DdVec3 b, DdVec3 c, uint32_t color);
    uint32_t flags;       /* DD_DRAW_* bits; 0 = DD_DRAW_SHAPES */
} DdDebugDrawCallbacks;

/* Either callback may be NULL to skip that primitive type. */
DD_API DdResult DD_CALL dragdoll_world_debug_draw(DdWorld* world, const DdDebugDrawCallbacks* callbacks);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DRAGDOLL_H */
