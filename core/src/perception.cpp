// SPDX-License-Identifier: MIT
// Perception (ADR-0024): what a character can know about moving objects around it.
//  * Objects: a group of colliders (a car's parts) or a single ungrouped moving collider, with its direction of
//    travel and its extents along that direction.
//  * Threat: time to collision and miss distance of the most imminent object.
//  * Front profile probe: the object's front silhouette measured from its own collision geometry (ray casts against
//    that object only), from which the bonnet leading edge etc. follow. No vehicle database needed in any host.
// Read-only: nothing here changes the simulation. Rules and thresholds are DragDoll's own (docs/reference/
// pedestrian-impact.md §6); the wrap / forward projection criterion follows Wood, Simms & Walsh 2005.
#include "character.hpp"
#include "world.hpp"

#include <Jolt/Physics/Body/BodyFilter.h>
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
constexpr float kMinObjectSpeed = 1.0f;   // m/s: slower things are not threats
constexpr float kMinClosingSpeed = 0.5f;  // m/s
constexpr float kHorizon = 2.0f;          // s: threats further in the future are ignored
constexpr float kBodyHalfDepth = 0.2f;    // m: a standing body, front to back
constexpr float kBodyHalfWidth = 0.25f;   // m
constexpr float kMissMargin = 0.3f;       // m: closer than this to the object's side still counts
constexpr float kVelocitySmoothing = 0.05f; // s
constexpr float kReprobeLateral = 0.15f;  // m: probe again when the impact line moved this far
constexpr float kRayStep = 0.1f;          // m between probe rays
constexpr float kMaxProbeLength = 4.0f;   // m of the object's top that is probed
constexpr float kMaxProbeHeight = 3.5f;   // m of the object's front that is probed
constexpr float kFaceTolerance = 0.05f;   // m: this close to the foremost point is still "the front face"
constexpr float kSurfaceAngle = 20.0f;    // deg: shallower = bonnet / roof, steeper = front or windscreen
// The bonnet leading edge as vehicle regulations define it (UN GTR 9 / EEVC WG17): the point where a straight edge
// held 50 deg rearward of vertical first touches the front. That is the point of the top profile lying furthest out
// along the edge's normal (50 deg above the horizontal, forwards): on a sharp corner the corner itself, on a rounded
// front (a sphere-built car) a point below the crest. No dependence on the spacing of the probe rays.
constexpr float kEdgeNormalUp = 0.7660444f;      // sin 50 deg
constexpr float kEdgeNormalForward = 0.6427876f; // cos 50 deg
constexpr float kShoulderAngle = 40.0f;          // deg: between the edge and the flat of a rounded bonnet
constexpr float kForwardProjectionRatio = 0.95f; // Leading edge at >= this share of the centre-of-mass height
constexpr float kCornerBand = 0.15f;      // m from a front corner: a fender vault is expected
constexpr float kRadToDeg = 180.0f / 3.14159265f;

class MemberFilter final : public JPH::BodyFilter
{
public:
    std::vector<JPH::BodyID> bodies;
    bool ShouldCollide(const JPH::BodyID& id) const override { return std::find(bodies.begin(), bodies.end(), id) != bodies.end(); }
    bool ShouldCollideLocked(const JPH::Body& body) const override { return ShouldCollide(body.GetID()); }
};

DdVec3 toDd(JPH::Vec3Arg v)
{
    return DdVec3{v.GetX(), v.GetY(), v.GetZ()};
}
} // namespace

void World::updatePerception()
{
    if (mCharacters.empty())
    {
        return;
    }
    refreshObjects();
    for (auto& [id, character] : mCharacters) // Ordered by id: deterministic
    {
        if (character->state != DD_STATE_FAILED)
        {
            updateThreat(*character);
        }
    }
}

