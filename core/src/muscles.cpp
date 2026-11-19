// SPDX-License-Identifier: MIT
// Muscles (Phase 3, task 3.1): joint motors that pull each joint towards the target pose, scaled by a tension
// profile. Internal torques only (ADR-0003): motors act between the two bodies of a joint.
#include "character.hpp"

#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>

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
    case DD_TENSION_BRACED: return {3.0f, 1.0f, 0.5f}; // [ours] between relaxed and tense (task 4.9)
    default: throw std::invalid_argument("unknown tension profile");
    }
}

void setRegionTension(DdCharacter& c, DdRegion region, DdTension tension, float blendSeconds)
{
    DdCharacter::RegionMuscles& r = c.regionMuscles.at(static_cast<size_t>(region));
    r.to = tensionProfile(tension);
    r.from = r.now;
    r.time = std::max(0.0f, blendSeconds);
    r.elapsed = 0.0f;
    r.tension = tension;
    c.musclesDirty = true;
    c.ragdoll->Activate(); // Sleeping bodies would ignore the new drive
}

void setTension(DdCharacter& c, DdTension tension, float blendSeconds)
{
    tensionProfile(tension); // Validates before anything changes
    for (int region = 0; region < DD_REGION_COUNT; ++region)
    {
        setRegionTension(c, static_cast<DdRegion>(region), tension, blendSeconds);
    }
    c.tension = tension;
}

void assignRegions(DdCharacter& c)
{
    c.region.assign(c.names.size(), static_cast<uint8_t>(DD_REGION_TRUNK));
    for (size_t i = 0; i < c.names.size(); ++i)
    {
        std::string name = c.names[i];
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        const auto has = [&](const char* word) { return name.find(word) != std::string::npos; };
        const bool left = has("left") || (name.size() >= 2 && name.compare(name.size() - 2, 2, "_l") == 0);
        if (has("arm") || has("hand") || has("shoulder") || has("clavicle"))
        {
            c.region[i] = static_cast<uint8_t>(left ? DD_REGION_ARM_LEFT : DD_REGION_ARM_RIGHT);
        }
        else if (has("thigh") || has("shin") || has("calf") || has("foot") || has("toe") || has("leg"))
        {
            c.region[i] = static_cast<uint8_t>(left ? DD_REGION_LEG_LEFT : DD_REGION_LEG_RIGHT);
        }
        else if (has("head") || has("neck"))
        {
            c.region[i] = static_cast<uint8_t>(DD_REGION_HEAD_NECK);
        }
    }
}

namespace
{
void applyMuscles(DdCharacter& c)
{
    for (size_t i = 0; i < c.parent.size(); ++i)
    {
        if (c.parent[i] < 0)
        {
            continue;
        }
        const MuscleParams& m = c.regionMuscles[c.region[i]].now;
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
    bool blending = false;
    for (DdCharacter::RegionMuscles& r : c.regionMuscles)
    {
        if (r.elapsed < r.time)
        {
            r.elapsed = std::min(r.time, r.elapsed + dt);
            c.musclesDirty = true;
        }
        blending = blending || r.elapsed < r.time;
    }
    if (!c.musclesDirty)
    {
        return;
    }
    for (DdCharacter::RegionMuscles& r : c.regionMuscles)
    {
        const float t = r.time > 0.0f ? r.elapsed / r.time : 1.0f;
        r.now.frequency = lerp(r.from.frequency, r.to.frequency, t);
        r.now.damping = lerp(r.from.damping, r.to.damping, t);
        r.now.share = lerp(r.from.share, r.to.share, t);
        // Limp has no spring. Blending into or out of it ramps the stiffness through a very soft spring instead (a
        // frequency of 0 would mean a RIGID joint in Jolt), so the blend is felt even when torques stay below the limit.
        constexpr float kSoftest = 0.5f; // Hz
        if (r.from.share == 0.0f)
        {
            r.now.frequency = lerp(kSoftest, r.to.frequency, t);
            r.now.damping = r.to.damping;
        }
        else if (r.to.share == 0.0f)
        {
            r.now.frequency = lerp(r.from.frequency, kSoftest, t);
            r.now.damping = r.from.damping;
        }
    }
    applyMuscles(c);
    c.musclesDirty = blending;
}
} // namespace dragdoll
