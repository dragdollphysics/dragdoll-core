// SPDX-License-Identifier: MIT
// C ABI: skeleton maps (dragdoll.h).
#include "capi_guard.hpp"
#include "character.hpp"
#include "rig.hpp"
#include "skeleton_map.hpp"

#include <memory>
#include <stdexcept>

using namespace dragdoll::detail;

extern "C"
{

DD_API const char* DD_CALL dragdoll_get_builtin_skeleton_map(const char* name)
{
    return name != nullptr ? dragdoll::builtinSkeletonMap(name) : nullptr;
}

DD_API DdResult DD_CALL dragdoll_skeleton_map_create(const char* map_json, const DdCharacter* character, const DdHostBone* bones,
                                                     uint32_t bone_count, DdSkeletonMap** out_map)
{
    DD_GUARD_BEGIN
    if (out_map == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_skeleton_map_create: out_map is required");
    }
    *out_map = nullptr;
    if (map_json == nullptr || character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_skeleton_map_create: map_json and character are required");
    }
    try
    {
        *out_map = new DdSkeletonMap(dragdoll::buildSkeletonMap(map_json, *character, bones, bone_count));
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API void DD_CALL dragdoll_skeleton_map_destroy(DdSkeletonMap* map)
{
    delete map;
}

DD_API DdResult DD_CALL dragdoll_skeleton_map_to_pose(const DdSkeletonMap* map, const DdQuat* bone_rotations, uint32_t bone_count,
                                                      const DdVec3* root_position, const DdTransform* model_to_world,
                                                      DdTransform* out_root, DdQuat* out_joints, uint32_t joint_capacity)
{
    DD_GUARD_BEGIN
    if (map == nullptr || bone_rotations == nullptr || model_to_world == nullptr || out_root == nullptr || out_joints == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_skeleton_map_to_pose: map, bone_rotations, model_to_world and outputs are required");
    }
    if (bone_count != map->boneParent.size() || joint_capacity < map->bodyBone.size())
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_skeleton_map_to_pose: bone_count must match the map, joint_capacity >= body count");
    }
    try
    {
        dragdoll::mapToPose(*map, bone_rotations, root_position, *model_to_world, *out_root, out_joints);
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_skeleton_map_from_pose(const DdSkeletonMap* map, const DdTransform* root, const DdQuat* joints,
                                                        uint32_t joint_count, const DdTransform* model_to_world,
                                                        DdQuat* out_bone_rotations, uint32_t bone_capacity)
{
    DD_GUARD_BEGIN
    if (map == nullptr || root == nullptr || joints == nullptr || model_to_world == nullptr || out_bone_rotations == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_skeleton_map_from_pose: map, root, joints, model_to_world and output are required");
    }
    if (joint_count != map->bodyBone.size() || bone_capacity < map->boneParent.size())
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_skeleton_map_from_pose: joint_count must equal the body count, bone_capacity >= bone count");
    }
    try
    {
        dragdoll::mapFromPose(*map, *root, joints, *model_to_world, out_bone_rotations);
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

} // extern "C"
