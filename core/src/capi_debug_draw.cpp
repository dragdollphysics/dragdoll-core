// SPDX-License-Identifier: MIT
// C ABI: debug drawing (dev builds only, ADR-0012: JPH_DEBUG_RENDERER follows DRAGDOLL_DEV_TOOLS).

#include "capi_guard.hpp"
#include "capi_world_handle.hpp"

#ifdef JPH_DEBUG_RENDERER
    #include <Jolt/Physics/Body/BodyManager.h>
    #include <Jolt/Renderer/DebugRendererSimple.h>
#endif

using namespace dragdoll::detail;

#ifdef JPH_DEBUG_RENDERER
namespace
{
DdVec3 toDd(JPH::RVec3Arg v)
{
    return DdVec3{static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())};
}

class CallbackRenderer final : public JPH::DebugRendererSimple
{
public:
    explicit CallbackRenderer(const DdDebugDrawCallbacks& callbacks) : mCallbacks(callbacks) {}

    void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override
    {
        if (mCallbacks.line != nullptr)
        {
            mCallbacks.line(mCallbacks.user, toDd(from), toDd(to), color.GetUInt32());
        }
    }

    void DrawTriangle(JPH::RVec3Arg a, JPH::RVec3Arg b, JPH::RVec3Arg c, JPH::ColorArg color, ECastShadow) override
    {
        if (mCallbacks.triangle != nullptr)
        {
            mCallbacks.triangle(mCallbacks.user, toDd(a), toDd(b), toDd(c), color.GetUInt32());
        }
    }

    void DrawText3D(JPH::RVec3Arg, const JPH::string_view&, JPH::ColorArg, float) override {}

private:
    const DdDebugDrawCallbacks& mCallbacks;
};
} // namespace
#endif

extern "C"
{

DD_API DdResult DD_CALL dragdoll_world_debug_draw(DdWorld* world, const DdDebugDrawCallbacks* callbacks)
{
    DD_GUARD_BEGIN
    if (world == nullptr || callbacks == nullptr || callbacks->struct_size != sizeof(DdDebugDrawCallbacks))
    {
        return fail(DD_ERROR_INVALID_ARGUMENT, "dragdoll_world_debug_draw: world and valid callbacks are required");
    }
#ifdef JPH_DEBUG_RENDERER
    CallbackRenderer renderer(*callbacks);
    const uint32_t flags = callbacks->flags == 0 ? DD_DRAW_SHAPES : callbacks->flags;
    if ((flags & DD_DRAW_SHAPES) != 0)
    {
        JPH::BodyManager::DrawSettings settings;
        settings.mDrawShape = true;
        settings.mDrawShapeWireframe = false;
        world->world.physics().DrawBodies(settings, &renderer);
    }
    if ((flags & DD_DRAW_CONSTRAINTS) != 0)
    {
        world->world.physics().DrawConstraints(&renderer);
    }
    if ((flags & DD_DRAW_CONSTRAINT_LIMITS) != 0)
    {
        world->world.physics().DrawConstraintLimits(&renderer);
    }
    return DD_OK;
#else
    return fail(DD_ERROR_UNSUPPORTED, "dragdoll_world_debug_draw: this build has no debug renderer (DRAGDOLL_DEV_TOOLS=OFF)");
#endif
    DD_GUARD_END(DD_ERROR_INTERNAL)
}

} // extern "C"
