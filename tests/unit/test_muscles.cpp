// SPDX-License-Identifier: MIT
// Muscles (Phase 3, task 3.1): tension profiles through the C ABI, their visible order (tense holds the pose best,
// limp worst) at SA-relevant host frame rates, and blending.
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/scenario.hpp>

#include <doctest/doctest.h>

#include <string>
#include <vector>

using dragdoll::scenarios::Instance;
namespace metrics = dragdoll::scenarios::metrics;

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

std::vector<DdTransform> pose(DdCharacter* c)
{
    std::vector<DdTransform> t(dragdoll_character_get_body_count(c));
    REQUIRE(dragdoll_character_get_body_transforms(c, 0, t.data(), static_cast<uint32_t>(t.size())) == DD_OK);
    return t;
}

// A Z-up world with a ground slab and one humanoid standing 1 m above it, rest pose captured.
struct Drop
{
    DdWorld* world = nullptr;
    DdCharacter* character = nullptr;
    std::vector<DdTransform> rest;
    Drop()
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
        DdColliderId id = DD_INVALID_COLLIDER;
        REQUIRE(dragdoll_world_add_colliders(world, &ground, 1, &id) == DD_OK);
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
        cd.transform.position = DdVec3{0, 0, 1};
        REQUIRE(dragdoll_character_create(world, &cd, &character) == DD_OK);
        rest = pose(character);
    }
    ~Drop() { dragdoll_world_destroy(world); }
    float errorAfter(double seconds)
    {
        dragdoll_world_step_fixed(world, static_cast<uint32_t>(seconds * 120.0 + 0.5));
        return metrics::poseError(character, rest, pose(character)).rms;
    }
};
} // namespace

TEST_CASE("muscles: tension API — default limp, set/get, invalid input rejected, body parents")
{
    Library lib;
    Drop d;
    CHECK(dragdoll_character_get_tension(d.character) == DD_TENSION_LIMP);
    CHECK(dragdoll_character_set_tension(d.character, DD_TENSION_TENSE, 0.5f) == DD_OK);
    CHECK(dragdoll_character_get_tension(d.character) == DD_TENSION_TENSE);
    CHECK(dragdoll_character_set_tension(d.character, DD_TENSION_COUNT, 0.0f) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_character_set_tension(d.character, DD_TENSION_RELAXED, -1.0f) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_character_set_tension(nullptr, DD_TENSION_RELAXED, 0.0f) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_character_get_tension(d.character) == DD_TENSION_TENSE); // Failed calls change nothing

    CHECK(dragdoll_character_get_body_parent(d.character, 0) == -1);
    const int32_t head = dragdoll_character_find_body(d.character, "head");
    CHECK(dragdoll_character_get_body_parent(d.character, static_cast<uint32_t>(head)) ==
          dragdoll_character_find_body(d.character, "upper_spine"));
    CHECK(dragdoll_character_get_body_parent(d.character, 99) == -1);
}

TEST_CASE("muscles: tension_lineup — tense holds the pose best, then relaxed, limp worst; stunned folds slower than limp")
{
    Library lib;
    const char* names[] = {"limp", "stunned", "relaxed", "tense"}; // Left to right in the scenario
    for (int fps : {30, 60, 144})
    {
        CAPTURE(fps);
        Instance run;
        std::string error;
        REQUIRE(Instance::make("tension_lineup", run, error));
        std::vector<DdCharacter*> c;
        for (uint32_t i = 0; i < dragdoll_world_get_character_count(run.world); ++i) c.push_back(dragdoll_world_get_character(run.world, i));
        REQUIRE(c.size() == 4);
        std::vector<std::vector<DdTransform>> rest;
        for (DdCharacter* ch : c) rest.push_back(pose(ch));
        auto errors = [&] {
            std::vector<float> e;
            for (size_t i = 0; i < c.size(); ++i) e.push_back(metrics::poseError(c[i], rest[i], pose(c[i])).rms);
            return e;
        };
        const int framesAt08 = static_cast<int>(0.8 * fps + 0.5);
        for (int f = 0; f < framesAt08; ++f) run.hostFrame(1.0f / static_cast<float>(fps));
        const std::vector<float> falling = errors();
        for (int f = framesAt08; f < 5 * fps; ++f) run.hostFrame(1.0f / static_cast<float>(fps));
        const std::vector<float> settled = errors();
        for (int i = 0; i < 4; ++i) MESSAGE(fps << " fps " << std::string(names[i]) << ": RMS " << falling[i] << " deg at 0.8 s, " << settled[i] << " deg settled");

        CHECK(falling[1] < falling[0] * 0.7f);  // Stunned folds slower than limp
        CHECK(falling[3] < 5.0f);               // Tense keeps the standing pose on landing
        CHECK(settled[3] < settled[2]);         // Tense < relaxed < limp, once everything lies still
        CHECK(settled[2] < settled[0] * 0.75f);
        CHECK(settled[3] < 15.0f);
    }
}

TEST_CASE("muscles: blending in tense over 1 s holds the pose less early on than switching at once")
{
    Library lib;
    Drop instant, blended;
    REQUIRE(dragdoll_character_set_tension(instant.character, DD_TENSION_TENSE, 0.0f) == DD_OK);
    REQUIRE(dragdoll_character_set_tension(blended.character, DD_TENSION_TENSE, 1.0f) == DD_OK);
    const float a = instant.errorAfter(0.8), b = blended.errorAfter(0.8);
    MESSAGE("RMS at 0.8 s: instant " << a << " deg, blended " << b << " deg");
    CHECK(a < b);
}
