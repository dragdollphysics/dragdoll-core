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
    uint32_t group;          /* Parts of one object share a group (a car's parts = one threat, ADR-0024); 0 = on its own. */
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
    float size_scale;      /* Uniform size: every length of the rig is multiplied by it (a 1.75 m rig at 1.09 stands 1.91 m
                            * tall), and its mass by size_scale^3 on top of mass_scale. <= 0 means 1. Lets a host match its
                            * own characters' proportions with one rig. */
} DdCharacterDesc;

/* Defaults: no rig, identity transform, mass_scale 1, size_scale 1. */
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

/* A display name for tools (live view, inspect), e.g. "CJ (puppet)". Copied; NULL clears it. */
DD_API DdResult DD_CALL dragdoll_character_set_name(DdCharacter* character, const char* name);
DD_API const char* DD_CALL dragdoll_character_get_name(const DdCharacter* character); /* "" if unnamed */

/* Whether dragdoll_world_debug_draw draws this character (default 1). A live-tap viewer can switch it remotely. */
DD_API DdResult DD_CALL dragdoll_character_set_debug_draw(DdCharacter* character, int visible);

/* A short text from the host about what the character is doing in the host (e.g. the animation it plays), at most 63
 * bytes. Never used by the simulation; recorded when it changes, streamed by the live tap and shown by tools, so a
 * recording or a capture says what the host was doing at each moment. */
DD_API DdResult DD_CALL dragdoll_character_set_label(DdCharacter* character, const char* label);
DD_API const char* DD_CALL dragdoll_character_get_label(const DdCharacter* character); /* "" if none */
DD_API int DD_CALL dragdoll_character_get_debug_draw(const DdCharacter* character);

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
/* The profile last set for the whole body (the blend towards it may still be running). */
DD_API DdTension DD_CALL dragdoll_character_get_tension(const DdCharacter* character);

/* Body regions (ADR-0025): muscles can be tensed per region. A joint belongs to the region of its child body (the
 * shoulder joint to the arm, the hip joint to the leg). Regions come from the rig's body names (arm / hand, thigh /
 * shin / foot / leg, head / neck; "_l" / "_r" or "left" / "right" for the side); everything else is the trunk. */
typedef enum DdRegion
{
    DD_REGION_HEAD_NECK = 0,
    DD_REGION_TRUNK = 1,
    DD_REGION_ARM_LEFT = 2,
    DD_REGION_ARM_RIGHT = 3,
    DD_REGION_LEG_LEFT = 4,
    DD_REGION_LEG_RIGHT = 5,
    DD_REGION_COUNT = 6,
    DD_REGION_FORCE_32BIT = 0x7FFFFFFF
} DdRegion;

#define DD_REGION_BIT(region) (1u << (region))
#define DD_REGIONS_ALL 0x3Fu

/* Like dragdoll_character_set_tension, for one region; set_tension itself sets every region. */
DD_API DdResult DD_CALL dragdoll_character_set_region_tension(DdCharacter* character, DdRegion region, DdTension tension,
                                                              float blend_seconds);
DD_API DdTension DD_CALL dragdoll_character_get_region_tension(const DdCharacter* character, DdRegion region);
/* The region of a body's joint (the root body counts as trunk). DD_REGION_COUNT if out of range. */
DD_API DdRegion DD_CALL dragdoll_character_get_body_region(const DdCharacter* character, uint32_t body);

/* Standing balance (ADR-0023): keeps the capture point over the feet with ankle and hip strategies, by offsetting the
 * muscles' targets (internal torques only). Needs a tension other than LIMP; no stepping yet. Off by default. */
DD_API DdResult DD_CALL dragdoll_character_set_balance(DdCharacter* character, int enabled);
DD_API int DD_CALL dragdoll_character_get_balance(const DdCharacter* character);

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
/* Kinematic mode: the host animates the character (e.g. the player, or a pedestrian before it is hit). Its bodies
 * then move only through dragdoll_character_activate (pose + velocity), ignore gravity, muscles and balance, cannot be
 * pushed, do not collide with static or other kinematic shapes, but push dynamic bodies (ragdolls) with their full
 * motion. Switching back to dynamic keeps the current velocities: the handover when the character gets hit. */
