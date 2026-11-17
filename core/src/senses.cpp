// SPDX-License-Identifier: MIT
// Senses (ADR-0024 §3): what a character knows about its own body after each fixed step.
//  * Contacts per body: the level, a moving object (a vehicle), another character; where and from which side.
//  * Severity: each body's velocity change over the step with gravity removed - how hard it was hit, whether by a
//    contact or through its joints. No solver internals; a recording replays it exactly.
//  * Self state: airborne, what carries it, height above the ground and the ground's normal, uprightness, angular
//    momentum, and a ballistic landing prediction.
// Read-only: nothing here changes the simulation.
#include "senses.hpp"

#include "character.hpp"
#include "world.hpp"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace dragdoll
{
namespace
{
constexpr uint32_t kAirborneSteps = 3;   // No contact for this many steps (25 ms at 120 Hz) = airborne
constexpr float kGroundReach = 30.0f;    // m: how far below the centre of mass the ground is looked for
constexpr float kBodyHalfThickness = 0.25f; // m: a lying body's centre of mass is about this far above the ground

DdVec3 toDd(JPH::Vec3Arg v)
{
    return DdVec3{v.GetX(), v.GetY(), v.GetZ()};
}
} // namespace

void ContactLog::record(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold)
{
    side(a, b, manifold, false);
    side(b, a, manifold, true);
}

// Records what `self` touches, if `self` is a body of a character (ragdoll bodies carry the character id and their
// rig index in their collision group; proxy colliders have no group filter and carry their collider id as user data).
void ContactLog::side(const JPH::Body& self, const JPH::Body& other, const JPH::ContactManifold& manifold, bool selfIsSecond)
{
    const JPH::CollisionGroup& group = self.GetCollisionGroup();
    if (group.GetGroupFilter() == nullptr)
    {
        return;
    }
    Entry e;
    e.character = group.GetGroupID();
    e.body = group.GetSubGroupID();
    const JPH::CollisionGroup& otherGroup = other.GetCollisionGroup();
    if (otherGroup.GetGroupFilter() != nullptr)
    {
        if (otherGroup.GetGroupID() == e.character)
        {
            return; // Its own body
        }
        e.kind = DD_CONTACT_CHARACTER;
    }
    else
    {
        e.kind = other.IsStatic() ? DD_CONTACT_GROUND : DD_CONTACT_OBJECT;
        e.collider = static_cast<DdColliderId>(other.GetUserData());
    }
    e.depth = manifold.mPenetrationDepth;
    const bool hasPoints = !manifold.mRelativeContactPointsOn1.empty();
    e.point = hasPoints ? JPH::Vec3(selfIsSecond ? manifold.GetWorldSpaceContactPointOn2(0) : manifold.GetWorldSpaceContactPointOn1(0))
                        : JPH::Vec3(manifold.mBaseOffset);
    e.normal = selfIsSecond ? manifold.mWorldSpaceNormal : -manifold.mWorldSpaceNormal; // Jolt's normal runs from body 1 to body 2
    const std::lock_guard<std::mutex> lock(mMutex);
    mEntries.push_back(e);
}

std::vector<ContactLog::Entry>& ContactLog::sorted()
{
    std::sort(mEntries.begin(), mEntries.end(), [](const Entry& a, const Entry& b) {
        if (a.character != b.character) return a.character < b.character;
        if (a.body != b.body) return a.body < b.body;
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.depth != b.depth) return a.depth > b.depth;
        if (a.collider != b.collider) return a.collider < b.collider;
        if (a.point.GetX() != b.point.GetX()) return a.point.GetX() < b.point.GetX();
        if (a.point.GetY() != b.point.GetY()) return a.point.GetY() < b.point.GetY();
        return a.point.GetZ() < b.point.GetZ();
    });
    return mEntries;
}

void World::beforePhysics()
{
    mContactLog->clear();
    const JPH::BodyInterface& bodies = mPhysics->GetBodyInterfaceNoLock();
    for (auto& [id, character] : mCharacters)
    {
        DdCharacter& c = *character;
        if (!c.inPhysics || c.kinematic)
        {
            continue;
        }
        c.velocityBefore.resize(c.currPosition.size());
        for (size_t i = 0; i < c.velocityBefore.size(); ++i)
        {
            c.velocityBefore[i] = bodies.GetLinearVelocity(c.ragdoll->GetBodyID(static_cast<int>(i)));
        }
    }
}

