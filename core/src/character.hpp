// SPDX-License-Identifier: MIT
// A character: one rig instantiated as a Jolt ragdoll inside a World. Defined at global scope because it is the
// opaque DdCharacter of the C ABI.
#pragma once

#include "rig.hpp"

#include <dragdoll/behaviour_sdk.h>
#include <dragdoll/dragdoll.h>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Ragdoll/Ragdoll.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dragdoll
{
class World;

// Muscle drive of one tension profile (ADR-0009: stiffness = frequency + damping ratio).
struct MuscleParams
{
    float frequency = 0.0f; // Hz
    float damping = 1.0f;   // Ratio (1 = critical)
    float share = 0.0f;     // Share of each joint's strength usable as torque (0 = limp)
};
} // namespace dragdoll

struct DdCharacter
{
    dragdoll::World* world = nullptr;
    uint32_t id = 0;
    uint64_t userData = 0;
    std::string name;          // Display name for tools
    bool debugVisible = true;  // Drawn by dragdoll_world_debug_draw
    float totalMass = 0.0f;

    JPH::Ref<JPH::RagdollSettings> settings;
    JPH::Ref<JPH::Ragdoll> ragdoll;
    std::vector<std::string> names;

    // Muscles (Phase 3): per body (index = rig body; the root has no joint).
    std::vector<int> parent;           // -1 for the root
    std::vector<float> strength;       // Peak joint torque, N*m (mass-scaled); 0 = no muscle
    std::vector<bool> hinge;           // Joint type to the parent
    std::vector<JPH::Quat> restRig;    // Body rotation at rest in rig space (+Y up, +Z forward, +X left)
    std::vector<JPH::Vec3> hingeAxisRig; // Hinge joints: the axis in rig space
    std::vector<float> hingeMin, hingeMax; // Hinge joints: limits (radians about the axis)
    std::vector<JPH::Quat> targetPose; // Joint rotations the muscles pull towards (offsets from rest, rig axes)
    // Effective inertia of each joint (kg*m^2): everything on the child side against everything on the parent side,
    // about the joint, at rest. Turns frequency + damping ratio into real stiffness, so the chain beyond a joint (a
    // whole arm at the shoulder) is accounted for.
    std::vector<float> jointInertia;
    std::vector<float> bodyMass; // kg, as simulated
    // Inertia of everything on the PARENT side of each joint (the load a planted leg carries): used for the stance
    // leg's joints while balancing, where the foot is fixed and the body above swings like an inverted pendulum.
    std::vector<float> loadInertia;
    std::vector<bool> stanceJoint; // thigh / shin / foot joints
    // Rest geometry for placing the body from a pose (forward kinematics): the joint pivot in the parent's body frame,
    // and this body's position relative to its joint pivot in its own body frame.
    std::vector<JPH::Vec3> pivotInParent, bodyFromPivot;
    std::vector<JPH::Vec3> pivotRig, centreRig; // Joint pivot / shape centre at rest, rig space (skeleton maps)
    DdTension tension = DD_TENSION_LIMP; // Last profile set for the whole body
    // Muscle drive per body region (ADR-0025): each region blends on its own.
    struct RegionMuscles
    {
        dragdoll::MuscleParams from, now, to; // `now` = what the motors use
        float time = 0.0f, elapsed = 0.0f;
        DdTension tension = DD_TENSION_LIMP;
    };
    std::array<RegionMuscles, DD_REGION_COUNT> regionMuscles;
    std::vector<uint8_t> region; // Per body: the region of its joint (DdRegion)
    bool musclesDirty = false;

    // Host contract (D17)
    float stillSeconds = 0.0f;
    DdCharacterState state = DD_STATE_ACTIVE;
    DdFailure failure = DD_FAILURE_NONE;
    bool inPhysics = true;          // false once FAILED: removed from the physics system
    bool kinematic = false;         // Host-animated: moved only by activate
    bool debugCorrupt = false;      // Test hook: inject NaN on the next capture

    // Standing balance (ADR-0023)
    bool balance = false;
    std::vector<JPH::Quat> balanceOffset; // Per body, applied on top of the target pose (rig axes)
    int footLeft = -1, footRight = -1, thighLeft = -1, thighRight = -1;
    float balanceError = 0.0f;
    float standForward = 0.0f, standLeft = 0.0f; // Rest-pose centre of mass relative to the feet centre (m, character axes)
    float standHeight = 0.9f;        // Rest-pose centre of mass height above the feet centres (m)
    bool balanceActive = false;      // Balance on AND actually standing (else the corrections fade out)

