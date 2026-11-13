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
    std::vector<JPH::Quat> restTarget; // Rotation relative to the parent at rest (body space): the target pose
    DdTension tension = DD_TENSION_LIMP;
    dragdoll::MuscleParams blendFrom, muscles, blendTo; // `muscles` = what the motors use now
    float blendTime = 0.0f, blendElapsed = 0.0f;
    bool musclesDirty = false;

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
} // namespace dragdoll
