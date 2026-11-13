// SPDX-License-Identifier: MIT
// Skeleton maps (Phase 3, task 3.4): translate a host skeleton's pose to/from DragDoll joint rotations. Defined at
// global scope because it is the opaque DdSkeletonMap of the C ABI.
//
// Notation (all rotations as quaternions): A = host model axes -> rig axes; B_k / H_k = host bone k's bind / current
// rotation in model space; G_k = A H_k B_k^-1 A^-1 = bone k's rotation since bind, in rig axes; D_i = bind correction of
// body i (rig rest direction -> host bind direction); P_i = body i's rest rotation in rig space.
// Body i in rig space: M_i = G_k D_i P_i, so the joint rotation d_i = (G_kp D_p)^-1 (G_k D_i).
#pragma once

#include <dragdoll/dragdoll.h>

#include <Jolt/Jolt.h>

#include <string_view>
#include <vector>

struct DdCharacter;

struct DdSkeletonMap
{
    JPH::Quat modelToRig = JPH::Quat::sIdentity(); // A
    // Per rig body
    std::vector<int> bodyParent;
    std::vector<int> bodyBone;               // Index into the host bones
    std::vector<JPH::Quat> correction;       // D_i
    std::vector<JPH::Quat> restRig;          // P_i
    // Per host bone
    std::vector<int> boneParent;
    std::vector<int> boneBody;               // Mapped body or -1
    std::vector<JPH::Quat> bindRotation;     // B_k
    std::vector<JPH::Vec3> bindPosition;
};

namespace dragdoll
{
// Throws std::invalid_argument with a precise message.
DdSkeletonMap buildSkeletonMap(std::string_view json, const DdCharacter& character, const DdHostBone* bones, uint32_t count);
void mapToPose(const DdSkeletonMap& map, const DdQuat* boneRotations, const DdVec3* rootPosition, const DdTransform& modelToWorld,
               DdTransform& outRoot, DdQuat* outJoints);
void mapFromPose(const DdSkeletonMap& map, const DdTransform& root, const DdQuat* joints, const DdTransform& modelToWorld,
                 DdQuat* outBoneRotations);
} // namespace dragdoll