DD_API DdResult DD_CALL dragdoll_character_set_kinematic(DdCharacter* character, int kinematic);
DD_API int DD_CALL dragdoll_character_get_kinematic(const DdCharacter* character);
/* Handover on impact (ADR-0025, Impact stage): while kinematic, the character turns dynamic by itself - keeping the
 * velocities of its animated motion - as soon as its threat (dragdoll_character_get_threat) is due within
 * `lead_seconds`, so the vehicle meets a physical body instead of passing through a host-animated one. Check
 * dragdoll_character_get_kinematic each frame and stop animating once it reads 0. 0 = off (default); about one fixed
 * step (1/120 s) plus a margin, e.g. 0.0125, hands over just before contact. */
DD_API DdResult DD_CALL dragdoll_character_set_auto_handover(DdCharacter* character, float lead_seconds);
DD_API float DD_CALL dragdoll_character_get_auto_handover(const DdCharacter* character);
/* Bodies on the road (ADR-0026): from the moment the trunk or head touches the ground after a hit (also while
 * the vehicle still touches it: a body dragged under a car), moving colliders (vehicles) pass through it instead of colliding, so a car driving over a body lying on the road
 * cannot wedge it under its front and squeeze it out. The static level still collides. The host may give its own
 * vehicle the bump (SA: the game ped's own collision). Off by default; recorded. */
DD_API DdResult DD_CALL dragdoll_character_set_vehicle_pass_through(DdCharacter* character, int enabled);
DD_API int DD_CALL dragdoll_character_get_vehicle_pass_through(const DdCharacter* character);

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
    float balance_error;    /* Balance on: horizontal distance (m) from the capture point to the support centre. */
    int32_t asleep;         /* 1 once the physics engine has put the body to sleep (at rest: Jolt's own deactivation);
                             * 0 while it moves, and for kinematic characters. */
} DdCharacterInfo;

DD_API DdResult DD_CALL dragdoll_character_get_info(const DdCharacter* character, DdCharacterInfo* out);

/* ---- Behaviours (ADR-0025) ----------------------------------------------------------------
 * A behaviour commands the character's muscles from what it senses (threats, its own state): tension per region,
 * target pose, balance. Nothing else - no forces. It runs every fixed step, also while the character is kinematic
 * (it watches; its muscle commands take effect once the host makes the character dynamic). While a behaviour is
 * attached it owns the muscles: host calls to set_tension / set_target_pose still work but may be overridden. */

/* Attaches a built-in behaviour by name ("car_hit"); NULL or "" detaches. Unknown names are an error. */
DD_API DdResult DD_CALL dragdoll_character_set_behaviour(DdCharacter* character, const char* name);
DD_API const char* DD_CALL dragdoll_character_get_behaviour(const DdCharacter* character); /* "" if none */
/* For tools: what the behaviour is doing now (e.g. its stage). Valid until the next step of this world. */
DD_API const char* DD_CALL dragdoll_character_get_behaviour_status(const DdCharacter* character);

/* Does the character know about its threat? 0..1 from the host (a game knows whether its pedestrian was looking,
 * fleeing, distracted); a negative value (the default) = automatic: aware when the threat lies within the head's
 * field of view. */
DD_API DdResult DD_CALL dragdoll_character_set_awareness(DdCharacter* character, float awareness);
DD_API float DD_CALL dragdoll_character_get_awareness(const DdCharacter* character);
/* Alertness and strength, 0..1 (default 1). 0 = nothing left to react with (e.g. the host's outcome is "killed"). */
DD_API DdResult DD_CALL dragdoll_character_set_vitality(DdCharacter* character, float vitality);
DD_API float DD_CALL dragdoll_character_get_vitality(const DdCharacter* character);

/* Points a limb (an arm or leg region) at a world position: sets the target pose of its two main joints (shoulder +
 * elbow, hip + knee) so that the limb's end - the hand's or foot's centre - lies towards `target`, at most `extension`
 * (0..1) of the limb's length from its first joint (1 = straight if the target is that far). Only a target: the
 * muscles have to get there (tension), and the joints' limits still hold. Call it again as the body moves.
 * DD_ERROR_INVALID_ARGUMENT for regions that are not two-joint limbs. */
DD_API DdResult DD_CALL dragdoll_character_aim(DdCharacter* character, DdRegion region, DdVec3 target, float extension);

/* Sets the target pose of the bodies in `region_mask` (DD_REGION_BIT, DD_REGIONS_ALL) from a named pose of a
 * behaviour's pack in this world ("poses" in the pack JSON: per body, degrees about the rig's X, Y, Z axes as offsets
 * from rest). The pose "rest" always exists. */