// Velocity, direction of travel and extents of every moving object, from its colliders' shapes as they are now.
void World::refreshObjects()
{
    const JPH::BodyLockInterface& lock = mPhysics->GetBodyLockInterfaceNoLock();
    for (auto& [key, object] : mObjects)
    {
        object.moving = false;
        if (object.members.empty())
        {
            continue;
        }
        JPH::Vec3 velocity = JPH::Vec3::sZero();
        {
            JPH::BodyLockRead body(lock, mColliders.at(object.members.front()).body);
            if (!body.Succeeded())
            {
                continue;
            }
            velocity = body.GetBody().GetLinearVelocity();
        }
        const JPH::Vec3 horizontal = velocity - mUp * velocity.Dot(mUp);
        const float speed = horizontal.Length();
        if (speed < kMinObjectSpeed)
        {
            continue;
        }
        // Along the slope it drives on (up to about 35 degrees; steeper is a fall or a jump: treated as level travel).
        const float climb = std::clamp(velocity.Dot(mUp), -0.7f * speed, 0.7f * speed);
        velocity = horizontal + mUp * climb;
        object.moving = true;
        object.velocity = velocity;
        object.direction = velocity.Normalized();
        object.left = mUp.Cross(object.direction).Normalized();
        object.up = object.direction.Cross(object.left);
        const float inf = std::numeric_limits<float>::max();
        object.front = object.leftMost = object.top = -inf;
        object.rear = object.rightMost = object.bottom = inf;
        for (DdColliderId member : object.members)
        {
            JPH::BodyLockRead body(lock, mColliders.at(member).body);
            if (!body.Succeeded())
            {
                continue;
            }
            const JPH::AABox box = body.GetBody().GetShape()->GetLocalBounds();
            const JPH::RMat44 transform = body.GetBody().GetCenterOfMassTransform();
            for (int corner = 0; corner < 8; ++corner)
            {
                const JPH::Vec3 local((corner & 1) ? box.mMax.GetX() : box.mMin.GetX(), (corner & 2) ? box.mMax.GetY() : box.mMin.GetY(),
                                      (corner & 4) ? box.mMax.GetZ() : box.mMin.GetZ());
                const JPH::Vec3 p(transform * local);
                const float along = p.Dot(object.direction), side = p.Dot(object.left), height = p.Dot(object.up);
                object.front = std::max(object.front, along);
                object.rear = std::min(object.rear, along);
                object.leftMost = std::max(object.leftMost, side);
                object.rightMost = std::min(object.rightMost, side);
                object.top = std::max(object.top, height);
                object.bottom = std::min(object.bottom, height);
            }
        }
    }
}

