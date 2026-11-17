// SPDX-License-Identifier: MIT
// DdCharacterDesc.size_scale: one rig matched to a host's character proportions (San Andreas people are ~9 % taller
// than humanoid16). Lengths scale, mass scales with the cube, and the body still stands and balances.
#include <dragdoll/dragdoll.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

struct Standing
{
    DdWorld* world = nullptr;
    DdCharacter* c = nullptr;
    explicit Standing(float size)
    {
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.up = DdVec3{0, 0, 1};
        desc.worker_threads = 0;
        REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
        DdColliderDesc ground;
        dragdoll_collider_desc_default(&ground);
        ground.shape = DD_SHAPE_BOX;
        ground.half_extents = DdVec3{20, 20, 0.5f};
        ground.transform.position = DdVec3{0, 0, -0.5f};
        DdColliderId id;
        REQUIRE(dragdoll_world_add_colliders(world, &ground, 1, &id) == DD_OK);
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        CHECK(cd.size_scale == 1.0f);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
        cd.transform.position = DdVec3{0, 0, 0.005f};
        cd.size_scale = size;
        REQUIRE(dragdoll_character_create(world, &cd, &c) == DD_OK);
    }
    ~Standing() { dragdoll_world_destroy(world); }
    float headHeight() const
    {
        std::vector<DdTransform> t(dragdoll_character_get_body_count(c));
        REQUIRE(dragdoll_character_get_body_transforms(c, 0, t.data(), static_cast<uint32_t>(t.size())) == DD_OK);
        return t[static_cast<size_t>(dragdoll_character_find_body(c, "head"))].position.z;
    }
};
} // namespace

TEST_CASE("size scale: lengths and mass follow it, and a scaled character still stands braced")
{
    Library lib;
    Standing normal(1.0f), tall(1.09f);
    MESSAGE("head centre at " << normal.headHeight() << " m (size 1) and " << tall.headHeight() << " m (size 1.09); mass "
                              << dragdoll_character_get_mass(normal.c) << " and " << dragdoll_character_get_mass(tall.c) << " kg");
    CHECK(tall.headHeight() == doctest::Approx(normal.headHeight() * 1.09f).epsilon(0.01));
    CHECK(dragdoll_character_get_mass(tall.c) == doctest::Approx(dragdoll_character_get_mass(normal.c) * 1.09f * 1.09f * 1.09f).epsilon(0.01));

    REQUIRE(dragdoll_character_set_tension(tall.c, DD_TENSION_TENSE, 0.0f) == DD_OK);
    REQUIRE(dragdoll_character_set_balance(tall.c, 1) == DD_OK);
    const float before = tall.headHeight();
    REQUIRE(dragdoll_world_step_fixed(tall.world, 240) == DD_OK); // 2 s
    CHECK(tall.headHeight() > before - 0.08f); // Still standing at its own height
}

TEST_CASE("label: the host's note on a character is kept, cut to 63 bytes, and replayed from a recording")
{
    Library lib;
    const char* path = "label_test.ddrec";
    {
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.worker_threads = 0;
        desc.record_path = path;
        DdWorld* world = nullptr;
        REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
        cd.size_scale = 1.09f;
        DdCharacter* c = nullptr;
        REQUIRE(dragdoll_character_create(world, &cd, &c) == DD_OK);
        CHECK(std::string(dragdoll_character_get_label(c)).empty());
        REQUIRE(dragdoll_character_set_label(c, "WALK_civi (ped)") == DD_OK);
        REQUIRE(dragdoll_world_step_fixed(world, 10) == DD_OK);
        REQUIRE(dragdoll_character_set_label(c, std::string(100, 'x').c_str()) == DD_OK);
        CHECK(std::string(dragdoll_character_get_label(c)).size() == 63);
        REQUIRE(dragdoll_character_set_label(c, "KO_skid_front (ped)") == DD_OK);
        REQUIRE(dragdoll_world_step_fixed(world, 10) == DD_OK);
        dragdoll_world_destroy(world);
    }
    DdReplay* replay = nullptr;
    REQUIRE(dragdoll_replay_open(path, &replay) == DD_OK);
    DdReplayFrame frame{};
    frame.struct_size = sizeof(DdReplayFrame);
    while (dragdoll_replay_advance(replay, &frame) == DD_OK && frame.at_end == 0) // At the end it stays OK with at_end set
    {
    }
    DdCharacter* replayed = dragdoll_world_get_character(dragdoll_replay_get_world(replay), 0);
    REQUIRE(replayed != nullptr);
    CHECK(std::string(dragdoll_character_get_label(replayed)) == "KO_skid_front (ped)");
    CHECK(dragdoll_character_get_mass(replayed) == doctest::Approx(75.0f * 1.09f * 1.09f * 1.09f).epsilon(0.01)); // size_scale replayed too
    dragdoll_replay_close(replay);
    std::remove(path);
}
