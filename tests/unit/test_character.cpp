// SPDX-License-Identifier: MIT
// Characters: rig loading, the humanoid16 ragdoll, joint limits and determinism.

#include <dragdoll_scenarios/scenario.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

using dragdoll::scenarios::Instance;
using dragdoll::scenarios::metrics::hingeBendDegrees;

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

struct World
{
    DdWorld* ptr = nullptr;
    explicit World(DdWorldDesc desc) { REQUIRE(dragdoll_world_create(&desc, &ptr) == DD_OK); }
    ~World() { dragdoll_world_destroy(ptr); }
    operator DdWorld*() const { return ptr; }
};

DdWorldDesc defaultDesc()
{
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    return desc;
}

DdCharacter* makeCharacter(DdWorld* world, DdVec3 position)
{
    DdCharacterDesc desc;
    dragdoll_character_desc_default(&desc);
    desc.rig_json = dragdoll_get_builtin_rig("humanoid16");
    desc.transform.position = position;
    DdCharacter* character = nullptr;
    REQUIRE_MESSAGE(dragdoll_character_create(world, &desc, &character) == DD_OK, dragdoll_last_error_message());
    return character;
}

std::vector<DdTransform> bodiesOf(DdCharacter* character)
{
    std::vector<DdTransform> out(dragdoll_character_get_body_count(character));
    REQUIRE(dragdoll_character_get_body_transforms(character, 0, out.data(), static_cast<uint32_t>(out.size())) == DD_OK);
    return out;
}

bool finite(const DdTransform& t)
{
    return std::isfinite(t.position.x) && std::isfinite(t.position.y) && std::isfinite(t.position.z) &&
           std::isfinite(t.rotation.x) && std::isfinite(t.rotation.y) && std::isfinite(t.rotation.z) &&
           std::isfinite(t.rotation.w);
}

float distance(const DdVec3& a, const DdVec3& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

uint64_t hashOf(DdWorld* world)
{
    uint64_t h = 0;
    REQUIRE(dragdoll_world_compute_state_hash(world, &h) == DD_OK);
    return h;
}

// How deep (m) an upper-arm capsule reaches into the chest box. Geometry from packs/rigs/humanoid16.json:
// upper arm = capsule radius 0.048, total length 0.28 along local +Y; chest = box half extents (0.15, 0.095, 0.105).
float armIntoChest(const DdTransform& arm, const DdTransform& chest)
{
    using dragdoll::scenarios::metrics::rotate;
    const DdQuat inverse{-chest.rotation.x, -chest.rotation.y, -chest.rotation.z, chest.rotation.w};
    const DdVec3 axis = rotate(arm.rotation, DdVec3{0, 1, 0});
    const float halfSegment = 0.14f - 0.048f;
    const float h[3] = {0.15f, 0.095f, 0.105f};
    float deepest = 0.0f;
    for (int i = 0; i <= 8; ++i)
    {
        const float t = -halfSegment + 2.0f * halfSegment * static_cast<float>(i) / 8.0f;
        const DdVec3 p{arm.position.x + axis.x * t - chest.position.x, arm.position.y + axis.y * t - chest.position.y,
                       arm.position.z + axis.z * t - chest.position.z};
        const DdVec3 q = rotate(inverse, p);
        // Signed distance from the point to the box surface (negative inside).
        const float d[3] = {std::fabs(q.x) - h[0], std::fabs(q.y) - h[1], std::fabs(q.z) - h[2]};
        const float outside = std::sqrt(std::fmax(d[0], 0.0f) * std::fmax(d[0], 0.0f) + std::fmax(d[1], 0.0f) * std::fmax(d[1], 0.0f) +
                                        std::fmax(d[2], 0.0f) * std::fmax(d[2], 0.0f));
        const float inside = std::fmin(std::fmax(d[0], std::fmax(d[1], d[2])), 0.0f);
        deepest = std::fmax(deepest, 0.048f - (outside + inside)); // capsule radius minus distance = penetration
    }
    return deepest;
}

// Indices of the hinge pairs we check every step.
struct Hinges
{
    int thighL, shinL, thighR, shinR, armL, foreL, armR, foreR;
};

Hinges hingesOf(DdCharacter* c)
{
    return Hinges{dragdoll_character_find_body(c, "thigh_l"),     dragdoll_character_find_body(c, "shin_l"),
                  dragdoll_character_find_body(c, "thigh_r"),     dragdoll_character_find_body(c, "shin_r"),
                  dragdoll_character_find_body(c, "upper_arm_l"), dragdoll_character_find_body(c, "forearm_l"),
                  dragdoll_character_find_body(c, "upper_arm_r"), dragdoll_character_find_body(c, "forearm_r")};
}
} // namespace

