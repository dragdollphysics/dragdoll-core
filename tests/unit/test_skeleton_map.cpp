// SPDX-License-Identifier: MIT
// Skeleton maps (Phase 3, task 3.4): a mock SA ped skeleton (SA bone IDs and hierarchy, our own proportions, a T-pose
// bind with arbitrary bone frames) maps to/from humanoid16 poses. Acceptance: round trip through DragDoll < 0.5 deg.
#include <dragdoll/dragdoll.h>

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

DdQuat axisAngle(float x, float y, float z, float degrees)
{
    const float n = std::sqrt(x * x + y * y + z * z), half = degrees * 3.14159265f / 360.0f, s = std::sin(half) / n;
    return DdQuat{x * s, y * s, z * s, std::cos(half)};
}
DdQuat mul(const DdQuat& a, const DdQuat& b)
{
    return DdQuat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                  a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
DdQuat conj(const DdQuat& q) { return DdQuat{-q.x, -q.y, -q.z, q.w}; }
DdVec3 rotate(const DdQuat& q, const DdVec3& v)
{
    const DdQuat r = mul(mul(q, DdQuat{v.x, v.y, v.z, 0}), conj(q));
    return DdVec3{r.x, r.y, r.z};
}
float angleBetween(const DdQuat& a, const DdQuat& b)
{
    return 2.0f * std::acos(std::fmin(1.0f, std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w))) * 57.29578f;
}

// Mock SA ped in SA model axes (+Z up, +Y forward, +X = the ped's right): ids and hierarchy as in the game, our own
// proportions, T-pose arms, and an arbitrary local frame per bone (games do not align bone frames with anything).
std::vector<DdHostBone> mockSaPed()
{
    struct B { int id, parent; float x, y, z; };
    const B bones[] = {
        {0, -1, 0, 0, 0},          {1, 0, 0, 0, 0.95f},       {2, 1, 0, 0, 1.10f},       {3, 2, 0, 0, 1.30f},
        {4, 3, 0, 0, 1.50f},       {5, 4, 0, 0, 1.62f},       {8, 5, 0, 0.05f, 1.58f},
        {31, 3, -0.08f, 0, 1.45f}, {32, 7, -0.20f, 0, 1.45f}, {33, 8, -0.48f, 0, 1.45f}, {34, 9, -0.74f, 0, 1.45f},
        {35, 10, -0.84f, 0, 1.45f},
        {21, 3, 0.08f, 0, 1.45f},  {22, 12, 0.20f, 0, 1.45f}, {23, 13, 0.48f, 0, 1.45f}, {24, 14, 0.74f, 0, 1.45f},
        {25, 15, 0.84f, 0, 1.45f},
        {41, 1, -0.10f, 0, 0.92f}, {42, 17, -0.10f, 0, 0.50f}, {43, 18, -0.10f, 0, 0.08f}, {44, 19, -0.10f, 0.15f, 0.02f},
        {51, 1, 0.10f, 0, 0.92f},  {52, 21, 0.10f, 0, 0.50f},  {53, 22, 0.10f, 0, 0.08f},  {54, 23, 0.10f, 0.15f, 0.02f},
    };
    std::vector<DdHostBone> out;
    int k = 0;
    for (const B& b : bones)
    {
        DdHostBone h{};
        h.id = b.id;
        h.parent = b.parent;
        h.bind.position = DdVec3{b.x, b.y, b.z};
        h.bind.rotation = axisAngle(static_cast<float>(k % 3 + 1), static_cast<float>(k % 5 - 2), 1.0f, 37.0f * static_cast<float>(k));
        out.push_back(h);
        ++k;
    }
    return out;
}

int boneIndex(const std::vector<DdHostBone>& bones, int id)
{
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].id == id) return static_cast<int>(i);
    return -1;
}

