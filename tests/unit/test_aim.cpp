// SPDX-License-Identifier: MIT
// Aim and named poses (ADR-0025, task 4.5b): "point this limb at that position" becomes joint targets the muscles
// reach; poses from a behaviour pack; and car_hit's Shield, which uses the aim to raise the hands towards a vehicle.
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/vehicles.hpp>

#include <doctest/doctest.h>

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

DdVec3 add(DdVec3 a, DdVec3 b, float s = 1.0f)
{
    return DdVec3{a.x + b.x * s, a.y + b.y * s, a.z + b.z * s};
}
float distance(DdVec3 a, DdVec3 b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
float degrees(const DdQuat& q) // Angle of a rotation
{
    return 2.0f * std::acos(std::fmin(1.0f, std::fabs(q.w))) * 180.0f / 3.14159265f;
}
float degreesBetween(const DdQuat& a, const DdQuat& b)
{
    return 2.0f * std::acos(std::fmin(1.0f, std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w))) * 180.0f / 3.14159265f;
}

// A tense human floating without gravity (only its own muscles move it), Z up.
struct Floating
{
    DdWorld* world = nullptr;
    DdCharacter* human = nullptr;
    DdVec3 forward{}, up{0, 0, 1};
    uint32_t count = 0;

    Floating()
    {
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.up = DdVec3{0, 0, 1};
        desc.gravity = 0.0f;
        desc.worker_threads = 0;
        REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
        cd.transform.position = DdVec3{0, 0, 2.0f};
        REQUIRE(dragdoll_character_create(world, &cd, &human) == DD_OK);
        REQUIRE(dragdoll_character_set_tension(human, DD_TENSION_TENSE, 0.0f) == DD_OK);
        count = dragdoll_character_get_body_count(human);
        // Forward from the pose itself: the foot's centre lies ahead of the shin's.
        const DdVec3 foot = at("foot_l"), shin = at("shin_l");
        const float fx = foot.x - shin.x, fy = foot.y - shin.y, length = std::hypot(fx, fy);
        REQUIRE(length > 0.02f);
        forward = DdVec3{fx / length, fy / length, 0.0f};
    }
    ~Floating() { dragdoll_world_destroy(world); }

    DdVec3 at(const char* body) const
    {
        std::vector<DdTransform> t(dragdoll_character_get_body_count(human));
        REQUIRE(dragdoll_character_get_body_transforms(human, 0, t.data(), static_cast<uint32_t>(t.size())) == DD_OK);
        const int index = dragdoll_character_find_body(human, body);
        REQUIRE(index >= 0);
        return t[static_cast<size_t>(index)].position;
    }
    DdQuat joint(const char* body) const
    {
        std::vector<DdQuat> pose(count);
        DdTransform root{};
        REQUIRE(dragdoll_character_get_pose(human, 0, pose.data(), count, &root) == DD_OK);
        return pose[static_cast<size_t>(dragdoll_character_find_body(human, body))];
    }
    // Aims every step for `seconds` (the body drifts a little in reaction, so the solve is repeated).
    void aim(DdRegion region, DdVec3 target, float extension, float seconds)
    {
        for (int i = 0; i < static_cast<int>(seconds * 120.0f); ++i)
        {
            REQUIRE(dragdoll_character_aim(human, region, target, extension) == DD_OK);
            REQUIRE(dragdoll_world_step_fixed(world, 1) == DD_OK);
        }
    }
    bool alive() const
    {
        DdCharacterInfo info{};
        info.struct_size = sizeof(DdCharacterInfo);
        REQUIRE(dragdoll_character_get_info(human, &info) == DD_OK);
        return info.state != DD_STATE_FAILED;
    }
};
} // namespace

TEST_CASE("aim: the hand reaches a reachable point; the foot too")
{
    Library lib;
    {
        Floating f;
        const DdVec3 target = add(add(f.at("hand_l"), f.forward, 0.30f), f.up, 0.25f);
        f.aim(DD_REGION_ARM_LEFT, target, 1.0f, 1.2f);
        const float error = distance(f.at("hand_l"), target);
        MESSAGE("left hand, 30 cm forward and 25 cm up from rest: " << error * 100.0f << " cm from the target");
        CHECK(error < 0.06f);
        CHECK(degrees(f.joint("upper_arm_r")) < 6.0f); // The other arm stayed at rest
    }
    {
        Floating f;
        const DdVec3 target = add(add(f.at("hand_r"), f.forward, 0.15f), f.up, 0.55f); // Higher: about face height
        f.aim(DD_REGION_ARM_RIGHT, target, 1.0f, 1.2f);
        const float error = distance(f.at("hand_r"), target);
        MESSAGE("right hand, 15 cm forward and 55 cm up from rest: " << error * 100.0f << " cm from the target");
        CHECK(error < 0.06f);
    }
    {
        Floating f;
        const DdVec3 target = add(add(f.at("foot_l"), f.forward, 0.30f), f.up, 0.20f);
        f.aim(DD_REGION_LEG_LEFT, target, 1.0f, 1.2f);
        const float error = distance(f.at("foot_l"), target);
        MESSAGE("left foot, 30 cm forward and 20 cm up from rest: " << error * 100.0f << " cm from the target");
        CHECK(error < 0.07f);
    }
}

