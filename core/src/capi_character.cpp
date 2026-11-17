// SPDX-License-Identifier: MIT
// C ABI: characters (dragdoll.h).

#include "capi_guard.hpp"
#include "capi_world_handle.hpp"
#include "character.hpp"
#include "world.hpp"

namespace
{
dragdoll::rec::Recorder* recorderOf(const DdCharacter* character)
{
    auto* handle = static_cast<DdWorld*>(character->world->owner());
    return handle != nullptr ? handle->recorder.get() : nullptr;
}
} // namespace

#include <cmath>
#include <cstring>
#include <vector>
#include <stdexcept>

using namespace dragdoll::detail;

extern "C"
{

DD_API uint32_t DD_CALL dragdoll_world_get_character_count(const DdWorld* world)
{
    return world != nullptr ? world->world.characterCount() : 0;
}

DD_API DdCharacter* DD_CALL dragdoll_world_get_character(DdWorld* world, uint32_t index)
{
    return world != nullptr ? world->world.characterAt(index) : nullptr;
}

DD_API const char* DD_CALL dragdoll_get_builtin_rig(const char* name)
{
    return name != nullptr ? dragdoll::builtinRig(name) : nullptr;
}

DD_API void DD_CALL dragdoll_character_desc_default(DdCharacterDesc* desc)
{
    if (desc == nullptr)
    {
        return;
    }
    *desc = DdCharacterDesc{};
    desc->struct_size = sizeof(DdCharacterDesc);
    desc->transform.rotation = DdQuat{0.0f, 0.0f, 0.0f, 1.0f};
    desc->mass_scale = 1.0f;
    desc->size_scale = 1.0f;
}

DD_API DdResult DD_CALL dragdoll_character_create(DdWorld* world, const DdCharacterDesc* desc, DdCharacter** out_character)
{
    DD_GUARD_BEGIN
    clearLastError();
    if (out_character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_create: out_character is required");
    }
    *out_character = nullptr;
    if (world == nullptr || desc == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_create: world and desc are required");
    }
    try
    {
        *out_character = world->world.createCharacter(*desc);
        if (world->recorder)
        {
            world->recorder->characterCreate((*out_character)->id, *desc);
        }
        if (world->tap)
        {
            world->tap->characterCreated((*out_character)->id, *desc);
        }
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API void DD_CALL dragdoll_character_destroy(DdCharacter* character)
{
    try
    {
        if (character != nullptr)
        {
            if (dragdoll::rec::Recorder* recorder = recorderOf(character))
            {
                recorder->characterDestroy(character->id);
            }
            if (auto* handle = static_cast<DdWorld*>(character->world->owner()); handle != nullptr && handle->tap)
            {
                handle->tap->characterDestroyed(character->id);
            }
            character->world->destroyCharacter(character);
        }
    }
    catch (...)
    {
        setLastError("dragdoll_character_destroy: unexpected internal error");
    }
}

DD_API uint32_t DD_CALL dragdoll_character_get_body_count(const DdCharacter* character)
{
    return character != nullptr ? static_cast<uint32_t>(character->names.size()) : 0;
}

DD_API const char* DD_CALL dragdoll_character_get_body_name(const DdCharacter* character, uint32_t body)
{
    if (character == nullptr || body >= character->names.size())
    {
        return nullptr;
    }
    return character->names[body].c_str();
}

DD_API int32_t DD_CALL dragdoll_character_find_body(const DdCharacter* character, const char* name)
{
    if (character == nullptr || name == nullptr)
    {
        return -1;
    }
    for (size_t i = 0; i < character->names.size(); ++i)
    {
        if (std::strcmp(character->names[i].c_str(), name) == 0)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

DD_API float DD_CALL dragdoll_character_get_mass(const DdCharacter* character)
{
    return character != nullptr ? character->totalMass : 0.0f;
}

DD_API DdResult DD_CALL dragdoll_character_get_body_transforms(const DdCharacter* character, int interpolated,
                                                               DdTransform* out, uint32_t capacity)
{
    DD_GUARD_BEGIN
    if (character == nullptr || out == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_body_transforms: character and out are required");
    }
    if (!character->world->characterTransforms(*character, interpolated != 0, out, capacity))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_body_transforms: capacity < body count");
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_apply_impulse(DdCharacter* character, uint32_t body, DdVec3 impulse,
                                                         const DdVec3* world_point)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_apply_impulse: character is required");
    }
    if (character->state == DD_STATE_FAILED)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_apply_impulse: the character has FAILED (destroy it and use the host animation)");
    }
    try
    {
        character->world->applyImpulse(*character, body, impulse, world_point);
        if (dragdoll::rec::Recorder* recorder = recorderOf(character))
        {
            recorder->impulse(character->id, body, impulse, world_point);
        }
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_set_name(DdCharacter* character, const char* name)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_name: character is required");
    }
    character->name = name != nullptr ? name : "";
    if (auto* handle = static_cast<DdWorld*>(character->world->owner()); handle != nullptr && handle->tap)
    {
        handle->tap->characterRenamed(character->id, character->name);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API const char* DD_CALL dragdoll_character_get_name(const DdCharacter* character)
{
    return character != nullptr ? character->name.c_str() : "";
}

DD_API DdResult DD_CALL dragdoll_character_set_debug_draw(DdCharacter* character, int visible)
{
    if (character == nullptr)
    {
        return DD_ERROR_INVALID_ARGUMENT;
    }
    character->debugVisible = visible != 0;
    return DD_OK;
}

DD_API int DD_CALL dragdoll_character_get_debug_draw(const DdCharacter* character)
{
    return character != nullptr && character->debugVisible ? 1 : 0;
}

DD_API float DD_CALL dragdoll_character_get_body_mass(const DdCharacter* character, uint32_t body)
{
    return character != nullptr && body < character->bodyMass.size() ? character->bodyMass[body] : 0.0f;
}

DD_API int32_t DD_CALL dragdoll_character_get_body_parent(const DdCharacter* character, uint32_t body)
{
    if (character == nullptr || body >= character->parent.size())
    {
        return -1;
    }
    return character->parent[body];
}

DD_API DdResult DD_CALL dragdoll_character_set_tension(DdCharacter* character, DdTension tension, float blend_seconds)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_tension: character is required");
    }
    if (character->state == DD_STATE_FAILED)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_tension: the character has FAILED (destroy it and use the host animation)");
    }
    if (static_cast<int>(tension) < 0 || tension >= DD_TENSION_COUNT)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_tension: unknown tension profile");
    }
    if (!std::isfinite(blend_seconds) || blend_seconds < 0.0f)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_tension: blend_seconds must be finite and >= 0");
    }
    dragdoll::setTension(*character, tension, blend_seconds);
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->tension(character->id, static_cast<uint32_t>(tension), blend_seconds);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_set_region_tension(DdCharacter* character, DdRegion region, DdTension tension,
                                                              float blend_seconds)
{
    DD_GUARD_BEGIN
    if (character == nullptr || character->state == DD_STATE_FAILED)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_region_tension: a character that has not FAILED is required");
    }
    if (static_cast<int>(region) < 0 || region >= DD_REGION_COUNT || static_cast<int>(tension) < 0 || tension >= DD_TENSION_COUNT)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_region_tension: unknown region or tension profile");
    }
    if (!std::isfinite(blend_seconds) || blend_seconds < 0.0f)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_region_tension: blend_seconds must be finite and >= 0");
    }
    dragdoll::setRegionTension(*character, region, tension, blend_seconds);
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->regionTension(character->id, static_cast<uint32_t>(region), static_cast<uint32_t>(tension), blend_seconds);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdTension DD_CALL dragdoll_character_get_region_tension(const DdCharacter* character, DdRegion region)
{
    if (character == nullptr || static_cast<int>(region) < 0 || region >= DD_REGION_COUNT)
    {
        return DD_TENSION_LIMP;
    }
    return character->regionMuscles[static_cast<size_t>(region)].tension;
}

