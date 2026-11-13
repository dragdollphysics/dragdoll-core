// SPDX-License-Identifier: MIT
// Rig definition: the data-driven description of a ragdoll (ADR-0010), parsed from JSON.
// Rig space: metres, +Y up, +Z forward, +X = character's left, origin = ground point between the feet.
#pragma once

#include <Jolt/Jolt.h>

#include <string>
#include <string_view>
#include <vector>

namespace dragdoll
{

struct RigShape
{
    enum class Type
    {
        Box,
        Capsule
    };
    Type type = Type::Box;
    JPH::Vec3 center = JPH::Vec3::sZero();      // Box
    JPH::Vec3 halfExtents = JPH::Vec3::sZero(); // Box
    JPH::Vec3 from = JPH::Vec3::sZero();        // Capsule: proximal end (overall extent)
    JPH::Vec3 to = JPH::Vec3::sZero();          // Capsule: distal end (overall extent)
    float radius = 0.0f;                        // Capsule
};

struct RigJoint
{
    enum class Type
    {
        SwingTwist,
        Hinge
    };
    Type type = Type::SwingTwist;
    JPH::Vec3 pivot = JPH::Vec3::sZero();
    // Swing-twist
    JPH::Vec3 twistAxis = JPH::Vec3::sAxisY(); // Child bone direction at rest
    JPH::Vec3 coneAxis = JPH::Vec3::sAxisY();  // Centre of the swing cone in the parent (defaults to twistAxis)
    float swingFwd = 0.0f;                     // Half-angle, radians: rotation about the lateral axis
    float swingSide = 0.0f;                    // Half-angle, radians: rotation about the forward axis
    float twistMin = 0.0f, twistMax = 0.0f;    // Radians
    // Hinge
    JPH::Vec3 axis = JPH::Vec3::sAxisX();
    float min = 0.0f, max = 0.0f; // Radians
    // Passive joint friction (N*m): the resistance of tissue and ligaments that damps a limp body. Must stay well
    // below the gravity torque on the limb, or the limb would no longer fall freely.
    float friction = 0.0f;
    // Muscle strength (N*m): the peak torque the joint's motors may use at full tension. 0 = no muscle (always limp).
    float strength = 0.0f;
};

struct RigBody
{
    std::string name;
    int parent = -1;
    float massPercent = 0.0f;
    bool collideWithParent = false; // Normally a body never collides with the body it hangs from.
    RigShape shape;
    RigJoint joint; // Unused for the root
};

struct Rig
{
    std::string name;
    float mass = 0.0f; // kg
    std::vector<RigBody> bodies; // Parents always precede their children.

    // Throws std::invalid_argument with a precise message on any problem.
    static Rig parse(std::string_view json);
};

// Built-in rigs compiled into the library (packs/rigs/*.json). nullptr if unknown.
const char* builtinRig(std::string_view name);
// Built-in skeleton maps (packs/skeletons/*.json). nullptr if unknown.
const char* builtinSkeletonMap(std::string_view name);

} // namespace dragdoll