TEST_CASE("aim: extension sets how far the limb stretches towards a far target")
{
    Library lib;
    float bent = 0.0f, straight = 0.0f, nearWhenBent = 0.0f, nearWhenStraight = 0.0f;
    {
        Floating f;
        const DdVec3 target = add(add(f.at("hand_l"), f.forward, 3.0f), f.up, 0.6f);
        f.aim(DD_REGION_ARM_LEFT, target, 0.7f, 1.2f);
        bent = degrees(f.joint("forearm_l"));
        nearWhenBent = distance(f.at("hand_l"), target);
    }
    {
        Floating f;
        const DdVec3 target = add(add(f.at("hand_l"), f.forward, 3.0f), f.up, 0.6f);
        f.aim(DD_REGION_ARM_LEFT, target, 1.0f, 1.2f);
        straight = degrees(f.joint("forearm_l"));
        nearWhenStraight = distance(f.at("hand_l"), target);
    }
    MESSAGE("elbow bend: " << bent << " deg at extension 0.7 (about 91 expected for this rig), " << straight << " deg at 1.0; hand "
                           << (nearWhenBent - nearWhenStraight) * 100.0f << " cm nearer the target when straight");
    CHECK(std::fabs(bent - 91.0f) < 10.0f); // Upper arm 0.28 m, forearm + hand 0.32 m, reach 0.7 x 0.60 m
    CHECK(straight < 12.0f);
    CHECK(nearWhenBent - nearWhenStraight > 0.10f);
}

