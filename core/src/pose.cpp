// SPDX-License-Identifier: MIT
// Host contract (D17, Phase 3 task 3.2): target pose input, pose output in the same form, and the character state with
// recovery hints. Joint rotations are offsets from the rest pose, in rig axes, relative to the parent (see dragdoll.h).
#include "character.hpp"

#include <cmath>
#include <stdexcept>

namespace dragdoll
{
void setTargetPose(DdCharacter& c, const DdQuat* jointRotations, uint32_t count)
{
    if (jointRotations == nullptr)
    {
        c.targetPose.assign(c.targetPose.size(), JPH::Quat::sIdentity());
    }
    else
    {
        if (count != c.targetPose.size())
        {
            throw std::invalid_argument("set_target_pose: count must equal the body count");
        }
        std::vector<JPH::Quat> pose(count, JPH::Quat::sIdentity());
        for (uint32_t i = 1; i < count; ++i) // Entry 0 (the root) is ignored
        {
            const DdQuat& q = jointRotations[i];
            const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
            if (!std::isfinite(length) || length < 1e-6f)
            {
                throw std::invalid_argument("set_target_pose: joint rotations must be finite, non-zero quaternions");
            }
            pose[i] = JPH::Quat(q.x / length, q.y / length, q.z / length, q.w / length);
        }
        c.targetPose = std::move(pose);
    }
    c.musclesDirty = true;
    c.ragdoll->Activate();
}

void jointRotations(const DdCharacter& c, const std::vector<JPH::Quat>& world, DdQuat* out)
{
    for (size_t i = 0; i < world.size(); ++i)
    {
        if (c.parent[i] < 0)
        {
            out[i] = DdQuat{0, 0, 0, 1};
            continue;
        }
        const size_t p = static_cast<size_t>(c.parent[i]);
        // d = P_parent * (R_parent^-1 * R_child) * P_child^-1   (inverse of the input mapping)
        JPH::Quat d = (c.restRig[p] * world[p].Conjugated() * world[i] * c.restRig[i].Conjugated()).Normalized();
        if (d.GetW() < 0.0f)
        {
            d = -d; // Canonical: shortest-arc sign
        }
        out[i] = DdQuat{d.GetX(), d.GetY(), d.GetZ(), d.GetW()};
    }
}

void updateState(DdCharacter& c, float dt)
{
    // "Nearly still": every body slower than 5 cm/s and 10 deg/s.
    constexpr float kLinear = 0.05f, kAngular = 10.0f * 3.14159265f / 180.0f;
    bool still = true;
    for (size_t i = 0; i < c.currPosition.size() && still; ++i)
    {
        const float linear = static_cast<float>((c.currPosition[i] - c.prevPosition[i]).Length()) / dt;
        const float dot = std::min(1.0f, std::abs(c.prevRotation[i].Dot(c.currRotation[i])));
        const float angular = 2.0f * std::acos(dot) / dt;
        still = linear < kLinear && angular < kAngular;
    }
    c.stillSeconds = still ? c.stillSeconds + dt : 0.0f;
    if (c.state == DD_STATE_ACTIVE || c.state == DD_STATE_SETTLED)
    {
        c.state = c.stillSeconds >= 0.5f ? DD_STATE_SETTLED : DD_STATE_ACTIVE;
    }
}
} // namespace dragdoll