DD_API DdRegion DD_CALL dragdoll_character_get_body_region(const DdCharacter* character, uint32_t body)
{
    if (character == nullptr || body >= character->region.size())
    {
        return DD_REGION_COUNT;
    }
    return static_cast<DdRegion>(character->region[body]);
}

DD_API DdResult DD_CALL dragdoll_character_set_behaviour(DdCharacter* character, const char* name)
{
    DD_GUARD_BEGIN
    if (character == nullptr || character->state == DD_STATE_FAILED)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_behaviour: a character that has not FAILED is required");
    }
    const std::string wanted = name != nullptr ? name : "";
    if (!wanted.empty() && !dragdoll::behaviourExists(wanted))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_behaviour: unknown behaviour name");
    }
    dragdoll::attachBehaviour(*character, wanted);
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->behaviour(character->id, wanted.c_str());
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API const char* DD_CALL dragdoll_character_get_behaviour(const DdCharacter* character)
{
    return character != nullptr ? character->behaviourName.c_str() : "";
}

DD_API const char* DD_CALL dragdoll_character_get_behaviour_status(const DdCharacter* character)
{
    return character != nullptr ? character->behaviourStatus.c_str() : "";
}

DD_API DdResult DD_CALL dragdoll_character_set_awareness(DdCharacter* character, float awareness)
{
    DD_GUARD_BEGIN
    if (character == nullptr || !std::isfinite(awareness) || awareness > 1.0f)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_awareness: character and a value <= 1 (negative = automatic) are required");
    }
    character->awarenessOverride = awareness < 0.0f ? -1.0f : awareness;
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->awareness(character->id, character->awarenessOverride);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API float DD_CALL dragdoll_character_get_awareness(const DdCharacter* character)
{
    return character != nullptr ? character->awarenessOverride : -1.0f;
}

