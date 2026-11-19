// SPDX-License-Identifier: MIT
// Senses (ADR-0024 §3, task 4.1): contacts per body, how hard it was hit (velocity change without gravity), airborne,
// the ground below, landing prediction, uprightness and angular momentum.
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/vehicles.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace
{
using namespace dragdoll::scenarios;

struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

struct Scene
{
    DdWorld* world = nullptr;
    DdCharacter* human = nullptr;
    DdColliderId ground = DD_INVALID_COLLIDER;

    // Z-up world; ground: a big box whose top passes through the origin, tilted by `slopeDegrees` (rising along +X).
    explicit Scene(float height, float gravity = 9.81f, uint32_t threads = 0, float slopeDegrees = 0.0f)
    {
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.up = DdVec3{0, 0, 1};
        desc.gravity = gravity;
        desc.worker_threads = threads;
        REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
        const float angle = slopeDegrees * 3.14159265f / 180.0f;
        DdColliderDesc box;
        dragdoll_collider_desc_default(&box);
        box.shape = DD_SHAPE_BOX;
        box.half_extents = DdVec3{80, 80, 0.5f};
        box.transform.position = DdVec3{std::sin(angle) * 0.5f, 0, -std::cos(angle) * 0.5f};
        box.transform.rotation = DdQuat{0, std::sin(-angle * 0.5f), 0, std::cos(-angle * 0.5f)};
        REQUIRE(dragdoll_world_add_colliders(world, &box, 1, &ground) == DD_OK);
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid18");
        cd.transform.position = DdVec3{0, 0, height};
        REQUIRE(dragdoll_character_create(world, &cd, &human) == DD_OK);
    }
    ~Scene() { dragdoll_world_destroy(world); }

    DdSenses senses() const
    {
        DdSenses s{};
        s.struct_size = sizeof(DdSenses);
        REQUIRE(dragdoll_character_get_senses(human, &s) == DD_OK);
        return s;
    }
    std::vector<DdBodyContact> contacts() const
    {
        std::vector<DdBodyContact> c(dragdoll_character_get_body_count(human));
        CHECK(dragdoll_character_get_contacts(human, c.data(), static_cast<uint32_t>(c.size())) == c.size());
        return c;
    }
    const DdBodyContact contact(const char* body) const
    {
        return contacts()[static_cast<size_t>(dragdoll_character_find_body(human, body))];
    }
    void step(int steps) { REQUIRE(dragdoll_world_step_fixed(world, static_cast<uint32_t>(steps)) == DD_OK); }
    void brace()
    {
        REQUIRE(dragdoll_character_set_tension(human, DD_TENSION_TENSE, 0.0f) == DD_OK);
        REQUIRE(dragdoll_character_set_balance(human, 1) == DD_OK);
    }
};
} // namespace

TEST_CASE("senses: a standing character touches the ground with its feet and knows it is upright, a metre above it")
{
    Library lib;
    Scene scene(0.005f);
    scene.brace();
    scene.step(60);
    const DdSenses s = scene.senses();
    CHECK(s.airborne == 0);
    CHECK(s.airborne_seconds == 0.0f);
    CHECK(s.ground_bodies >= 2);
    CHECK(s.object_bodies == 0);
    CHECK(s.carried_by == DD_INVALID_COLLIDER);
    CHECK(s.ground_height > 0.85f);
    CHECK(s.ground_height < 1.15f);
    CHECK(s.ground_normal.z > 0.99f);
    CHECK(s.upright > 0.95f);
    CHECK(s.landing_seconds < 0.0f);
    // Standing still it reads what an accelerometer would: the ground holding it up against gravity, g x dt = 0.082
    // m/s per step. (Free fall reads zero: see the next test.) Far below any hit.
    CHECK(s.impact_delta_v == doctest::Approx(9.81f / 120.0f).epsilon(0.25));
    for (const char* foot : {"foot_l", "foot_r"})
    {
        CAPTURE(foot);
        const DdBodyContact c = scene.contact(foot);
        CHECK((c.flags & DD_CONTACT_GROUND) != 0);
        CHECK(c.collider == scene.ground);
        CHECK(c.normal.z > 0.9f); // The ground pushes up into the foot
        CHECK(std::fabs(c.point.z) < 0.03f);
    }
    CHECK(scene.contact("head").flags == 0);
}