    // Behaviour (ADR-0025): commands the muscles from what the character senses.
    std::unique_ptr<dragdoll::sdk::Behaviour> behaviour;
    std::string behaviourName, behaviourStatus;
    uint32_t behaviourParamsRevision = 0;
    float awarenessOverride = -1.0f; // < 0: awareness comes from what the head can see
    float vitality = 1.0f;
    int headBody = -1;               // For the field of view (-1: use the root)
    DdColliderId threatFrom = DD_INVALID_COLLIDER; // The object the timers below refer to
    float threatSeconds = 0.0f, threatSeenSeconds = 0.0f;

    // Perception (ADR-0024): the most imminent threat and the probed front of that object.
    DdThreat threat{};
    uint32_t ridingGroup = 0; // The object (group) the character rides: never a threat to it
    JPH::Vec3 sensedVelocity = JPH::Vec3::sZero(); // Centre-of-mass velocity, smoothed (host-animated bodies move in bursts)
    float sensedComHeight = 0.0f;                  // Centre of mass above the probed ground (for tools)
    std::vector<DdVec3> threatSilhouette; // Per point: x = distance behind the object's front, z = height above the ground
    float probedGround = 0.0f;            // Ground height (along up) under the object's front when probed
    uint64_t probedKey = 0;               // Which object the profile belongs to (0 = none)
    uint32_t probedRevision = 0;          // ...and its member set at that time
    float probedLateral = 0.0f;           // ...and the lateral line that was probed

    // Last two fixed-step states per body, for interpolated output (ADR-0006).
    std::vector<JPH::RVec3> prevPosition, currPosition;
    std::vector<JPH::Quat> prevRotation, currRotation;
};

namespace dragdoll
{
// Builds the Jolt ragdoll settings for `rig` placed with `worldFromRig` (positions and directions in rig space are
// mapped to world space). Throws std::invalid_argument on degenerate rigs.
JPH::Ref<JPH::RagdollSettings> buildRagdollSettings(const Rig& rig, JPH::Mat44Arg worldFromRig, float massScale,
                                                    uint32_t characterId, float& outTotalMass);

// Muscle drive of a tension profile. Throws std::invalid_argument for an unknown profile.
MuscleParams tensionProfile(DdTension tension);

// Starts blending the muscles towards `tension` over `blendSeconds` (0 = at once): the whole body, or one region.
void setTension(DdCharacter& character, DdTension tension, float blendSeconds);
void setRegionTension(DdCharacter& character, DdRegion region, DdTension tension, float blendSeconds);
// Fills character.region from the body names (see DdRegion).
void assignRegions(DdCharacter& character);
// Points a limb (arm or leg region) at a world position: sets the targets of its two main joints so that its end
// (hand / foot centre) lies towards `target`, `extension` (0..1) of the limb's length away at most. False if the
// region is not a two-joint limb.
bool aimLimb(DdCharacter& character, DdRegion region, JPH::RVec3Arg target, float extension);

// Advances the blend by one fixed step and pushes the muscle state into the joint motors when it changed.
void updateMuscles(DdCharacter& character, float dt);

// Host contract (pose.cpp). Throws std::invalid_argument on bad input.
void setTargetPose(DdCharacter& character, const DdQuat* jointRotations, uint32_t count);
// Joint rotations (offsets from rest, rig axes) from body world rotations.
void jointRotations(const DdCharacter& character, const std::vector<JPH::Quat>& world, DdQuat* out);
// Standing balance (balance.cpp): updates balanceOffset from the last fixed step; call before updateMuscles.
void updateBalance(DdCharacter& character, JPH::Vec3Arg up, float gravity, float dt);

// After each fixed step: stillness timer and state.
void updateState(DdCharacter& character, float dt);
// World transforms of every body for a pose (root = the root body's world transform; joints may be null = rest).
void bodiesFromPose(const DdCharacter& character, const DdTransform& root, const DdQuat* joints,
                    std::vector<JPH::RVec3>& positions, std::vector<JPH::Quat>& rotations);
} // namespace dragdoll