void World::updateSenses()
{
    const std::vector<ContactLog::Entry>& entries = mContactLog->sorted();
    const JPH::BodyInterface& bodies = mPhysics->GetBodyInterfaceNoLock();
    const float dt = static_cast<float>(mFixedDt);
    size_t next = 0; // Entries are sorted by character id, like mCharacters
    for (auto& [id, character] : mCharacters)
    {
        DdCharacter& c = *character;
        const size_t count = c.currPosition.size();
        // A body at rest falls asleep, and Jolt reports no contacts for sleeping bodies: it still lies where it lay, so
        // it keeps the contacts it fell asleep with (otherwise a body lying on the ground would feel airborne).
        const bool asleep = count > 0 && c.inPhysics && !c.kinematic && c.contacts.size() == count &&
                            !bodies.IsActive(c.ragdoll->GetBodyID(0));
        if (asleep)
        {
            for (DdBodyContact& contact : c.contacts) contact.delta_v = 0.0f;
        }
        else
        {
            c.contacts.assign(count, DdBodyContact{});
        }
        DdSenses& s = c.senses;
        const float peakHead = s.peak_head_delta_v, peakImpact = s.peak_impact_delta_v;
        s = DdSenses{};
        s.struct_size = sizeof(DdSenses);
        s.peak_head_delta_v = peakHead;
        s.peak_impact_delta_v = peakImpact;
        s.ground_height = -1.0f;
        s.ground_normal = toDd(mUp);
        s.landing_seconds = -1.0f;
        s.landing_body = -1;
        while (next < entries.size() && entries[next].character < id) ++next;
        if (c.state == DD_STATE_FAILED || count == 0)
        {
            while (next < entries.size() && entries[next].character == id) ++next;
            continue;
        }

        // --- Contacts: per body, which kinds; the deepest contact with a moving object, else with the level, is kept.
        std::vector<std::pair<DdColliderId, uint32_t>> carriers; // Moving collider -> how many bodies touch it
        for (; next < entries.size() && entries[next].character == id; ++next)
        {
            const ContactLog::Entry& e = entries[next];
            if (e.body >= count)
            {
                continue;
            }
            DdBodyContact& contact = c.contacts[e.body];
            const bool first = (contact.flags & e.kind) == 0; // Sorted deepest first within a kind
            contact.flags |= e.kind;
            const bool better = e.kind == DD_CONTACT_OBJECT ? first
                                : e.kind == DD_CONTACT_GROUND ? first && (contact.flags & DD_CONTACT_OBJECT) == 0
                                : first && (contact.flags & (DD_CONTACT_OBJECT | DD_CONTACT_GROUND)) == 0;
            if (better)
            {
                contact.collider = e.collider;
                const auto collider = mColliders.find(e.collider);
                contact.group = collider != mColliders.end() ? collider->second.group : 0;
                contact.point = toDd(e.point);
                contact.normal = toDd(e.normal);
            }
        }
        for (const DdBodyContact& contact : c.contacts) // From the kept contacts too (a body asleep on a bonnet)
        {
            s.ground_bodies += (contact.flags & DD_CONTACT_GROUND) != 0 ? 1u : 0u;
            if ((contact.flags & DD_CONTACT_OBJECT) != 0)
            {
                ++s.object_bodies;
                auto carrier = std::find_if(carriers.begin(), carriers.end(), [&](const auto& x) { return x.first == contact.collider; });
                if (carrier == carriers.end()) carriers.emplace_back(contact.collider, 1u);
                else ++carrier->second;
            }
        }
        uint32_t most = 0;
        for (const auto& [collider, touching] : carriers) // In body order: deterministic
        {
            if (touching > most)
            {
                most = touching;
                s.carried_by = collider;
                const auto found = mColliders.find(collider);
                s.carried_by_group = found != mColliders.end() ? found->second.group : 0;
            }
        }
        bool touching = false;
        for (const DdBodyContact& contact : c.contacts) touching = touching || contact.flags != 0;
        const bool simulated = c.inPhysics && !c.kinematic;
        c.airborneSteps = simulated && !touching ? c.airborneSteps + 1 : 0;
        s.airborne = c.airborneSteps >= kAirborneSteps ? 1 : 0;
        s.airborne_seconds = static_cast<float>(c.airborneSteps) * dt;

        // --- Centre of mass, its velocity, angular momentum about it.
        JPH::Vec3 com = JPH::Vec3::sZero(), velocity = JPH::Vec3::sZero();
        float mass = 0.0f;
        std::vector<JPH::Vec3> linear(count), angular(count);
        for (size_t i = 0; i < count; ++i)
        {
            if (c.inPhysics)
            {
                bodies.GetLinearAndAngularVelocity(c.ragdoll->GetBodyID(static_cast<int>(i)), linear[i], angular[i]);
            }
            else
            {
                linear[i] = angular[i] = JPH::Vec3::sZero();
            }
            com += JPH::Vec3(c.currPosition[i]) * c.bodyMass[i];
            velocity += linear[i] * c.bodyMass[i];
            mass += c.bodyMass[i];
        }
        com /= mass;
        velocity /= mass;
        JPH::Vec3 momentum = JPH::Vec3::sZero();
        for (size_t i = 0; i < count; ++i)
        {
            momentum += (JPH::Vec3(c.currPosition[i]) - com).Cross(linear[i] - velocity) * c.bodyMass[i] + angular[i] * c.bodyInertia[i];
        }
        s.angular_momentum = toDd(momentum);

        // --- Severity: velocity change over the step, gravity removed.
        if (simulated && c.velocityBefore.size() == count)
        {
            const JPH::Vec3 fall = mUp * (mGravity * dt); // Added back: free fall is not a hit
            JPH::Vec3 whole = JPH::Vec3::sZero();
            for (size_t i = 0; i < count; ++i)
            {
                const JPH::Vec3 change = linear[i] - c.velocityBefore[i] + fall;
                c.contacts[i].delta_v = change.Length();
                whole += change * c.bodyMass[i];
            }
            s.impact_delta_v = (whole / mass).Length();
            s.head_delta_v = c.headBody >= 0 ? c.contacts[static_cast<size_t>(c.headBody)].delta_v : 0.0f;
            s.peak_head_delta_v = std::max(s.peak_head_delta_v, s.head_delta_v);
            s.peak_impact_delta_v = std::max(s.peak_impact_delta_v, s.impact_delta_v);
        }

        // --- Uprightness: the trunk's direction (pelvis to head) against up.
        if (c.headBody > 0)
        {
            const JPH::Vec3 trunk = JPH::Vec3(c.currPosition[static_cast<size_t>(c.headBody)] - c.currPosition[0]);
            s.upright = trunk.LengthSq() > 1e-8f ? trunk.Normalized().Dot(mUp) : 0.0f;
        }

        // --- The ground straight below the centre of mass.
        {
            JPH::RayCastResult hit;
            const JPH::RRayCast ray(JPH::RVec3(com), mUp * -kGroundReach);
            if (mPhysics->GetNarrowPhaseQueryNoLock().CastRay(ray, hit, JPH::SpecifiedBroadPhaseLayerFilter(bp_layers::kNonMoving),
                                                             JPH::SpecifiedObjectLayerFilter(layers::kStatic)))
            {
                s.ground_height = hit.mFraction * kGroundReach;
                JPH::BodyLockRead lock(mPhysics->GetBodyLockInterfaceNoLock(), hit.mBodyID);
                if (lock.Succeeded())
                {
                    const JPH::Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction));
                    s.ground_normal = toDd(normal.Dot(mUp) < 0.0f ? -normal : normal); // A mesh hit from its back side
                }
            }
        }

        // --- Landing prediction (airborne only): ballistic flight of the centre of mass, the body turning about it as
        // the pelvis turns now; the body that would be lowest on arrival lands first.
        if (s.airborne != 0 && s.ground_height >= 0.0f && mGravity > 0.0f)
        {
            const float height = std::max(0.0f, s.ground_height - kBodyHalfThickness), rising = velocity.Dot(mUp);
            const float seconds = (rising + std::sqrt(rising * rising + 2.0f * mGravity * height)) / mGravity;
            s.landing_seconds = seconds;
            const JPH::Vec3 spin = angular[0];
            const float turned = spin.Length() * seconds;
            const JPH::Quat turn = turned > 1e-4f ? JPH::Quat::sRotation(spin.Normalized(), turned) : JPH::Quat::sIdentity();
            float lowest = std::numeric_limits<float>::max();
            for (size_t i = 0; i < count; ++i)
            {
                const float h = (turn * (JPH::Vec3(c.currPosition[i]) - com)).Dot(mUp);
                if (h < lowest)
                {
                    lowest = h;
                    s.landing_body = static_cast<int32_t>(i);
                }
            }
        }
        updateImpactReport(c);
    }
}
} // namespace dragdoll
