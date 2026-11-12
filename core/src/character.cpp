// SPDX-License-Identifier: MIT
#include "character.hpp"

#include "world.hpp"

#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Skeleton/Skeleton.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dragdoll
{
namespace
{
// Removes the component of `v` along unit vector `axis` and normalises; falls back to `fallback` if degenerate.
JPH::Vec3 perpendicular(JPH::Vec3Arg v, JPH::Vec3Arg axis, JPH::Vec3Arg fallback)
{
    JPH::Vec3 p = v - axis * v.Dot(axis);
    if (p.Length() < 1e-4f)
    {
        p = fallback - axis * fallback.Dot(axis);
    }
    return p.Normalized();
}

// Rotation whose local +Y is `boneDir` and whose local +X is as close as possible to `lateral`.
JPH::Quat boneRotation(JPH::Vec3Arg boneDir, JPH::Vec3Arg lateral, JPH::Vec3Arg forward)
{
    const JPH::Vec3 y = boneDir.Normalized();
    const JPH::Vec3 x = perpendicular(lateral, y, forward);
    const JPH::Vec3 z = x.Cross(y);
    JPH::Mat44 m(JPH::Vec4(x, 0), JPH::Vec4(y, 0), JPH::Vec4(z, 0), JPH::Vec4(0, 0, 0, 1));
    return m.GetQuaternion().Normalized();
}
} // namespace

JPH::Ref<JPH::RagdollSettings> buildRagdollSettings(const Rig& rig, JPH::Mat44Arg worldFromRig, float massScale,
                                                    uint32_t characterId, float& outTotalMass)
{
    auto toWorld = [&](JPH::Vec3Arg p) { return worldFromRig * p; };
    auto toWorldDir = [&](JPH::Vec3Arg d) { return worldFromRig.Multiply3x3(d).Normalized(); };
    const JPH::Quat rigRotation = worldFromRig.GetQuaternion().Normalized();
    const JPH::Vec3 lateral = toWorldDir(JPH::Vec3::sAxisX()); // Character's left
    const JPH::Vec3 forward = toWorldDir(JPH::Vec3::sAxisZ());
    const JPH::Vec3 down = toWorldDir(-JPH::Vec3::sAxisY());

    JPH::Ref<JPH::RagdollSettings> settings = new JPH::RagdollSettings();
    settings->mSkeleton = new JPH::Skeleton();
    settings->mParts.resize(rig.bodies.size());
    std::vector<JPH::Mat44> bodyMatrices(rig.bodies.size());

    outTotalMass = 0.0f;
    for (size_t i = 0; i < rig.bodies.size(); ++i)
    {
        const RigBody& body = rig.bodies[i];
        settings->mSkeleton->AddJoint(body.name, body.parent);

        JPH::RagdollSettings::Part& part = settings->mParts[i];
        JPH::Vec3 center;
        JPH::Quat rotation;
        JPH::Vec3 boneDir = down; // Used for hinge normals of children
        if (body.shape.type == RigShape::Type::Capsule)
        {
            const JPH::Vec3 a = toWorld(body.shape.from);
            const JPH::Vec3 b = toWorld(body.shape.to);
            const float length = (b - a).Length();
            const float halfCylinder = std::max(0.5f * length - body.shape.radius, 0.005f);
            center = 0.5f * (a + b);
            boneDir = (b - a).Normalized();
            rotation = boneRotation(boneDir, lateral, forward);
            part.SetShape(new JPH::CapsuleShape(halfCylinder, body.shape.radius));
        }
        else
        {
            center = toWorld(body.shape.center);
            rotation = rigRotation;
            const float convexRadius = std::min(JPH::cDefaultConvexRadius, body.shape.halfExtents.ReduceMin() * 0.5f);
            part.SetShape(new JPH::BoxShape(body.shape.halfExtents, convexRadius));
        }
        part.mPosition = JPH::RVec3(center);
        part.mRotation = rotation;
        part.mMotionType = JPH::EMotionType::Dynamic;
        part.mObjectLayer = layers::kMoving;
        part.mMotionQuality = JPH::EMotionQuality::LinearCast; // Fast impacts (ADR-0016)
        part.mFriction = 0.8f;
        part.mRestitution = 0.0f;
        part.mNumVelocityStepsOverride = 16;
        part.mNumPositionStepsOverride = 8; // Stiffer joint limits on violent impacts
        part.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        part.mMassPropertiesOverride.mMass = rig.mass * massScale * body.massPercent / 100.0f;
        part.mUserData = (1ull << 63) | (static_cast<uint64_t>(characterId) << 8) | static_cast<uint64_t>(i);
        outTotalMass += part.mMassPropertiesOverride.mMass;
        bodyMatrices[i] = JPH::Mat44::sRotationTranslation(rotation, center);

        if (body.parent < 0)
        {
            continue;
        }

        // Joint to the parent, expressed in world space at the rest pose.
        const RigJoint& joint = body.joint;
        const JPH::RVec3 pivot(toWorld(joint.pivot));
        if (joint.type == RigJoint::Type::SwingTwist)
        {
            JPH::Ref<JPH::SwingTwistConstraintSettings> c = new JPH::SwingTwistConstraintSettings();
            c->mSpace = JPH::EConstraintSpace::WorldSpace;
            c->mPosition1 = c->mPosition2 = pivot;
            c->mTwistAxis1 = toWorldDir(joint.coneAxis);  // Centre of the allowed swing, fixed to the parent
            c->mTwistAxis2 = toWorldDir(joint.twistAxis); // The child bone at rest
            // Plane axis = the character's lateral axis: rotation about it is forward/backward swing.
            c->mPlaneAxis1 = perpendicular(lateral, c->mTwistAxis1, forward);
            c->mPlaneAxis2 = perpendicular(lateral, c->mTwistAxis2, forward);
            c->mPlaneHalfConeAngle = joint.swingFwd;   // About the plane (lateral) axis: forward / backward
            c->mNormalHalfConeAngle = joint.swingSide; // About the normal axis: sideways
            c->mTwistMinAngle = joint.twistMin;
            c->mTwistMaxAngle = joint.twistMax;
            c->mDrawConstraintSize = 0.12f; // Debug overlay sized to a human joint
            part.mToParent = c;
        }
        else
        {
            JPH::Ref<JPH::HingeConstraintSettings> c = new JPH::HingeConstraintSettings();
            c->mSpace = JPH::EConstraintSpace::WorldSpace;
            c->mPoint1 = c->mPoint2 = pivot;
            c->mHingeAxis1 = c->mHingeAxis2 = toWorldDir(joint.axis);
            c->mNormalAxis1 = c->mNormalAxis2 = perpendicular(boneDir, c->mHingeAxis1, forward);
            c->mLimitsMin = joint.min;
            c->mLimitsMax = joint.max;
            c->mDrawConstraintSize = 0.12f; // Debug overlay sized to a human joint
            part.mToParent = c;
        }
    }

    settings->mSkeleton->CalculateParentJointIndices();
    // Neighbouring bones never collide; also any bodies that already touch in the rest pose.
    settings->DisableParentChildCollisions(bodyMatrices.data(), 0.005f);
    // Bodies that must not sink into their parent (e.g. upper arms vs chest) collide with it again. The rig must leave
    // a small gap between them at rest, otherwise they fight at the joint.
    for (size_t i = 0; i < rig.bodies.size(); ++i)
    {
        if (!rig.bodies[i].collideWithParent)
        {
            continue;
        }
        auto* table = static_cast<JPH::GroupFilterTable*>(
            const_cast<JPH::GroupFilter*>(settings->mParts[i].mCollisionGroup.GetGroupFilter()));
        table->EnableCollision(static_cast<JPH::CollisionGroup::SubGroupID>(i),
                               static_cast<JPH::CollisionGroup::SubGroupID>(rig.bodies[i].parent));
    }
    settings->Stabilize();
    settings->CalculateBodyIndexToConstraintIndex();
    settings->CalculateConstraintIndexToBodyIdxPair();

    // Stabilize may redistribute mass along chains: report what Jolt actually uses.
    outTotalMass = 0.0f;
    for (const JPH::RagdollSettings::Part& part : settings->mParts)
    {
        outTotalMass += part.mMassPropertiesOverride.mMass;
    }
    return settings;
}

} // namespace dragdoll
