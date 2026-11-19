// SPDX-License-Identifier: MIT
// Aim and named poses (ADR-0025 §1, task 4.5b): ways to say what the muscles should pull towards without writing
// quaternions by hand.
//  * aimLimb: "point this arm / leg at that world position, this much extended" -> targets for the limb's two main
//    joints (shoulder + elbow, hip + knee). An analytic two-bone solve in the frame of the limb's parent body. It only
//    sets targets: the muscles still have to get there, against gravity, contacts and the joints' own limits.
//  * applyNamedPose: a pose from a behaviour pack (per body: degrees about the rig's X, Y, Z axes, offsets from rest).
#include "behaviour_host.hpp"
#include "character.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dragdoll
{
namespace
{
constexpr float kDegToRad = 3.14159265f / 180.0f;

float angleBetween(JPH::Vec3Arg a, JPH::Vec3Arg b)
{
    return std::acos(std::clamp(a.Dot(b) / std::max(a.Length() * b.Length(), 1e-9f), -1.0f, 1.0f));
}
} // namespace

bool aimLimb(DdCharacter& c, DdRegion region, JPH::RVec3Arg target, float extension)
{
    // The limb: its first body (upper arm / thigh), the next one (forearm / shin) and the last (hand / foot).
    int root = -1, mid = -1, tip = -1;
    const int count = static_cast<int>(c.parent.size());
    for (int i = 0; i < count; ++i)
    {
        if (c.region[static_cast<size_t>(i)] == region && c.parent[static_cast<size_t>(i)] >= 0 &&
            c.region[static_cast<size_t>(c.parent[static_cast<size_t>(i)])] != region)
        {
            root = i;
            break;
        }
    }
    for (int i = 0; i < count && root >= 0 && mid < 0; ++i)
    {
        if (c.parent[static_cast<size_t>(i)] == root && c.region[static_cast<size_t>(i)] == region) mid = i;
    }
    if (root < 0 || mid < 0 || c.hinge[static_cast<size_t>(root)])
    {
        return false; // Not a two-joint limb with a free first joint (head, trunk, or an unusual rig)
    }
    tip = mid;
    for (int i = 0; i < count; ++i)
    {
        if (c.parent[static_cast<size_t>(i)] == mid && c.region[static_cast<size_t>(i)] == region) tip = i;
    }
    const size_t r = static_cast<size_t>(root), m = static_cast<size_t>(mid), p = static_cast<size_t>(c.parent[r]);

    // Rest geometry in rig space: first joint S, second joint E, end of the limb H (centre of its last body).
    const JPH::Vec3 upper = c.pivotRig[m] - c.pivotRig[r];
    const JPH::Vec3 lower = c.centreRig[static_cast<size_t>(tip)] - c.pivotRig[m];
    const float a = upper.Length(), b = lower.Length();
    if (a < 1e-3f || b < 1e-3f)
    {
        return false;
    }
    // The target as the parent body carries the rig's axes (the frame joint targets are expressed in).
    const JPH::Quat carried = c.currRotation[p] * c.restRig[p].Conjugated();
    const JPH::RVec3 first = c.currPosition[p] + c.currRotation[p] * c.pivotInParent[r];
    const JPH::Vec3 toTarget = carried.Conjugated() * JPH::Vec3(target - first);
    const float distance = toTarget.Length();
    if (!(distance > 1e-4f) || !std::isfinite(distance))
    {
        return false;
    }
    const float reach = std::clamp(std::min(distance, std::clamp(extension, 0.0f, 1.0f) * (a + b)), std::fabs(a - b) + 0.01f, a + b);

    // Second joint: bend until the end of the limb is `reach` away from the first joint.
    const float wanted = std::acos(std::clamp((a * a + b * b - reach * reach) / (2.0f * a * b), -1.0f, 1.0f)); // Interior angle
    JPH::Vec3 axis = c.hinge[m] ? c.hingeAxisRig[m] : upper.Cross(lower);
    if (axis.LengthSq() < 1e-8f)
    {
        axis = upper.GetNormalizedPerpendicular(); // Straight limb, free second joint: any axis across the bone
    }
    axis = axis.Normalized();
    float low = -3.14159265f, high = 3.14159265f;
    if (c.hinge[m])
    {
        low = c.hingeMin[m];
        high = c.hingeMax[m];
    }
    const auto interior = [&](float angle) { return angleBetween(-upper, JPH::Quat::sRotation(axis, angle) * lower); };
    // Flexion is the side on which the joint has more room; search it for the angle that gives the wanted bend.
    const float side = high >= -low ? high : low;
    float from = 0.0f, to = side;
    float bend = 0.0f;
    if (wanted >= interior(0.0f))
    {
        bend = 0.0f; // Already at least as bent as needed at rest: leave it
    }
    else if (wanted <= interior(side))
    {
        bend = side; // More than the joint allows
    }
    else
    {
        for (int i = 0; i < 24; ++i)
        {
            bend = 0.5f * (from + to);
            (interior(bend) > wanted ? from : to) = bend;
        }
    }

    // First joint. A reach (end at least 85 % of the limb's length away): turn the bent limb by the shortest arc from
    // rest so that its end points at the target (reaching for the ground, fending). A fold (end closer): place it so
    // that its end lies on the target AND its middle joint points the natural way (elbows down and out, knees
    // forward) - a person folding an arm onto the face or across the belly lifts the elbow out to the side; the
    // shortest turn left the bend plane wherever rest had it and folded the forearm into the head or the trunk (25 cm
    // short of the forehead, founder's review 2026-11-19).
    const JPH::Vec3 end = upper + JPH::Quat::sRotation(axis, bend) * lower;
    if (end.LengthSq() < 1e-8f)
    {
        return false;
    }
    const JPH::Vec3 t = toTarget / distance;
    const float endLength = end.Length();
    // Pole: where the middle joint should point. Arms: down along the hanging arm and out to its side; legs: forward.
    const bool arm = region == DD_REGION_ARM_LEFT || region == DD_REGION_ARM_RIGHT;
    const JPH::Vec3 outward(c.pivotRig[r].GetX() >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
    JPH::Vec3 pole = arm ? upper.Normalized() + outward * 0.6f : JPH::Vec3(0.0f, 0.0f, 1.0f);
    pole -= t * pole.Dot(t);
    JPH::Quat shoulder = JPH::Quat::sFromTo(end / endLength, t); // The shortest turn (reaches, and the fallback)
    if (reach < 0.85f * (a + b) && pole.LengthSq() > 1e-6f)
    {
        pole = pole.Normalized();
        // In the rest-bent limb, the middle joint's offset from the line first joint -> end.
        const JPH::Vec3 e0 = end / endLength;
        JPH::Vec3 off0 = upper - e0 * upper.Dot(e0);
        if (off0.LengthSq() > 1e-8f)
        {
            off0 = off0.Normalized();
            // Rotation taking the rest frame (e0, off0) onto the wanted frame (t, pole).
            const JPH::Vec3 s0 = e0.Cross(off0), s1 = t.Cross(pole);
            const JPH::Mat44 restFrame(JPH::Vec4(e0, 0), JPH::Vec4(off0, 0), JPH::Vec4(s0, 0), JPH::Vec4(0, 0, 0, 1));
            const JPH::Mat44 wantedFrame(JPH::Vec4(t, 0), JPH::Vec4(pole, 0), JPH::Vec4(s1, 0), JPH::Vec4(0, 0, 0, 1));
            shoulder = (wantedFrame * restFrame.Transposed3x3()).GetQuaternion();
        }
    }
    c.targetPose[r] = shoulder.Normalized();
    c.targetPose[m] = JPH::Quat::sRotation(axis, bend);
    c.musclesDirty = true;
    c.ragdoll->Activate(); // A body at rest is asleep and would ignore the new targets
    return true;
}

void applyNamedPose(DdCharacter& c, const ParamSet& pack, const std::string& pose, uint32_t regionMask)
{
    const std::map<std::string, std::array<float, 3>>* entries = nullptr;
    if (pose != "rest")
    {
        const auto it = pack.poses.find(pose);
        if (it == pack.poses.end())
        {
            throw std::invalid_argument("unknown pose \"" + pose + "\"");
        }
        entries = &it->second;
    }
    for (size_t i = 0; i < c.parent.size(); ++i)
    {
        if (c.parent[i] < 0 || (regionMask & (1u << c.region[i])) == 0)
        {
            continue;
        }
        JPH::Quat q = JPH::Quat::sIdentity();
        if (entries != nullptr)
        {
            const auto entry = entries->find(c.names[i]);
            if (entry != entries->end())
            {
                // Degrees about the rig's X (left), then Y (up), then Z (forward).
                q = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), entry->second[2] * kDegToRad) *
                    JPH::Quat::sRotation(JPH::Vec3::sAxisY(), entry->second[1] * kDegToRad) *
                    JPH::Quat::sRotation(JPH::Vec3::sAxisX(), entry->second[0] * kDegToRad);
            }
        }
        c.targetPose[i] = q;
    }
    c.musclesDirty = true;
    c.ragdoll->Activate(); // A body at rest is asleep and would ignore the new targets
}
} // namespace dragdoll
