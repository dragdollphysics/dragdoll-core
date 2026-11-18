// SPDX-License-Identifier: MIT
// Behaviours (ADR-0025, task 4.5): tension per body region, the behaviour runner (percept in, muscle command out),
// awareness, vitality, live parameters, and that all of it is recorded and replays bit-identically.
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/vehicles.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
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

// Z-up world, ground, a kinematic human at the origin turned by `yawDegrees` about up, and a sedan that drives along
// +X towards it from 20 m away at 10 m/s.
struct Scene
{
    DdWorld* world = nullptr;
    DdCharacter* human = nullptr;
    DdColliderId car = DD_INVALID_COLLIDER;
    VehicleMesh mesh;
    float x = -20.0f;

    explicit Scene(float yawDegrees, const char* record = nullptr)
    {
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.up = DdVec3{0, 0, 1};
        desc.worker_threads = 0;
        desc.record_path = record;
        REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
        DdColliderDesc ground;
        dragdoll_collider_desc_default(&ground);
        ground.shape = DD_SHAPE_BOX;
        ground.half_extents = DdVec3{80, 80, 0.5f};
        ground.transform.position = DdVec3{0, 0, -0.5f};
        DdColliderId id;
        REQUIRE(dragdoll_world_add_colliders(world, &ground, 1, &id) == DD_OK);
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
        cd.transform.position = DdVec3{0, 0, 0.01f};
        const float half = yawDegrees * 3.14159265f / 360.0f;
        cd.transform.rotation = DdQuat{0, 0, std::sin(half), std::cos(half)};
        REQUIRE(dragdoll_character_create(world, &cd, &human) == DD_OK);
        REQUIRE(dragdoll_character_set_kinematic(human, 1) == DD_OK);
        const VehicleProfile sedan = parseVehicle(builtinVehicle("sedan"));
        mesh = buildVehicleMesh(sedan, DdVec3{1, 0, 0}, DdVec3{0, 1, 0}, DdVec3{0, 0, 1});
        DdColliderDesc c;
        dragdoll_collider_desc_default(&c);
        c.shape = DD_SHAPE_MESH;
        c.motion = DD_MOTION_KINEMATIC;
        c.vertices = mesh.vertices.data();
        c.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
        c.indices = mesh.indices.data();
        c.index_count = static_cast<uint32_t>(mesh.indices.size());
        c.transform.position = DdVec3{x, 0, 0};
        c.group = 1;
        REQUIRE(dragdoll_world_add_colliders(world, &c, 1, &car) == DD_OK);
    }
    ~Scene() { dragdoll_world_destroy(world); }

    // Drives the car for `seconds` at 10 m/s along +X (lateral position y), 60 host frames a second.
    void drive(float seconds, float y = 0.0f)
    {
        for (int f = 0; f < static_cast<int>(std::lround(seconds * 60.0f)); ++f)
        {
            x += 10.0f / 60.0f;
            const DdTransform target{{x, y, 0}, {0, 0, 0, 1}};
            REQUIRE(dragdoll_world_set_kinematic_targets(world, &car, &target, 1) == DD_OK);
            REQUIRE(dragdoll_world_step(world, 1.0f / 60.0f, nullptr) == DD_OK);
        }
    }
    bool allRegions(DdTension tension) const
    {
        for (int r = 0; r < DD_REGION_COUNT; ++r)
        {
            if (dragdoll_character_get_region_tension(human, static_cast<DdRegion>(r)) != tension) return false;
        }
        return true;
    }
    float timeToCollision() const
    {
        DdThreat t{};
        t.struct_size = sizeof(DdThreat);
        REQUIRE(dragdoll_character_get_threat(human, &t) == DD_OK);
        return t.collider != DD_INVALID_COLLIDER ? t.time_to_collision : -1.0f;
    }
    std::string status() const { return dragdoll_character_get_behaviour_status(human); }
};

