// SPDX-License-Identifier: MIT
// DragDoll behaviour SDK (Layer 1, ADR-0013 / ADR-0025). The ONLY header a behaviour may include.
//
// A behaviour reads a Percept (what the character senses, ADR-0024) and writes a Command (what its muscles should
// do). Nothing else: no forces, no velocities, no teleports (ADR-0003). It runs once per fixed step, before the
// muscles, and must be deterministic: its only inputs are the percept, its parameters and its own state.
//
// Not a stable ABI yet: behaviours are compiled into the library (a C ABI for third parties comes after v0.2).
#pragma once

#include <dragdoll/dragdoll.h>

#include <cstdint>
#include <memory>

namespace dragdoll::sdk
{
inline constexpr uint32_t kMaxBodies = 64;

// Tunable numbers of a behaviour (packs/behaviours/<name>.json, editable live). Keys are flat ("anticipate.time").
class Params
{
public:
    virtual ~Params() = default;
    virtual float number(const char* key, float fallback) const = 0;
};

struct Percept
{
    float dt = 0.0f;   // The fixed step (s)
    uint64_t step = 0; // Fixed steps simulated so far

    // Self
    DdVec3 up{};                 // World up (unit)
    DdTransform root{};          // Root body (pelvis)
    DdVec3 centreOfMass{};
    DdVec3 velocity{};           // Of the centre of mass
    bool kinematic = false;      // Host-animated: muscles have no effect until the host hands over
    float stillSeconds = 0.0f;   // How long every body has been nearly still
    DdTension tension[DD_REGION_COUNT] = {}; // What each region is set to now

    // Contacts and self state after the last step (ADR-0024 §3): airborne, ground, landing prediction, spin, how
    // hard the head and the whole body were hit; and per body what it touches (`contacts[bodyCount]`, rig order;
    // valid during update only).
    DdSenses senses{};
    const DdBodyContact* contacts = nullptr;
    uint32_t bodyCount = 0;

    // Threat (ADR-0024). threat.collider == DD_INVALID_COLLIDER: none.
    DdThreat threat{};
    float threatSeconds = 0.0f;     // How long this object has been the threat
    float threatSeenSeconds = 0.0f; // ...and how long the character has been aware of it (the stimulus for reactions)

    // Human limits (ADR-0025)
    float awareness = 0.0f; // 0..1: does the character know about the threat (seen, or told by the host)
    float vitality = 1.0f;  // 0..1: alertness and strength; 0 = nothing left to react with
};

struct Command
{
    // Tension per region; applied only where `tense[region]` is set (and only when it differs from the current one).
    bool tense[DD_REGION_COUNT] = {};
    DdTension tension[DD_REGION_COUNT] = {};
    float blendSeconds[DD_REGION_COUNT] = {};

    // Target pose (joint rotations as offsets from rest, in rig axes), `poseCount` joints; 0 = leave the target alone.
    uint32_t poseCount = 0;
    DdQuat pose[kMaxBodies] = {};

    // A named pose from the behaviour's pack ("rest" = the rest pose), applied to the regions in `poseRegions` (bit per
    // DdRegion). Applied before `pose` and before aims. Targets stay until something else sets them.
    const char* poseName = nullptr;
    uint32_t poseRegions = 0;

    // Point a limb at a world position (arm and leg regions): its end goes towards `target`, at most `extension`
    // (0..1) of the limb's length from its first joint. Solved by the core every step it is requested (the body
    // moves); when no longer requested the joints keep their last targets.
    struct Aim
    {
        bool active = false;
        DdVec3 target{};
        float extension = 1.0f;
    };
    Aim aim[DD_REGION_COUNT] = {};

    int balance = -1; // -1 = leave as is, 0 = off, 1 = on

    void usePose(const char* name, uint32_t regions = DD_REGIONS_ALL)
    {
        poseName = name;
        poseRegions = regions;
    }
    void aimAt(DdRegion region, DdVec3 worldTarget, float extension)
    {
        aim[region].active = true;
        aim[region].target = worldTarget;
        aim[region].extension = extension;
    }

    void setTension(DdRegion region, DdTension value, float blend)
    {
        tense[region] = true;
        tension[region] = value;
        blendSeconds[region] = blend;
    }
    void setTensionAll(DdTension value, float blend)
    {
        for (int r = 0; r < DD_REGION_COUNT; ++r) setTension(static_cast<DdRegion>(r), value, blend);
    }
};

class Behaviour
{
public:
    virtual ~Behaviour() = default;
    // Called when attached and whenever the parameters change.
    virtual void configure(const Params& /*params*/) {}
    // Once per fixed step.
    virtual void update(const Percept& percept, Command& command) = 0;
    // For tools: a short description of what the behaviour is doing now (e.g. the stage name).
    virtual const char* status() const { return ""; }
};

using Factory = std::unique_ptr<Behaviour> (*)();

// Implemented by the core: makes a behaviour available under `name` (dragdoll_character_set_behaviour).
void registerBehaviour(const char* name, Factory factory);

// Implemented by the behaviours library: registers every built-in behaviour. Called once from dragdoll_init.
void registerBuiltinBehaviours();

// The reaction clock (ADR-0025 §2): people react late. A stimulus starts it; reflexes become available after the
// reflex latency, chosen actions after the voluntary latency.
struct ReactionClock
{
    float reflexLatency = 0.1f;    // s
    float voluntaryLatency = 0.5f; // s
    float elapsed = -1.0f;         // < 0: no stimulus yet

    void stimulus()
    {
        if (elapsed < 0.0f) elapsed = 0.0f;
    }
    void clear() { elapsed = -1.0f; }
    void tick(float dt)
    {
        if (elapsed >= 0.0f) elapsed += dt;
    }
    bool reflex() const { return elapsed >= reflexLatency; }
    bool voluntary() const { return elapsed >= voluntaryLatency; }
};
} // namespace dragdoll::sdk