void World::updateThreat(DdCharacter& c)
{
    // Centre of mass and its velocity over the last fixed step.
    JPH::Vec3 com = JPH::Vec3::sZero(), before = JPH::Vec3::sZero();
    float mass = 0.0f;
    for (size_t i = 0; i < c.currPosition.size(); ++i)
    {
        com += JPH::Vec3(c.currPosition[i]) * c.bodyMass[i];
        before += JPH::Vec3(c.prevPosition[i]) * c.bodyMass[i];
        mass += c.bodyMass[i];
    }
    if (mass <= 0.0f)
    {
        return;
    }
    com /= mass;
    before /= mass;
    // Velocity from the bodies themselves, not from position differences: a host-animated character is re-posed once
    // per host frame (its recorded positions do not move in between), but its bodies carry the animated velocity
    // (activation from motion) - so a passenger reads as moving with its vehicle. Lightly smoothed.
    JPH::Vec3 instant = JPH::Vec3::sZero();
    if (c.inPhysics)
    {
        const JPH::BodyInterface& bodies = mPhysics->GetBodyInterfaceNoLock();
        for (size_t i = 0; i < c.currPosition.size(); ++i)
        {
            instant += bodies.GetLinearVelocity(c.ragdoll->GetBodyID(static_cast<int>(i))) * c.bodyMass[i];
        }
        instant /= mass;
    }
    instant -= mUp * instant.Dot(mUp);
    c.sensedVelocity += (instant - c.sensedVelocity) * std::min(1.0f, static_cast<float>(mFixedDt) / kVelocitySmoothing);
    const JPH::Vec3 velocity = c.sensedVelocity;

    const Object* best = nullptr;
    uint64_t bestKey = 0;
    float bestTime = kHorizon, bestMiss = 0.0f, bestLateral = 0.0f, bestClosing = 0.0f;
    for (const auto& [key, object] : mObjects)
    {
        if (!object.moving || (c.ridingGroup != 0 && object.group == c.ridingGroup))
        {
            continue; // Standing still, or the character's own vehicle
        }
        const float closing = (object.velocity - velocity).Dot(object.direction);
        if (closing < kMinClosingSpeed)
        {
            continue;
        }
        const float gap = com.Dot(object.direction) - object.front - kBodyHalfDepth; // Front face to the near side of the body
        if (gap < -(object.front - object.rear) - 2.0f * kBodyHalfDepth)
        {
            continue; // Already behind it
        }
        const float time = std::max(gap, 0.0f) / closing;
        if (time > bestTime)
        {
            continue;
        }
        const float centre = 0.5f * (object.leftMost + object.rightMost), half = 0.5f * (object.leftMost - object.rightMost);
        const float lateral = com.Dot(object.left) + velocity.Dot(object.left) * time - centre;
        const float miss = std::max(0.0f, std::fabs(lateral) - half - kBodyHalfWidth);
        if (miss > kMissMargin || (gap < 0.0f && std::fabs(lateral) > half))
        {
            continue; // It passes by, or is passing right beside (only a body within its width is "on" it)
        }
        best = &object;
        bestKey = key;
        bestTime = time;
        bestMiss = miss;
        bestLateral = lateral;
        bestClosing = closing;
    }
    if (best == nullptr)
    {
        c.threat = DdThreat{};
        c.threat.struct_size = sizeof(DdThreat);
        c.threatSilhouette.clear();
        c.probedKey = 0;
        return;
    }
    const DdFrontProfile kept = c.threat.profile;
    c.threat.struct_size = sizeof(DdThreat);
    c.threat.group = best->group;
    c.threat.collider = best->members.front();
    c.threat.time_to_collision = bestTime;
    c.threat.miss_distance = bestMiss;
    c.threat.lateral_offset = bestLateral;
    c.threat.closing_speed = bestClosing;
    c.threat.direction = toDd(best->direction);
    c.threat.profile = kept;

    // Probe the front along the predicted impact line (inside the object's width), once per approach.
    const float half = 0.5f * (best->leftMost - best->rightMost);
    const float line = std::clamp(bestLateral, -std::max(half - 0.1f, 0.0f), std::max(half - 0.1f, 0.0f));
    if (c.probedKey != bestKey || c.probedRevision != best->revision || std::fabs(line - c.probedLateral) > kReprobeLateral)
    {
        probeFront(c, bestKey, *best, line);
    }
    const DdFrontProfile& profile = c.threat.profile;
    c.threat.predicted = DD_TRAJECTORY_NONE;
    if (profile.valid != 0)
    {
        // Wood, Simms & Walsh 2005: forward projection when the centre of gravity lies within the primary impact zone,
        // wrap when the body is struck below it. A hit at a front corner is expected to vault off the side.
        // The character's centre of mass above the ground it stands on (not the ground under the vehicle: on a slope the
        // two differ), against the leading edge above the vehicle's own ground.
        float standsOn = com.Dot(mUp) - 1.0f; // No ground found below: assume a standing height
        {
            constexpr float kReach = 3.0f;
            JPH::RayCastResult hit;
            if (mPhysics->GetNarrowPhaseQueryNoLock().CastRay(JPH::RRayCast(JPH::RVec3(com), mUp * -kReach), hit,
                                                             JPH::SpecifiedBroadPhaseLayerFilter(bp_layers::kNonMoving),
                                                             JPH::SpecifiedObjectLayerFilter(layers::kStatic)))
            {
                standsOn = com.Dot(mUp) - hit.mFraction * kReach;
            }
        }
        const float comHeight = com.Dot(mUp) - standsOn;
        c.sensedComHeight = comHeight;
        c.threat.predicted = std::fabs(bestLateral) > half - kCornerBand ? DD_TRAJECTORY_FENDER_VAULT
                             : profile.leading_edge_height >= kForwardProjectionRatio * comHeight ? DD_TRAJECTORY_FORWARD_PROJECTION
                             : DD_TRAJECTORY_WRAP;
    }
}