DD_API DdResult DD_CALL dragdoll_character_set_vitality(DdCharacter* character, float vitality)
{
    DD_GUARD_BEGIN
    if (character == nullptr || !std::isfinite(vitality) || vitality < 0.0f || vitality > 1.0f)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_vitality: character and a value in [0, 1] are required");
    }
    character->vitality = vitality;
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->vitality(character->id, vitality);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API float DD_CALL dragdoll_character_get_vitality(const DdCharacter* character)
{
    return character != nullptr ? character->vitality : 0.0f;
}

DD_API DdResult DD_CALL dragdoll_character_aim(DdCharacter* character, DdRegion region, DdVec3 target, float extension)
{
    DD_GUARD_BEGIN
    if (character == nullptr || character->state == DD_STATE_FAILED)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_aim: a character that has not FAILED is required");
    }
    if (static_cast<int>(region) < 0 || region >= DD_REGION_COUNT || !std::isfinite(target.x) || !std::isfinite(target.y) ||
        !std::isfinite(target.z) || !std::isfinite(extension))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_aim: a region and finite target / extension are required");
    }
    if (!dragdoll::aimLimb(*character, region, JPH::RVec3(target.x, target.y, target.z), extension))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_aim: the region is not a two-joint limb, or the target is at its first joint");
    }
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->aim(character->id, static_cast<uint32_t>(region), target, extension);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_apply_pose(DdCharacter* character, const char* behaviour, const char* pose, uint32_t region_mask)
{
    DD_GUARD_BEGIN
    if (character == nullptr || character->state == DD_STATE_FAILED || behaviour == nullptr || pose == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_apply_pose: character (not FAILED), behaviour and pose names are required");
    }
    try
    {
        dragdoll::applyNamedPose(*character, character->world->behaviourParams().of(behaviour), pose, region_mask);
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->namedPose(character->id, behaviour, pose, region_mask);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_set_balance(DdCharacter* character, int enabled)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_balance: character is required");
    }
    if (character->state == DD_STATE_FAILED)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_balance: the character has FAILED (destroy it and use the host animation)");
    }
    character->balance = enabled != 0;
    character->ragdoll->Activate();
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->balance(character->id, enabled != 0);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API int DD_CALL dragdoll_character_get_balance(const DdCharacter* character)
{
    return character != nullptr && character->balance ? 1 : 0;
}

DD_API DdTension DD_CALL dragdoll_character_get_tension(const DdCharacter* character)
{
    return character != nullptr ? character->tension : DD_TENSION_LIMP;
}

