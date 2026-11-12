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

struct DdCharacter; // character.hpp

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

    // Characters (ragdolls from rigs)
    DdCharacter* createCharacter(const DdCharacterDesc& desc);
    void destroyCharacter(DdCharacter* character);
    bool characterTransforms(const DdCharacter& character, bool interpolated, DdTransform* out, uint32_t capacity) const;
    void applyImpulse(DdCharacter& character, uint32_t body, const DdVec3& impulse, const DdVec3* point);

    JPH::PhysicsSystem& physics() { return *mPhysics; }

private:
    struct KinematicSample
    {
        double time = 0.0;
        JPH::RVec3 position;
        JPH::Quat rotation = JPH::Quat::sIdentity();
    };

    struct Collider
    {
        JPH::BodyID body;
        DdMotionType motion = DD_MOTION_STATIC;
        uint64_t userData = 0;
        JPH::RVec3 prevPosition, currPosition;
        JPH::Quat prevRotation = JPH::Quat::sIdentity(), currRotation = JPH::Quat::sIdentity();
        // Kinematic trajectory: host targets stamped with host time. Every fixed step samples
        // this trajectory at its exact end time, so velocities don't depend on how many fixed
        // steps a host frame happens to get (30 / 60 / 144 fps / unlocked).
        std::vector<KinematicSample> samples;
        bool hasPending = false;
        KinematicSample pending; // Set by the host; stamped with time in the next step call.
    };

    JPH::Ref<JPH::Shape> createShape(const DdColliderDesc& desc) const;
    void stampPendingTargets(double hostTime);
    void simulate(uint32_t steps);
    void driveKinematics(double stepEndTime);
    void captureTransforms();
    double simTime() const { return static_cast<double>(mStepCount) * mFixedDt; }

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

    // Characters, ordered by id (deterministic iteration).
    std::map<uint32_t, std::unique_ptr<DdCharacter>> mCharacters;
    uint32_t mNextCharacterId = 1;
};

} // namespace dragdoll