DD_API DdResult DD_CALL dragdoll_character_apply_pose(DdCharacter* character, const char* behaviour, const char* pose,
                                                      uint32_t region_mask);

/* Replaces a behaviour's tunable numbers in this world from pack JSON (packs/behaviours/<name>.json format); every
 * character using it picks them up on the next step. Recorded, so replays use the same numbers. */
DD_API DdResult DD_CALL dragdoll_world_set_behaviour_params(DdWorld* world, const char* behaviour, const char* json);

/* ---- Perception: contacts and self state (ADR-0024 §3). Read-only senses, refreshed after every fixed step. -------
 * Contacts and hit severity exist for simulated (dynamic) characters; a host-animated (kinematic) character is not
 * pushed by anything, so it reports none. */

#define DD_CONTACT_GROUND 0x1u    /* Touching static geometry (the level). */
#define DD_CONTACT_OBJECT 0x2u    /* Touching a moving collider (a vehicle). */
#define DD_CONTACT_CHARACTER 0x4u /* Touching another character. */

/* What one body of a character touched during the last fixed step. */
typedef struct DdBodyContact
{
    uint32_t flags;        /* DD_CONTACT_* bits; 0 = touching nothing. */
    DdColliderId collider; /* The moving collider it touches (the deepest contact), else the static one; 0 if none. */
    uint32_t group;        /* That collider's group. */
    DdVec3 point;          /* World position of that contact. */
    DdVec3 normal;         /* Unit vector from the other thing into this body. */
    float delta_v;         /* m/s: this body's velocity change over the step without gravity's share: what an accelerometer
                            * on it would report. 0 in free fall, g x dt (0.08 at 120 Hz) when resting, large when hit. */
} DdBodyContact;

typedef struct DdSenses
{
    uint32_t struct_size;       /* = sizeof(DdSenses) */
    int32_t airborne;           /* 1 = no body has touched anything for at least 3 steps (25 ms). */
    float airborne_seconds;     /* How long nothing has been touched (0 while in contact). */
    uint32_t ground_bodies;     /* How many bodies touch the level. */
    uint32_t object_bodies;     /* How many bodies touch moving colliders. */
    DdColliderId carried_by;    /* The moving collider most of those bodies touch (0 = none): what the body lies on. */
    uint32_t carried_by_group;
    float ground_height;        /* m: centre of mass above the level straight below it; < 0 = no ground within 30 m. */
    DdVec3 ground_normal;       /* Unit normal of that ground (the world's up if unknown). */
    float upright;              /* 1 = trunk pointing up, 0 = horizontal, -1 = head down. */
    DdVec3 angular_momentum;    /* kg m^2/s about the centre of mass (which way, and how fast, the body is turning). */
    float landing_seconds;      /* While airborne: ballistic time until the body reaches the ground; else < 0. */
    int32_t landing_body;       /* ...and the body predicted to arrive first (-1 = unknown). */
    float head_delta_v;         /* m/s: the head's delta_v over the last step (see DdBodyContact.delta_v). */
    float impact_delta_v;       /* m/s: the same for the whole body (its centre of mass). */
    float peak_head_delta_v;    /* Largest head_delta_v since the character was created or last made dynamic. */
    float peak_impact_delta_v;
} DdSenses;

DD_API DdResult DD_CALL dragdoll_character_get_senses(const DdCharacter* character, DdSenses* out);
/* Copies one DdBodyContact per body (rig order); returns the body count, copying at most `capacity`. */
DD_API uint32_t DD_CALL dragdoll_character_get_contacts(const DdCharacter* character, DdBodyContact* out, uint32_t capacity);

/* ---- Perception: threat (ADR-0024). Read-only senses, updated every fixed step for every character. ---- */

/* Post-impact trajectory classes of the pedestrian-impact literature. DragDoll predicts and measures them; it never
 * forces one. */
typedef enum DdTrajectory
{
    DD_TRAJECTORY_NONE = 0,
    DD_TRAJECTORY_WRAP = 1,               /* Struck below the centre of mass: rotates onto the bonnet. */
    DD_TRAJECTORY_FORWARD_PROJECTION = 2, /* Centre of mass inside the front face: thrown forward. */
    DD_TRAJECTORY_FENDER_VAULT = 3,
    DD_TRAJECTORY_ROOF_VAULT = 4,
    DD_TRAJECTORY_SOMERSAULT = 5,
    DD_TRAJECTORY_FORCE_32BIT = 0x7FFFFFFF
} DdTrajectory;

