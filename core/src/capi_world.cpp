// SPDX-License-Identifier: MIT
// C ABI: world and proxy colliders (dragdoll.h).

#include "capi_guard.hpp"
#include "capi_world_handle.hpp"

#include <stdexcept>

using namespace dragdoll::detail;

namespace
{
// Converts the exceptions World throws into result codes (inside DD_GUARD_*).
template <typename Fn>
DdResult call(Fn&& fn)
{
    try
    {
        fn();
        return DD_OK;
    }
    catch (const std::invalid_argument& e)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, e.what());
    }
}
} // namespace

extern "C"
{

DD_API void DD_CALL dragdoll_world_desc_default(DdWorldDesc* desc)
{
    if (desc == nullptr)
    {
        return;
    }
    *desc = DdWorldDesc{};
    desc->struct_size = sizeof(DdWorldDesc);
    desc->up = DdVec3{0.0f, 1.0f, 0.0f};
    desc->gravity = 9.81f;
    desc->step_hz = 120.0f;
    desc->max_steps_per_call = 8;
    desc->worker_threads = 0;
    desc->max_bodies = 16384;
    desc->record_path = nullptr;
}

DD_API DdResult DD_CALL dragdoll_world_create(const DdWorldDesc* desc, DdWorld** out_world)
{
    DD_GUARD_BEGIN
    clearLastError();
    if (out_world == nullptr || desc == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_world_create: desc and out_world are required");
    }
    *out_world = nullptr;
    if (!isLibraryInitialized())
    {
        return fail(DD_ERROR_NOT_INITIALIZED, "dragdoll_world_create: call dragdoll_init first");
    }
    return call([&] { *out_world = new DdWorld(*desc); });
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API void DD_CALL dragdoll_world_destroy(DdWorld* world)
{
    try
    {
        delete world;
    }
    catch (...)
    {
        setLastError("dragdoll_world_destroy: unexpected internal error");
    }
}

DD_API DdResult DD_CALL dragdoll_world_step(DdWorld* world, float dt, uint32_t* out_steps)
{
    DD_GUARD_BEGIN
    if (out_steps != nullptr)
    {
        *out_steps = 0;
    }
    if (world == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_world_step: world is NULL");
    }
    return call([&] {
        const uint32_t steps = world->world.step(static_cast<double>(dt));
        if (out_steps != nullptr)
        {
            *out_steps = steps;
        }
        if (world->recorder)
        {
            world->recorder->step(dt, steps, world->world.stateHash());
        }
    });
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_world_step_fixed(DdWorld* world, uint32_t steps)
{
    DD_GUARD_BEGIN
    if (world == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_world_step_fixed: world is NULL");
    }
    return call([&] {
        world->world.stepFixed(steps);
        if (world->recorder)
        {
            world->recorder->stepFixed(steps, world->world.stateHash());
        }
    });
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API float DD_CALL dragdoll_world_get_interpolation_alpha(const DdWorld* world)
{
    return world != nullptr ? world->world.interpolationAlpha() : 0.0f;
}

DD_API uint64_t DD_CALL dragdoll_world_get_step_count(const DdWorld* world)
{
    return world != nullptr ? world->world.stepCount() : 0;
}

DD_API DdResult DD_CALL dragdoll_world_compute_state_hash(const DdWorld* world, uint64_t* out_hash)
{
    DD_GUARD_BEGIN
    if (world == nullptr || out_hash == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_world_compute_state_hash: world and out_hash are required");
    }
    *out_hash = world->world.stateHash();
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API void DD_CALL dragdoll_collider_desc_default(DdColliderDesc* desc)
{
    if (desc == nullptr)
    {
        return;
    }
    *desc = DdColliderDesc{};
    desc->struct_size = sizeof(DdColliderDesc);
    desc->shape = DD_SHAPE_SPHERE;
    desc->motion = DD_MOTION_STATIC;
    desc->transform.rotation = DdQuat{0.0f, 0.0f, 0.0f, 1.0f};
    desc->radius = 0.5f;
    desc->half_height = 0.5f;
    desc->half_extents = DdVec3{0.5f, 0.5f, 0.5f};
    desc->friction = 0.6f;
    desc->restitution = 0.0f;
}

DD_API DdResult DD_CALL dragdoll_world_add_colliders(DdWorld* world, const DdColliderDesc* descs, uint32_t count,
                                                     DdColliderId* out_ids)
{
    DD_GUARD_BEGIN
    if (world == nullptr || (count > 0 && (descs == nullptr || out_ids == nullptr)))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_world_add_colliders: world, descs and out_ids are required");
    }
    return call([&] {
        world->world.addColliders(descs, count, out_ids);
        if (world->recorder)
        {
            world->recorder->addColliders(descs, count, out_ids);
        }
    });
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_world_remove_colliders(DdWorld* world, const DdColliderId* ids, uint32_t count)
{
    DD_GUARD_BEGIN
    if (world == nullptr || (count > 0 && ids == nullptr))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_world_remove_colliders: world and ids are required");
    }
    return call([&] {
        world->world.removeColliders(ids, count);
        if (world->recorder)
        {
            world->recorder->removeColliders(ids, count);
        }
    });
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_world_set_kinematic_targets(DdWorld* world, const DdColliderId* ids,
                                                             const DdTransform* targets, uint32_t count)
{
    DD_GUARD_BEGIN
    if (world == nullptr || (count > 0 && (ids == nullptr || targets == nullptr)))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT,
                    "dragdoll_world_set_kinematic_targets: world, ids and targets are required");
    }
    return call([&] {
        world->world.setKinematicTargets(ids, targets, count);
        if (world->recorder)
        {
            world->recorder->kinematicTargets(ids, targets, count);
        }
    });
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_world_get_collider_transform(const DdWorld* world, DdColliderId id,
                                                              int interpolated, DdTransform* out_transform)
{
    DD_GUARD_BEGIN
    if (world == nullptr || out_transform == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT,
                    "dragdoll_world_get_collider_transform: world and out_transform are required");
    }
    if (!world->world.colliderTransform(id, interpolated != 0, *out_transform))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_world_get_collider_transform: unknown collider id");
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API DdResult DD_CALL dragdoll_world_record_note(DdWorld* world, const char* text)
{
    DD_GUARD_BEGIN
    if (world == nullptr || text == nullptr)
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_world_record_note: world and text are required");
    }
    if (world->recorder)
    {
        world->recorder->note(text);
    }
    return DD_OK;
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

DD_API int DD_CALL dragdoll_world_is_recording(const DdWorld* world)
{
    return world != nullptr && world->recorder != nullptr ? 1 : 0;
}

} // extern "C"