TEST_CASE("senses: free fall is airborne and not a hit; the landing is predicted, and felt when it comes")
{
    Library lib;
    Scene scene(3.0f); // Limp, feet 3 m above the ground
    scene.step(2);
    CHECK(scene.senses().airborne == 0); // Not yet: 3 steps without contact are needed
    scene.step(22);                      // 0.2 s into the fall
    const DdSenses falling = scene.senses();
    CHECK(falling.airborne == 1);
    CHECK(falling.airborne_seconds == doctest::Approx(0.2f).epsilon(0.02));
    CHECK(falling.ground_bodies == 0);
    CHECK(falling.impact_delta_v < 0.02f); // Gravity alone: no velocity change is reported
    CHECK(falling.head_delta_v < 0.05f);
    CHECK(falling.peak_impact_delta_v < 0.05f);
    // The centre of mass started about 3.97 m up and has fallen 0.2 m; it lands (0.25 m above the ground) after
    // sqrt(2 * 3.72 / 9.81) = 0.87 s of fall in all, so about 0.67 s from now.
    MESSAGE("0.2 s into a 3 m drop: ground " << falling.ground_height << " m below, landing in " << falling.landing_seconds
                                            << " s, first body " << falling.landing_body);
    CHECK(falling.ground_height == doctest::Approx(3.77f).epsilon(0.03));
    CHECK(falling.landing_seconds == doctest::Approx(0.67f).epsilon(0.06));
    REQUIRE(falling.landing_body >= 0);
    // Falling feet first without spin: a foot arrives first.
    const std::string first = dragdoll_character_get_body_name(scene.human, static_cast<uint32_t>(falling.landing_body));
    CHECK(first.rfind("foot", 0) == 0);

    float before = falling.landing_seconds;
    scene.step(24); // 0.2 s later the prediction has come 0.2 s closer
    CHECK(scene.senses().landing_seconds == doctest::Approx(before - 0.2f).epsilon(0.08));

    scene.step(480); // Landed, folded and lying
    const DdSenses landed = scene.senses();
    CHECK(landed.airborne == 0);
    CHECK(landed.ground_bodies >= 3);
    CHECK(landed.landing_seconds < 0.0f);
    CHECK(landed.ground_height < 0.5f);
    MESSAGE("landing from 3 m: peak velocity change " << landed.peak_impact_delta_v << " m/s for the whole body, " << landed.peak_head_delta_v
                                                     << " m/s for the head (in one 1/120 s step)");
    CHECK(landed.peak_impact_delta_v > 1.0f);
    CHECK(landed.peak_head_delta_v > 1.0f);
    CHECK(std::fabs(landed.upright) < 0.6f); // No longer standing
}

TEST_CASE("senses: a car hit is felt as contact with that car, from its direction, and as a hard velocity change")
{
    Library lib;
    Scene scene(0.005f);
    scene.brace();
    const VehicleProfile sedan = parseVehicle(builtinVehicle("sedan"));
    const VehicleMesh mesh = buildVehicleMesh(sedan, DdVec3{1, 0, 0}, DdVec3{0, 1, 0}, DdVec3{0, 0, 1});
    DdColliderDesc car;
    dragdoll_collider_desc_default(&car);
    car.shape = DD_SHAPE_MESH;
    car.motion = DD_MOTION_KINEMATIC;
    car.vertices = mesh.vertices.data();
    car.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
    car.indices = mesh.indices.data();
    car.index_count = static_cast<uint32_t>(mesh.indices.size());
    float x = -6.0f - sedan.length * 0.5f;
    car.transform.position = DdVec3{x, 0, 0};
    car.group = 9;
    DdColliderId carId = DD_INVALID_COLLIDER;
    REQUIRE(dragdoll_world_add_colliders(scene.world, &car, 1, &carId) == DD_OK);

    bool touched = false, carried = false;
    float fromBehind = 0.0f;
    uint32_t group = 0;
    for (int f = 0; f < 120 && x < 3.0f; ++f) // 11 m/s (40 km/h) along +X
    {
        x += 11.0f / 120.0f;
        const DdTransform target{{x, 0, 0}, {0, 0, 0, 1}};
        REQUIRE(dragdoll_world_set_kinematic_targets(scene.world, &carId, &target, 1) == DD_OK);
        REQUIRE(dragdoll_world_step(scene.world, 1.0f / 120.0f, nullptr) == DD_OK);
        const DdSenses s = scene.senses();
        carried = carried || s.carried_by == carId;
        for (const DdBodyContact& c : scene.contacts())
        {
            if ((c.flags & DD_CONTACT_OBJECT) != 0 && !touched)
            {
                touched = true;
                CHECK(c.collider == carId);
                group = c.group;
                fromBehind = c.normal.x; // The car pushes along its travel
            }
        }
    }
    const DdSenses after = scene.senses();
    CHECK(touched);
    CHECK(carried);
    CHECK(group == 9);
    CHECK(fromBehind > 0.5f);
    MESSAGE("sedan at 40 km/h: peak velocity change " << after.peak_impact_delta_v << " m/s for the whole body, " << after.peak_head_delta_v
                                                     << " m/s for the head (in one 1/120 s step)");
    CHECK(after.peak_impact_delta_v > 1.0f);
    CHECK(after.peak_head_delta_v > 1.0f);
}

