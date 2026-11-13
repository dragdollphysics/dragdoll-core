// SPDX-License-Identifier: MIT
// Standing balance (ADR-0023): capture point (Pratt et al. 2006; Hof et al. 2005) kept over the feet with the ankle and
// hip strategies (Horak & Nashner 1986), by offsetting the muscles' joint targets. Internal torques only (ADR-0003).
#include "character.hpp"

#include <algorithm>
#include <cmath>

namespace dragdoll
{
namespace
{
constexpr float kDeg = 3.14159265f / 180.0f;
// Gains (tuned on hold_stance / bump_5kmh): joint angle per metre of capture-point error.
constexpr float kAnkleForward = 120.0f * kDeg; // rad/m
constexpr float kAnkleSide = 120.0f * kDeg;
constexpr float kAnkleMaxForward = 30.0f * kDeg, kAnkleMaxSide = 18.0f * kDeg;
constexpr float kHipThreshold = 0.05f;      // m: beyond this the hips join in
constexpr float kHip = 400.0f * kDeg;       // rad/m of error beyond the threshold
constexpr float kHipMax = 40.0f * kDeg;
// Sideways the hips dominate (Winter 1995, load/unload): abduct/adduct both hips to shift the pelvis over the feet.
constexpr float kHipSide = 150.0f * kDeg; // rad/m
constexpr float kHipSideMax = 15.0f * kDeg;
} // namespace

void updateBalance(DdCharacter& c, JPH::Vec3Arg up, float gravity, float dt)
{
    const bool active = c.balance && c.tension != DD_TENSION_LIMP && c.footLeft >= 0 && c.footRight >= 0 && gravity > 0.0f;
    auto release = [&] {
        // Not balancing (off, limp, or no longer standing): drop the corrections and the stance stiffness at once,
        // so a fallen body lies still instead of flailing to reach a support it no longer has.
        if (c.balanceActive || c.balanceError != 0.0f)
        {
            std::fill(c.balanceOffset.begin(), c.balanceOffset.end(), JPH::Quat::sIdentity());
            c.balanceError = 0.0f;
            c.balanceActive = false;
            c.musclesDirty = true;
        }
    };
    if (!active)
    {
        release();
        return;
    }
    // Centre of mass and its velocity over the last fixed step.
    JPH::Vec3 com = JPH::Vec3::sZero(), comBefore = JPH::Vec3::sZero();
    float mass = 0.0f;
    for (size_t i = 0; i < c.currPosition.size(); ++i)
    {
        com += JPH::Vec3(c.currPosition[i]) * c.bodyMass[i];
        comBefore += JPH::Vec3(c.prevPosition[i]) * c.bodyMass[i];
        mass += c.bodyMass[i];
    }
    com /= mass;
    comBefore /= mass;
    const JPH::Vec3 velocity = (com - comBefore) / dt;

    const JPH::Vec3 footL(c.currPosition[static_cast<size_t>(c.footLeft)]), footR(c.currPosition[static_cast<size_t>(c.footRight)]);
    const JPH::Vec3 support = (footL + footR) * 0.5f;
    const float rise = (com - support).Dot(up);
    // Standing? Centre of mass at least 70% of its standing height above the feet, and the trunk within ~45 degrees of
    // upright (pelvis rig +Y against world up).
    const JPH::Quat pelvisFrame = c.currRotation[0] * c.restRig[0].Conjugated();
    const bool standing = rise > 0.7f * c.standHeight && (pelvisFrame * JPH::Vec3::sAxisY()).Dot(up) > 0.7f;
    if (!standing)
    {
        release();
        return;
    }
    c.balanceActive = true;
    const float height = std::max(0.3f, rise + 0.04f); // Feet centres sit ~4 cm above the sole
    const float omega = std::sqrt(gravity / height);
    JPH::Vec3 error = com + velocity / omega - support; // Capture point - support centre
    error -= up * error.Dot(up);                         // Horizontal only

    // The character's own forward/left axes on the ground plane (from the pelvis: rig +Z forward, +X left).
    JPH::Vec3 forward = pelvisFrame * JPH::Vec3::sAxisZ();
    forward = (forward - up * forward.Dot(up)).NormalizedOr(JPH::Vec3::sAxisZ());
    const JPH::Vec3 left = up.Cross(forward);
    // Relative to the body's own standing point: the centre of mass sits above the ankles, not over the feet centres.
    // Not smoothed: even a 30 ms filter delays the loop enough to make the sideways response swing (a "waddle").
    const float eForward = error.Dot(forward) - c.standForward, eLeft = error.Dot(left) - c.standLeft;
    c.balanceError = std::sqrt(eForward * eForward + eLeft * eLeft);

    // Ankle strategy: capture point ahead -> toes down (+X in rig axes) pushes the body back; to the left -> rotate the
    // feet about -Z so the shins tilt right.
    const float ankleF = std::clamp(kAnkleForward * eForward, -kAnkleMaxForward, kAnkleMaxForward);
    const float ankleS = std::clamp(-kAnkleSide * eLeft, -kAnkleMaxSide, kAnkleMaxSide);
    const JPH::Quat ankle = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), ankleF) * JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), ankleS);
    c.balanceOffset[static_cast<size_t>(c.footLeft)] = ankle;
    c.balanceOffset[static_cast<size_t>(c.footRight)] = ankle;

    // Hip strategy: past the threshold, flex the hips when falling forward (trunk forward, hips back) and extend them
    // when falling back. Hip flexion is a rotation about -X in rig axes.
    const float excess = std::copysign(std::max(0.0f, std::fabs(eForward) - kHipThreshold), eForward);
    const float hip = std::clamp(kHip * excess, -kHipMax, kHipMax);
    // Sideways (load/unload): capture point to the left -> rotate both thighs about +Z so the feet move left relative
    // to the pelvis, i.e. with the feet planted the pelvis shifts right, over the support.
    const float hipSide = std::clamp(kHipSide * eLeft, -kHipSideMax, kHipSideMax);
    const JPH::Quat hips = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), -hip) * JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), hipSide);
    if (c.thighLeft >= 0) c.balanceOffset[static_cast<size_t>(c.thighLeft)] = hips;
    if (c.thighRight >= 0) c.balanceOffset[static_cast<size_t>(c.thighRight)] = hips;
    c.musclesDirty = true;
}
} // namespace dragdoll
