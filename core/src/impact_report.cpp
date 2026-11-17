// SPDX-License-Identifier: MIT
// Impact report (ADR-0024 §5, task 4.2): what happened when a moving object hit a character, measured from the
// character's senses after every fixed step - first contact, contact order, head strike (zone, wrap-around distance,
// speed), separation and launch, flight, first ground contact, rest, distances, the throw corridor and the measured
// trajectory class. Read-only: nothing here changes the simulation.
//
// Sources for the quantities: docs/reference/pedestrian-impact.md. The class rules are DragDoll's own (after the
// categories of Ravani et al. 1981); the throw corridor follows the general throw equation (Searle; Han & Brach) at
// launch angles between 0 and 45 degrees.
#include "character.hpp"
#include "world.hpp"

#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace dragdoll
{
namespace
{
constexpr uint32_t kApartSteps = 3;        // No contact with the object for 25 ms = separated
constexpr float kRestSeconds = 0.5f;       // Still this long after landing = at rest (as DD_STATE_SETTLED)
constexpr float kRoadFriction = 0.7f;      // Body on road (Searle: 0.66-0.79; reference file §3)
constexpr float kEdgeBand = 0.15f;         // m behind the leading edge that still counts as "leading edge"
constexpr float kCornerBand = 0.15f;       // m from a front corner: a fender-vault candidate
constexpr float kRoofOvershoot = 0.5f;     // m beyond the bonnet's end at separation: over the roof
constexpr float kRadToDeg = 180.0f / 3.14159265f;

DdVec3 toDd(JPH::Vec3Arg v)
{
    return DdVec3{v.GetX(), v.GetY(), v.GetZ()};
}

float horizontalDistance(JPH::RVec3Arg a, JPH::RVec3Arg b, JPH::Vec3Arg up)
{
    const JPH::Vec3 d(a - b);
    return (d - up * d.Dot(up)).Length();
}

struct Located
{
    DdZone zone = DD_ZONE_NONE;
    float wrapAround = -1.0f; // Along the contour from the ground
    float height = -1.0f;     // Above the ground under the object's front
};

// Where a world point lies on the object's probed front: the nearest point of the silhouette (local to the anchor).
Located locate(const DdCharacter::ImpactTracking& t, JPH::RMat44Arg anchorWorld, JPH::RVec3Arg point)
{
    Located out;
    const size_t n = t.silhouette.size();
    if (n < 2 || t.profile.size() != n || t.front.valid == 0)
    {
        return out;
    }
    const JPH::Vec3 local(anchorWorld.Inversed() * point);
    float best = std::numeric_limits<float>::max(), along = 0.0f, bestAlong = 0.0f;
    size_t bestSegment = 0;
    float bestT = 0.0f;
    for (size_t i = 0; i + 1 < n; ++i)
    {
        const JPH::Vec3 a(t.silhouette[i].x, t.silhouette[i].y, t.silhouette[i].z), b(t.silhouette[i + 1].x, t.silhouette[i + 1].y, t.silhouette[i + 1].z);
        const JPH::Vec3 ab = b - a;
        const float length = ab.Length();
        const float u = length > 1e-6f ? std::clamp((local - a).Dot(ab) / (length * length), 0.0f, 1.0f) : 0.0f;
        const float d = (local - (a + ab * u)).Length();
        if (d < best)
        {
            best = d;
            bestAlong = along + u * length;
            bestSegment = i;
            bestT = u;
        }
        along += length;
    }
    const auto& p0 = t.profile[bestSegment];
    const auto& p1 = t.profile[bestSegment + 1];
    const float back = p0[0] + (p1[0] - p0[0]) * bestT;
    out.height = p0[1] + (p1[1] - p0[1]) * bestT;
    out.wrapAround = t.profile.front()[1] + bestAlong; // The lowest probed point sits that high above the ground
    const DdFrontProfile& f = t.front;
    const bool onFace = bestSegment + 1 < t.faceCount || (bestSegment + 1 == t.faceCount && bestT < 0.5f);
    if (onFace && out.height < f.leading_edge_height - 0.05f) out.zone = DD_ZONE_FRONT;
    else if (back <= f.leading_edge_setback + kEdgeBand) out.zone = DD_ZONE_LEADING_EDGE;
    else if (back <= f.leading_edge_setback + f.bonnet_length) out.zone = DD_ZONE_BONNET;
    else if (out.height < 0.92f * f.roof_height) out.zone = DD_ZONE_WINDSCREEN;
    else out.zone = DD_ZONE_ROOF;
    return out;
}
} // namespace

void World::updateImpactReport(DdCharacter& c)
{
    DdImpactReport& r = c.impact;
    DdCharacter::ImpactTracking& t = c.impactTracking;
    const size_t count = c.currPosition.size();
    if (r.finished != 0 || !c.inPhysics || c.kinematic || c.state == DD_STATE_FAILED || count == 0 || c.contacts.size() != count)
    {
        return;
    }
    const float dt = static_cast<float>(mFixedDt);
    const JPH::BodyInterface& bodies = mPhysics->GetBodyInterfaceNoLock();

    // Centre of mass and its velocity.
    JPH::RVec3 com = JPH::RVec3::sZero();
    JPH::Vec3 velocity = JPH::Vec3::sZero();
    float mass = 0.0f;
    for (size_t i = 0; i < count; ++i)
    {
        com += c.currPosition[i] * c.bodyMass[i];
        velocity += bodies.GetLinearVelocity(c.ragdoll->GetBodyID(static_cast<int>(i))) * c.bodyMass[i];
        mass += c.bodyMass[i];
    }
    com /= mass;
    velocity /= mass;
    const DdSenses& s = c.senses;

    // Is this body touching the object of the report?
    const auto onObject = [&](const DdBodyContact& contact) {
        if ((contact.flags & DD_CONTACT_OBJECT) == 0) return false;
        return t.group != 0 ? contact.group == t.group : contact.collider == r.collider;
    };

    if (!t.started)
    {
        // The first contact with a moving object starts the report: the lowest touching body is the first contact.
        int first = -1;
        float lowest = std::numeric_limits<float>::max();
        for (size_t i = 0; i < count; ++i)
        {
            const DdBodyContact& contact = c.contacts[i];
            const float h = contact.point.x * mUp.GetX() + contact.point.y * mUp.GetY() + contact.point.z * mUp.GetZ();
            if ((contact.flags & DD_CONTACT_OBJECT) != 0 && h < lowest)
            {
                lowest = h;
                first = static_cast<int>(i);
            }
        }
        if (first < 0)
        {
            return;
        }
        const DdBodyContact& hit = c.contacts[static_cast<size_t>(first)];
        t = DdCharacter::ImpactTracking{};
        t.started = true;
        t.startStep = mStepCount;
        t.group = hit.group;
        t.touched.assign(count, false);
        t.touched[static_cast<size_t>(first)] = true;
        r = DdImpactReport{};
        r.struct_size = sizeof(DdImpactReport);
        r.valid = 1;
        r.group = hit.group;
        r.collider = hit.collider;
        r.head_strike_time = r.separation_time = r.ground_time = r.rest_time = -1.0f;
        r.wrap_around_distance = r.launch_height = r.apex_height = r.corridor_min = r.corridor_max = -1.0f;
        r.ground_body = -1;
        r.first_body = first;
        r.contact_order[r.contact_count++] = static_cast<uint32_t>(first); // The order starts with the first contact

        // The object's frame and extents now; its probed front if the threat was this object.
        const auto object = mObjects.find(objectKey(hit.group, hit.collider));
        const bool probed = c.threat.profile.valid != 0 && c.probedAnchor != DD_INVALID_COLLIDER && mColliders.count(c.probedAnchor) != 0 &&
                            (hit.group != 0 ? mColliders.at(c.probedAnchor).group == hit.group : c.probedAnchor == hit.collider);
        t.anchor = probed ? c.probedAnchor : hit.collider;
        if (mColliders.count(t.anchor) == 0)
        {
            t.anchor = hit.collider;
        }
        t.anchorStart = bodies.GetPosition(mColliders.at(t.anchor).body);
        const JPH::Vec3 objectVelocity = bodies.GetLinearVelocity(mColliders.at(t.anchor).body);
        r.vehicle_speed = objectVelocity.Length();
        if (object != mObjects.end() && object->second.moving)
        {
            const Object& o = object->second;
            t.direction = o.direction;
            t.left = o.left;
            t.up = o.up;
            t.frontStart = o.front;
            t.halfWidth = 0.5f * (o.leftMost - o.rightMost);
            r.lateral_offset = JPH::Vec3(com).Dot(o.left) - 0.5f * (o.leftMost + o.rightMost);
        }
        else
        {
            const JPH::Vec3 flat = objectVelocity - mUp * objectVelocity.Dot(mUp);
            t.direction = flat.LengthSq() > 1e-6f ? flat.Normalized() : JPH::Vec3::sZero();
            t.left = t.direction.IsNearZero() ? JPH::Vec3::sZero() : mUp.Cross(t.direction).Normalized();
            t.up = mUp;
        }
        if (probed)
        {
            t.silhouette = c.threatSilhouette;
            t.profile = c.threatSilhouetteProfile;
            t.faceCount = c.threatSilhouetteFace;
            t.front = c.threat.profile;
        }
        t.impactCom = com;
        const JPH::RMat44 anchorWorld = bodies.GetWorldTransform(mColliders.at(t.anchor).body);
        const Located where = locate(t, anchorWorld, JPH::RVec3(hit.point.x, hit.point.y, hit.point.z));
        r.first_zone = where.zone;
        r.first_height = where.height >= 0.0f ? where.height
                                              : JPH::Vec3(hit.point.x, hit.point.y, hit.point.z).Dot(mUp) -
                                                    (JPH::Vec3(com).Dot(mUp) - std::max(s.ground_height, 0.0f));
    }

    const auto anchor = mColliders.find(t.anchor);
    const bool anchorAlive = anchor != mColliders.end();
    const JPH::RMat44 anchorWorld = anchorAlive ? bodies.GetWorldTransform(anchor->second.body) : JPH::RMat44::sIdentity();
    const JPH::Vec3 objectVelocity = anchorAlive ? bodies.GetLinearVelocity(anchor->second.body) : JPH::Vec3::sZero();
    const float elapsed = static_cast<float>(mStepCount - t.startStep) * dt;

    // --- In contact with the object: order, head strike, what lies on its top.
    bool touching = false;
    for (size_t i = 0; i < count; ++i)
    {
        const DdBodyContact& contact = c.contacts[i];
        if (!onObject(contact))
        {
            continue;
        }
        touching = true;
        if (!t.touched[i])
        {
            t.touched[i] = true;
            if (r.contact_count < DD_IMPACT_MAX_ORDER) r.contact_order[r.contact_count++] = static_cast<uint32_t>(i);
        }
        const bool head = static_cast<int>(i) == c.headBody, upper = head || static_cast<int>(i) == c.upperTrunkBody;
        if (upper && anchorAlive)
        {
            const Located where = locate(t, anchorWorld, JPH::RVec3(contact.point.x, contact.point.y, contact.point.z));
            if (where.zone >= DD_ZONE_BONNET) t.upperOnTop = true;
            if (head && !t.headStruck)
            {
                t.headStruck = true;
                r.head_strike_time = elapsed;
                r.head_strike_zone = where.zone;
                r.wrap_around_distance = where.wrapAround;
                const JPH::Vec3 before = c.velocityBefore.size() == count ? c.velocityBefore[i] : JPH::Vec3::sZero();
                r.head_strike_speed = (before - objectVelocity).Length();
            }
        }
    }
    if (c.headBody >= 0)
    {
        r.peak_head_delta_v = std::max(r.peak_head_delta_v, c.contacts[static_cast<size_t>(c.headBody)].delta_v);
    }

    // --- Rotation about the object's left axis until the first ground contact (somersault rule).
    if (!t.grounded && !t.left.IsNearZero())
    {
        r.pitch_degrees += bodies.GetAngularVelocity(c.ragdoll->GetBodyID(0)).Dot(t.left) * dt * kRadToDeg;
    }

    // --- Separation and launch.
    if (!t.separated)
    {
        t.stepsApart = touching ? 0 : t.stepsApart + 1;
        if (t.stepsApart >= kApartSteps)
        {
            t.separated = true;
            r.separation_time = elapsed;
            t.separationCom = com;
            r.launch_velocity = toDd(velocity);
            r.launch_speed = velocity.Length();
            const float rising = velocity.Dot(mUp);
            r.launch_angle = std::atan2(rising, (velocity - mUp * rising).Length()) * kRadToDeg;
            r.launch_height = s.ground_height;
            r.apex_height = s.ground_height;
            r.carry_distance = horizontalDistance(t.impactCom, com, mUp);
            if (anchorAlive && !t.direction.IsNearZero())
            {
                const float frontNow = t.frontStart + JPH::Vec3(bodies.GetPosition(anchor->second.body) - t.anchorStart).Dot(t.direction);
                t.behindFrontAtSeparation = frontNow - JPH::Vec3(com).Dot(t.direction);
            }
        }
    }

    // --- Flight, first ground contact, rest.
    if (t.separated && !t.grounded)
    {
        r.apex_height = std::max(r.apex_height, s.ground_height);
        for (size_t i = 0; i < count && !t.grounded; ++i)
        {
            if ((c.contacts[i].flags & DD_CONTACT_GROUND) != 0)
            {
                t.grounded = true;
                r.ground_time = elapsed;
                r.ground_body = static_cast<int32_t>(i);
            }
        }
    }
    if (t.grounded && c.stillSeconds >= kRestSeconds)
    {
        r.finished = 1;
        r.rest_time = elapsed;
        r.flight_slide_distance = horizontalDistance(t.separationCom, com, mUp);
        r.throw_distance = horizontalDistance(t.impactCom, com, mUp);
        if (r.launch_speed > 0.0f && mGravity > 0.0f)
        {
            // Flight + slide for a launch speed u from height h, at launch angles of 0 to 45 degrees: the general
            // throw equation's least (horizontal launch) and greatest (tan angle = friction) values.
            const float u = r.launch_speed, mu = kRoadFriction, h = std::max(r.launch_height, 0.0f);
            r.corridor_min = mu * h + u * u / (2.0f * mu * mGravity);
            r.corridor_max = mu * h + u * u * (1.0f + mu * mu) / (2.0f * mu * mGravity);
            r.in_corridor = r.flight_slide_distance >= r.corridor_min && r.flight_slide_distance <= r.corridor_max ? 1 : 0;
        }
    }

    // --- The class the motion fits (ADR-0024 §5), once it has left the object.
    if (t.separated)
    {
        DdTrajectory measured = t.upperOnTop ? DD_TRAJECTORY_WRAP : DD_TRAJECTORY_FORWARD_PROJECTION;
        if (t.upperOnTop && t.front.valid != 0 && t.behindFrontAtSeparation > t.front.leading_edge_setback + t.front.bonnet_length + kRoofOvershoot)
        {
            measured = DD_TRAJECTORY_ROOF_VAULT;
        }
        if (!t.left.IsNearZero() && t.halfWidth > 0.0f && std::fabs(r.lateral_offset) > t.halfWidth - kCornerBand)
        {
            const JPH::Vec3 launch(r.launch_velocity.x, r.launch_velocity.y, r.launch_velocity.z);
            const JPH::Vec3 relative = launch - objectVelocity;
            if (std::fabs(relative.Dot(t.left)) > 0.5f * std::fabs(relative.Dot(t.direction)))
            {
                measured = DD_TRAJECTORY_FENDER_VAULT;
            }
        }
        if (measured == DD_TRAJECTORY_WRAP && std::fabs(r.pitch_degrees) > 270.0f)
        {
            measured = DD_TRAJECTORY_SOMERSAULT;
        }
        r.measured = measured;
    }
}
} // namespace dragdoll