TEST_CASE("rig: built-in humanoid16 loads with 16 named bodies and a de Leva mass of about 75 kg")
{
    Library lib;
    CHECK(dragdoll_get_builtin_rig("humanoid16") != nullptr);
    CHECK(dragdoll_get_builtin_rig("nope") == nullptr);
    CHECK(dragdoll_get_builtin_rig(nullptr) == nullptr);

    World world(defaultDesc());
    DdCharacter* c = makeCharacter(world, DdVec3{0, 0, 0});
    REQUIRE(dragdoll_character_get_body_count(c) == 16);
    CHECK(std::string(dragdoll_character_get_body_name(c, 0)) == "pelvis");
    CHECK(dragdoll_character_get_body_name(c, 16) == nullptr);
    for (const char* name : {"pelvis", "lower_spine", "upper_spine", "head", "upper_arm_l", "forearm_l", "hand_l",
                             "upper_arm_r", "forearm_r", "hand_r", "thigh_l", "shin_l", "foot_l", "thigh_r", "shin_r", "foot_r"})
    {
        CAPTURE(name);
        CHECK(dragdoll_character_find_body(c, name) >= 0);
    }
    CHECK(dragdoll_character_find_body(c, "tail") == -1);
    MESSAGE("simulated mass: " << dragdoll_character_get_mass(c) << " kg");
    CHECK(dragdoll_character_get_mass(c) == doctest::Approx(75.0f).epsilon(0.10));

    // Rest pose (Y-up world): head on top at ~1.6 m, feet at the bottom, knees and elbows straight.
    const std::vector<DdTransform> rest = bodiesOf(c);
    CHECK(rest[3].position.y == doctest::Approx(1.63f).epsilon(0.03));          // head
    CHECK(rest[12].position.y < 0.1f);                                           // foot_l
    CHECK(rest[12].position.z > 0.0f);                                           // feet point forward (+Z)
    CHECK(std::fabs(hingeBendDegrees(rest[10], rest[11])) < 0.5f);               // knee straight
    CHECK(std::fabs(hingeBendDegrees(rest[4], rest[5])) < 0.5f);                 // elbow straight
    CHECK(rest[4].position.x > 0.0f);                                            // "_l" is on the character's left (+X)
}

