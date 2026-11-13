// SPDX-License-Identifier: MIT
// The FAILED state (Phase 3, task 3.5, ADR-0017): a numerical blow-up takes one character out of the simulation and
// reports why; nothing crashes, other characters carry on, the host gets clean errors and the last good pose.
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/scenario.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <string>
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
    DdCharacter* human(float x)
    {
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
        cd.transform.position = DdVec3{x, 0, 1.0f};
        DdCharacter* c = nullptr;
        REQUIRE(dragdoll_character_create(world, &cd, &c) == DD_OK);
        return c;
    }
};

DdCharacterInfo info(DdCharacter* c)
{
    DdCharacterInfo i{};
    i.struct_size = sizeof(DdCharacterInfo);
    REQUIRE(dragdoll_character_get_info(c, &i) == DD_OK);
    return i;
}

std::vector<DdTransform> bodies(DdCharacter* c)
{
    std::vector<DdTransform> t(dragdoll_character_get_body_count(c));
    REQUIRE(dragdoll_character_get_body_transforms(c, 0, t.data(), static_cast<uint32_t>(t.size())) == DD_OK);
    return t;
}

bool finite(const std::vector<DdTransform>& t)
{
    for (const DdTransform& b : t)
    {
        if (!std::isfinite(b.position.x) || !std::isfinite(b.position.y) || !std::isfinite(b.position.z) || !std::isfinite(b.rotation.w))
            return false;
    }
    return true;
}
} // namespace

TEST_CASE("failed: a forced NaN ends in FAILED, never a crash; the host gets the last good pose and clean errors")
{
    Library lib;
    Scene s;
    DdCharacter* broken = s.human(0.0f);
    DdCharacter* healthy = s.human(3.0f);
    dragdoll_world_step_fixed(s.world, 30);
    const std::vector<DdTransform> before = bodies(broken);

    REQUIRE(dragdoll_debug_corrupt_character(broken) == DD_OK);
    dragdoll_world_step_fixed(s.world, 1);
    const DdCharacterInfo i = info(broken);
    CHECK(static_cast<int>(i.state) == DD_STATE_FAILED);
    CHECK(static_cast<int>(i.failure) == DD_FAILURE_NOT_FINITE);

    // The last good pose stays readable (the step that produced NaN is discarded).
    const std::vector<DdTransform> frozen = bodies(broken);
    CHECK(finite(frozen));
    CHECK(frozen[0].position.z == doctest::Approx(before[0].position.z).epsilon(0.05));
    DdTransform root;
    std::vector<DdQuat> joints(16);
    CHECK(dragdoll_character_get_pose(broken, 0, joints.data(), 16, &root) == DD_OK);

    // Commands are refused with a clear message.
    CHECK(dragdoll_character_apply_impulse(broken, 0, DdVec3{1, 0, 0}, nullptr) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(dragdoll_last_error_message()).find("FAILED") != std::string::npos);
    CHECK(dragdoll_character_set_tension(broken, DD_TENSION_TENSE, 0.0f) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_character_set_target_pose(broken, nullptr, 0) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_character_activate(broken, nullptr, nullptr, &root, nullptr, 0, 0.0f) == DD_ERROR_INVALID_ARGUMENT);

    // The world and the other character carry on; the broken one stays frozen.
    const float healthyBefore = bodies(healthy)[0].position.z;
    CHECK(dragdoll_world_step_fixed(s.world, 60) == DD_OK);
    CHECK(bodies(healthy)[0].position.z < healthyBefore - 0.05f); // Still falling normally
    CHECK(bodies(broken)[0].position.z == doctest::Approx(frozen[0].position.z));
    CHECK(static_cast<int>(info(healthy).state) != DD_STATE_FAILED);
    uint64_t hash = 0;
    CHECK(dragdoll_world_compute_state_hash(s.world, &hash) == DD_OK);

    // The host destroys it and falls back to its own animation.
    dragdoll_character_destroy(broken);
    CHECK(dragdoll_world_step_fixed(s.world, 10) == DD_OK);
}

TEST_CASE("failed: a real energy explosion (absurd impulse) is caught by the guards")
{
    Library lib;
    Scene s;
    DdCharacter* c = s.human(0.0f);
    const uint32_t hand = static_cast<uint32_t>(dragdoll_character_find_body(c, "hand_l"));
    REQUIRE(dragdoll_character_apply_impulse(c, hand, DdVec3{0, 1e6f, 0}, nullptr) == DD_OK);
    dragdoll_world_step_fixed(s.world, 5);
    const DdCharacterInfo i = info(c);
    CHECK(static_cast<int>(i.state) == DD_STATE_FAILED);
    const bool caught = i.failure == DD_FAILURE_TOO_FAST || i.failure == DD_FAILURE_TORN_APART;
    CHECK(caught);
    MESSAGE("explosion caught as failure " << static_cast<int>(i.failure));
    CHECK(finite(bodies(c)));
}

TEST_CASE("failed: no false alarms — no character in any scenario fails at 30/60/144 fps hosts")
{
    Library lib;
    for (const std::string& name : dragdoll::scenarios::list())
    {
        for (int fps : {30, 60, 144})
        {
            CAPTURE(name);
            CAPTURE(fps);
            dragdoll::scenarios::Instance run;
            std::string error;
            REQUIRE(dragdoll::scenarios::Instance::make(name, run, error));
            const double seconds = run.scenario->duration() > 0.0 ? run.scenario->duration() : 4.0;
            for (int f = 0; f < static_cast<int>(seconds * fps); ++f) run.hostFrame(1.0f / static_cast<float>(fps));
            for (uint32_t i = 0; i < dragdoll_world_get_character_count(run.world); ++i)
            {
                CHECK(static_cast<int>(info(dragdoll_world_get_character(run.world, i)).state) != DD_STATE_FAILED);
            }
        }
    }
}
