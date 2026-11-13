// SPDX-License-Identifier: MIT
// Host contract (Phase 3, task 3.2): target pose in, the same pose out; rest = identity; state and recovery hints.
#include <dragdoll/dragdoll.h>

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

DdQuat axisAngle(float x, float y, float z, float degrees)
{
    const float half = degrees * 3.14159265f / 360.0f, s = std::sin(half);
    return DdQuat{x * s, y * s, z * s, std::cos(half)};
}

float angleBetween(const DdQuat& a, const DdQuat& b)
{
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.0f * std::acos(std::fmin(1.0f, d)) * 57.29578f;
}

struct Body
{
    DdWorld* world = nullptr;
    DdCharacter* c = nullptr;
    explicit Body(float gravity, DdVec3 at = DdVec3{0, 0, 1.2f}, DdQuat rotation = DdQuat{0, 0, 0, 1})
    {
        DdWorldDesc desc;
        dragdoll_world_desc_default(&desc);
        desc.up = DdVec3{0, 0, 1};
        desc.gravity = gravity;
        desc.worker_threads = 0;
        REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
        if (gravity > 0.0f)
        {
            DdColliderDesc ground;
            dragdoll_collider_desc_default(&ground);
            ground.shape = DD_SHAPE_BOX;
            ground.half_extents = DdVec3{20, 20, 0.5f};
            ground.transform.position = DdVec3{0, 0, -0.5f};
            DdColliderId id;
            REQUIRE(dragdoll_world_add_colliders(world, &ground, 1, &id) == DD_OK);
        }
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
        cd.transform.position = at;
        cd.transform.rotation = rotation;
        REQUIRE(dragdoll_character_create(world, &cd, &c) == DD_OK);
    }
    ~Body() { dragdoll_world_destroy(world); }
    uint32_t index(const char* name) const { return static_cast<uint32_t>(dragdoll_character_find_body(c, name)); }
    std::vector<DdQuat> pose() const
    {
        std::vector<DdQuat> q(dragdoll_character_get_body_count(c));
        REQUIRE(dragdoll_character_get_pose(c, 0, q.data(), static_cast<uint32_t>(q.size()), nullptr) == DD_OK);
        return q;
    }
};
} // namespace

TEST_CASE("pose: at rest every joint rotation is identity, and the root is the pelvis")
{
    Library lib;
    Body b(9.81f);
    for (const DdQuat& q : b.pose())
    {
        CHECK(angleBetween(q, DdQuat{0, 0, 0, 1}) < 0.05f);
    }
    DdTransform root{}, pelvis[16];
    REQUIRE(dragdoll_character_get_pose(b.c, 0, nullptr, 0, &root) == DD_OK);
    REQUIRE(dragdoll_character_get_body_transforms(b.c, 0, pelvis, 16) == DD_OK);
    CHECK(root.position.z == doctest::Approx(pelvis[0].position.z));
}

TEST_CASE("pose: a target pose in zero gravity comes back out of get_pose (tense muscles, internal torques only)")
{
    Library lib;
    for (float heading : {0.0f, 90.0f, 200.0f}) // The convention is independent of how the character is turned
    {
        CAPTURE(heading);
        Body b(0.0f, DdVec3{0, 0, 1.2f}, axisAngle(0, 0, 1, heading));
        // Rig axes: +X = character's left, +Y up, +Z forward. Shin/forearm flexion are rotations about +X / -X.
        std::vector<DdQuat> target(dragdoll_character_get_body_count(b.c), DdQuat{0, 0, 0, 1});
        target[b.index("shin_l")] = axisAngle(1, 0, 0, 60);        // Knee bent 60 deg
        target[b.index("forearm_r")] = axisAngle(-1, 0, 0, 80);    // Elbow bent 80 deg
        target[b.index("upper_arm_l")] = axisAngle(1, 0, 0, -45);  // Arm swung 45 deg forward
        target[b.index("thigh_r")] = axisAngle(1, 0, 0, -40);      // Hip flexed 40 deg
        target[b.index("head")] = axisAngle(0, 1, 0, 30);          // Head turned 30 deg to the left
        target[b.index("lower_spine")] = axisAngle(1, 0, 0, 15);   // Bent 15 deg forward
        REQUIRE(dragdoll_character_set_target_pose(b.c, target.data(), static_cast<uint32_t>(target.size())) == DD_OK);
        REQUIRE(dragdoll_character_set_tension(b.c, DD_TENSION_TENSE, 0.0f) == DD_OK);
        // A floating body passes reaction motion from joint to joint, so it settles gradually: within the Phase 3
        // criterion (5 deg) after 2 s, and close to exact after 5 s.
        for (const auto& [steps, limit] : {std::pair<uint32_t, float>{240, 5.0f}, std::pair<uint32_t, float>{360, 2.0f}})
        {
            dragdoll_world_step_fixed(b.world, steps);
            const std::vector<DdQuat> out = b.pose();
            float worst = 0.0f;
            for (size_t i = 1; i < out.size(); ++i)
            {
                const float error = angleBetween(out[i], target[i]);
                const std::string joint = dragdoll_character_get_body_name(b.c, static_cast<uint32_t>(i));
                CAPTURE(joint);
                CHECK(error < limit);
                worst = std::fmax(worst, error);
            }
            MESSAGE("heading " << heading << ", " << dragdoll_world_get_step_count(b.world) / 120 << " s: worst joint tracking error " << worst << " deg");
        }
    }
}

