// SPDX-License-Identifier: MIT
// Activation from motion (Phase 3, task 3.3, ADR-0017): a pose read with get_pose places the body exactly back, and a
// walking character handed over between two host frames keeps its momentum (acceptance: within 5%).
#include <dragdoll/dragdoll.h>

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

DdQuat axisAngle(float x, float y, float z, float degrees)
{
    const float half = degrees * 3.14159265f / 360.0f, s = std::sin(half);
    return DdQuat{x * s, y * s, z * s, std::cos(half)};
}

struct Scene
{
    DdWorld* world = nullptr;
    explicit Scene(float gravity = 9.81f)
    {
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.up = DdVec3{0, 0, 1};
        desc.gravity = gravity;
        desc.worker_threads = 0;
        REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
        DdColliderDesc ground;
        dragdoll_collider_desc_default(&ground);
        ground.shape = DD_SHAPE_BOX;
        ground.half_extents = DdVec3{50, 50, 0.5f};
        ground.transform.position = DdVec3{0, 0, -0.5f};
        DdColliderId id;
        REQUIRE(dragdoll_world_add_colliders(world, &ground, 1, &id) == DD_OK);
    }
    ~Scene() { dragdoll_world_destroy(world); }
    DdCharacter* human(DdVec3 at = DdVec3{0, 0, 0.01f})
    {
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
        cd.transform.position = at;
        DdCharacter* c = nullptr;
        REQUIRE(dragdoll_character_create(world, &cd, &c) == DD_OK);
        return c;
    }
};

std::vector<DdTransform> bodies(DdCharacter* c)
{
    std::vector<DdTransform> t(dragdoll_character_get_body_count(c));
    REQUIRE(dragdoll_character_get_body_transforms(c, 0, t.data(), static_cast<uint32_t>(t.size())) == DD_OK);
    return t;
}

DdVec3 centreOfMass(DdCharacter* c)
{
    const std::vector<DdTransform> t = bodies(c);
    DdVec3 sum{0, 0, 0};
    float mass = 0.0f;
    for (uint32_t i = 0; i < t.size(); ++i)
    {
        const float m = dragdoll_character_get_body_mass(c, i);
        sum.x += m * t[i].position.x;
        sum.y += m * t[i].position.y;
        sum.z += m * t[i].position.z;
        mass += m;
    }
    return DdVec3{sum.x / mass, sum.y / mass, sum.z / mass};
}

// A synthetic walk (host side): the root moves along the character's forward axis at 1.4 m/s, legs and arms swing.
struct Walk
{
    DdTransform restRoot;
    DdVec3 forward;
    std::vector<int32_t> thigh, shin, arm;
    uint32_t count = 0;
    explicit Walk(DdCharacter* c)
    {
        count = dragdoll_character_get_body_count(c);
        REQUIRE(dragdoll_character_get_pose(c, 0, nullptr, 0, &restRoot) == DD_OK);
        // Rig +Z (forward) in world space: the pelvis box keeps the rig orientation.
        const DdQuat& q = restRoot.rotation;
        forward = DdVec3{2.0f * (q.x * q.z + q.w * q.y), 2.0f * (q.y * q.z - q.w * q.x), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)};
        thigh = {dragdoll_character_find_body(c, "thigh_l"), dragdoll_character_find_body(c, "thigh_r")};
        shin = {dragdoll_character_find_body(c, "shin_l"), dragdoll_character_find_body(c, "shin_r")};
        arm = {dragdoll_character_find_body(c, "upper_arm_l"), dragdoll_character_find_body(c, "upper_arm_r")};
    }
    void at(double t, DdTransform& root, std::vector<DdQuat>& joints) const
    {
        constexpr double kSpeed = 1.4, kStride = 2.0 * 3.14159265 * 0.9;
        root = restRoot;
        root.position.x += forward.x * static_cast<float>(kSpeed * t);
        root.position.y += forward.y * static_cast<float>(kSpeed * t);
        joints.assign(count, DdQuat{0, 0, 0, 1});
        for (int side = 0; side < 2; ++side)
        {
            const float phase = static_cast<float>(std::sin(kStride * t + side * 3.14159265));
            joints[static_cast<size_t>(thigh[side])] = axisAngle(1, 0, 0, -25.0f * phase);
            joints[static_cast<size_t>(shin[side])] = axisAngle(1, 0, 0, 20.0f + 20.0f * phase);
            joints[static_cast<size_t>(arm[side])] = axisAngle(1, 0, 0, 20.0f * phase);
        }
    }
};
} // namespace

TEST_CASE("activate: a pose read with get_pose places every body exactly back (forward kinematics round trip)")
{
    Library lib;
    Scene s;
    DdCharacter* fallen = s.human(DdVec3{0, 0, 1.0f});
    dragdoll_world_step_fixed(s.world, 150); // Mid-crumple: a far-from-rest pose
    DdTransform root;
    std::vector<DdQuat> joints(dragdoll_character_get_body_count(fallen));
    REQUIRE(dragdoll_character_get_pose(fallen, 0, joints.data(), static_cast<uint32_t>(joints.size()), &root) == DD_OK);
    const std::vector<DdTransform> expected = bodies(fallen);

    DdCharacter* copy = s.human(DdVec3{5, 0, 0.01f});
    REQUIRE(dragdoll_character_activate(copy, nullptr, nullptr, &root, joints.data(), static_cast<uint32_t>(joints.size()), 0.0f) == DD_OK);
    const std::vector<DdTransform> placed = bodies(copy);
    for (size_t i = 0; i < placed.size(); ++i)
    {
        const DdVec3 &a = placed[i].position, &b = expected[i].position;
        // 5 mm: a simulated body's joints stretch by a few mm under load (the solver is never perfectly rigid); placing
        // from a pose rebuilds exact joints, so that stretch is not reproduced. Rotations must match closely.
        CHECK(std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)) < 0.005f);
        const DdQuat &p = placed[i].rotation, &q = expected[i].rotation;
        CHECK(2.0f * std::acos(std::fmin(1.0f, std::fabs(p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w))) * 57.29578f < 0.2f);
    }
}

