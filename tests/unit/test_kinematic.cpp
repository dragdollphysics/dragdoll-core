// SPDX-License-Identifier: MIT
// Kinematic characters (host-animated, e.g. the player): no gravity, not pushed, but they push ragdolls; switching
// back to dynamic keeps the motion.
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

struct Scene
{
    DdWorld* world = nullptr;
    Scene()
    {
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.up = DdVec3{0, 0, 1};
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
    DdCharacter* human(DdVec3 at)
    {
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid18");
        cd.transform.position = at;
        DdCharacter* c = nullptr;
        REQUIRE(dragdoll_character_create(world, &cd, &c) == DD_OK);
        return c;
    }
};

DdTransform root(DdCharacter* c)
{
    DdTransform r{};
    REQUIRE(dragdoll_character_get_pose(c, 0, nullptr, 0, &r) == DD_OK);
    return r;
}
} // namespace

TEST_CASE("kinematic: a host-animated character hangs in the air (no gravity) and is moved only by activate")
{
    Library lib;
    Scene s;
    DdCharacter* c = s.human(DdVec3{0, 0, 1.0f}); // 1 m above the ground
    REQUIRE(dragdoll_character_set_kinematic(c, 1) == DD_OK);
    CHECK(dragdoll_character_get_kinematic(c) == 1);
    const float z = root(c).position.z;
    dragdoll_world_step_fixed(s.world, 120);
    CHECK(root(c).position.z == doctest::Approx(z).epsilon(0.001)); // Did not fall
    DdTransform moved = root(c);
    moved.position.x += 0.5f;
    REQUIRE(dragdoll_character_activate(c, nullptr, nullptr, &moved, nullptr, 0, 0.0f) == DD_OK);
    dragdoll_world_step_fixed(s.world, 10);
    CHECK(root(c).position.x == doctest::Approx(0.5f).epsilon(0.01));
}

TEST_CASE("kinematic: a kinematic character walking through a ragdoll pushes it, and is not pushed itself")
{
    Library lib;
    Scene s;
    DdCharacter* ragdoll = s.human(DdVec3{0.6f, 0, 0.01f});
    DdCharacter* walker = s.human(DdVec3{-0.6f, 0, 0.01f});
    REQUIRE(dragdoll_character_set_kinematic(walker, 1) == DD_OK);
    DdTransform start = root(walker), before = start;
    const float ragdollX = root(ragdoll).position.x;
    for (int f = 0; f < 60; ++f) // Walk 1.5 m along +X at 1.5 m/s, 60 fps
    {
        DdTransform now = start;
        now.position.x += 1.5f * static_cast<float>(f + 1) / 60.0f;
        REQUIRE(dragdoll_character_activate(walker, &before, nullptr, &now, nullptr, 0, 1.0f / 60.0f) == DD_OK);
        before = now;
        dragdoll_world_step(s.world, 1.0f / 60.0f, nullptr);
    }
    CHECK(root(ragdoll).position.x > ragdollX + 0.3f); // Pushed along
    CHECK(root(walker).position.x == doctest::Approx(start.position.x + 1.5f).epsilon(0.05)); // Went where the host said
}

TEST_CASE("kinematic: switching back to dynamic keeps the momentum (the handover when hit) and gravity returns")
{
    Library lib;
    Scene s;
    DdCharacter* c = s.human(DdVec3{0, 0, 1.0f});
    REQUIRE(dragdoll_character_set_kinematic(c, 1) == DD_OK);
    DdTransform before = root(c), now = before;
    now.position.x += 2.0f / 60.0f; // Moving at 2 m/s
    REQUIRE(dragdoll_character_activate(c, &before, nullptr, &now, nullptr, 0, 1.0f / 60.0f) == DD_OK);
    REQUIRE(dragdoll_character_set_kinematic(c, 0) == DD_OK);
    const DdTransform start = root(c);
    dragdoll_world_step_fixed(s.world, 12); // 0.1 s
    const DdTransform after = root(c);
    CHECK((after.position.x - start.position.x) / 0.1f == doctest::Approx(2.0f).epsilon(0.1));
    CHECK(after.position.z < start.position.z - 0.02f); // Falling again
}
