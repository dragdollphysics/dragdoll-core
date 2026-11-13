// SPDX-License-Identifier: MIT
// Guest-process memory budget (ADR-0015): a world with the ship-criteria load (8 simultaneous ragdolls) stays well
// under 64 MB, and everything is returned when the world goes away.

#include <dragdoll/dragdoll.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

namespace
{
DdMemoryStats stats()
{
    DdMemoryStats s{};
    s.struct_size = sizeof(DdMemoryStats);
    REQUIRE(dragdoll_get_memory_stats(&s) == DD_OK);
    return s;
}

double megabytes(uint64_t bytes)
{
    return static_cast<double>(bytes) / (1024.0 * 1024.0);
}
} // namespace

TEST_CASE("memory: stats API validates its argument")
{
    REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK);
    CHECK(dragdoll_get_memory_stats(nullptr) == DD_ERROR_INVALID_ARGUMENT);
    DdMemoryStats bad{};
    bad.struct_size = 4;
    CHECK(dragdoll_get_memory_stats(&bad) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(stats().allocation_count > 0);
    dragdoll_shutdown();
}

TEST_CASE("memory: 8 simultaneous ragdolls on streamed level geometry stay under the 64 MB guest budget")
{
    REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK);
    const uint64_t before = stats().current_bytes;
    dragdoll_reset_peak_memory();

    {
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.up = DdVec3{0, 0, 1};
        desc.worker_threads = 2; // San Andreas default (ADR-0015)
        DdWorld* world = nullptr;
        REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);

        // Level geometry: 400 static boxes (streamed COL objects) and one big triangle-mesh floor.
        std::vector<DdColliderDesc> boxes(400);
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            dragdoll_collider_desc_default(&boxes[i]);
            boxes[i].shape = DD_SHAPE_BOX;
            boxes[i].half_extents = DdVec3{1.0f, 1.0f, 1.0f};
            boxes[i].transform.position = DdVec3{20.0f + static_cast<float>(i % 20) * 5.0f, static_cast<float>(i / 20) * 5.0f, 1.0f};
        }
        std::vector<DdColliderId> ids(boxes.size());
        REQUIRE(dragdoll_world_add_colliders(world, boxes.data(), static_cast<uint32_t>(boxes.size()), ids.data()) == DD_OK);

        std::vector<DdVec3> verts;
        std::vector<uint32_t> indices;
        constexpr int kGrid = 60; // 60 x 60 cells = 7,200 triangles
        for (int y = 0; y <= kGrid; ++y)
        {
            for (int x = 0; x <= kGrid; ++x)
            {
                verts.push_back(DdVec3{static_cast<float>(x) * 2.0f - 60.0f, static_cast<float>(y) * 2.0f - 60.0f, 0.0f});
            }
        }
        for (int y = 0; y < kGrid; ++y)
        {
            for (int x = 0; x < kGrid; ++x)
            {
                const uint32_t a = static_cast<uint32_t>(y * (kGrid + 1) + x);
                const uint32_t b = a + 1, c = a + (kGrid + 1), d = c + 1;
                indices.insert(indices.end(), {a, b, c, b, d, c});
            }
        }
        DdColliderDesc floor;
        dragdoll_collider_desc_default(&floor);
        floor.shape = DD_SHAPE_MESH;
        floor.vertices = verts.data();
        floor.vertex_count = static_cast<uint32_t>(verts.size());
        floor.indices = indices.data();
        floor.index_count = static_cast<uint32_t>(indices.size());
        DdColliderId floorId = 0;
        REQUIRE(dragdoll_world_add_colliders(world, &floor, 1, &floorId) == DD_OK);

        // 8 ragdolls falling and piling up, 3 simulated seconds.
        for (int i = 0; i < 8; ++i)
        {
            DdCharacterDesc character;
            dragdoll_character_desc_default(&character);
            character.rig_json = dragdoll_get_builtin_rig("humanoid16");
            character.transform.position = DdVec3{static_cast<float>(i % 4) * 0.7f, static_cast<float>(i / 4) * 0.7f, 1.0f + 0.3f * static_cast<float>(i)};
            DdCharacter* c = nullptr;
            REQUIRE(dragdoll_character_create(world, &character, &c) == DD_OK);
        }
        REQUIRE(dragdoll_world_step_fixed(world, 360) == DD_OK);

        const DdMemoryStats during = stats();
        MESSAGE("8 ragdolls + 400 boxes + 7,200-triangle mesh: peak " << megabytes(during.peak_bytes - before) << " MB, live "
                << megabytes(during.current_bytes - before) << " MB");
        CHECK(megabytes(during.peak_bytes - before) < 64.0);
        dragdoll_world_destroy(world);
    }

    // Everything Jolt allocated for the world is returned (small slack for lazily created statics).
    CHECK(megabytes(stats().current_bytes - before) < 1.0);
    dragdoll_shutdown();
}