DD_API DdResult DD_CALL dragdoll_character_set_target_pose(DdCharacter* character, const DdQuat* joint_rotations,
                                                           uint32_t count)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_target_pose: character is required");
    }
    if (character->state == DD_STATE_FAILED)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_target_pose: the character has FAILED (destroy it and use the host animation)");
    }
    try
    {
        dragdoll::setTargetPose(*character, joint_rotations, count);
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->targetPose(character->id, joint_rotations, joint_rotations != nullptr ? count : 0);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_get_pose(const DdCharacter* character, int interpolated, DdQuat* joint_rotations,
                                                    uint32_t capacity, DdTransform* root)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_pose: character is required");
    }
    const size_t count = character->currRotation.size();
    if (joint_rotations != nullptr && capacity < count)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_pose: capacity < body count");
    }
    std::vector<DdTransform> bodies(count);
    character->world->characterTransforms(*character, interpolated != 0, bodies.data(), static_cast<uint32_t>(count));
    if (joint_rotations != nullptr)
    {
        std::vector<JPH::Quat> world(count);
        for (size_t i = 0; i < count; ++i)
        {
            const DdQuat& q = bodies[i].rotation;
            world[i] = JPH::Quat(q.x, q.y, q.z, q.w);
        }
        dragdoll::jointRotations(*character, world, joint_rotations);
    }
    if (root != nullptr)
    {
        *root = bodies[0];
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_set_kinematic(DdCharacter* character, int kinematic)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_kinematic: character is required");
    }
    if (character->state == DD_STATE_FAILED)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_kinematic: the character has FAILED (destroy it and use the host animation)");
    }
    character->world->setCharacterKinematic(*character, kinematic != 0);
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->kinematic(character->id, kinematic != 0);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_set_auto_handover(DdCharacter* character, float lead_seconds)
{
    DD_GUARD_BEGIN
    if (character == nullptr || !std::isfinite(lead_seconds) || lead_seconds < 0.0f)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_auto_handover: character and lead_seconds >= 0 are required");
    }
    character->autoHandover = lead_seconds;
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->autoHandover(character->id, lead_seconds);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API float DD_CALL dragdoll_character_get_auto_handover(const DdCharacter* character)
{
    return character != nullptr ? character->autoHandover : 0.0f;
}

DD_API int DD_CALL dragdoll_character_get_kinematic(const DdCharacter* character)
{
    return character != nullptr && character->kinematic ? 1 : 0;
}