/* The front of a moving object, measured from its own collision geometry along the predicted impact line (about 50
 * ray casts, once per approach). Heights are above the ground under its front; lengths run back from its foremost
 * point; angles are in degrees from horizontal. */
typedef struct DdFrontProfile
{
    int32_t valid;              /* 0 until a threat has been probed. */
    float front_top_height;     /* Top of the near-vertical front face (a car's bumper line). */
    float leading_edge_height;  /* Where the front turns into a top surface (bonnet leading edge; the roof for a flat front). */
    float leading_edge_setback; /* How far behind the foremost point that is. */
    float bonnet_length;        /* Length of the low, shallow surface behind the leading edge (0 for a flat front). */
    float bonnet_angle;
    float windscreen_angle;     /* 0 if there is none. */
    float roof_height;          /* Highest point within the probed length. */
} DdFrontProfile;

typedef struct DdThreat
{
    uint32_t struct_size;     /* = sizeof(DdThreat) */
    uint32_t group;           /* Group of the threatening object (0 if it is a single ungrouped collider). */
    DdColliderId collider;    /* One of its colliders; DD_INVALID_COLLIDER = no threat (the other fields are then 0). */
    float time_to_collision;  /* s until its front reaches the character (0 = now). */
    float miss_distance;      /* m between the character and the object's side at that moment (0 = in its path). */
    float lateral_offset;     /* m from the object's centre line at that moment (+ = the object's left). */
    float closing_speed;      /* m/s */
    DdVec3 direction;         /* Unit vector: the object's direction of travel (horizontal). */
    DdTrajectory predicted;   /* Expected class from the front profile and the character's centre-of-mass height. */
    DdFrontProfile profile;
} DdThreat;

/* The most imminent threat to this character: a moving collider or group (> 1 m/s) on course to reach it within 2 s. */
DD_API DdResult DD_CALL dragdoll_character_get_threat(const DdCharacter* character, DdThreat* out);

/* The object this character rides in or on (a collider group: its car, its bike), or 0 for none. That object is never
 * a threat to it. Hosts know this exactly (a game knows who sits in which vehicle); set it when it changes. */
DD_API DdResult DD_CALL dragdoll_character_set_riding(DdCharacter* character, uint32_t group);
DD_API uint32_t DD_CALL dragdoll_character_get_riding(const DdCharacter* character);

/* ---- Impact report (ADR-0024 §5): what happened when a moving object hit the character ------------------------
 * Measured, never steered: the first hit by a moving object after the character was created or last made dynamic,
 * from first contact to rest. Times are seconds after the first contact (< 0 = has not happened). Distances are
 * horizontal, metres. Zones are where on the object's front a contact lies (from its probed profile). */

typedef enum DdZone
{
    DD_ZONE_NONE = 0,
    DD_ZONE_FRONT = 1,        /* The near-vertical front face (bumper, grille). */
    DD_ZONE_LEADING_EDGE = 2, /* Within 15 cm of the bonnet leading edge. */
    DD_ZONE_BONNET = 3,
    DD_ZONE_WINDSCREEN = 4,
    DD_ZONE_ROOF = 5,
    DD_ZONE_FORCE_32BIT = 0x7FFFFFFF
} DdZone;

#define DD_IMPACT_MAX_ORDER 16