TEST_CASE("pose: input validation, NULL resets to rest, blend weights are 1")
{
    Library lib;
    Body b(0.0f);
    std::vector<DdQuat> target(16, DdQuat{0, 0, 0, 1});
    CHECK(dragdoll_character_set_target_pose(b.c, target.data(), 15) == DD_ERROR_INVALID_ARGUMENT);
    target[3] = DdQuat{0, 0, 0, 0};
    CHECK(dragdoll_character_set_target_pose(b.c, target.data(), 16) == DD_ERROR_INVALID_ARGUMENT);
    target[3] = DdQuat{0, 0, 0, 1};
    target[b.index("shin_l")] = axisAngle(1, 0, 0, 60);
    REQUIRE(dragdoll_character_set_target_pose(b.c, target.data(), 16) == DD_OK);
    REQUIRE(dragdoll_character_set_tension(b.c, DD_TENSION_TENSE, 0.0f) == DD_OK);
    dragdoll_world_step_fixed(b.world, 240);
    CHECK(angleBetween(b.pose()[b.index("shin_l")], target[b.index("shin_l")]) < 3.0f);
    REQUIRE(dragdoll_character_set_target_pose(b.c, nullptr, 0) == DD_OK);
    dragdoll_world_step_fixed(b.world, 240);
    CHECK(angleBetween(b.pose()[b.index("shin_l")], DdQuat{0, 0, 0, 1}) < 3.0f);

    float weights[16];
    REQUIRE(dragdoll_character_get_blend_weights(b.c, weights, 16) == DD_OK);
    for (float w : weights) CHECK(w == 1.0f);
    CHECK(dragdoll_character_get_blend_weights(b.c, weights, 15) == DD_ERROR_INVALID_ARGUMENT);
}

TEST_CASE("pose: state goes active -> settled, with facing and head direction for the host's get-up choice")
{
    Library lib;
    // Tipped forward 60 deg (rig faces -Y in this Z-up world): falls face down, head towards -Y.
    Body b(9.81f, DdVec3{0, 0, 0.3f}, axisAngle(1, 0, 0, 60));
    DdCharacterInfo info{};
    info.struct_size = sizeof(DdCharacterInfo);
    dragdoll_world_step_fixed(b.world, 12);
    REQUIRE(dragdoll_character_get_info(b.c, &info) == DD_OK);
    CHECK(info.state == DD_STATE_ACTIVE);
    dragdoll_world_step_fixed(b.world, 120 * 4);
    REQUIRE(dragdoll_character_get_info(b.c, &info) == DD_OK);
    CHECK(info.state == DD_STATE_SETTLED);
    CHECK(info.still_seconds >= 0.5f);
    CHECK(info.facing == -1);
    CHECK(info.head_direction.y < -0.7f);
    MESSAGE("settled for " << info.still_seconds << " s, facing " << info.facing << ", head direction (" << info.head_direction.x
                           << ", " << info.head_direction.y << ", " << info.head_direction.z << ")");
    info.struct_size = 4;
    CHECK(dragdoll_character_get_info(b.c, &info) == DD_ERROR_INVALID_ARGUMENT);
}