DD_API DdResult DD_CALL dragdoll_character_activate(DdCharacter* character, const DdTransform* previous_root,
                                                    const DdQuat* previous_joints, const DdTransform* current_root,
                                                    const DdQuat* current_joints, uint32_t count, float dt)
{
    DD_GUARD_BEGIN
    if (character == nullptr || current_root == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_activate: character and current_root are required");
    }
    if (character->state == DD_STATE_FAILED)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_activate: the character has FAILED (destroy it and use the host animation)");
    }
    if ((current_joints != nullptr || previous_joints != nullptr) && count != character->parent.size())
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_activate: count must equal the body count");
    }
    try
    {
        character->world->activateCharacter(*character, previous_root, previous_joints, *current_root, current_joints, dt);
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
    if (dragdoll::rec::Recorder* recorder = recorderOf(character))
    {
        recorder->activate(character->id, previous_root, previous_joints, *current_root, current_joints,
                           static_cast<uint32_t>(character->parent.size()), dt);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_get_info(const DdCharacter* character, DdCharacterInfo* out)
{
    DD_GUARD_BEGIN
    if (character == nullptr || out == nullptr || out->struct_size != sizeof(DdCharacterInfo))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_info: character and out (struct_size set) are required");
    }
    const JPH::Vec3 up = character->world->up();
    const JPH::Quat pelvis = character->currRotation[0];
    // Facing: the pelvis' forward (rig +Z) against world up.
    const JPH::Vec3 forward = (pelvis * character->restRig[0].Conjugated()) * JPH::Vec3::sAxisZ();
    const float forwardUp = forward.Dot(up);
    // Head direction: pelvis -> head (or the last body of the spine chain), flattened onto the ground plane.
    size_t head = 0;
    for (size_t i = 0; i < character->names.size(); ++i)
    {
        if (character->names[i] == "head") head = i;
    }
    const JPH::Vec3 along = JPH::Vec3(character->currPosition[head] - character->currPosition[0]);
    const JPH::Vec3 flat = along - up * along.Dot(up);
    const bool lying = along.Length() > 1e-4f && flat.Length() > 0.7f * along.Length();
    const JPH::Vec3 direction = lying ? flat.Normalized() : JPH::Vec3::sZero();
    out->state = character->state;
    out->still_seconds = character->stillSeconds;
    out->facing = !lying ? 0 : forwardUp > 0.5f ? 1 : forwardUp < -0.5f ? -1 : 0;
    out->head_direction = DdVec3{direction.GetX(), direction.GetY(), direction.GetZ()};
    out->failure = character->failure;
    out->balance_error = character->balanceError;
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_get_threat(const DdCharacter* character, DdThreat* out)
{
    DD_GUARD_BEGIN
    if (character == nullptr || out == nullptr || out->struct_size != sizeof(DdThreat))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_threat: character and out (with struct_size set) are required");
    }
    *out = character->threat;
    out->struct_size = sizeof(DdThreat);
    if (out->collider == DD_INVALID_COLLIDER)
    {
        const DdFrontProfile none{};
        *out = DdThreat{};
        out->struct_size = sizeof(DdThreat);
        out->profile = none;
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_set_label(DdCharacter* character, const char* label)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_label: character is required");
    }
    std::string text = label != nullptr ? label : "";
    if (text.size() > 63) text.resize(63);
    if (text != character->label)
    {
        character->label = text;
        if (dragdoll::rec::Recorder* recorder = recorderOf(character))
        {
            recorder->label(character->id, text.c_str());
        }
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API const char* DD_CALL dragdoll_character_get_label(const DdCharacter* character)
{
    return character != nullptr ? character->label.c_str() : "";
}

DD_API DdResult DD_CALL dragdoll_character_get_senses(const DdCharacter* character, DdSenses* out)
{
    DD_GUARD_BEGIN
    if (character == nullptr || out == nullptr || out->struct_size != sizeof(DdSenses))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_senses: character and out (with struct_size set) are required");
    }
    *out = character->senses;
    out->struct_size = sizeof(DdSenses);
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API uint32_t DD_CALL dragdoll_character_get_contacts(const DdCharacter* character, DdBodyContact* out, uint32_t capacity)
{
    if (character == nullptr)
    {
        return 0;
    }
    const uint32_t count = static_cast<uint32_t>(character->contacts.size());
    for (uint32_t i = 0; i < count && i < capacity && out != nullptr; ++i)
    {
        out[i] = character->contacts[i];
    }
    return count;
}

DD_API DdResult DD_CALL dragdoll_character_get_impact_report(const DdCharacter* character, DdImpactReport* out)
{
    DD_GUARD_BEGIN
    if (character == nullptr || out == nullptr || out->struct_size != sizeof(DdImpactReport))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_impact_report: character and out (with struct_size set) are required");
    }
    *out = character->impact;
    out->struct_size = sizeof(DdImpactReport);
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_set_riding(DdCharacter* character, uint32_t group)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_set_riding: character is required");
    }
    if (character->ridingGroup != group)
    {
        character->ridingGroup = group;
        if (dragdoll::rec::Recorder* recorder = recorderOf(character))
        {
            recorder->riding(character->id, group);
        }
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API uint32_t DD_CALL dragdoll_character_get_riding(const DdCharacter* character)
{
    return character != nullptr ? character->ridingGroup : 0;
}

DD_API uint32_t DD_CALL dragdoll_character_get_threat_silhouette(const DdCharacter* character, DdVec3* out_points, uint32_t capacity)
{
    if (character == nullptr || character->world == nullptr || character->threat.collider == DD_INVALID_COLLIDER)
    {
        return 0;
    }
    return character->world->threatSilhouette(*character, out_points, capacity);
}

DD_API DdResult DD_CALL dragdoll_debug_corrupt_character(DdCharacter* character)
{
    DD_GUARD_BEGIN
    if (character == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_debug_corrupt_character: character is required");
    }
#ifdef JPH_DEBUG_RENDERER // Dev builds only (follows DRAGDOLL_DEV_TOOLS, ADR-0012)
    character->debugCorrupt = true;
    return DD_OK;
#else
    return fail(DD_ERROR_UNSUPPORTED, "dragdoll_debug_corrupt_character: test hook not available in this build");
#endif
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_character_get_blend_weights(const DdCharacter* character, float* weights, uint32_t capacity)
{
    DD_GUARD_BEGIN
    if (character == nullptr || weights == nullptr || capacity < character->currRotation.size())
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_character_get_blend_weights: character, weights and capacity >= body count are required");
    }
    for (size_t i = 0; i < character->currRotation.size(); ++i)
    {
        weights[i] = 1.0f; // Full physics until behaviours set weights
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

} // extern "C\"
