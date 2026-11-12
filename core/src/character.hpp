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
}

struct DdCharacter
{
    dragdoll::World* world = nullptr;
    uint32_t id = 0;
    uint64_t userData = 0;
    float totalMass = 0.0f;

    JPH::Ref<JPH::RagdollSettings> settings;
    JPH::Ref<JPH::Ragdoll> ragdoll;
    std::vector<std::string> names;

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
} // namespace dragdoll
