// SPDX-License-Identifier: MIT
// World, fixed-step clock and proxy collider tests — all through the public C ABI.

#include <dragdoll/dragdoll.h>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace
{
// Initialises the library for the duration of a test.
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

// Owns a world.
struct World
{
    DdWorld* ptr = nullptr;
    explicit World(DdWorldDesc desc) { REQUIRE_MESSAGE(dragdoll_world_create(&desc, &ptr) == DD_OK, dragdoll_last_error_message()); }
    ~World() { dragdoll_world_destroy(ptr); }
    operator DdWorld*() const { return ptr; }
};

DdWorldDesc defaultDesc()
{
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    return desc;
}

DdVec3 scale(DdVec3 v, float s)
{
    return DdVec3{v.x * s, v.y * s, v.z * s};
}

float dot(DdVec3 a, DdVec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

DdColliderId addOne(DdWorld* world, const DdColliderDesc& desc)
{
    DdColliderId id = DD_INVALID_COLLIDER;
    REQUIRE_MESSAGE(dragdoll_world_add_colliders(world, &desc, 1, &id) == DD_OK, dragdoll_last_error_message());
    return id;
}

// Large static ground slab whose top surface is the plane through the origin, normal = up.
DdColliderId addGround(DdWorld* world, DdVec3 up)
{
    DdColliderDesc ground;
    dragdoll_collider_desc_default(&ground);
    ground.shape = DD_SHAPE_BOX;
    ground.half_extents = DdVec3{50.0f, 50.0f, 50.0f};
    ground.transform.position = scale(up, -50.0f);
    return addOne(world, ground);
}

// A small pile of mixed dynamic props: enough contacts to make determinism meaningful.
void buildPile(DdWorld* world, DdVec3 up)
{
    addGround(world, up);
    std::vector<DdColliderDesc> props;
    for (int i = 0; i < 12; ++i)
    {
        DdColliderDesc d;
        dragdoll_collider_desc_default(&d);
        d.motion = DD_MOTION_DYNAMIC;
        d.shape = (i % 3 == 0) ? DD_SHAPE_SPHERE : (i % 3 == 1) ? DD_SHAPE_BOX : DD_SHAPE_CAPSULE;
        d.radius = 0.25f;
        d.half_height = 0.3f;
        d.half_extents = DdVec3{0.3f, 0.2f, 0.25f};
        const float lateral = 0.15f * static_cast<float>(i % 4) - 0.2f;
        DdVec3 p = scale(up, 1.0f + 0.7f * static_cast<float>(i));
        // Offset sideways along an axis perpendicular to up so the pile topples.
        p.x += (std::fabs(up.x) > 0.5f) ? 0.0f : lateral;
        p.z += (std::fabs(up.z) > 0.5f) ? 0.0f : lateral * 0.5f;
        if (std::fabs(up.z) > 0.5f)
        {
            p.y += lateral * 0.5f;
        }
        d.transform.position = p;
        d.transform.rotation = DdQuat{0.0f, std::sin(0.2f * i), 0.0f, std::cos(0.2f * i)};
        props.push_back(d);
    }
    std::vector<DdColliderId> ids(props.size());
    REQUIRE(dragdoll_world_add_colliders(world, props.data(), static_cast<uint32_t>(props.size()), ids.data()) == DD_OK);
}

uint64_t hashOf(DdWorld* world)
{
    uint64_t h = 0;
    REQUIRE(dragdoll_world_compute_state_hash(world, &h) == DD_OK);
    return h;
}
} // namespace

TEST_CASE("world: create requires init and validates the description")
{
    DdWorldDesc desc = defaultDesc();
    DdWorld* world = nullptr;
    CHECK(dragdoll_world_create(&desc, &world) == DD_ERROR_NOT_INITIALIZED);
    CHECK(world == nullptr);

    Library lib;
    desc.step_hz = 30.0f;
    CHECK(dragdoll_world_create(&desc, &world) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(dragdoll_last_error_message()).find("step_hz") != std::string::npos);

    desc = defaultDesc();
    desc.up = DdVec3{0, 0, 0};
    CHECK(dragdoll_world_create(&desc, &world) == DD_ERROR_INVALID_ARGUMENT);

    desc = defaultDesc();
    desc.struct_size = 4;
    CHECK(dragdoll_world_create(&desc, &world) == DD_ERROR_INVALID_ARGUMENT);

    desc = defaultDesc();
    REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
    dragdoll_world_destroy(world);
    dragdoll_world_destroy(nullptr); // Safe.
}

TEST_CASE("clock: accumulator, interpolation alpha and spiral-of-death guard")
{
    Library lib;
    World world(defaultDesc()); // 120 Hz, max 8 steps per call

    uint32_t steps = 99;
    REQUIRE(dragdoll_world_step(world, 1.0f / 60.0f, &steps) == DD_OK);
    CHECK(steps == 2);

    REQUIRE(dragdoll_world_step(world, 0.004f, &steps) == DD_OK); // < 1/120
    CHECK(steps == 0);
    CHECK(dragdoll_world_get_interpolation_alpha(world) == doctest::Approx(0.48).epsilon(0.01));

    REQUIRE(dragdoll_world_step(world, 0.005f, &steps) == DD_OK); // total 0.009 > 1/120
    CHECK(steps == 1);
    const float alpha = dragdoll_world_get_interpolation_alpha(world);
    CHECK(alpha >= 0.0f);
    CHECK(alpha < 1.0f);

    REQUIRE(dragdoll_world_step(world, 10.0f, &steps) == DD_OK); // a 10 s hitch
    CHECK(steps == 8);
    CHECK(dragdoll_world_get_interpolation_alpha(world) < 1.0f);
    CHECK(dragdoll_world_get_step_count(world) == 11);

    CHECK(dragdoll_world_step(world, -1.0f, &steps) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_world_step(world, NAN, &steps) == DD_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("up axis: a sphere falls along -up and rests on the ground")
{
    Library lib;
    const DdVec3 ups[] = {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}, {0, -1, 0}};
    for (const DdVec3& up : ups)
    {
        CAPTURE(up.x);
        CAPTURE(up.y);
        CAPTURE(up.z);
        DdWorldDesc desc = defaultDesc();
        desc.up = up;
        World world(desc);
        addGround(world, up);

        DdColliderDesc ball;
        dragdoll_collider_desc_default(&ball);
        ball.motion = DD_MOTION_DYNAMIC;
        ball.radius = 0.3f;
        ball.transform.position = scale(up, 2.0f);
        const DdColliderId id = addOne(world, ball);

        REQUIRE(dragdoll_world_step_fixed(world, 360) == DD_OK); // 3 s
        DdTransform t{};
        REQUIRE(dragdoll_world_get_collider_transform(world, id, 0, &t) == DD_OK);
        CHECK(dot(t.position, up) == doctest::Approx(0.3).epsilon(0.05));
        // No sideways drift.
        const DdVec3 lateral{t.position.x - up.x * dot(t.position, up), t.position.y - up.y * dot(t.position, up),
                             t.position.z - up.z * dot(t.position, up)};
        CHECK(std::sqrt(dot(lateral, lateral)) < 0.01f);
    }
}

TEST_CASE("capsule axis follows the world up axis")
{
    Library lib;
    for (const DdVec3 up : {DdVec3{0, 1, 0}, DdVec3{0, 0, 1}})
    {
        DdWorldDesc desc = defaultDesc();
        desc.up = up;
        World world(desc);
        addGround(world, up);
        DdColliderDesc capsule;
        dragdoll_collider_desc_default(&capsule);
        capsule.shape = DD_SHAPE_CAPSULE;
        capsule.motion = DD_MOTION_DYNAMIC;
        capsule.radius = 0.2f;
        capsule.half_height = 0.5f;
        capsule.transform.position = scale(up, 0.71f); // standing, just above the ground
        const DdColliderId id = addOne(world, capsule);
        REQUIRE(dragdoll_world_step_fixed(world, 60) == DD_OK);
        DdTransform t{};
        REQUIRE(dragdoll_world_get_collider_transform(world, id, 0, &t) == DD_OK);
        // Standing capsule: centre stays at half_height + radius above ground (it would be at
        // radius height if it had been created lying down).
        CHECK(dot(t.position, up) == doctest::Approx(0.7).epsilon(0.03));
    }
}

TEST_CASE("determinism: any host frame rate gives identical physics (SA vanilla, 60 fps mods, 144 Hz, unlocked)")
{
    Library lib;
    const DdVec3 up{0, 0, 1}; // SA is Z-up
    constexpr uint32_t kSteps = 600;   // 5 s at 120 Hz

    // Reference: one fixed step at a time, hash after every step.
    std::vector<uint64_t> reference(kSteps + 1);
    {
        DdWorldDesc desc = defaultDesc();
        desc.up = up;
        World world(desc);
        buildPile(world, up);
        reference[0] = hashOf(world);
        for (uint32_t i = 1; i <= kSteps; ++i)
        {
            REQUIRE(dragdoll_world_step_fixed(world, 1) == DD_OK);
            reference[i] = hashOf(world);
        }
    }
    CHECK(reference[0] != reference[kSteps]); // Things actually moved.

    struct RateProfile
    {
        const char* name;
        float minFps, maxFps;
    };
    const RateProfile profiles[] = {
        {"SA vanilla 25-30 fps", 25.0f, 30.0f},
        {"60 fps mod (55-65)", 55.0f, 65.0f},
        {"144 Hz monitor (138-150)", 138.0f, 150.0f},
        {"unlocked 30-200 fps", 30.0f, 200.0f},
    };
    for (const RateProfile& profile : profiles)
    {
        CAPTURE(profile.name);
        DdWorldDesc desc = defaultDesc();
        desc.up = up;
        World world(desc);
        buildPile(world, up);
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> dtDist(1.0f / profile.maxFps, 1.0f / profile.minFps);
        uint32_t compared = 0;
        while (dragdoll_world_get_step_count(world) < kSteps)
        {
            REQUIRE(dragdoll_world_step(world, dtDist(rng), nullptr) == DD_OK);
            const uint64_t step = dragdoll_world_get_step_count(world);
            if (step <= kSteps)
            {
                REQUIRE(hashOf(world) == reference[static_cast<size_t>(step)]);
                ++compared;
            }
        }
        CHECK(compared > 100);
    }
}

TEST_CASE("determinism: identical results for 0, 1, 4 and 8 worker threads")
{
    Library lib;
    const DdVec3 up{0, 1, 0};
    uint64_t expected = 0;
    for (uint32_t threads : {0u, 1u, 4u, 8u})
    {
        CAPTURE(threads);
        DdWorldDesc desc = defaultDesc();
        desc.worker_threads = threads;
        World world(desc);
        buildPile(world, up);
        REQUIRE(dragdoll_world_step_fixed(world, 600) == DD_OK);
        const uint64_t h = hashOf(world);
        if (threads == 0)
        {
            expected = h;
        }
        CHECK(h == expected);
    }
}

TEST_CASE("colliders: bulk add/remove for streaming")
{
    Library lib;
    World world(defaultDesc());

    constexpr uint32_t kCount = 1000;
    std::vector<DdColliderDesc> descs(kCount);
    for (uint32_t i = 0; i < kCount; ++i)
    {
        dragdoll_collider_desc_default(&descs[i]);
        descs[i].shape = DD_SHAPE_BOX;
        descs[i].half_extents = DdVec3{0.5f, 0.5f, 0.5f};
        descs[i].transform.position = DdVec3{static_cast<float>(i % 40) * 2.0f, 0.0f, static_cast<float>(i / 40) * 2.0f};
    }
    std::vector<DdColliderId> ids(kCount);
    REQUIRE(dragdoll_world_add_colliders(world, descs.data(), kCount, ids.data()) == DD_OK);
    for (uint32_t i = 1; i < kCount; ++i)
    {
        CHECK(ids[i] == ids[i - 1] + 1); // Ids follow input order.
    }

    // Remove every other one (plus an unknown id, which is ignored).
    std::vector<DdColliderId> toRemove;
    for (uint32_t i = 0; i < kCount; i += 2)
    {
        toRemove.push_back(ids[i]);
    }
    toRemove.push_back(999999);
    REQUIRE(dragdoll_world_remove_colliders(world, toRemove.data(), static_cast<uint32_t>(toRemove.size())) == DD_OK);

    DdTransform t{};
    CHECK(dragdoll_world_get_collider_transform(world, ids[0], 0, &t) == DD_ERROR_INVALID_ARGUMENT);
    REQUIRE(dragdoll_world_get_collider_transform(world, ids[1], 0, &t) == DD_OK);
    CHECK(t.position.x == doctest::Approx(2.0f));

    // Stream a new batch in; ids stay unique.
    std::vector<DdColliderId> ids2(kCount);
    REQUIRE(dragdoll_world_add_colliders(world, descs.data(), kCount, ids2.data()) == DD_OK);
    CHECK(ids2[0] > ids.back());
    REQUIRE(dragdoll_world_step_fixed(world, 2) == DD_OK);
}

TEST_CASE("colliders: a batch with one invalid entry adds nothing")
{
    Library lib;
    World world(defaultDesc());
    const uint64_t before = hashOf(world);

    DdColliderDesc descs[3];
    for (auto& d : descs)
    {
        dragdoll_collider_desc_default(&d);
        d.motion = DD_MOTION_DYNAMIC;
    }
    descs[2].radius = -1.0f; // invalid
    DdColliderId ids[3] = {};
    CHECK(dragdoll_world_add_colliders(world, descs, 3, ids) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(dragdoll_last_error_message()).find("radius") != std::string::npos);
    CHECK(hashOf(world) == before);

    DdColliderDesc mesh;
    dragdoll_collider_desc_default(&mesh);
    mesh.shape = DD_SHAPE_MESH;
    mesh.motion = DD_MOTION_DYNAMIC;
    const DdVec3 tri[3] = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    mesh.vertices = tri;
    mesh.vertex_count = 3;
    DdColliderId id = 0;
    CHECK(dragdoll_world_add_colliders(world, &mesh, 1, &id) == DD_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("colliders: static triangle mesh supports a resting ball")
{
    Library lib;
    World world(defaultDesc());
    // Two triangles forming a 20 x 20 m floor at y = 0 (indexed).
    const DdVec3 verts[4] = {{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}, {-10, 0, 10}};
    const uint32_t indices[6] = {0, 2, 1, 0, 3, 2};
    DdColliderDesc floor;
    dragdoll_collider_desc_default(&floor);
    floor.shape = DD_SHAPE_MESH;
    floor.vertices = verts;
    floor.vertex_count = 4;
    floor.indices = indices;
    floor.index_count = 6;
    addOne(world, floor);

    DdColliderDesc ball;
    dragdoll_collider_desc_default(&ball);
    ball.motion = DD_MOTION_DYNAMIC;
    ball.radius = 0.25f;
    ball.transform.position = DdVec3{0, 1, 0};
    const DdColliderId id = addOne(world, ball);
    REQUIRE(dragdoll_world_step_fixed(world, 240) == DD_OK);
    DdTransform t{};
    REQUIRE(dragdoll_world_get_collider_transform(world, id, 0, &t) == DD_OK);
    CHECK(t.position.y == doctest::Approx(0.25).epsilon(0.05));
}

TEST_CASE("kinematic: reaches its target over the batch, then stands still")
{
    Library lib;
    World world(defaultDesc());
    DdColliderDesc car;
    dragdoll_collider_desc_default(&car);
    car.shape = DD_SHAPE_BOX;
    car.motion = DD_MOTION_KINEMATIC;
    car.half_extents = DdVec3{2.0f, 0.7f, 0.9f};
    car.transform.position = DdVec3{0, 0.7f, 0};
    const DdColliderId id = addOne(world, car);

    // Host frame at 30 fps: move 1 m along +x (= 30 m/s, ~108 km/h).
    const DdTransform target{{1.0f, 0.7f, 0.0f}, {0, 0, 0, 1}};
    REQUIRE(dragdoll_world_set_kinematic_targets(world, &id, &target, 1) == DD_OK);
    uint32_t steps = 0;
    REQUIRE(dragdoll_world_step(world, 1.0f / 30.0f, &steps) == DD_OK);
    CHECK(steps == 4);
    DdTransform t{};
    REQUIRE(dragdoll_world_get_collider_transform(world, id, 0, &t) == DD_OK);
    CHECK(t.position.x == doctest::Approx(1.0f).epsilon(0.001));

    // No new target: it must not keep sliding.
    REQUIRE(dragdoll_world_step(world, 1.0f / 30.0f, &steps) == DD_OK);
    REQUIRE(dragdoll_world_get_collider_transform(world, id, 0, &t) == DD_OK);
    CHECK(t.position.x == doctest::Approx(1.0f).epsilon(0.001));

    // Wrong usage is rejected without side effects.
    DdColliderDesc wall;
    dragdoll_collider_desc_default(&wall);
    const DdColliderId staticId = addOne(world, wall);
    CHECK(dragdoll_world_set_kinematic_targets(world, &staticId, &target, 1) == DD_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("kinematic: a fast car pushes a dynamic prop (no tunnelling at 120 km/h)")
{
    Library lib;
    World world(defaultDesc());
    addGround(world, DdVec3{0, 1, 0});

    DdColliderDesc car;
    dragdoll_collider_desc_default(&car);
    car.shape = DD_SHAPE_BOX;
    car.motion = DD_MOTION_KINEMATIC;
    car.half_extents = DdVec3{2.0f, 0.7f, 0.9f};
    car.transform.position = DdVec3{-6.0f, 0.75f, 0};
    const DdColliderId carId = addOne(world, car);

    // A thin "shin" sized pole standing in the way.
    DdColliderDesc pole;
    dragdoll_collider_desc_default(&pole);
    pole.shape = DD_SHAPE_CAPSULE;
    pole.motion = DD_MOTION_DYNAMIC;
    pole.radius = 0.05f;
    pole.half_height = 0.4f;
    pole.mass = 4.0f;
    pole.transform.position = DdVec3{0, 0.46f, 0};
    const DdColliderId poleId = addOne(world, pole);

    // 120 km/h = 33.3 m/s; host at 30 fps moves the car 1.11 m per frame.
    float x = -6.0f;
    for (int frame = 0; frame < 15; ++frame)
    {
        x += 33.333f / 30.0f;
        const DdTransform target{{x, 0.75f, 0}, {0, 0, 0, 1}};
        REQUIRE(dragdoll_world_set_kinematic_targets(world, &carId, &target, 1) == DD_OK);
        REQUIRE(dragdoll_world_step(world, 1.0f / 30.0f, nullptr) == DD_OK);
    }
    DdTransform t{};
    REQUIRE(dragdoll_world_get_collider_transform(world, poleId, 0, &t) == DD_OK);
    CHECK(t.position.x > 3.0f); // Thrown forward, not left behind the car.
}

TEST_CASE("interpolation: rendering transform lies between the last two steps")
{
    Library lib;
    World world(defaultDesc());
    DdColliderDesc ball;
    dragdoll_collider_desc_default(&ball);
    ball.motion = DD_MOTION_DYNAMIC;
    ball.transform.position = DdVec3{0, 10, 0};
    const DdColliderId id = addOne(world, ball);

    REQUIRE(dragdoll_world_step(world, 0.5f / 120.0f + 3.0f / 120.0f, nullptr) == DD_OK); // 3 steps + half
    DdTransform latest{}, rendered{};
    REQUIRE(dragdoll_world_get_collider_transform(world, id, 0, &latest) == DD_OK);
    REQUIRE(dragdoll_world_get_collider_transform(world, id, 1, &rendered) == DD_OK);
    CHECK(dragdoll_world_get_interpolation_alpha(world) == doctest::Approx(0.5).epsilon(0.01));
    CHECK(rendered.position.y > latest.position.y); // Still falling: the blend lags behind the latest step.
}

TEST_CASE("kinematic: vehicle speed is identical on every fixed step at any host frame rate")
{
    // The car-hit mod depends on this: impact impulses must not depend on how many fixed steps a
    // host frame happens to get (1, 2 or 3 at 60 fps; 0 or 1 at 144 fps).
    Library lib;
    constexpr float kSpeed = 50.0f / 3.6f; // 50 km/h
    constexpr double kFixedDt = 1.0 / 120.0;

    struct RateProfile
    {
        const char* name;
        float minFps, maxFps;
    };
    const RateProfile profiles[] = {
        {"30 fps steady", 30.0f, 30.0f},
        {"60 fps jittery", 55.0f, 65.0f},
        {"144 fps", 138.0f, 150.0f},
        {"unlocked 30-200 fps", 30.0f, 200.0f},
    };
    for (const RateProfile& profile : profiles)
    {
        CAPTURE(profile.name);
        World world(defaultDesc());
        DdColliderDesc car;
        dragdoll_collider_desc_default(&car);
        car.shape = DD_SHAPE_BOX;
        car.motion = DD_MOTION_KINEMATIC;
        car.half_extents = DdVec3{2.0f, 0.7f, 0.9f};
        car.transform.position = DdVec3{0, 0.7f, 0};
        const DdColliderId id = addOne(world, car);

        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dtDist(1.0f / profile.maxFps, 1.0f / profile.minFps);
        double hostTime = 0.0;
        float lastX = 0.0f;
        uint64_t lastStep = 0;
        int measured = 0;
        for (int frame = 0; frame < 400; ++frame)
        {
            const float dt = dtDist(rng);
            hostTime += dt;
            // The host moves the car along its own (exact) trajectory and reports this frame's pose.
            const DdTransform target{{static_cast<float>(kSpeed * hostTime), 0.7f, 0.0f}, {0, 0, 0, 1}};
            REQUIRE(dragdoll_world_set_kinematic_targets(world, &id, &target, 1) == DD_OK);
            uint32_t steps = 0;
            REQUIRE(dragdoll_world_step(world, dt, &steps) == DD_OK);

            DdTransform t{};
            REQUIRE(dragdoll_world_get_collider_transform(world, id, 0, &t) == DD_OK);
            const uint64_t step = dragdoll_world_get_step_count(world);
            if (steps == 0)
            {
                CHECK(t.position.x == lastX); // No simulated time, no movement.
            }
            else if (frame > 10) // skip the first frames (trajectory start-up)
            {
                const double speed = (t.position.x - lastX) / (static_cast<double>(step - lastStep) * kFixedDt);
                CHECK(speed == doctest::Approx(kSpeed).epsilon(0.01));
                ++measured;
            }
            lastX = t.position.x;
            lastStep = step;
        }
        CHECK(measured > 50);
    }
}

TEST_CASE("kinematic: without new targets the vehicle holds its last pose")
{
    Library lib;
    World world(defaultDesc());
    DdColliderDesc car;
    dragdoll_collider_desc_default(&car);
    car.shape = DD_SHAPE_BOX;
    car.motion = DD_MOTION_KINEMATIC;
    car.half_extents = DdVec3{2.0f, 0.7f, 0.9f};
    const DdColliderId id = addOne(world, car);
    const DdTransform target{{3.0f, 0.0f, 0.0f}, {0, 0, 0, 1}};
    REQUIRE(dragdoll_world_set_kinematic_targets(world, &id, &target, 1) == DD_OK);
    for (int i = 0; i < 30; ++i)
    {
        REQUIRE(dragdoll_world_step(world, 1.0f / 60.0f, nullptr) == DD_OK);
    }
    DdTransform t{};
    REQUIRE(dragdoll_world_get_collider_transform(world, id, 0, &t) == DD_OK);
    CHECK(t.position.x == doctest::Approx(3.0f).epsilon(0.001));
}