void World::probeFront(DdCharacter& c, uint64_t key, const Object& object, float lateral)
{
    c.probedKey = key;
    c.probedRevision = object.revision;
    c.probedLateral = lateral;
    c.threat.profile = DdFrontProfile{};
    c.threatSilhouette.clear();

    const JPH::NarrowPhaseQuery& query = mPhysics->GetNarrowPhaseQueryNoLock();
    MemberFilter members;
    for (DdColliderId member : object.members)
    {
        members.bodies.push_back(mColliders.at(member).body);
    }
    const float side = 0.5f * (object.leftMost + object.rightMost) + lateral;
    // A point from its coordinates along the object's direction of travel, to its left, and up.
    // A point from its coordinates in the object's frame: along its travel, to its left, and along its own up. On a
    // slope the whole probe tilts with the vehicle, so the measured front does not depend on the road's gradient.
    const auto point = [&](float along, float height) { return JPH::RVec3(object.direction * along + object.left * side + object.up * height); };
    const auto cast = [&](JPH::RVec3Arg origin, JPH::Vec3Arg ray, const JPH::BroadPhaseLayerFilter& bp, const JPH::ObjectLayerFilter& layer,
                          const JPH::BodyFilter& filter, float& outFraction) {
        JPH::RayCastResult hit;
        if (!query.CastRay(JPH::RRayCast(origin, ray), hit, bp, layer, filter))
        {
            return false;
        }
        outFraction = hit.mFraction;
        return true;
    };

    // The ground under the front: static geometry just ahead of the object; else the object's own lowest point
    // (wheels) or the character's feet level, whichever is lower.
    float ground = object.bottom;
    {
        const float from = object.top + 0.5f, length = from - object.bottom + 3.0f;
        float fraction = 0.0f;
        if (cast(point(object.front + 0.5f, from), object.up * -length, JPH::SpecifiedBroadPhaseLayerFilter(bp_layers::kNonMoving),
                 JPH::SpecifiedObjectLayerFilter(layers::kStatic), JPH::BodyFilter(), fraction))
        {
            ground = from - fraction * length;
        }
    }
    const JPH::BroadPhaseLayerFilter anyBroadPhase;
    const JPH::ObjectLayerFilter anyLayer;

    // Front face: horizontal rays against the object's travel, every 10 cm up.
    struct Sample
    {
        float back, height; // Behind the foremost extent / above the ground
    };
    std::vector<Sample> face;
    const float faceLength = 1.0f + std::min(object.front - object.rear, 2.5f);
    const float heightLimit = std::min(object.top - ground + 0.05f, kMaxProbeHeight);
    for (float h = kRayStep; h <= heightLimit; h += kRayStep)
    {
        float fraction = 0.0f;
        if (cast(point(object.front + 1.0f, ground + h), object.direction * -faceLength, anyBroadPhase, anyLayer, members, fraction))
        {
            face.push_back(Sample{fraction * faceLength - 1.0f, h});
        }
    }
    if (face.empty())
    {
        return;
    }
    float foremost = face.front().back;
    for (const Sample& s : face) foremost = std::min(foremost, s.back);
    size_t first = 0;
    while (first < face.size() && face[first].back > foremost + kFaceTolerance) ++first; // Lowest ray on the front face
    size_t last = first;
    while (last + 1 < face.size() && face[last + 1].back <= foremost + kFaceTolerance) ++last;

    // Top: rays straight down, every 10 cm back from the front.
    std::vector<Sample> top;
    const float from = object.top + 0.5f, down = from - ground;
    const float probeLength = std::min(object.front - object.rear, kMaxProbeLength);
    for (float x = 0.5f * kRayStep; x <= probeLength; x += kRayStep)
    {
        float fraction = 0.0f;
        if (cast(point(object.front - x, from), object.up * -down, anyBroadPhase, anyLayer, members, fraction))
        {
            top.push_back(Sample{x, down * (1.0f - fraction)});
        }
    }
    if (top.size() < 4)
    {
        return;
    }
    // Median of three: a single odd sample (a gap between a sphere-built car's parts) does not make an edge.
    std::vector<float> z(top.size());
    for (size_t i = 0; i < top.size(); ++i)
    {
        const float a = top[i == 0 ? 0 : i - 1].height, b = top[i].height, d = top[i + 1 < top.size() ? i + 1 : i].height;
        z[i] = std::max(std::min(a, b), std::min(std::max(a, b), d));
    }
    const size_t n = top.size();
    // Slope over the next two samples (about 20 cm), degrees from horizontal.
    const auto slope = [&](size_t i) {
        const size_t j = std::min(i + 2, n - 1);
        return j == i ? 0.0f : std::atan2(z[j] - z[i], top[j].back - top[i].back) * kRadToDeg;
    };
    float roof = 0.0f;
    for (float height : z) roof = std::max(roof, height);

    size_t edge = 0; // Bonnet leading edge (see kEdgeNormal*)
    float furthest = -std::numeric_limits<float>::max();
    for (size_t i = 0; i < n; ++i)
    {
        const float out = z[i] * kEdgeNormalUp - top[i].back * kEdgeNormalForward;
        if (out > furthest + 1e-4f)
        {
            furthest = out;
            edge = i;
        }
    }
    size_t bonnetEnd = edge; // The top surface from there on: shallow and clearly below the roof
    while (bonnetEnd + 1 < n && slope(bonnetEnd) >= kSurfaceAngle && slope(bonnetEnd) < kShoulderAngle && z[bonnetEnd] < 0.9f * roof)
    {
        ++bonnetEnd; // The rounded shoulder between the edge and the flat of the bonnet belongs to the bonnet
    }
    while (bonnetEnd + 1 < n && slope(bonnetEnd) < kSurfaceAngle && z[bonnetEnd] < 0.9f * roof) ++bonnetEnd;
    if (z[bonnetEnd] >= 0.9f * roof) bonnetEnd = edge; // A flat front: the "top surface" is the roof itself
    size_t screenEnd = bonnetEnd;
    while (screenEnd + 1 < n && slope(screenEnd) >= kSurfaceAngle) ++screenEnd;

    DdFrontProfile& profile = c.threat.profile;
    profile.valid = 1;
    profile.front_top_height = face[last].height;
    profile.leading_edge_height = z[edge];
    profile.leading_edge_setback = top[edge].back;
    profile.roof_height = roof;
    if (bonnetEnd > edge)
    {
        profile.bonnet_length = top[bonnetEnd].back - top[edge].back;
        profile.bonnet_angle = std::atan2(z[bonnetEnd] - z[edge], profile.bonnet_length) * kRadToDeg;
    }
    if (screenEnd > bonnetEnd)
    {
        profile.windscreen_angle = std::atan2(z[screenEnd] - z[bonnetEnd], top[screenEnd].back - top[bonnetEnd].back) * kRadToDeg;
    }

    // For tools: the silhouette (front face bottom to top, then back over the top), stored in the local space of the
    // object's first collider so that it moves, climbs and tilts with the vehicle.
    c.probedAnchor = object.members.front();
    const JPH::RMat44 anchor = mPhysics->GetBodyInterfaceNoLock().GetWorldTransform(mColliders.at(c.probedAnchor).body).Inversed();
    for (size_t i = first; i <= last; ++i)
    {
        c.threatSilhouette.push_back(toDd(JPH::Vec3(anchor * point(object.front - face[i].back, ground + face[i].height))));
    }
    for (size_t i = 0; i < n; ++i)
    {
        c.threatSilhouette.push_back(toDd(JPH::Vec3(anchor * point(object.front - top[i].back, ground + z[i]))));
    }
}

uint32_t World::threatSilhouette(const DdCharacter& c, DdVec3* out, uint32_t capacity) const
{
    const auto collider = mColliders.find(c.probedAnchor);
    if (collider == mColliders.end() || c.threat.profile.valid == 0)
    {
        return 0;
    }
    const JPH::RMat44 anchor = mPhysics->GetBodyInterfaceNoLock().GetWorldTransform(collider->second.body);
    const uint32_t count = static_cast<uint32_t>(c.threatSilhouette.size());
    for (uint32_t i = 0; i < count && i < capacity && out != nullptr; ++i)
    {
        const DdVec3& p = c.threatSilhouette[i];
        out[i] = toDd(JPH::Vec3(anchor * JPH::Vec3(p.x, p.y, p.z)));
    }
    return count;
}
} // namespace dragdoll
