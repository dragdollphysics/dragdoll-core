// SPDX-License-Identifier: MIT
// Contact log (ADR-0024 §3): what Jolt's contact callbacks report during one physics step, for the characters' senses.
#pragma once

#include <dragdoll/dragdoll.h>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace dragdoll
{
// Jolt calls the listener from its worker threads, in no fixed order: entries are appended under a lock and sorted
// before use, so what a character senses does not depend on thread timing (ADR-0007).
class ContactLog final : public JPH::ContactListener
{
public:
    struct Entry
    {
        uint32_t character = 0; // Character id
        uint32_t body = 0;      // Its body (rig order)
        uint32_t kind = 0;      // DD_CONTACT_GROUND / OBJECT / CHARACTER
        DdColliderId collider = DD_INVALID_COLLIDER;
        float depth = 0.0f;
        JPH::Vec3 point = JPH::Vec3::sZero();
        JPH::Vec3 normal = JPH::Vec3::sZero(); // From the other thing into the body
    };

    // Bodies on the road (ADR-0026): a landed character marked for it does not collide with moving colliders.
    JPH::ValidateResult OnContactValidate(const JPH::Body& a, const JPH::Body& b, JPH::RVec3Arg, const JPH::CollideShapeResult&) override
    {
        if (mPassThrough.empty()) return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
        return passesThrough(a, b) || passesThrough(b, a) ? JPH::ValidateResult::RejectAllContactsForThisBodyPair
                                                          : JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }
    // Characters whose landed body moving colliders pass through; set between steps (read-only during a step).
    void setPassThrough(std::vector<uint32_t> characters)
    {
        std::sort(characters.begin(), characters.end());
        mPassThrough = std::move(characters);
    }

    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
    {
        record(a, b, manifold);
    }
    void OnContactPersisted(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
    {
        record(a, b, manifold);
    }

    void clear() { mEntries.clear(); }
    // Sorted by character, body, kind, deepest first, collider: a fixed order whatever the threads did.
    std::vector<Entry>& sorted();

private:
    void record(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold);
    void side(const JPH::Body& self, const JPH::Body& other, const JPH::ContactManifold& manifold, bool selfIsSecond);

    // `self` is a body of a pass-through character and `other` a moving collider (not the level, not a ragdoll).
    // Ragdoll bodies are known by their collision group (character id); proxy colliders have no group filter.
    bool passesThrough(const JPH::Body& self, const JPH::Body& other) const
    {
        const JPH::CollisionGroup& group = self.GetCollisionGroup();
        if (group.GetGroupFilter() == nullptr || other.IsStatic() || other.GetCollisionGroup().GetGroupFilter() != nullptr) return false;
        return std::binary_search(mPassThrough.begin(), mPassThrough.end(), static_cast<uint32_t>(group.GetGroupID()));
    }

    std::mutex mMutex;
    std::vector<Entry> mEntries;
    std::vector<uint32_t> mPassThrough;
};
} // namespace dragdoll