struct Fixture
{
    Library lib;
    DdWorld* world = nullptr;
    DdCharacter* c = nullptr;
    DdSkeletonMap* map = nullptr;
    std::vector<DdHostBone> bones = mockSaPed();
    DdTransform modelToWorld{{0, 0, 0}, {0, 0, 0, 1}};
    Fixture()
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
        REQUIRE(dragdoll_character_create(world, &cd, &c) == DD_OK);
        const char* json = dragdoll_get_builtin_skeleton_map("sa_ped");
        REQUIRE(json != nullptr);
        REQUIRE(dragdoll_skeleton_map_create(json, c, bones.data(), static_cast<uint32_t>(bones.size()), &map) == DD_OK);
    }
    ~Fixture()
    {
        dragdoll_skeleton_map_destroy(map);
        dragdoll_world_destroy(world);
    }
    uint32_t body(const char* name) const { return static_cast<uint32_t>(dragdoll_character_find_body(c, name)); }
    std::vector<DdQuat> bindRotations() const
    {
        std::vector<DdQuat> r;
        for (const DdHostBone& b : bones) r.push_back(b.bind.rotation);
        return r;
    }
};
} // namespace

TEST_CASE("skeleton map: a T-pose bind maps to arms straight out, every other joint at rest")
{
    Fixture f;
    std::vector<DdQuat> joints(16);
    DdTransform root{};
    const std::vector<DdQuat> bind = f.bindRotations();
    REQUIRE(dragdoll_skeleton_map_to_pose(f.map, bind.data(), static_cast<uint32_t>(bind.size()), nullptr, &f.modelToWorld, &root,
                                          joints.data(), 16) == DD_OK);
    // Rig axes: +X = the character's left. The left upper arm hangs along -Y at rest; out to the side = +X.
    const DdVec3 left = rotate(joints[f.body("upper_arm_l")], DdVec3{0, -1, 0});
    const DdVec3 right = rotate(joints[f.body("upper_arm_r")], DdVec3{0, -1, 0});
    CHECK(left.x > 0.99f);
    CHECK(right.x < -0.99f);
    for (const char* name : {"lower_spine", "upper_spine", "forearm_l", "forearm_r", "thigh_l", "shin_l", "thigh_r", "shin_r"})
    {
        CAPTURE(std::string(name));
        CHECK(angleBetween(joints[f.body(name)], DdQuat{0, 0, 0, 1}) < 1.0f);
    }
    // The rig's head sits 1 cm forward of the neck (3.6 deg); the mock stacks it straight up. A proportion difference
    // between the two skeletons, not a mapping error.
    CHECK(angleBetween(joints[f.body("head")], DdQuat{0, 0, 0, 1}) < 5.0f);
}

TEST_CASE("skeleton map: the rig's rest pose maps to the host with the arms hanging down")
{
    Fixture f;
    std::vector<DdQuat> joints(16), out(f.bones.size());
    DdTransform root{};
    const std::vector<DdQuat> bind = f.bindRotations();
    REQUIRE(dragdoll_skeleton_map_to_pose(f.map, bind.data(), static_cast<uint32_t>(bind.size()), nullptr, &f.modelToWorld, &root,
                                          joints.data(), 16) == DD_OK);
    const std::vector<DdQuat> rest(16, DdQuat{0, 0, 0, 1});
    REQUIRE(dragdoll_skeleton_map_from_pose(f.map, &root, rest.data(), 16, &f.modelToWorld, out.data(),
                                            static_cast<uint32_t>(out.size())) == DD_OK);
    // Host upper arm direction now = H * B^-1 * (bind direction); in the T-pose bind the left arm points along -X.
    const int arm = boneIndex(f.bones, 32);
    const DdVec3 now = rotate(mul(out[static_cast<size_t>(arm)], conj(f.bones[static_cast<size_t>(arm)].bind.rotation)), DdVec3{-1, 0, 0});
    CHECK(now.z < -0.99f); // Hanging down
    // Unmapped bones follow their parent: the left finger keeps its bind offset from the hand.
    const int hand = boneIndex(f.bones, 34), finger = boneIndex(f.bones, 35);
    const DdQuat handLocal = mul(conj(f.bones[static_cast<size_t>(hand)].bind.rotation), f.bones[static_cast<size_t>(finger)].bind.rotation);
    const DdQuat nowLocal = mul(conj(out[static_cast<size_t>(hand)]), out[static_cast<size_t>(finger)]);
    CHECK(angleBetween(handLocal, nowLocal) < 0.1f);
}