TEST_CASE("senses: a host-animated character feels no contacts or hits, but still knows the ground below it")
{
    Library lib;
    Scene scene(0.005f);
    REQUIRE(dragdoll_character_set_kinematic(scene.human, 1) == DD_OK);
    scene.step(30);
    const DdSenses s = scene.senses();
    CHECK(s.airborne == 0); // Unknown rather than "in the air": nothing pushes a kinematic body
    CHECK(s.ground_bodies == 0);
    CHECK(s.impact_delta_v == 0.0f);
    CHECK(s.ground_height > 0.85f);
    CHECK(s.upright > 0.95f);
    for (const DdBodyContact& c : scene.contacts()) CHECK(c.flags == 0);

    // Handing over (kinematic -> dynamic) restarts the peaks.
    REQUIRE(dragdoll_character_set_kinematic(scene.human, 0) == DD_OK);
    scene.step(60);
    CHECK(scene.senses().ground_bodies > 0);
}

TEST_CASE("senses: the ground's normal follows a slope")
{
    Library lib;
    Scene scene(1.0f, 9.81f, 0, 15.0f);
    scene.step(1);
    const DdSenses s = scene.senses();
    const float angle = 15.0f * 3.14159265f / 180.0f;
    CHECK(s.ground_normal.x == doctest::Approx(-std::sin(angle)).epsilon(0.02));
    CHECK(s.ground_normal.z == doctest::Approx(std::cos(angle)).epsilon(0.02));
    CHECK(s.ground_height > 1.5f); // Straight down to the ramp from about 2 m up
}

TEST_CASE("senses: angular momentum appears with a push off-centre and is kept in free flight")
{
    Library lib;
    Scene scene(5.0f, 0.0f); // Floating, limp
    scene.step(2);
    const DdVec3 still = scene.senses().angular_momentum;
    CHECK(std::sqrt(still.x * still.x + still.y * still.y + still.z * still.z) < 0.05f);
    REQUIRE(dragdoll_character_apply_impulse(scene.human, static_cast<uint32_t>(dragdoll_character_find_body(scene.human, "head")),
                                             DdVec3{30.0f, 0, 0}, nullptr) == DD_OK); // 30 N*s at the head: the body tumbles
    scene.step(2);
    const DdVec3 a = scene.senses().angular_momentum;
    const float size = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    MESSAGE("30 N*s at the head: angular momentum " << size << " kg m^2/s");
    // About r x p: the head is roughly 0.65 m above the centre of mass.
    CHECK(size > 12.0f);
    CHECK(size < 28.0f);
    CHECK(std::fabs(a.y) > 0.9f * size); // Pushed along X, lever along Z: it turns about Y
    scene.step(60);
    const DdVec3 b = scene.senses().angular_momentum;
    const float later = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    CHECK(later == doctest::Approx(size).epsilon(0.1)); // Nothing outside acts on it: conserved while the limbs flail
    CHECK(scene.senses().airborne == 1);
    CHECK(scene.senses().landing_seconds < 0.0f); // No gravity: no landing
}

TEST_CASE("senses: the same with and without worker threads (contact callbacks arrive in any order)")
{
    Library lib;
    std::vector<std::vector<DdBodyContact>> contacts;
    std::vector<DdSenses> senses;
    for (uint32_t threads : {0u, 4u})
    {
        Scene scene(1.0f, 9.81f, threads);
        REQUIRE(dragdoll_character_apply_impulse(scene.human, 0, DdVec3{40.0f, 15.0f, 0}, nullptr) == DD_OK);
        scene.step(150); // Falls, lands and tumbles
        contacts.push_back(scene.contacts());
        senses.push_back(scene.senses());
    }
    REQUIRE(contacts[0].size() == contacts[1].size());
    CHECK(senses[0].ground_bodies > 0);
    CHECK(senses[0].ground_bodies == senses[1].ground_bodies);
    CHECK(senses[0].peak_head_delta_v == senses[1].peak_head_delta_v);
    CHECK(senses[0].peak_impact_delta_v == senses[1].peak_impact_delta_v);
    CHECK(senses[0].ground_height == senses[1].ground_height);
    for (size_t i = 0; i < contacts[0].size(); ++i)
    {
        CAPTURE(i);
        CHECK(contacts[0][i].flags == contacts[1][i].flags);
        CHECK(contacts[0][i].collider == contacts[1][i].collider);
        CHECK(contacts[0][i].point.x == contacts[1][i].point.x);
        CHECK(contacts[0][i].point.z == contacts[1][i].point.z);
        CHECK(contacts[0][i].delta_v == contacts[1][i].delta_v);
    }
}
