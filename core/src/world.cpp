// SPDX-License-Identifier: MIT
#include "world.hpp"

#include "character.hpp"

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace dragdoll
{
namespace
{
constexpr size_t kTempAllocatorBytes = 16u * 1024u * 1024u; // Counts toward the 64 MB guest budget (ADR-0015)
constexpr uint32_t kMaxWorkerThreads = 64;

JPH::Vec3 toJolt(const DdVec3& v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

JPH::Quat toJolt(const DdQuat& q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

DdVec3 toDd(JPH::RVec3Arg v)
{
    return DdVec3{static_cast<float>(v.GetX()), static_cast<float>(v.GetY()), static_cast<float>(v.GetZ())};
}

DdQuat toDd(JPH::QuatArg q)
{
    return DdQuat{q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

bool isFinite(const DdVec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

JPH::Quat normalizedRotation(const DdQuat& q)
{
    JPH::Quat r = toJolt(q);
    const float len = r.Length();
    if (!std::isfinite(len) || len < 1e-6f)
    {
        throw std::invalid_argument("rotation quaternion is zero or not finite");
    }
    return r / len;
}

// FNV-1a over raw bytes: stable, fast, good enough for state comparison.
struct Fnv1a
{
    uint64_t value = 1469598103934665603ull;
    void add(const void* data, size_t size)
    {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            value ^= bytes[i];
            value *= 1099511628211ull;
        }
    }
    void addFloat(float f) { add(&f, sizeof(f)); }
    void addVec3(JPH::Vec3Arg v)
    {
        addFloat(v.GetX());
        addFloat(v.GetY());
        addFloat(v.GetZ());
    }
};
} // namespace

World::World(const DdWorldDesc& desc)
{
    if (desc.struct_size != sizeof(DdWorldDesc))
    {
        throw std::invalid_argument("DdWorldDesc.struct_size mismatch (header/binary version skew?)");
    }
    if (!isFinite(desc.up))
    {
        throw std::invalid_argument("up axis is not finite");
    }
    JPH::Vec3 up = toJolt(desc.up);
    const float upLength = up.Length();
    if (upLength < 1e-6f)
    {
        throw std::invalid_argument("up axis is zero");
    }
    mUp = up / upLength;
    mYToUp = JPH::Quat::sFromTo(JPH::Vec3::sAxisY(), mUp);

    if (!(desc.step_hz >= 60.0f && desc.step_hz <= 240.0f))
    {
        throw std::invalid_argument("step_hz must be within 60..240");
    }
    if (!std::isfinite(desc.gravity) || desc.gravity < 0.0f)
    {
        throw std::invalid_argument("gravity must be finite and >= 0");
    }
    if (desc.max_steps_per_call == 0)
    {
        throw std::invalid_argument("max_steps_per_call must be >= 1");
    }
    if (desc.max_bodies == 0)
    {
        throw std::invalid_argument("max_bodies must be >= 1");
    }

    mFixedDt = 1.0 / static_cast<double>(desc.step_hz);
    mMaxStepsPerCall = desc.max_steps_per_call;

    mTempAllocator = std::make_unique<JPH::TempAllocatorImpl>(kTempAllocatorBytes);
    if (desc.worker_threads == 0)
    {
        mJobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
    }
    else
    {
        const int threads = static_cast<int>(std::min(desc.worker_threads, kMaxWorkerThreads));
        mJobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
    }

    mPhysics = std::make_unique<JPH::PhysicsSystem>();
    mPhysics->Init(desc.max_bodies, 0, desc.max_bodies, desc.max_bodies, mBroadPhaseLayers, mObjectVsBroadPhase,
                   mObjectPairs);
    mPhysics->SetGravity(-mUp * desc.gravity);

    // Jolt tolerates 2 cm of penetration by default; hands and feet are only 4-7 cm thick, so that much sinking into
    // the floor is visible. 5 mm keeps contacts stable (8 position iterations on ragdolls) and invisible.
    JPH::PhysicsSettings physicsSettings = mPhysics->GetPhysicsSettings();
    physicsSettings.mPenetrationSlop = 0.005f;
    mPhysics->SetPhysicsSettings(physicsSettings);
}

World::~World()
{
    if (!mPhysics)
    {
        return;
    }
    for (auto& [id, character] : mCharacters)
    {
        character->ragdoll->RemoveFromPhysicsSystem();
    }
    mCharacters.clear(); // Releases the ragdolls (and their bodies) while the physics system still exists.

    JPH::BodyInterface& bodies = mPhysics->GetBodyInterface();
    mScratchBodies.clear();
    for (const auto& [id, collider] : mColliders)
    {
        mScratchBodies.push_back(collider.body);
    }
    if (!mScratchBodies.empty())
    {
        bodies.RemoveBodies(mScratchBodies.data(), static_cast<int>(mScratchBodies.size()));
        bodies.DestroyBodies(mScratchBodies.data(), static_cast<int>(mScratchBodies.size()));
    }
}

// ---------------------------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------------------------

uint32_t World::step(double dt)
{
    if (!std::isfinite(dt) || dt < 0.0)
    {
        throw std::invalid_argument("dt must be finite and >= 0");
    }
    mAccumulator += dt;
    uint32_t steps = static_cast<uint32_t>(std::floor(mAccumulator / mFixedDt));
    if (steps > mMaxStepsPerCall)
    {
        steps = mMaxStepsPerCall; // Spiral-of-death guard: drop the excess time below.
    }
    mAccumulator -= static_cast<double>(steps) * mFixedDt;
    if (mAccumulator >= mFixedDt)
    {
        mAccumulator = std::fmod(mAccumulator, mFixedDt);
    }
    if (mAccumulator < 0.0)
    {
        mAccumulator = 0.0; // Guard against rounding just below zero.
    }
    // Host time is defined as simulated time + accumulated remainder, so dropped time (hitches)
    // vanishes from both clocks and the two can never drift apart.
    const double hostTimeAfter = static_cast<double>(mStepCount + steps) * mFixedDt + mAccumulator;
    stampPendingTargets(hostTimeAfter);
    simulate(steps);
    return steps;
}

void World::stepFixed(uint32_t steps)
{
    // No host clock: the host advances by exactly the simulated time.
    stampPendingTargets(static_cast<double>(mStepCount + steps) * mFixedDt + mAccumulator);
    simulate(steps);
}

float World::interpolationAlpha() const
{
    return static_cast<float>(std::clamp(mAccumulator / mFixedDt, 0.0, 0.999999));
}

void World::stampPendingTargets(double hostTime)
{
    constexpr size_t kMaxSamples = 16;
    for (auto& [id, collider] : mColliders)
    {
        if (!collider.hasPending)
        {
            continue;
        }
        collider.hasPending = false;
        KinematicSample sample = collider.pending;
        sample.time = hostTime;
        // A newer target for the same instant replaces the older one.
        while (!collider.samples.empty() && collider.samples.back().time >= hostTime)
        {
            collider.samples.pop_back();
        }
        collider.samples.push_back(sample);
        if (collider.samples.size() > kMaxSamples)
        {
            collider.samples.erase(collider.samples.begin());
        }
    }
}

void World::simulate(uint32_t steps)
{
    const float fixedDt = static_cast<float>(mFixedDt);
    for (uint32_t i = 0; i < steps; ++i)
    {
        driveKinematics(static_cast<double>(mStepCount + 1) * mFixedDt);
        for (auto& [characterId, character] : mCharacters) // Ordered by id: deterministic
        {
            updateMuscles(*character, fixedDt);
        }
        const JPH::EPhysicsUpdateError error = mPhysics->Update(fixedDt, 1, mTempAllocator.get(), mJobSystem.get());
        if (error != JPH::EPhysicsUpdateError::None)
        {
            throw std::runtime_error("physics update error (body pair / contact capacity exceeded)");
        }
        ++mStepCount;
        captureTransforms();
    }
}

void World::driveKinematics(double stepEndTime)
{
    JPH::BodyInterface& bodies = mPhysics->GetBodyInterfaceNoLock();
    const float fixedDt = static_cast<float>(mFixedDt);
    for (auto& [id, collider] : mColliders)
    {
        if (collider.motion != DD_MOTION_KINEMATIC)
        {
            continue;
        }
        std::vector<KinematicSample>& samples = collider.samples;
        // Drop samples that lie entirely in the past (keep one before stepEndTime as the bracket start).
        while (samples.size() >= 2 && samples[1].time <= stepEndTime)
        {
            samples.erase(samples.begin());
        }

        // Sample the host trajectory at exactly stepEndTime (clamped to the known range).
        JPH::RVec3 position = samples.front().position;
        JPH::Quat rotation = samples.front().rotation;
        if (samples.size() >= 2 && stepEndTime > samples[0].time)
        {
            const KinematicSample& a = samples[0];
            const KinematicSample& b = samples[1];
            const double span = b.time - a.time;
            const float t = span > 0.0 ? static_cast<float>(std::clamp((stepEndTime - a.time) / span, 0.0, 1.0)) : 1.0f;
            position = a.position + (b.position - a.position) * t;
            rotation = a.rotation.SLERP(b.rotation, t).Normalized();
        }
        // MoveKinematic derives the velocity from the displacement; no displacement = standing still.
        bodies.MoveKinematic(collider.body, position, rotation, fixedDt);
    }
}

void World::captureTransforms()
{
    const JPH::BodyInterface& bodies = mPhysics->GetBodyInterfaceNoLock();
    for (auto& [id, collider] : mColliders)
    {
        if (collider.motion == DD_MOTION_STATIC)
        {
            continue;
        }
        collider.prevPosition = collider.currPosition;
        collider.prevRotation = collider.currRotation;
        bodies.GetPositionAndRotation(collider.body, collider.currPosition, collider.currRotation);
    }
    for (auto& [id, character] : mCharacters)
    {
        DdCharacter& c = *character;
        c.prevPosition = c.currPosition;
        c.prevRotation = c.currRotation;
        for (size_t i = 0; i < c.currPosition.size(); ++i)
        {
            bodies.GetPositionAndRotation(c.ragdoll->GetBodyID(static_cast<int>(i)), c.currPosition[i], c.currRotation[i]);
        }
    }
}

uint64_t World::stateHash() const
{
    const JPH::BodyInterface& bodies = mPhysics->GetBodyInterfaceNoLock();
    Fnv1a hash;
    hash.add(&mStepCount, sizeof(mStepCount));
    for (const auto& [id, collider] : mColliders)
    {
        hash.add(&id, sizeof(id));
        JPH::RVec3 position;
        JPH::Quat rotation;
        bodies.GetPositionAndRotation(collider.body, position, rotation);
        hash.addVec3(JPH::Vec3(position));
        hash.addFloat(rotation.GetX());
        hash.addFloat(rotation.GetY());
        hash.addFloat(rotation.GetZ());
        hash.addFloat(rotation.GetW());
        hash.addVec3(bodies.GetLinearVelocity(collider.body));
        hash.addVec3(bodies.GetAngularVelocity(collider.body));
    }
    for (const auto& [id, character] : mCharacters)
    {
        hash.add(&id, sizeof(id));
        for (size_t i = 0; i < character->ragdoll->GetBodyCount(); ++i)
        {
            const JPH::BodyID body = character->ragdoll->GetBodyID(static_cast<int>(i));
            JPH::RVec3 position;
            JPH::Quat rotation;
            bodies.GetPositionAndRotation(body, position, rotation);
            hash.addVec3(JPH::Vec3(position));
            hash.addFloat(rotation.GetX());
            hash.addFloat(rotation.GetY());
            hash.addFloat(rotation.GetZ());
            hash.addFloat(rotation.GetW());
            hash.addVec3(bodies.GetLinearVelocity(body));
            hash.addVec3(bodies.GetAngularVelocity(body));
        }
    }
    return hash.value;
}

// ---------------------------------------------------------------------------------------------
// Proxy colliders
// ---------------------------------------------------------------------------------------------

JPH::Ref<JPH::Shape> World::createShape(const DdColliderDesc& desc) const
{
    JPH::Ref<JPH::ShapeSettings> settings;
    switch (desc.shape)
    {
    case DD_SHAPE_SPHERE:
        if (!(desc.radius > 0.0f) || !std::isfinite(desc.radius))
        {
            throw std::invalid_argument("sphere: radius must be > 0");
        }
        settings = new JPH::SphereShapeSettings(desc.radius);
        break;
    case DD_SHAPE_BOX:
        if (!isFinite(desc.half_extents) || !(desc.half_extents.x > 0.0f && desc.half_extents.y > 0.0f &&
                                              desc.half_extents.z > 0.0f))
        {
            throw std::invalid_argument("box: half_extents must be > 0");
        }
        {
            const JPH::Vec3 half = toJolt(desc.half_extents);
            // Jolt's default convex radius must not exceed the smallest half extent.
            const float convexRadius = std::min(JPH::cDefaultConvexRadius, half.ReduceMin() * 0.5f);
            settings = new JPH::BoxShapeSettings(half, convexRadius);
        }
        break;
    case DD_SHAPE_CAPSULE:
        if (!(desc.radius > 0.0f) || !(desc.half_height > 0.0f) || !std::isfinite(desc.radius) ||
            !std::isfinite(desc.half_height))
        {
            throw std::invalid_argument("capsule: radius and half_height must be > 0");
        }
        settings = new JPH::CapsuleShapeSettings(desc.half_height, desc.radius);
        if (!mYToUp.IsClose(JPH::Quat::sIdentity()))
        {
            // Jolt capsules run along +Y; align them with the world up axis (ADR-0004).
            settings = new JPH::RotatedTranslatedShapeSettings(JPH::Vec3::sZero(), mYToUp, settings);
        }
        break;
    case DD_SHAPE_MESH:
    {
        if (desc.motion == DD_MOTION_DYNAMIC)
        {
            throw std::invalid_argument("mesh: dynamic meshes are not supported");
        }
        if (desc.vertices == nullptr || desc.vertex_count < 3)
        {
            throw std::invalid_argument("mesh: needs at least 3 vertices");
        }
        JPH::VertexList vertices;
        vertices.reserve(desc.vertex_count);
        for (uint32_t i = 0; i < desc.vertex_count; ++i)
        {
            const DdVec3& v = desc.vertices[i];
            if (!isFinite(v))
            {
                throw std::invalid_argument("mesh: vertex is not finite");
            }
            vertices.push_back(JPH::Float3(v.x, v.y, v.z));
        }
        JPH::IndexedTriangleList triangles;
        if (desc.indices != nullptr)
        {
            if (desc.index_count < 3 || desc.index_count % 3 != 0)
            {
                throw std::invalid_argument("mesh: index_count must be a non-zero multiple of 3");
            }
            for (uint32_t i = 0; i < desc.index_count; i += 3)
            {
                const uint32_t a = desc.indices[i], b = desc.indices[i + 1], c = desc.indices[i + 2];
                if (a >= desc.vertex_count || b >= desc.vertex_count || c >= desc.vertex_count)
                {
                    throw std::invalid_argument("mesh: index out of range");
                }
                triangles.push_back(JPH::IndexedTriangle(a, b, c));
            }
        }
        else
        {
            if (desc.vertex_count % 3 != 0)
            {
                throw std::invalid_argument("mesh: without indices, vertex_count must be a multiple of 3");
            }
            for (uint32_t i = 0; i < desc.vertex_count; i += 3)
            {
                triangles.push_back(JPH::IndexedTriangle(i, i + 1, i + 2));
            }
        }
        settings = new JPH::MeshShapeSettings(std::move(vertices), std::move(triangles));
        break;
    }
    default:
        throw std::invalid_argument("unknown shape type");
    }

    JPH::ShapeSettings::ShapeResult result = settings->Create();
    if (result.HasError())
    {
        throw std::invalid_argument(std::string("shape creation failed: ") + result.GetError().c_str());
    }
    return result.Get();
}

void World::addColliders(const DdColliderDesc* descs, uint32_t count, DdColliderId* outIds)
{
    JPH::BodyInterface& bodies = mPhysics->GetBodyInterface();

    // Phase 1: validate and create every body. On any failure, undo and rethrow (all or nothing).
    std::vector<JPH::BodyID> created;
    std::vector<JPH::BodyID> toActivate, toKeepAsleep;
    created.reserve(count);
    try
    {
        for (uint32_t i = 0; i < count; ++i)
        {
            const DdColliderDesc& desc = descs[i];
            if (desc.struct_size != sizeof(DdColliderDesc))
            {
                throw std::invalid_argument("DdColliderDesc.struct_size mismatch (header/binary version skew?)");
            }
            if (!isFinite(desc.transform.position))
            {
                throw std::invalid_argument("collider position is not finite");
            }
            if (desc.motion != DD_MOTION_STATIC && desc.motion != DD_MOTION_KINEMATIC &&
                desc.motion != DD_MOTION_DYNAMIC)
            {
                throw std::invalid_argument("unknown motion type");
            }
            const JPH::Quat rotation = normalizedRotation(desc.transform.rotation);
            JPH::Ref<JPH::Shape> shape = createShape(desc);

            const bool isStatic = desc.motion == DD_MOTION_STATIC;
            const JPH::EMotionType motionType = isStatic ? JPH::EMotionType::Static
                                               : desc.motion == DD_MOTION_KINEMATIC ? JPH::EMotionType::Kinematic
                                                                                    : JPH::EMotionType::Dynamic;
            JPH::BodyCreationSettings settings(shape, JPH::RVec3(toJolt(desc.transform.position)), rotation, motionType,
                                               isStatic ? layers::kStatic : layers::kMoving);
            settings.mFriction = std::isfinite(desc.friction) ? desc.friction : 0.6f;
            settings.mRestitution = std::isfinite(desc.restitution) ? desc.restitution : 0.0f;
            if (!isStatic)
            {
                settings.mMotionQuality = JPH::EMotionQuality::LinearCast; // Fast impacts (ADR-0016)
            }
            if (desc.motion == DD_MOTION_DYNAMIC && desc.mass > 0.0f && std::isfinite(desc.mass))
            {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = desc.mass;
            }
            if (desc.motion == DD_MOTION_KINEMATIC && desc.shape == DD_SHAPE_MESH)
            {
                // Jolt cannot compute mass for a mesh, and a kinematic body never uses it (it is never pushed), but
                // the body still needs valid values: a nominal 1000 kg solid box of the mesh's bounds.
                const JPH::Vec3 size = JPH::Vec3::sMax(shape->GetLocalBounds().GetSize(), JPH::Vec3::sReplicate(0.01f));
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
                settings.mMassPropertiesOverride.SetMassAndInertiaOfSolidBox(size, 1000.0f / (size.GetX() * size.GetY() * size.GetZ()));
            }
            settings.mUserData = mNextColliderId + i; // Collider id, for contact reporting later.

            JPH::Body* body = bodies.CreateBody(settings);
            if (body == nullptr)
            {
                throw std::runtime_error("out of bodies (raise DdWorldDesc.max_bodies)");
            }
            created.push_back(body->GetID());
            (isStatic ? toKeepAsleep : toActivate).push_back(body->GetID());
        }
    }
    catch (...)
    {
        if (!created.empty())
        {
            bodies.DestroyBodies(created.data(), static_cast<int>(created.size()));
        }
        throw;
    }

    // Phase 2: add to the simulation in batches (fast broadphase insertion for streaming).
    auto addBatch = [&bodies](std::vector<JPH::BodyID>& batch, JPH::EActivation activation) {
        if (batch.empty())
        {
            return;
        }
        JPH::BodyInterface::AddState state = bodies.AddBodiesPrepare(batch.data(), static_cast<int>(batch.size()));
        bodies.AddBodiesFinalize(batch.data(), static_cast<int>(batch.size()), state, activation);
    };
    addBatch(toKeepAsleep, JPH::EActivation::DontActivate);
    addBatch(toActivate, JPH::EActivation::Activate);

    // Phase 3: register (ids follow input order, independent of how Jolt shuffled the batches).
    for (uint32_t i = 0; i < count; ++i)
    {
        const DdColliderId id = mNextColliderId++;
        Collider collider;
        collider.body = created[i];
        collider.motion = descs[i].motion;
        collider.userData = descs[i].user_data;
        bodies.GetPositionAndRotation(collider.body, collider.currPosition, collider.currRotation);
        collider.prevPosition = collider.currPosition;
        collider.prevRotation = collider.currRotation;
        if (collider.motion == DD_MOTION_KINEMATIC)
        {
            // The trajectory starts where the collider was created, at the current host time.
            collider.samples.push_back(KinematicSample{simTime() + mAccumulator, collider.currPosition, collider.currRotation});
        }
        mColliders.emplace(id, std::move(collider));
        outIds[i] = id;
    }
}

void World::removeColliders(const DdColliderId* ids, uint32_t count)
{
    mScratchBodies.clear();
    for (uint32_t i = 0; i < count; ++i)
    {
        auto it = mColliders.find(ids[i]);
        if (it == mColliders.end())
        {
            continue; // Unknown or already removed: ignored by contract.
        }
        mScratchBodies.push_back(it->second.body);
        mColliders.erase(it);
    }
    if (!mScratchBodies.empty())
    {
        JPH::BodyInterface& bodies = mPhysics->GetBodyInterface();
        bodies.RemoveBodies(mScratchBodies.data(), static_cast<int>(mScratchBodies.size()));
        bodies.DestroyBodies(mScratchBodies.data(), static_cast<int>(mScratchBodies.size()));
    }
}

void World::setKinematicTargets(const DdColliderId* ids, const DdTransform* targets, uint32_t count)
{
    // Validate everything first so a bad entry changes nothing.
    for (uint32_t i = 0; i < count; ++i)
    {
        auto it = mColliders.find(ids[i]);
        if (it == mColliders.end())
        {
            throw std::invalid_argument("set_kinematic_targets: unknown collider id");
        }
        if (it->second.motion != DD_MOTION_KINEMATIC)
        {
            throw std::invalid_argument("set_kinematic_targets: collider is not kinematic");
        }
        if (!isFinite(targets[i].position))
        {
            throw std::invalid_argument("set_kinematic_targets: target position is not finite");
        }
        (void)normalizedRotation(targets[i].rotation);
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        Collider& collider = mColliders.find(ids[i])->second;
        collider.hasPending = true;
        collider.pending.position = JPH::RVec3(toJolt(targets[i].position));
        collider.pending.rotation = normalizedRotation(targets[i].rotation);
    }
}

bool World::colliderTransform(DdColliderId id, bool interpolated, DdTransform& out) const
{
    auto it = mColliders.find(id);
    if (it == mColliders.end())
    {
        return false;
    }
    const Collider& collider = it->second;
    if (interpolated && collider.motion != DD_MOTION_STATIC)
    {
        const float alpha = interpolationAlpha();
        const JPH::RVec3 position = collider.prevPosition + (collider.currPosition - collider.prevPosition) * alpha;
        const JPH::Quat rotation = collider.prevRotation.SLERP(collider.currRotation, alpha).Normalized();
        out.position = toDd(position);
        out.rotation = toDd(rotation);
    }
    else
    {
        out.position = toDd(collider.currPosition);
        out.rotation = toDd(collider.currRotation);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Characters
// ---------------------------------------------------------------------------------------------

DdCharacter* World::createCharacter(const DdCharacterDesc& desc)
{
    if (desc.struct_size != sizeof(DdCharacterDesc))
    {
        throw std::invalid_argument("DdCharacterDesc.struct_size mismatch (header/binary version skew?)");
    }
    if (desc.rig_json == nullptr)
    {
        throw std::invalid_argument("rig_json is required (e.g. dragdoll_get_builtin_rig(\"humanoid16\"))");
    }
    if (!isFinite(desc.transform.position))
    {
        throw std::invalid_argument("character position is not finite");
    }
    const float massScale = desc.mass_scale > 0.0f && std::isfinite(desc.mass_scale) ? desc.mass_scale : 1.0f;
    const Rig rig = Rig::parse(desc.rig_json);

    // Rig space (+Y up) is first aligned with the world up axis, then turned by the host's rotation.
    const JPH::Quat rotation = normalizedRotation(desc.transform.rotation) * mYToUp;
    const JPH::Mat44 worldFromRig = JPH::Mat44::sRotationTranslation(rotation, toJolt(desc.transform.position));

    auto character = std::make_unique<DdCharacter>();
    character->world = this;
    character->id = mNextCharacterId;
    character->userData = desc.user_data;
    character->settings = buildRagdollSettings(rig, worldFromRig, massScale, character->id, character->totalMass);
    character->ragdoll = character->settings->CreateRagdoll(character->id, desc.user_data, mPhysics.get());
    if (character->ragdoll == nullptr)
    {
        throw std::runtime_error("out of bodies (raise DdWorldDesc.max_bodies)");
    }
    character->ragdoll->AddToPhysicsSystem(JPH::EActivation::Activate);

    const size_t count = rig.bodies.size();
    for (const RigBody& body : rig.bodies)
    {
        character->names.push_back(body.name);
    }
    character->currPosition.resize(count);
    character->currRotation.resize(count);
    const JPH::BodyInterface& bodies = mPhysics->GetBodyInterfaceNoLock();
    for (size_t i = 0; i < count; ++i)
    {
        bodies.GetPositionAndRotation(character->ragdoll->GetBodyID(static_cast<int>(i)), character->currPosition[i],
                                      character->currRotation[i]);
    }
    character->prevPosition = character->currPosition;
    character->prevRotation = character->currRotation;

    // Muscles: the rest pose (as created) is the target pose; they start limp.
    character->parent.resize(count);
    character->strength.resize(count);
    character->hinge.resize(count);
    character->restTarget.resize(count, JPH::Quat::sIdentity());
    for (size_t i = 0; i < count; ++i)
    {
        const RigBody& body = rig.bodies[i];
        character->parent[i] = body.parent;
        character->strength[i] = body.parent >= 0 ? body.joint.strength * massScale : 0.0f;
        character->hinge[i] = body.parent >= 0 && body.joint.type == RigJoint::Type::Hinge;
        if (body.parent >= 0)
        {
            character->restTarget[i] = character->currRotation[static_cast<size_t>(body.parent)].Conjugated() * character->currRotation[i];
        }
    }

    ++mNextCharacterId;
    DdCharacter* raw = character.get();
    mCharacters.emplace(raw->id, std::move(character));
    return raw;
}

void World::destroyCharacter(DdCharacter* character)
{
    auto it = mCharacters.find(character->id);
    if (it == mCharacters.end() || it->second.get() != character)
    {
        return;
    }
    character->ragdoll->RemoveFromPhysicsSystem();
    mCharacters.erase(it);
}

bool World::characterTransforms(const DdCharacter& c, bool interpolated, DdTransform* out, uint32_t capacity) const
{
    if (capacity < c.currPosition.size())
    {
        return false;
    }
    const float alpha = interpolated ? interpolationAlpha() : 1.0f;
    for (size_t i = 0; i < c.currPosition.size(); ++i)
    {
        const JPH::RVec3 position = c.prevPosition[i] + (c.currPosition[i] - c.prevPosition[i]) * alpha;
        const JPH::Quat rotation = interpolated ? c.prevRotation[i].SLERP(c.currRotation[i], alpha).Normalized()
                                                : c.currRotation[i];
        out[i].position = toDd(position);
        out[i].rotation = toDd(rotation);
    }
    return true;
}

void World::applyImpulse(DdCharacter& c, uint32_t body, const DdVec3& impulse, const DdVec3* point)
{
    if (body >= c.currPosition.size())
    {
        throw std::invalid_argument("apply_impulse: body index out of range");
    }
    if (!isFinite(impulse) || (point != nullptr && !isFinite(*point)))
    {
        throw std::invalid_argument("apply_impulse: impulse/point must be finite");
    }
    JPH::BodyInterface& bodies = mPhysics->GetBodyInterface();
    const JPH::BodyID id = c.ragdoll->GetBodyID(static_cast<int>(body));
    if (point != nullptr)
    {
        bodies.AddImpulse(id, toJolt(impulse), JPH::RVec3(toJolt(*point)));
    }
    else
    {
        bodies.AddImpulse(id, toJolt(impulse));
    }
    bodies.ActivateBody(id);
}

DdCharacter* World::characterAt(uint32_t index) const
{
    if (index >= mCharacters.size())
    {
        return nullptr;
    }
    auto it = mCharacters.begin();
    std::advance(it, index);
    return it->second.get();
}

} // namespace dragdoll