// How far the rig must be turned about up so that it faces `direction`. Found from the pose (feet point forward),
// not assumed.
float yawFacing(DdVec3 direction)
{
    Library lib;
    Scene probe(0.0f);
    std::vector<DdTransform> bodies(dragdoll_character_get_body_count(probe.human));
    REQUIRE(dragdoll_character_get_body_transforms(probe.human, 0, bodies.data(), static_cast<uint32_t>(bodies.size())) == DD_OK);
    const int foot = dragdoll_character_find_body(probe.human, "foot_l"), shin = dragdoll_character_find_body(probe.human, "shin_l");
    REQUIRE(foot >= 0);
    REQUIRE(shin >= 0);
    const float fx = bodies[static_cast<size_t>(foot)].position.x - bodies[static_cast<size_t>(shin)].position.x;
    const float fy = bodies[static_cast<size_t>(foot)].position.y - bodies[static_cast<size_t>(shin)].position.y;
    REQUIRE(std::hypot(fx, fy) > 0.02f); // The foot's centre lies ahead of the shin's
    return (std::atan2(direction.y, direction.x) - std::atan2(fy, fx)) * 180.0f / 3.14159265f;
}
} // namespace

TEST_CASE("regions: every body has a region, tension can be set per region, set_tension sets them all")
{
    Library lib;
    Scene scene(0.0f);
    const struct
    {
        const char* body;
        DdRegion region;
    } expected[] = {{"pelvis", DD_REGION_TRUNK},        {"lower_spine", DD_REGION_TRUNK},    {"upper_spine", DD_REGION_TRUNK},
                    {"head", DD_REGION_HEAD_NECK},      {"upper_arm_l", DD_REGION_ARM_LEFT}, {"hand_l", DD_REGION_ARM_LEFT},
                    {"forearm_r", DD_REGION_ARM_RIGHT}, {"thigh_l", DD_REGION_LEG_LEFT},     {"foot_r", DD_REGION_LEG_RIGHT}};
    for (const auto& e : expected)
    {
        CAPTURE(e.body);
        const int body = dragdoll_character_find_body(scene.human, e.body);
        REQUIRE(body >= 0);
        CHECK(dragdoll_character_get_body_region(scene.human, static_cast<uint32_t>(body)) == e.region);
    }
    CHECK(dragdoll_character_get_body_region(scene.human, 999) == DD_REGION_COUNT);

    REQUIRE(dragdoll_character_set_tension(scene.human, DD_TENSION_RELAXED, 0.0f) == DD_OK);
    CHECK(scene.allRegions(DD_TENSION_RELAXED));
    REQUIRE(dragdoll_character_set_region_tension(scene.human, DD_REGION_ARM_LEFT, DD_TENSION_TENSE, 0.1f) == DD_OK);
    CHECK(dragdoll_character_get_region_tension(scene.human, DD_REGION_ARM_LEFT) == DD_TENSION_TENSE);
    CHECK(dragdoll_character_get_region_tension(scene.human, DD_REGION_ARM_RIGHT) == DD_TENSION_RELAXED);
    CHECK(dragdoll_character_get_tension(scene.human) == DD_TENSION_RELAXED); // The whole-body setting is unchanged
    CHECK(dragdoll_character_set_region_tension(scene.human, DD_REGION_COUNT, DD_TENSION_TENSE, 0.0f) == DD_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("regions: a tensed arm holds its pose under a push that swings the limp arm")
{
    Library lib;
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    desc.up = DdVec3{0, 0, 1};
    desc.gravity = 0.0f; // Floating: only the pushes move anything
    desc.worker_threads = 0;
    DdWorld* world = nullptr;
    REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
    DdCharacterDesc cd;
    dragdoll_character_desc_default(&cd);
    cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
    cd.transform.position = DdVec3{0, 0, 5.0f};
    DdCharacter* c = nullptr;
    REQUIRE(dragdoll_character_create(world, &cd, &c) == DD_OK);
    REQUIRE(dragdoll_character_set_region_tension(c, DD_REGION_ARM_LEFT, DD_TENSION_TENSE, 0.0f) == DD_OK);
    REQUIRE(dragdoll_world_step_fixed(world, 12) == DD_OK);
    // The same push on both hands, away from the body's front.
    const DdVec3 push{1.5f, 1.5f, 1.5f};
    REQUIRE(dragdoll_character_apply_impulse(c, static_cast<uint32_t>(dragdoll_character_find_body(c, "hand_l")), push, nullptr) == DD_OK);
    REQUIRE(dragdoll_character_apply_impulse(c, static_cast<uint32_t>(dragdoll_character_find_body(c, "hand_r")), push, nullptr) == DD_OK);
    REQUIRE(dragdoll_world_step_fixed(world, 48) == DD_OK); // 0.4 s later
    const uint32_t count = dragdoll_character_get_body_count(c);
    std::vector<DdQuat> pose(count);
    DdTransform root{};
    REQUIRE(dragdoll_character_get_pose(c, 0, pose.data(), count, &root) == DD_OK);
    const auto bend = [&](const char* name) {
        const DdQuat& q = pose[static_cast<size_t>(dragdoll_character_find_body(c, name))];
        return 2.0f * std::acos(std::fmin(1.0f, std::fabs(q.w))) * 180.0f / 3.14159265f;
    };
    const float tense = bend("upper_arm_l") + bend("forearm_l"), limp = bend("upper_arm_r") + bend("forearm_r");
    MESSAGE("0.4 s after the same push on both hands: tense arm " << tense << " deg from rest, limp arm " << limp << " deg");
    CHECK(tense < 10.0f);
    CHECK(limp > 3.0f * tense);
    dragdoll_world_destroy(world);
}

TEST_CASE("behaviour: car_hit startles an aware character after the reflex latency; an unaware one stays as it is")
{
    const float facing = yawFacing(DdVec3{-1, 0, 0}); // Towards the oncoming car
    Library lib;
    {
        Scene scene(facing);
        CHECK(std::string(dragdoll_character_get_behaviour(scene.human)).empty());
        CHECK(dragdoll_character_set_behaviour(scene.human, "no_such_behaviour") == DD_ERROR_INVALID_ARGUMENT);
        REQUIRE(dragdoll_character_set_behaviour(scene.human, "car_hit") == DD_OK);
        CHECK(std::string(dragdoll_character_get_behaviour(scene.human)) == "car_hit");
        REQUIRE(dragdoll_character_set_tension(scene.human, DD_TENSION_RELAXED, 0.0f) == DD_OK);

        scene.drive(0.3f); // About 1.4 s away: watched, nothing yet
        CHECK(scene.timeToCollision() > 1.25f);
        CHECK(scene.status() == "watch");
        CHECK(scene.allRegions(DD_TENSION_RELAXED));

        // Step until the threat is inside the 1.2 s window, then count how long the startle takes.
        int steps = 0;
        while (scene.status() == "watch" && steps < 200)
        {
            scene.x += 10.0f / 120.0f;
            const DdTransform target{{scene.x, 0, 0}, {0, 0, 0, 1}};
            REQUIRE(dragdoll_world_set_kinematic_targets(scene.world, &scene.car, &target, 1) == DD_OK);
            REQUIRE(dragdoll_world_step(scene.world, 1.0f / 120.0f, nullptr) == DD_OK);
            ++steps;
        }
        REQUIRE(steps < 200);
        const float entered = scene.timeToCollision();
        CHECK(entered <= 1.21f);
        CHECK(entered > 1.15f);
        CHECK(scene.allRegions(DD_TENSION_RELAXED)); // Aware, but the reflex has not fired yet
        int latency = 0;
        while (!scene.allRegions(DD_TENSION_TENSE) && latency < 200)
        {
            scene.x += 10.0f / 120.0f;
            const DdTransform target{{scene.x, 0, 0}, {0, 0, 0, 1}};
            REQUIRE(dragdoll_world_set_kinematic_targets(scene.world, &scene.car, &target, 1) == DD_OK);
            REQUIRE(dragdoll_world_step(scene.world, 1.0f / 120.0f, nullptr) == DD_OK);
            ++latency;
        }
        MESSAGE("startle " << latency << " steps (" << static_cast<float>(latency) / 120.0f << " s) after the threat entered the window");
        CHECK(latency >= 11); // 0.1 s reflex latency = 12 steps, give or take the step it is counted on
        CHECK(latency <= 13);
        CHECK(scene.status() == "anticipate: startle");
    }
    {
        Scene scene(facing + 180.0f); // Back to the car: it never sees it
        REQUIRE(dragdoll_character_set_behaviour(scene.human, "car_hit") == DD_OK);
        REQUIRE(dragdoll_character_set_tension(scene.human, DD_TENSION_RELAXED, 0.0f) == DD_OK);
        scene.drive(1.3f);
        CHECK(scene.timeToCollision() >= 0.0f);
        CHECK(scene.timeToCollision() < 0.6f);
        CHECK(scene.allRegions(DD_TENSION_RELAXED));
        CHECK(scene.status() == "watch");

        // The host can say it knows anyway (it heard the horn)...
        REQUIRE(dragdoll_character_set_awareness(scene.human, 1.0f) == DD_OK);
        scene.drive(0.2f);
        CHECK(scene.allRegions(DD_TENSION_TENSE));
    }
    {
        Scene scene(facing); // ...or that there is nothing left to react with.
        REQUIRE(dragdoll_character_set_behaviour(scene.human, "car_hit") == DD_OK);
        REQUIRE(dragdoll_character_set_tension(scene.human, DD_TENSION_RELAXED, 0.0f) == DD_OK);
        REQUIRE(dragdoll_character_set_vitality(scene.human, 0.0f) == DD_OK);
        scene.drive(1.3f);
        // Vitality 0 stops the chain: the body is the limp tier (ADR-0025 §2), no startle.
        CHECK(scene.allRegions(DD_TENSION_LIMP));
        CHECK(scene.status() == "limp");
    }
}

TEST_CASE("behaviour: car_hit after contact - auto handover, struck leg yields, brace after the reflex latency, carried, after")
{
    const float facing = yawFacing(DdVec3{-1, 0, 0});
    Library lib;
    for (bool aware : {false, true})
    {
        CAPTURE(aware);
        Scene scene(aware ? facing : facing + 90.0f); // Aware: facing the car; unaware: side-on, looking away
        REQUIRE(dragdoll_character_set_behaviour(scene.human, "car_hit") == DD_OK);
        REQUIRE(dragdoll_character_set_tension(scene.human, DD_TENSION_RELAXED, 0.0f) == DD_OK);
        REQUIRE(dragdoll_character_set_auto_handover(scene.human, 0.0125f) == DD_OK);
        CHECK(dragdoll_character_get_auto_handover(scene.human) == doctest::Approx(0.0125f));
        if (!aware)
        {
            REQUIRE(dragdoll_character_set_awareness(scene.human, 0.0f) == DD_OK);
        }

        // Drive at 120 host frames a second and note when things happen.
        double handover = -1, impact = -1, brace = -1, carried = -1, after = -1, guarded = -1;
        bool legYielded = false;
        for (int f = 0; f < 3 * 120; ++f)
        {
            scene.x += 10.0f / 120.0f;
            const DdTransform target{{scene.x, 0, 0}, {0, 0, 0, 1}};
            REQUIRE(dragdoll_world_set_kinematic_targets(scene.world, &scene.car, &target, 1) == DD_OK);
            REQUIRE(dragdoll_world_step(scene.world, 1.0f / 120.0f, nullptr) == DD_OK);
            const double t = f / 120.0;
            const std::string status = scene.status();
            if (handover < 0 && dragdoll_character_get_kinematic(scene.human) == 0) handover = t;
            if (impact < 0 && status.rfind("impact", 0) == 0)
            {
                impact = t;
                // A leg braced by the startle gives at once where the bumper meets it.
                legYielded = dragdoll_character_get_region_tension(scene.human, DD_REGION_LEG_LEFT) == DD_TENSION_RELAXED ||
                             dragdoll_character_get_region_tension(scene.human, DD_REGION_LEG_RIGHT) == DD_TENSION_RELAXED;
            }
            // The reflex co-contraction (the trunk may already be easing again if the body lies on the car in the same step).
            if (brace < 0 && impact >= 0 && dragdoll_character_get_region_tension(scene.human, DD_REGION_HEAD_NECK) == DD_TENSION_TENSE)
            {
                brace = t;
            }
            if (carried < 0 && status.rfind("carried", 0) == 0) carried = t;
            if (guarded < 0 && status.find("guard") != std::string::npos) guarded = t;
            if (after < 0 && (status.rfind("airborne", 0) == 0 || status.rfind("landing", 0) == 0 || status.rfind("tumble", 0) == 0)) after = t;
        }
        MESSAGE(std::string(aware ? "aware" : "unaware") << ": handover " << handover << " s, impact " << impact << ", brace " << brace
                                              << ", carried " << carried << ", after " << after);
        REQUIRE(handover >= 0.0);
        REQUIRE(impact >= 0.0);
        CHECK(impact - handover < 0.05); // Handed over just before the contact
        CHECK(handover <= impact);
        REQUIRE(brace >= 0.0);
        if (!aware) // (An aware character's neck is already tense from the startle.)
        {
            CHECK(brace - impact >= 0.08); // The 0.1 s reflex latency, give or take the step it is counted on
            CHECK(brace - impact <= 0.11);
        }
        CHECK(after > brace);         // It left the car in the end
        REQUIRE(guarded >= 0.0);
        MESSAGE("guard " << guarded - impact << " s after contact");
        CHECK(guarded - carried <= 0.01); // Guards as soon as it lies on the car (reflexes available by then)
        CHECK(guarded < after);           // ...and only on the car: never starts in the air
        if (aware)
        {
            CHECK(legYielded); // The startle had tensed the legs
        }
    }
}

TEST_CASE("behaviour: without auto handover a host-animated character stays kinematic as the car passes")
{
    const float facing = yawFacing(DdVec3{-1, 0, 0});
    Library lib;
    Scene scene(facing + 90.0f);
    scene.drive(3.0f);
    CHECK(dragdoll_character_get_kinematic(scene.human) == 1);
    CHECK(dragdoll_character_get_auto_handover(scene.human) == 0.0f);
}

TEST_CASE("behaviour: a car that swerves away lets the character relax again; parameters can be changed live")
{
    const float facing = yawFacing(DdVec3{-1, 0, 0});
    Library lib;
    Scene scene(facing);
    REQUIRE(dragdoll_character_set_behaviour(scene.human, "car_hit") == DD_OK);
    REQUIRE(dragdoll_character_set_tension(scene.human, DD_TENSION_RELAXED, 0.0f) == DD_OK);
    // A much shorter window and a slower reflex than the pack's defaults.
    const char* slow = "{\"format\":\"dragdoll-behaviour\",\"version\":1,\"params\":{\"anticipate\":{\"time\":0.6},\"reaction\":{\"reflex\":0.3}}}";
    CHECK(dragdoll_world_set_behaviour_params(scene.world, "car_hit", "{not json") == DD_ERROR_INVALID_ARGUMENT);
    REQUIRE(dragdoll_world_set_behaviour_params(scene.world, "car_hit", slow) == DD_OK);
    scene.drive(0.9f); // About 0.85 s away: inside the default window (1.2 s), outside the new one (0.6 s)
    CHECK(scene.timeToCollision() < 0.95f);
    CHECK(scene.timeToCollision() > 0.65f);
    CHECK(scene.allRegions(DD_TENSION_RELAXED));
    scene.drive(0.4f); // About 0.45 s away: inside for some 0.15 s, the 0.3 s reflex has not fired
    CHECK(scene.allRegions(DD_TENSION_RELAXED));
    scene.drive(0.25f);
    CHECK(scene.allRegions(DD_TENSION_TENSE));

    // The car is gone from its path (moved 6 m aside and past): no threat any more; after the recover delay the
    // tension eases back to what it was.
    scene.x += 12.0f;
    scene.drive(0.3f, 6.0f);
    CHECK(scene.timeToCollision() < 0.0f);
    CHECK(scene.allRegions(DD_TENSION_TENSE)); // Still braced for a moment
    scene.drive(0.6f, 6.0f);
    CHECK(scene.allRegions(DD_TENSION_RELAXED));
    CHECK(scene.status() == "watch");
}

TEST_CASE("behaviour: a session with a behaviour, live parameters, awareness and region tension replays bit-identically")
{
    const float facing = yawFacing(DdVec3{-1, 0, 0});
    Library lib;
    const char* path = "behaviour_replay_test.ddrec";
    uint64_t recordedHash = 0;
    {
        Scene scene(facing, path);
        REQUIRE(dragdoll_character_set_kinematic(scene.human, 0) == DD_OK); // Simulated: the muscles matter
        REQUIRE(dragdoll_character_set_tension(scene.human, DD_TENSION_RELAXED, 0.0f) == DD_OK);
        REQUIRE(dragdoll_character_set_region_tension(scene.human, DD_REGION_LEG_LEFT, DD_TENSION_STUNNED, 0.2f) == DD_OK);
        REQUIRE(dragdoll_character_set_behaviour(scene.human, "car_hit") == DD_OK);
        REQUIRE(dragdoll_world_set_behaviour_params(
                    scene.world, "car_hit",
                    "{\"format\":\"dragdoll-behaviour\",\"version\":1,\"params\":{\"anticipate\":{\"time\":1.0},\"reaction\":{\"reflex\":0.15}}}") ==
                DD_OK);
        REQUIRE(dragdoll_character_set_awareness(scene.human, 1.0f) == DD_OK);
        REQUIRE(dragdoll_character_set_vitality(scene.human, 0.8f) == DD_OK);
        scene.drive(2.4f); // Through the startle and the impact
        REQUIRE(dragdoll_world_compute_state_hash(scene.world, &recordedHash) == DD_OK);
    }
    DdReplay* replay = nullptr;
    REQUIRE(dragdoll_replay_open(path, &replay) == DD_OK);
    DdReplayFrame frame{};
    frame.struct_size = sizeof(DdReplayFrame);
    int frames = 0;
    bool identical = true;
    while (dragdoll_replay_advance(replay, &frame) == DD_OK && frame.at_end == 0 && frames < 100000)
    {
        ++frames;
        identical = identical && frame.hash_matches != 0;
    }
    CHECK(frames > 100);
    CHECK(identical); // Every recorded frame, not only the last
    uint64_t replayedHash = 0;
    REQUIRE(dragdoll_world_compute_state_hash(dragdoll_replay_get_world(replay), &replayedHash) == DD_OK);
    CHECK(replayedHash == recordedHash);
    DdCharacter* replayed = dragdoll_world_get_character(dragdoll_replay_get_world(replay), 0);
    REQUIRE(replayed != nullptr);
    CHECK(std::string(dragdoll_character_get_behaviour(replayed)) == "car_hit");
    CHECK(dragdoll_character_get_vitality(replayed) == doctest::Approx(0.8f));
    dragdoll_replay_close(replay);
    std::remove(path);
}

TEST_CASE("vehicle pass-through: a car driving over a landed body does not launch it (ADR-0026)")
{
    const float facing = yawFacing(DdVec3{-1, 0, 0});
    Library lib;
    float fastest[2] = {0.0f, 0.0f};
    for (int pass = 0; pass < 2; ++pass)
    {
        CAPTURE(pass);
        Scene scene(facing + 90.0f);
        REQUIRE(dragdoll_character_set_auto_handover(scene.human, 0.0125f) == DD_OK);
        REQUIRE(dragdoll_character_set_vehicle_pass_through(scene.human, pass) == DD_OK);
        CHECK(dragdoll_character_get_vehicle_pass_through(scene.human) == pass);
        // The car hits at 10 m/s and brakes to a stop; the body lands ahead of it; 3 s later the car drives on at
        // 8 m/s, over the body lying on the road.
        float v = 10.0f, stopped = -1.0f;
        DdVec3 last{};
        bool haveLast = false, overBody = false;
        for (int f = 0; f < 12 * 120; ++f)
        {
            const float t = f / 120.0f;
            DdImpactReport r{};
            r.struct_size = sizeof(DdImpactReport);
            REQUIRE(dragdoll_character_get_impact_report(scene.human, &r) == DD_OK);
            const bool landed = r.valid != 0 && r.ground_time >= 0.0f;
            if (r.valid != 0 && stopped < 0.0f) v = std::fmax(0.0f, v - 7.0f / 120.0f); // Brakes after contact
            if (stopped < 0.0f && r.valid != 0 && v == 0.0f) stopped = t;
            if (stopped >= 0.0f && t > stopped + 3.0f) v = std::fmin(8.0f, v + 3.0f / 120.0f); // Drives on
            scene.x += v / 120.0f;
            const DdTransform target{{scene.x, 0, 0}, {0, 0, 0, 1}};
            REQUIRE(dragdoll_world_set_kinematic_targets(scene.world, &scene.car, &target, 1) == DD_OK);
            REQUIRE(dragdoll_world_step(scene.world, 1.0f / 120.0f, nullptr) == DD_OK);
            DdTransform root{};
            REQUIRE(dragdoll_character_get_pose(scene.human, 0, nullptr, 0, &root) == DD_OK);
            if (stopped >= 0.0f && t > stopped + 3.0f && landed && haveLast)
            {
                const float dx = root.position.x - last.x, dy = root.position.y - last.y, dz = root.position.z - last.z;
                fastest[pass] = std::fmax(fastest[pass], std::sqrt(dx * dx + dy * dy + dz * dz) * 120.0f);
                if (scene.x > root.position.x) overBody = true; // The car's centre went past the body
            }
            last = root.position;
            haveLast = true;
        }
        REQUIRE(stopped >= 0.0f);
        if (pass == 1) CHECK(overBody); // Colliding, the car pushes the body ahead of it instead
    }
    MESSAGE("fastest pelvis while the car drives over the body: " << fastest[0] << " m/s colliding, " << fastest[1] << " m/s passing through (car up to 8 m/s)");
    CHECK(fastest[0] > 3.0f);  // Colliding, the car shoves the body (the in-game launches)
    CHECK(fastest[1] < 0.5f);  // Passing through, the body stays where it lies
}