TEST_CASE("aim: only arms and legs can aim; an unreachable target does no harm")
{
    Library lib;
    Floating f;
    const DdVec3 somewhere = add(f.at("head"), f.forward, 1.0f);
    CHECK(dragdoll_character_aim(f.human, DD_REGION_HEAD_NECK, somewhere, 1.0f) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_character_aim(f.human, DD_REGION_TRUNK, somewhere, 1.0f) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(dragdoll_character_aim(f.human, DD_REGION_COUNT, somewhere, 1.0f) == DD_ERROR_INVALID_ARGUMENT);

    // Behind the back, through the trunk: the joints' limits and the body itself stop the arm; nothing breaks.
    const DdVec3 behind = add(add(f.at("hand_l"), f.forward, -0.8f), f.up, 0.4f);
    const float before = distance(f.at("hand_l"), behind);
    f.aim(DD_REGION_ARM_LEFT, behind, 1.0f, 1.5f);
    CHECK(f.alive());
    CHECK(distance(f.at("hand_l"), behind) < before); // It went as far towards it as it could
    CHECK(degrees(f.joint("forearm_l")) < 150.0f);    // Within the elbow's range
}

TEST_CASE("poses: a named pose from a behaviour pack sets the chosen regions; \"rest\" undoes it")
{
    Library lib;
    Floating f;
    const char* pack = "{\"format\":\"dragdoll-behaviour\",\"version\":1,\"params\":{},"
                       "\"poses\":{\"raise\":{\"upper_arm_l\":[-60,0,0],\"forearm_l\":[-70,0,0],\"upper_arm_r\":[-60,0,0]}}}";
    CHECK(dragdoll_world_set_behaviour_params(f.world, "test_pack",
                                              "{\"format\":\"dragdoll-behaviour\",\"version\":1,\"params\":{},\"poses\":{\"bad\":{\"head\":[1,2]}}}") ==
          DD_ERROR_INVALID_ARGUMENT);
    REQUIRE(dragdoll_world_set_behaviour_params(f.world, "test_pack", pack) == DD_OK);
    CHECK(dragdoll_character_apply_pose(f.human, "test_pack", "no_such_pose", DD_REGIONS_ALL) == DD_ERROR_INVALID_ARGUMENT);

    // Only the left arm: the pack's entry for the right arm is not applied.
    REQUIRE(dragdoll_character_apply_pose(f.human, "test_pack", "raise", DD_REGION_BIT(DD_REGION_ARM_LEFT)) == DD_OK);
    REQUIRE(dragdoll_world_step_fixed(f.world, 144) == DD_OK);
    const float s = std::sin(-30.0f * 3.14159265f / 180.0f), c = std::cos(-30.0f * 3.14159265f / 180.0f);
    const DdQuat raised{s, 0, 0, c}; // -60 degrees about the rig's X axis
    MESSAGE("upper arm " << degreesBetween(f.joint("upper_arm_l"), raised) << " deg from the pose, elbow bend " << degrees(f.joint("forearm_l"))
                         << " deg (70 asked), other arm " << degrees(f.joint("upper_arm_r")) << " deg from rest");
    CHECK(degreesBetween(f.joint("upper_arm_l"), raised) < 8.0f);
    CHECK(std::fabs(degrees(f.joint("forearm_l")) - 70.0f) < 8.0f);
    CHECK(degrees(f.joint("upper_arm_r")) < 6.0f);

    REQUIRE(dragdoll_character_apply_pose(f.human, "test_pack", "rest", DD_REGIONS_ALL) == DD_OK);
    REQUIRE(dragdoll_world_step_fixed(f.world, 144) == DD_OK);
    CHECK(degrees(f.joint("upper_arm_l")) < 6.0f);
    CHECK(degrees(f.joint("forearm_l")) < 6.0f);
}

TEST_CASE("behaviour: car_hit raises both hands towards the vehicle once the voluntary latency has passed (Shield)")
{
    Library lib;
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    desc.up = DdVec3{0, 0, 1};
    desc.worker_threads = 0;
    DdWorld* world = nullptr;
    REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
    DdColliderDesc ground;
    dragdoll_collider_desc_default(&ground);
    ground.shape = DD_SHAPE_BOX;
    ground.half_extents = DdVec3{80, 80, 0.5f};
    ground.transform.position = DdVec3{0, 0, -0.5f};
    DdColliderId groundId;
    REQUIRE(dragdoll_world_add_colliders(world, &ground, 1, &groundId) == DD_OK);
    DdCharacterDesc cd;
    dragdoll_character_desc_default(&cd);
    cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
    cd.transform.position = DdVec3{0, 0, 0.005f};
    DdCharacter* human = nullptr;
    REQUIRE(dragdoll_character_create(world, &cd, &human) == DD_OK);
    REQUIRE(dragdoll_character_set_tension(human, DD_TENSION_TENSE, 0.0f) == DD_OK); // Standing braced, as in bump_5kmh
    REQUIRE(dragdoll_character_set_balance(human, 1) == DD_OK);
    REQUIRE(dragdoll_character_set_behaviour(human, "car_hit") == DD_OK);

    const uint32_t count = dragdoll_character_get_body_count(human);
    std::vector<DdTransform> t(count);
    const auto body = [&](const char* name) {
        REQUIRE(dragdoll_character_get_body_transforms(human, 0, t.data(), count) == DD_OK);
        return t[static_cast<size_t>(dragdoll_character_find_body(human, name))].position;
    };
    // The car comes from where the character faces (forward found from the feet).
    const DdVec3 foot = body("foot_l"), shin = body("shin_l");
    const float length = std::hypot(foot.x - shin.x, foot.y - shin.y);
    const DdVec3 forward{(foot.x - shin.x) / length, (foot.y - shin.y) / length, 0.0f};
    const VehicleProfile sedan = parseVehicle(builtinVehicle("sedan"));
    const VehicleMesh mesh = buildVehicleMesh(sedan, DdVec3{-forward.x, -forward.y, 0}, DdVec3{forward.y, -forward.x, 0}, DdVec3{0, 0, 1});
    DdColliderDesc car;
    dragdoll_collider_desc_default(&car);
    car.shape = DD_SHAPE_MESH;
    car.motion = DD_MOTION_KINEMATIC;
    car.vertices = mesh.vertices.data();
    car.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
    car.indices = mesh.indices.data();
    car.index_count = static_cast<uint32_t>(mesh.indices.size());
    float away = 20.0f; // Metres ahead of the character, closing at 10 m/s
    car.transform.position = DdVec3{forward.x * away, forward.y * away, 0};
    car.group = 1;
    DdColliderId carId = DD_INVALID_COLLIDER;
    REQUIRE(dragdoll_world_add_colliders(world, &car, 1, &carId) == DD_OK);

    const auto handsForward = [&] { // How far ahead of the pelvis the hands are, towards the car (average of both)
        const DdVec3 pelvis = body("pelvis"), left = body("hand_l"), right = body("hand_r");
        return 0.5f * ((left.x - pelvis.x) * forward.x + (left.y - pelvis.y) * forward.y + (right.x - pelvis.x) * forward.x +
                       (right.y - pelvis.y) * forward.y);
    };
    const auto drive = [&](float seconds) {
        for (int f = 0; f < static_cast<int>(std::lround(seconds * 120.0f)); ++f)
        {
            away -= 10.0f / 120.0f;
            const DdTransform target{{forward.x * away, forward.y * away, 0}, {0, 0, 0, 1}};
            REQUIRE(dragdoll_world_set_kinematic_targets(world, &carId, &target, 1) == DD_OK);
            REQUIRE(dragdoll_world_step(world, 1.0f / 120.0f, nullptr) == DD_OK);
        }
    };
    const auto timeToCollision = [&] {
        DdThreat threat{};
        threat.struct_size = sizeof(DdThreat);
        REQUIRE(dragdoll_character_get_threat(human, &threat) == DD_OK);
        return threat.collider != DD_INVALID_COLLIDER ? threat.time_to_collision : -1.0f;
    };

    drive(0.9f); // About 0.85 s away: startled 0.25 s ago, the 0.5 s voluntary latency not yet over
    const float atStartle = handsForward();
    CHECK(std::string(dragdoll_character_get_behaviour_status(human)) == "anticipate: startle");
    CHECK(std::fabs(atStartle) < 0.12f); // Hands still at the sides
    drive(0.55f); // About 0.3 s away
    CHECK(timeToCollision() > 0.15f);
    CHECK(std::string(dragdoll_character_get_behaviour_status(human)) == "anticipate: shield");
    MESSAGE("hands " << atStartle * 100.0f << " cm ahead of the pelvis when startled, " << handsForward() * 100.0f
                     << " cm with the shield up, " << timeToCollision() << " s before the impact");
    CHECK(handsForward() > atStartle + 0.25f);
    CHECK(body("hand_l").z > body("pelvis").z); // Raised above the hips
    dragdoll_world_destroy(world);
}

TEST_CASE("aim: the two pain gestures are reachable - palms on the forehead, arms crossed around the belly")
{
    // car_hit's Hurt stage (founder, 2026-11-19): the hands must actually get there on an upright, floating body before
    // it can work on a lying one. Alone each hand gets within a few cm; together the arms touch and push each other a
    // little (self-collision), hence 11 cm.
    Library lib;
    const auto left = [](DdVec3 f) { return DdVec3{-f.y, f.x, 0.0f}; }; // up x forward (Z up)
    {
        Floating f;
        const DdVec3 forehead = add(f.at("head"), f.forward, 0.13f);
        const DdVec3 l = add(forehead, left(f.forward), 0.09f), r = add(forehead, left(f.forward), -0.09f);
        f.aim(DD_REGION_ARM_LEFT, l, 0.95f, 1.2f);
        MESSAGE("forehead, left alone: " << distance(f.at("hand_l"), l) * 100.0f << " cm");
        f.aim(DD_REGION_ARM_RIGHT, r, 0.95f, 1.2f);
        MESSAGE("forehead: left hand " << distance(f.at("hand_l"), l) * 100.0f << " cm, right hand " << distance(f.at("hand_r"), r) * 100.0f
                                       << " cm from their places");
        CHECK(distance(f.at("hand_l"), l) < 0.11f);
        CHECK(distance(f.at("hand_r"), r) < 0.11f);
    }
    {
        Floating f;
        const DdVec3 belly = f.at("lower_spine");
        const DdVec3 l = add(add(belly, f.forward, 0.17f), left(f.forward), -0.10f), r = add(add(belly, f.forward, 0.25f), left(f.forward), 0.10f);
        f.aim(DD_REGION_ARM_LEFT, l, 0.95f, 1.2f);
        MESSAGE("belly, left alone: " << distance(f.at("hand_l"), l) * 100.0f << " cm");
        f.aim(DD_REGION_ARM_RIGHT, r, 0.95f, 1.2f);
        MESSAGE("belly: left hand " << distance(f.at("hand_l"), l) * 100.0f << " cm, right hand " << distance(f.at("hand_r"), r) * 100.0f
                                    << " cm from their places");
        CHECK(distance(f.at("hand_l"), l) < 0.11f);
        CHECK(distance(f.at("hand_r"), r) < 0.11f);
    }
}
