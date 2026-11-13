// SPDX-License-Identifier: MIT
// Muscles (Phase 3, task 3.1): joint motors that pull each joint towards the target pose, scaled by a tension
// profile. Internal torques only (ADR-0003): motors act between the two bodies of a joint.
#include "character.hpp"

#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <algorithm>
#include <stdexcept>

namespace dragdoll
{
MuscleParams tensionProfile(DdTension tension)
{
    switch (tension)
    {
    case DD_TENSION_LIMP: return {0.0f, 1.0f, 0.0f};
    case DD_TENSION_RELAXED: return {4.0f, 1.0f, 0.4f};
    case DD_TENSION_TENSE: return {6.0f, 0.9f, 1.0f};
    case DD_TENSION_STUNNED: return {3.0f, 0.35f, 0.3f};
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
        auto configure = [&](JPH::MotorSettings& motor) {
            motor.mSpringSettings.mMode = JPH::ESpringMode::FrequencyAndDamping;
            motor.mSpringSettings.mFrequency = m.frequency;
            motor.mSpringSettings.mDamping = m.damping;
            motor.SetTorqueLimit(torque);
        };
        if (c.hinge[i])
        {
            auto* hinge = static_cast<JPH::HingeConstraint*>(constraint);
            configure(hinge->GetMotorSettings());
            hinge->SetTargetAngle(0.0f); // The hinge angle is measured from the rest pose
            hinge->SetMotorState(state);
        }
        else
        {
            auto* joint = static_cast<JPH::SwingTwistConstraint*>(constraint);
            configure(joint->GetSwingMotorSettings());
            configure(joint->GetTwistMotorSettings());
            joint->SetTargetOrientationBS(c.restTarget[i]);
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