TEST_CASE("activate: a walking character handed over between two host frames keeps its momentum within 5%")
{
    Library lib;
    for (float hostFps : {30.0f, 60.0f, 144.0f})
    {
        CAPTURE(hostFps);
        Scene s;
        DdCharacter* walker = s.human();
        DdCharacter* probe = s.human(DdVec3{0, 5, 0.01f}); // Host-side reference: where the bodies were one frame ago
        const Walk walk(walker);
        const float dt = 1.0f / hostFps;
        const double t = 0.43; // Mid-stride
        DdTransform before, now;
        std::vector<DdQuat> jointsBefore, jointsNow;
        walk.at(t - dt, before, jointsBefore);
        walk.at(t, now, jointsNow);
        REQUIRE(dragdoll_character_activate(walker, &before, jointsBefore.data(), &now, jointsNow.data(), walk.count, dt) == DD_OK);
        DdTransform probeRoot = before;
        probeRoot.position.y += 5.0f;
        REQUIRE(dragdoll_character_activate(probe, nullptr, nullptr, &probeRoot, jointsBefore.data(), walk.count, 0.0f) == DD_OK);
        DdVec3 comBefore = centreOfMass(probe);
        comBefore.y -= 5.0f;
        const DdVec3 comNow = centreOfMass(walker);
        // The host's centre-of-mass velocity (horizontal), from the two frames it handed over.
        const float hx = (comNow.x - comBefore.x) / dt, hy = (comNow.y - comBefore.y) / dt;

        dragdoll_world_step_fixed(s.world, 1);
        const DdVec3 comAfter = centreOfMass(walker);
        const float vx = (comAfter.x - comNow.x) * 120.0f, vy = (comAfter.y - comNow.y) * 120.0f;
        const float host = std::sqrt(hx * hx + hy * hy), sim = std::sqrt(vx * vx + vy * vy);
        MESSAGE(hostFps << " fps host: centre of mass " << host << " m/s handed over, " << sim << " m/s in the first step");
        CHECK(host > 1.2f);
        CHECK(std::fabs(sim - host) < 0.05f * host);
        CHECK(vx * hx + vy * hy > 0.0f); // Same direction
    }
}

TEST_CASE("activate: input validation")
{
    Library lib;
    Scene s;
    DdCharacter* c = s.human();
    DdTransform root;
    REQUIRE(dragdoll_character_get_pose(c, 0, nullptr, 0, &root) == DD_OK);
    std::vector<DdQuat> joints(dragdoll_character_get_body_count(c), DdQuat{0, 0, 0, 1});
    CHECK(dragdoll_character_activate(c, nullptr, nullptr, nullptr, nullptr, 0, 0.0f) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_character_activate(c, nullptr, nullptr, &root, joints.data(), 3, 0.0f) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_character_activate(c, &root, nullptr, &root, nullptr, 0, 0.0f) == DD_ERROR_INVALID_ARGUMENT); // dt 0
    CHECK(dragdoll_character_activate(c, nullptr, nullptr, &root, nullptr, 0, 0.0f) == DD_OK); // Rest pose, no velocity
    CHECK(dragdoll_character_get_body_mass(c, 0) > 5.0f);
    CHECK(dragdoll_character_get_body_mass(c, 99) == 0.0f);
}

TEST_CASE("activate: the muscles hold the handed-over pose, not the rest pose (founder: tense walker snapped straight)")
{
    Library lib;
    Scene s(0.0f); // Zero gravity: only the muscles act
    DdCharacter* c = s.human(DdVec3{0, 0, 1.0f});
    DdTransform root;
    REQUIRE(dragdoll_character_get_pose(c, 0, nullptr, 0, &root) == DD_OK);
    std::vector<DdQuat> stride(dragdoll_character_get_body_count(c), DdQuat{0, 0, 0, 1});
    const uint32_t knee = static_cast<uint32_t>(dragdoll_character_find_body(c, "shin_l"));
    stride[knee] = axisAngle(1, 0, 0, 50);
    REQUIRE(dragdoll_character_activate(c, nullptr, nullptr, &root, stride.data(), static_cast<uint32_t>(stride.size()), 0.0f) == DD_OK);
    REQUIRE(dragdoll_character_set_tension(c, DD_TENSION_TENSE, 0.0f) == DD_OK);
    dragdoll_world_step_fixed(s.world, 240);
    std::vector<DdQuat> out(stride.size());
    REQUIRE(dragdoll_character_get_pose(c, 0, out.data(), static_cast<uint32_t>(out.size()), nullptr) == DD_OK);
    const DdQuat &a = out[knee], &b = stride[knee];
    CHECK(2.0f * std::acos(std::fmin(1.0f, std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w))) * 57.29578f < 2.0f);
}
