// SPDX-License-Identifier: MIT
// C ABI: debug drawing (dev builds only, ADR-0012: JPH_DEBUG_RENDERER follows DRAGDOLL_DEV_TOOLS).

#include "capi_guard.hpp"
#include "character.hpp"
#include "capi_world_handle.hpp"

#ifdef JPH_DEBUG_RENDERER
#include <unordered_set>
    #include <Jolt/Physics/Body/Body.h>
    #include <Jolt/Physics/Body/BodyFilter.h>
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

// Skips static bodies (DD_DRAW_SKIP_STATIC), the bodies of characters hidden with set_debug_draw(0), and moving bodies
// that are no character's (DD_DRAW_SKIP_OBJECTS) or everything else (DD_DRAW_ONLY_OBJECTS).
class DrawFilter final : public JPH::BodyDrawFilter
{
public:
    bool skipStatic = false, skipObjects = false, onlyObjects = false;
    std::unordered_set<uint32_t> hidden;     // Body id values
    std::unordered_set<uint32_t> characters; // Every character's bodies (for the object flags)
    bool ShouldDraw(const JPH::Body& body) const override
    {
        const uint32_t id = body.GetID().GetIndexAndSequenceNumber();
        const bool object = !body.IsStatic() && characters.count(id) == 0;
        if ((skipObjects && object) || (onlyObjects && !object)) return false;
        return !(skipStatic && body.IsStatic()) && hidden.count(id) == 0;
    }
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
    if ((callbacks->flags & DD_DRAW_CAMERA_LOD) != 0)
    {
        // Without a camera position Jolt draws every shape at full detail (e.g. 25 collision spheres per SA car).
        const DdVec3& c = callbacks->camera_position;
        renderer.SetCameraPos(JPH::RVec3(c.x, c.y, c.z));
    }
    const uint32_t flags = callbacks->flags == 0 ? DD_DRAW_SHAPES : callbacks->flags;
    if ((flags & DD_DRAW_SHAPES) != 0)
    {
        JPH::BodyManager::DrawSettings settings;
        settings.mDrawShape = true;
        settings.mDrawShapeWireframe = false;
        DrawFilter filter;
        filter.skipStatic = (flags & DD_DRAW_SKIP_STATIC) != 0;
        filter.skipObjects = (flags & DD_DRAW_SKIP_OBJECTS) != 0;
        filter.onlyObjects = (flags & DD_DRAW_ONLY_OBJECTS) != 0;
        for (uint32_t i = 0; i < world->world.characterCount(); ++i)
        {
            const DdCharacter* c = world->world.characterAt(i);
            for (int b = 0; b < static_cast<int>(c->currPosition.size()); ++b)
            {
                const uint32_t id = c->ragdoll->GetBodyID(b).GetIndexAndSequenceNumber();
                filter.characters.insert(id);
                if (!c->debugVisible) filter.hidden.insert(id);
            }
        }
        const bool filtered = filter.skipStatic || filter.skipObjects || filter.onlyObjects || !filter.hidden.empty();
        world->world.physics().DrawBodies(settings, &renderer, filtered ? &filter : nullptr);
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