TEST_CASE("skeleton map: host pose -> DragDoll (activate, get_pose) -> host round trip loses < 0.5 deg")
{
    Fixture f;
    // A lively host pose: bind, then a DragDoll pose mapped back to the host (knee, elbow, spine, head, hip, arm).
    std::vector<DdQuat> poseIn(16, DdQuat{0, 0, 0, 1}), joints(16), hostPose(f.bones.size()), back(f.bones.size());
    DdTransform root{};
    const std::vector<DdQuat> bind = f.bindRotations();
    REQUIRE(dragdoll_skeleton_map_to_pose(f.map, bind.data(), static_cast<uint32_t>(bind.size()), nullptr, &f.modelToWorld, &root,
                                          joints.data(), 16) == DD_OK);
    poseIn[f.body("shin_l")] = axisAngle(1, 0, 0, 70);
    poseIn[f.body("forearm_r")] = axisAngle(-1, 0, 0, 90);
    poseIn[f.body("lower_spine")] = axisAngle(1, 0.2f, 0, 20);
    poseIn[f.body("head")] = axisAngle(0, 1, 0.3f, 35);
    poseIn[f.body("thigh_r")] = axisAngle(1, 0, 0.2f, -50);
    poseIn[f.body("upper_arm_l")] = axisAngle(0.3f, 0, 1, 60);
    f.modelToWorld = DdTransform{{3, -2, 0.5f}, axisAngle(0, 0, 1, 130)}; // The ped stands somewhere, turned
    REQUIRE(dragdoll_skeleton_map_from_pose(f.map, &root, poseIn.data(), 16, &f.modelToWorld, hostPose.data(),
                                            static_cast<uint32_t>(hostPose.size())) == DD_OK);

    // The host hands that pose to DragDoll, which places the body; then the pose is read back for the host.
    DdTransform dRoot{};
    std::vector<DdQuat> dJoints(16), readJoints(16);
    REQUIRE(dragdoll_skeleton_map_to_pose(f.map, hostPose.data(), static_cast<uint32_t>(hostPose.size()), nullptr, &f.modelToWorld,
                                          &dRoot, dJoints.data(), 16) == DD_OK);
    REQUIRE(dragdoll_character_activate(f.c, nullptr, nullptr, &dRoot, dJoints.data(), 16, 0.0f) == DD_OK);
    DdTransform readRoot{};
    REQUIRE(dragdoll_character_get_pose(f.c, 0, readJoints.data(), 16, &readRoot) == DD_OK);
    REQUIRE(dragdoll_skeleton_map_from_pose(f.map, &readRoot, readJoints.data(), 16, &f.modelToWorld, back.data(),
                                            static_cast<uint32_t>(back.size())) == DD_OK);
    float worst = 0.0f;
    for (size_t k = 0; k < back.size(); ++k)
    {
        CAPTURE(f.bones[k].id);
        const float e = angleBetween(back[k], hostPose[k]);
        CHECK(e < 0.5f);
        worst = std::fmax(worst, e);
    }
    MESSAGE("worst host bone error after the full round trip: " << worst << " deg");
}

TEST_CASE("skeleton map: invalid maps and inputs are rejected with a precise message")
{
    Fixture f;
    DdSkeletonMap* m = nullptr;
    CHECK(dragdoll_skeleton_map_create("{}", f.c, f.bones.data(), static_cast<uint32_t>(f.bones.size()), &m) == DD_ERROR_INVALID_ARGUMENT);
    std::vector<DdHostBone> missing = f.bones;
    missing.resize(10); // No legs
    CHECK(dragdoll_skeleton_map_create(dragdoll_get_builtin_skeleton_map("sa_ped"), f.c, missing.data(),
                                       static_cast<uint32_t>(missing.size()), &m) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(dragdoll_last_error_message()).find("no bone") != std::string::npos);
    CHECK(m == nullptr);
    CHECK(dragdoll_get_builtin_skeleton_map("unknown") == nullptr);
}
