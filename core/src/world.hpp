// SPDX-License-Identifier: MIT
// dragdoll::World — one Jolt PhysicsSystem plus DragDoll's fixed-step clock and proxy colliders.
#pragma once

#include <dragdoll/dragdoll.h>

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dragdoll
{

// Object layers. Static geometry never collides with itself; everything else collides.
namespace layers
{
inline constexpr JPH::ObjectLayer kStatic = 0;
inline constexpr JPH::ObjectLayer kMoving = 1;
inline constexpr JPH::uint kCount = 2;
} // namespace layers

namespace bp_layers
{
inline constexpr JPH::BroadPhaseLayer kNonMoving{0};
inline constexpr JPH::BroadPhaseLayer kMoving{1};
inline constexpr JPH::uint kCount = 2;
} // namespace bp_layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return bp_layers::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == layers::kStatic ? bp_layers::kNonMoving : bp_layers::kMoving;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == bp_layers::kNonMoving ? "NonMoving" : "Moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bpLayer) const override
    {
        return layer != layers::kStatic || bpLayer == bp_layers::kMoving;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        return a != layers::kStatic || b != layers::kStatic;
    }
};

class World
{
public:
    // Throws std::invalid_argument / std::runtime_error; the C boundary converts them.
    explicit World(const DdWorldDesc& desc);
    ~World();

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    // Clock
    uint32_t step(double dt);
    void stepFixed(uint32_t steps);
    float interpolationAlpha() const;
    uint64_t stepCount() const { return mStepCount; }
    uint64_t stateHash() const;

    // Proxy colliders
    void addColliders(const DdColliderDesc* descs, uint32_t count, DdColliderId* outIds);
    void removeColliders(const DdColliderId* ids, uint32_t count);
    void setKinematicTargets(const DdColliderId* ids, const DdTransform* targets, uint32_t count);
    bool colliderTransform(DdColliderId id, bool interpolated, DdTransform& out) const;

private:
    struct Collider
    {
        JPH::BodyID body;
        DdMotionType motion = DD_MOTION_STATIC;
        uint64_t userData = 0;
        JPH::RVec3 prevPosition, currPosition;
        JPH::Quat prevRotation = JPH::Quat::sIdentity(), currRotation = JPH::Quat::sIdentity();
        // Pending kinematic target: reached at the end of the next simulated batch of steps.
        bool hasTarget = false;
        JPH::RVec3 targetPosition;
        JPH::Quat targetRotation = JPH::Quat::sIdentity();
        JPH::RVec3 batchStartPosition;
        JPH::Quat batchStartRotation = JPH::Quat::sIdentity();
    };

    JPH::Ref<JPH::Shape> createShape(const DdColliderDesc& desc) const;
    void simulate(uint32_t steps);
    void driveKinematics(uint32_t stepIndex, uint32_t stepsInBatch);
    void captureTransforms();

    // Settings
    JPH::Vec3 mUp;
    JPH::Quat mYToUp; // Rotates Jolt's +Y shape axis (capsules) onto the world up axis.
    double mFixedDt;
    uint32_t mMaxStepsPerCall;

    // Clock state
    double mAccumulator = 0.0;
    uint64_t mStepCount = 0;

    // Jolt (declaration order = construction order; layer objects must outlive the system)
    BroadPhaseLayers mBroadPhaseLayers;
    ObjectVsBroadPhaseFilter mObjectVsBroadPhase;
    ObjectPairFilter mObjectPairs;
    std::unique_ptr<JPH::TempAllocator> mTempAllocator;
    std::unique_ptr<JPH::JobSystem> mJobSystem;
    std::unique_ptr<JPH::PhysicsSystem> mPhysics;

    // Colliders, ordered by id so iteration (hashing, capture) is deterministic.
    std::map<DdColliderId, Collider> mColliders;
    DdColliderId mNextColliderId = 1;
    std::vector<JPH::BodyID> mScratchBodies;
};

} // namespace dragdoll