typedef struct DdImpactReport
{
    uint32_t struct_size;     /* = sizeof(DdImpactReport) */
    int32_t valid;            /* 1 once a moving object has touched the character (the other fields are then set). */
    int32_t finished;         /* 1 once the character has come to rest after it. */
    uint32_t group;           /* The object (group) that hit it, and the collider first touched. */
    DdColliderId collider;
    float vehicle_speed;      /* m/s, the object's speed at first contact. */
    float lateral_offset;     /* m from the object's centre line at first contact (+ = its left). */
    int32_t first_body;       /* The body that touched first, where on the front, and at what height above the ground. */
    DdZone first_zone;
    float first_height;
    uint32_t contact_order[DD_IMPACT_MAX_ORDER]; /* Bodies in the order they first touched the object. */
    uint32_t contact_count;
    float head_strike_time;   /* First head contact with the object. */
    DdZone head_strike_zone;
    float wrap_around_distance; /* m along the object's front contour, from the ground to the head contact. */
    float head_strike_speed;  /* m/s, the head's speed relative to the object just before the strike. */
    float separation_time;    /* No body has touched the object for 25 ms. */
    DdVec3 launch_velocity;   /* Centre of mass, at separation. */
    float launch_speed;
    float launch_angle;       /* Degrees above horizontal. */
    float launch_height;      /* m, centre of mass above the ground at separation. */
    float apex_height;        /* m, highest centre of mass above the ground during the flight. */
    float com_over_front;     /* m, the most the centre of mass got behind the object's foremost point while in
                               * contact (< leading-edge setback: never over the bonnet; -100 if unknown). */
    float contact_seconds;    /* First contact -> separation. */
    float ground_time;        /* First ground contact after separation, and the body that made it. */
    int32_t ground_body;
    float rest_time;          /* Still for 0.5 s (settled). */
    float carry_distance;     /* First contact -> separation (centre of mass). */
    float flight_slide_distance; /* Separation -> rest. */
    float throw_distance;     /* First contact -> rest. */
    float corridor_min;       /* Flight + slide the throw equation expects for the measured launch (speed, angle,
                               * height) over body-on-road friction 0.66-0.84: see ADR-0024 §5. -1 without a flight. */
    float corridor_max;
    int32_t in_corridor;      /* 1 = flight_slide_distance inside [corridor_min, corridor_max]; 0 outside; -1 = no
                               * flight (the feet were still on the road at separation: pushed, not thrown). */
    float pitch_degrees;      /* Body rotation about the object's left axis from first contact to first ground contact. */
    float peak_head_delta_v;  /* m/s, hardest head hit (one step) from first contact to rest. */
    DdTrajectory measured;    /* The class the motion fits (ADR-0024 §5 rules). */
} DdImpactReport;

DD_API DdResult DD_CALL dragdoll_character_get_impact_report(const DdCharacter* character, DdImpactReport* out);

/* For tools: the probed front silhouette as a world-space polyline (front face bottom to top, then back over the
 * top). Returns the number of points (0 = no profile); copies at most `capacity`. */
DD_API uint32_t DD_CALL dragdoll_character_get_threat_silhouette(const DdCharacter* character, DdVec3* out_points, uint32_t capacity);

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

/* Live tap (dev builds, Windows; DD_ERROR_UNSUPPORTED otherwise): streams this world — DragDoll's proxy colliders and
 * characters, nothing else — to a viewer on 127.0.0.1:port as JSON lines (`dragdoll_view --attach port`). Start it
 * right after creating the world, so it sees every collider and character. Frames are dropped, never queued, when the
 * viewer lags: the host never waits. */
DD_API DdResult DD_CALL dragdoll_world_start_live_tap(DdWorld* world, uint16_t port);
DD_API void DD_CALL dragdoll_world_stop_live_tap(DdWorld* world);

/* Messages the viewer addresses to the host itself (e.g. input forwarded from the viewer), oldest first. Copies the next
 * one into buffer (NUL-terminated, cut to capacity - 1) and returns its full length; returns 0 when there is none (or no
 * tap). Not simulated, not recorded: the host decides what a message means. */
DD_API uint32_t DD_CALL dragdoll_world_poll_live_message(DdWorld* world, char* buffer, uint32_t capacity);

/* For tools only (not simulated, not recorded): the host's camera — world position, view direction, up and VERTICAL
 * field of view in degrees. Sent with every live-tap frame so a viewer can show the host's own view. */
DD_API void DD_CALL dragdoll_world_set_host_camera(DdWorld* world, DdVec3 position, DdVec3 forward, DdVec3 up, float fov_degrees);

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
#define DD_DRAW_CAMERA_LOD 0x10u       /* Coarser detail for distant shapes, from camera_position (much faster). */

typedef struct DdDebugDrawCallbacks
{
    uint32_t struct_size; /* = sizeof(DdDebugDrawCallbacks) */
    void* user;           /* Passed back to every callback. */
    void(DD_CALL* line)(void* user, DdVec3 from, DdVec3 to, uint32_t color);
    void(DD_CALL* triangle)(void* user, DdVec3 a, DdVec3 b, DdVec3 c, uint32_t color);
    uint32_t flags;       /* DD_DRAW_* bits; 0 = DD_DRAW_SHAPES */
    DdVec3 camera_position; /* With DD_DRAW_CAMERA_LOD: the viewpoint that picks each shape's level of detail. */
} DdDebugDrawCallbacks;

/* Either callback may be NULL to skip that primitive type. */
DD_API DdResult DD_CALL dragdoll_world_debug_draw(DdWorld* world, const DdDebugDrawCallbacks* callbacks);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DRAGDOLL_H */
