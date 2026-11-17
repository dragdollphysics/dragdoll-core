// SPDX-License-Identifier: MIT
// Contact log (ADR-0024 §3): what Jolt's contact callbacks report during one physics step, for the characters' senses.
#pragma once

#include <dragdoll/dragdoll.h>

#include <Jolt/Jolt.h>

#include <Jolt/Physics/Collision/ContactListener.h>

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

    std::mutex mMutex;
    std::vector<Entry> mEntries;
};
} // namespace dragdoll
