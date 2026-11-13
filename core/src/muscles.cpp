// SPDX-License-Identifier: MIT
// Muscles (Phase 3, task 3.1): joint motors that pull each joint towards the target pose, scaled by a tension
// profile. Internal torques only (ADR-0003): motors act between the two bodies of a joint.
#include "character.hpp"

#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dragdoll
{
MuscleParams tensionProfile(DdTension tension)
{
    switch (tension)
    {
    case DD_TENSION_LIMP: return {0.0f, 1.0f, 0.0f};
    case DD_TENSION_RELAXED: return {1.2f, 1.0f, 0.15f};
    case DD_TENSION_TENSE: return {8.0f, 2.0f, 1.0f};
    case DD_TENSION_STUNNED: return {0.6f, 0.3f, 0.3f};
    default: throw std::invalid_argument("unknown tension profile");
    }
}

void setTension(DdCharacter& c, DdTension tension, float blendSeconds)
{
    c.blendTo = tensionProfile(tension);
    c.blendFrom = c.muscles;
    c.blendTime = std::max(0.0f, blendSeconds);
    c.blendElapsed = 0.0f;
    c.tension = tension;
    c.musclesDirty = true;
    c.ragdoll->Activate(); // Sleeping bodies would ignore the new drive
}

namespace
{
void applyMuscles(DdCharacter& c)
{
    const MuscleParams& m = c.muscles;
    for (size_t i = 0; i < c.parent.size(); ++i)
    {
        if (c.parent[i] < 0)
        {
            continue;
        }
        JPH::TwoBodyConstraint* constraint = c.ragdoll->GetConstraint(c.settings->GetConstraintIndexForBodyIndex(static_cast<int>(i)));
        const float torque = c.strength[i] * m.share;
        const JPH::EMotorState state = torque > 0.0f ? JPH::EMotorState::Position : JPH::EMotorState::Off;
        // ADR-0009 units, applied to the joint's real (effective) inertia: k = I w^2, c = 2 zeta I w.
        const float omega = 2.0f * 3.14159265f * m.frequency;
        // Balancing: the planted legs carry the body above them (inverted pendulum), so size them by that load.
        const bool stance = c.balanceActive && c.stanceJoint[i];
        const float inertia = stance ? c.loadInertia[i] : c.jointInertia[i];
        auto configure = [&](JPH::MotorSettings& motor) {
            motor.mSpringSettings.mMode = JPH::ESpringMode::StiffnessAndDamping;
            motor.mSpringSettings.mStiffness = inertia * omega * omega;
            motor.mSpringSettings.mDamping = 2.0f * m.damping * inertia * omega;
            motor.SetTorqueLimit(torque);
        };
        const JPH::Quat d = c.balanceOffset[i] * c.targetPose[i]; // Offset from rest, rig axes (+ balance)
        if (c.hinge[i])
        {
            auto* hinge = static_cast<JPH::HingeConstraint*>(constraint);
            configure(hinge->GetMotorSettings());
            // Only the rotation about the hinge axis counts (its twist component); the angle is measured from rest.
            const float along = d.GetXYZ().Dot(c.hingeAxisRig[i]);
            hinge->SetTargetAngle(2.0f * std::atan2(d.GetW() < 0.0f ? -along : along, std::abs(d.GetW())));
            hinge->SetMotorState(state);
        }
        else
        {
            auto* joint = static_cast<JPH::SwingTwistConstraint*>(constraint);
            configure(joint->GetSwingMotorSettings());
            configure(joint->GetTwistMotorSettings());
            // Child rotation relative to the parent (body space): conj(P_parent) * d * P_child, P = rest in rig space.
            joint->SetTargetOrientationBS(c.restRig[static_cast<size_t>(c.parent[i])].Conjugated() * d * c.restRig[i]);
            joint->SetSwingMotorState(state);
            joint->SetTwistMotorState(state);
        }
    }
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}
} // namespace

void updateMuscles(DdCharacter& c, float dt)
{
    if (c.blendElapsed < c.blendTime)
    {
        c.blendElapsed = std::min(c.blendTime, c.blendElapsed + dt);
        c.musclesDirty = true;
    }
    if (!c.musclesDirty)
    {
        return;
    }
    const float t = c.blendTime > 0.0f ? c.blendElapsed / c.blendTime : 1.0f;
    c.muscles.frequency = lerp(c.blendFrom.frequency, c.blendTo.frequency, t);
    c.muscles.damping = lerp(c.blendFrom.damping, c.blendTo.damping, t);
    c.muscles.share = lerp(c.blendFrom.share, c.blendTo.share, t);
    // Limp has no spring. Blending into or out of it ramps the stiffness through a very soft spring instead (a
    // frequency of 0 would mean a RIGID joint in Jolt), so the blend is felt even when torques stay below the limit.
    constexpr float kSoftest = 0.5f; // Hz
    if (c.blendFrom.share == 0.0f)
    {
        c.muscles.frequency = lerp(kSoftest, c.blendTo.frequency, t);
        c.muscles.damping = c.blendTo.damping;
    }
    else if (c.blendTo.share == 0.0f)
    {
        c.muscles.frequency = lerp(c.blendFrom.frequency, kSoftest, t);
        c.muscles.damping = c.blendFrom.damping;
    }
    applyMuscles(c);
    c.musclesDirty = c.blendElapsed < c.blendTime;
}
} // namespace dragdoll