TEST_CASE("rig: invalid rigs are rejected with a precise message")
{
    Library lib;
    World world(defaultDesc());
    DdCharacterDesc desc;
    dragdoll_character_desc_default(&desc);
    DdCharacter* c = nullptr;

    CHECK(dragdoll_character_create(world, &desc, &c) == DD_ERROR_INVALID_ARGUMENT); // no rig
    desc.rig_json = "{ not json";
    CHECK(dragdoll_character_create(world, &desc, &c) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(dragdoll_last_error_message()).find("rig: json") != std::string::npos);
    desc.rig_json = R"({"format":"dragdoll-rig","version":1,"name":"x","mass":70,"bodies":[
        {"name":"a","parent":"","mass_percent":60,"shape":{"type":"box","center":[0,1,0],"half_extents":[0.1,0.1,0.1]}},
        {"name":"b","parent":"zzz","mass_percent":40,"shape":{"type":"box","center":[0,2,0],"half_extents":[0.1,0.1,0.1]}}]})";
    CHECK(dragdoll_character_create(world, &desc, &c) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(dragdoll_last_error_message()).find("parent \"zzz\"") != std::string::npos);
    CHECK(c == nullptr);
    desc.struct_size = 8;
    desc.rig_json = dragdoll_get_builtin_rig("humanoid16");
    CHECK(dragdoll_character_create(world, &desc, &c) == DD_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("ragdoll scenarios: no NaN, no explosion, knees and elbows never hyperextend, body comes to rest")
{
    Library lib;
    for (const std::string name : {"drop_from_1m", "fall_forward", "shove_mid_fall"})
    {
        CAPTURE(name);
        Instance instance;
        std::string error;
        REQUIRE_MESSAGE(Instance::make(name, instance, error), error);
        DdCharacter* c = instance.scenario->character();
        REQUIRE(c != nullptr);
        const Hinges h = hingesOf(c);
        const int pelvis = dragdoll_character_find_body(c, "pelvis");

        const int chest = dragdoll_character_find_body(c, "upper_spine");
        float worstKnee = 0.0f, worstElbow = 0.0f, farthest = 0.0f, lowestZ = 1e9f, armInChest = 0.0f, lowestTime = 0.0f;
        std::string lowestBody;
        std::vector<DdTransform> bodies;
        for (int frame = 0; frame < 4 * 60; ++frame) // 4 s at 60 fps
        {
            instance.hostFrame(1.0f / 60.0f);
            bodies = bodiesOf(c);
            for (size_t i = 0; i < bodies.size(); ++i)
            {
                const DdTransform& t = bodies[i];
                REQUIRE(finite(t));
                farthest = std::fmax(farthest, distance(t.position, bodies[static_cast<size_t>(pelvis)].position));
                if (t.position.z < lowestZ)
                {
                    lowestZ = t.position.z;
                    lowestBody = dragdoll_character_get_body_name(c, static_cast<uint32_t>(i));
                    lowestTime = static_cast<float>(frame) / 60.0f;
                }
            }
            // Knees flex positive, elbows negative (see metrics::hingeBendDegrees): track the worst wrong-way bend.
            worstKnee = std::fmin(worstKnee, std::fmin(hingeBendDegrees(bodies[h.thighL], bodies[h.shinL]),
                                                       hingeBendDegrees(bodies[h.thighR], bodies[h.shinR])));
            worstElbow = std::fmin(worstElbow, std::fmin(-hingeBendDegrees(bodies[h.armL], bodies[h.foreL]),
                                                         -hingeBendDegrees(bodies[h.armR], bodies[h.foreR])));
            armInChest = std::fmax(armInChest, std::fmax(armIntoChest(bodies[h.armL], bodies[static_cast<size_t>(chest)]),
                                                         armIntoChest(bodies[h.armR], bodies[static_cast<size_t>(chest)])));
        }
        MESSAGE(name << ": worst knee " << worstKnee << " deg, worst elbow " << worstElbow << " deg, farthest from pelvis "
                     << farthest << " m, lowest body centre z " << lowestZ << " m (" << lowestBody << " at " << lowestTime << " s), deepest arm-in-chest " << armInChest << " m");
        CHECK(armInChest < 0.02f); // Upper arms collide with the chest (rig: collide_with_parent).
        CHECK(worstKnee > -6.0f);  // limit is -3; allow a little solver softness on impact
        CHECK(worstElbow > -6.0f);
        CHECK(farthest < 1.2f);    // A 1.75 m body can't have a part further than this from its pelvis.
        CHECK(lowestZ > 0.0f);     // No body centre below the floor.

        // At rest: the pelvis barely moves during the last half second.
        const DdVec3 before = bodies[static_cast<size_t>(pelvis)].position;
        for (int frame = 0; frame < 30; ++frame)
        {
            instance.hostFrame(1.0f / 60.0f);
        }
        CHECK(distance(bodiesOf(c)[static_cast<size_t>(pelvis)].position, before) < 0.02f);
    }
}

TEST_CASE("ragdoll determinism: identical results for 0 and 4 worker threads")
{
    Library lib;
    uint64_t expected = 0;
    for (uint32_t threads : {0u, 4u})
    {
        DdWorldDesc desc = defaultDesc();
        desc.worker_threads = threads;
        World world(desc);
        DdColliderDesc ground;
        dragdoll_collider_desc_default(&ground);
        ground.shape = DD_SHAPE_BOX;
        ground.half_extents = DdVec3{20, 1, 20};
        ground.transform.position = DdVec3{0, -1, 0};
        DdColliderId id = 0;
        REQUIRE(dragdoll_world_add_colliders(world, &ground, 1, &id) == DD_OK);
        makeCharacter(world, DdVec3{0, 1, 0});
        makeCharacter(world, DdVec3{0.3f, 3, 0.2f}); // falls onto the first one
        REQUIRE(dragdoll_world_step_fixed(world, 360) == DD_OK);
        const uint64_t h = hashOf(world);
        if (threads == 0)
        {
            expected = h;
        }
        CHECK(h == expected);
    }
}

TEST_CASE("characters: impulses, destroy mid-simulation, world teardown")
{
    Library lib;
    DdWorldDesc desc = defaultDesc();
    desc.gravity = 0.0f;
    World world(desc);
    DdCharacter* c = makeCharacter(world, DdVec3{0, 0, 0});
    const uint32_t pelvis = static_cast<uint32_t>(dragdoll_character_find_body(c, "pelvis"));
    const DdVec3 start = bodiesOf(c)[pelvis].position;
    REQUIRE(dragdoll_character_apply_impulse(c, pelvis, DdVec3{75.0f, 0, 0}, nullptr) == DD_OK);
    REQUIRE(dragdoll_world_step_fixed(world, 60) == DD_OK);
    CHECK(bodiesOf(c)[pelvis].position.x - start.x > 0.1f);
    CHECK(dragdoll_character_apply_impulse(c, 99, DdVec3{1, 0, 0}, nullptr) == DD_ERROR_INVALID_ARGUMENT);

    std::vector<DdTransform> tooSmall(3);
    CHECK(dragdoll_character_get_body_transforms(c, 0, tooSmall.data(), 3) == DD_ERROR_INVALID_ARGUMENT);

    DdCharacter* second = makeCharacter(world, DdVec3{3, 0, 0});
    dragdoll_character_destroy(c);
    REQUIRE(dragdoll_world_step_fixed(world, 10) == DD_OK);
    CHECK(dragdoll_character_get_body_count(second) == 16);
    dragdoll_character_destroy(nullptr);
    // `second` is destroyed together with the world.
}

TEST_CASE("ragdoll scenarios: no body keeps spinning in place while the rest is still (founder review: twisting lower spine)")
{
    // A body that hardly moves (< 2 cm/s) but rotates fast (> 30 deg/s) while every other body is nearly still
    // (< 10 deg/s) is a part twisting freely between its neighbours. Real limbs are damped by tissue.
    Library lib;
    for (const std::string name : {"drop_from_1m", "fall_forward", "shove_mid_fall"})
    {
        for (float fps : {30.0f, 60.0f, 144.0f})
        {
            CAPTURE(name);
            CAPTURE(fps);
            Instance instance;
            std::string error;
            REQUIRE(Instance::make(name, instance, error));
            DdCharacter* c = instance.scenario->character();
            std::vector<DdTransform> previous = bodiesOf(c);
            double previousTime = 0.0;
            float worstSpin = 0.0f;
            std::string worstBody, spinLog;
            while (instance.hostTime < 4.0)
            {
                instance.hostFrame(1.0f / fps);
                const double t = static_cast<double>(dragdoll_world_get_step_count(instance.world)) / 120.0;
                if (t - previousTime < 0.1)
                {
                    continue;
                }
                const std::vector<DdTransform> now = bodiesOf(c);
                std::vector<float> lin(now.size()), ang(now.size());
                for (size_t i = 0; i < now.size(); ++i)
                {
                    lin[i] = distance(now[i].position, previous[i].position) / static_cast<float>(t - previousTime);
                    const DdQuat& a = now[i].rotation;
                    const DdQuat& b = previous[i].rotation;
                    const float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
                    ang[i] = 2.0f * std::acos(std::fmin(1.0f, dot)) * 57.29578f / static_cast<float>(t - previousTime);
                }
                for (size_t i = 0; i < now.size(); ++i)
                {
                    // The founder's observation: one part keeps moving while the REST of the body is already still.
                    float others = 0.0f;
                    for (size_t j = 0; j < now.size(); ++j)
                    {
                        if (j != i)
                        {
                            others = std::fmax(others, ang[j]);
                        }
                    }
                    if (t > 1.0 && lin[i] < 0.02f && others < 10.0f && ang[i] > 30.0f)
                    {
                        spinLog += std::string(" ") + dragdoll_character_get_body_name(c, static_cast<uint32_t>(i)) + "@" +
                                   std::to_string(t).substr(0, 4) + "s=" + std::to_string(static_cast<int>(ang[i]));
                    }
                    if (t > 1.0 && lin[i] < 0.02f && others < 10.0f && ang[i] > worstSpin)
                    {
                        worstSpin = ang[i];
                        worstBody = dragdoll_character_get_body_name(c, static_cast<uint32_t>(i));
                    }
                }
                previous = now;
                previousTime = t;
            }
            MESSAGE(name << " @" << fps << " fps: worst spin-in-place " << worstSpin << " deg/s (" << worstBody << ")" << spinLog);
            CHECK(worstSpin < 30.0f);
        }
    }
}
