// SPDX-License-Identifier: MIT
// A character: one rig instantiated as a Jolt ragdoll inside a World. Defined at global scope because it is the
// opaque DdCharacter of the C ABI.
#pragma once

#include "rig.hpp"

#include <dragdoll/dragdoll.h>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Ragdoll/Ragdoll.h>

#include <cstdint>
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
    std::vector<JPH::Quat> targetPose; // Joint rotations the muscles pull towards (offsets from rest, rig axes)
    // Effective inertia of each joint (kg*m^2): everything on the child side against everything on the parent side,
    // about the joint, at rest. Turns frequency + damping ratio into real stiffness, so the chain beyond a joint (a
    // whole arm at the shoulder) is accounted for.
    std::vector<float> jointInertia;
    std::vector<float> bodyMass; // kg, as simulated
    // Rest geometry for placing the body from a pose (forward kinematics): the joint pivot in the parent's body frame,
    // and this body's position relative to its joint pivot in its own body frame.
    std::vector<JPH::Vec3> pivotInParent, bodyFromPivot;
    std::vector<JPH::Vec3> pivotRig, centreRig; // Joint pivot / shape centre at rest, rig space (skeleton maps)
    DdTension tension = DD_TENSION_LIMP;
    dragdoll::MuscleParams blendFrom, muscles, blendTo; // `muscles` = what the motors use now
    float blendTime = 0.0f, blendElapsed = 0.0f;
    bool musclesDirty = false;

    // Host contract (D17)
    float stillSeconds = 0.0f;
    DdCharacterState state = DD_STATE_ACTIVE;

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

// Starts blending the muscles towards `tension` over `blendSeconds` (0 = at once).
void setTension(DdCharacter& character, DdTension tension, float blendSeconds);

// Advances the blend by one fixed step and pushes the muscle state into the joint motors when it changed.
void updateMuscles(DdCharacter& character, float dt);

// Host contract (pose.cpp). Throws std::invalid_argument on bad input.
void setTargetPose(DdCharacter& character, const DdQuat* jointRotations, uint32_t count);
// Joint rotations (offsets from rest, rig axes) from body world rotations.
void jointRotations(const DdCharacter& character, const std::vector<JPH::Quat>& world, DdQuat* out);
// After each fixed step: stillness timer and state.
void updateState(DdCharacter& character, float dt);
// World transforms of every body for a pose (root = the root body's world transform; joints may be null = rest).
void bodiesFromPose(const DdCharacter& character, const DdTransform& root, const DdQuat* joints,
                    std::vector<JPH::RVec3>& positions, std::vector<JPH::Quat>& rotations);
} // namespace dragdoll
