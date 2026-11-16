// SPDX-License-Identifier: MIT
// Perception (ADR-0024, task 4.4): time to collision, miss distance, and the front profile probe that measures a
// vehicle's front from its own collision geometry — checked against the vehicle packs' known profiles and against a
// car built from spheres (as San Andreas builds its cars).
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

// Z-up world with a ground plane and a host-animated (kinematic) human standing at the origin.
struct Scene
{
    DdWorld* world = nullptr;
    DdCharacter* human = nullptr;
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
        ground.half_extents = DdVec3{80, 80, 0.5f};
        ground.transform.position = DdVec3{0, 0, -0.5f};
        DdColliderId id;
        REQUIRE(dragdoll_world_add_colliders(world, &ground, 1, &id) == DD_OK);
        DdCharacterDesc cd;
        dragdoll_character_desc_default(&cd);
        cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
        cd.transform.position = DdVec3{0, 0, 0.01f}; // The rig's origin is between the feet
        REQUIRE(dragdoll_character_create(world, &cd, &human) == DD_OK);
        REQUIRE(dragdoll_character_set_kinematic(human, 1) == DD_OK);
    }
    ~Scene() { dragdoll_world_destroy(world); }

    DdThreat threat() const
    {
        DdThreat t{};
        t.struct_size = sizeof(DdThreat);
        REQUIRE(dragdoll_character_get_threat(human, &t) == DD_OK);
        return t;
    }

    // Drives `ids` (all at `offsets` from the vehicle origin) along +X at `speed` for `seconds`, 60 host frames a second.
    void drive(const std::vector<DdColliderId>& ids, const std::vector<DdVec3>& offsets, float& x, float y, float speed, float seconds)
    {
        const int frames = static_cast<int>(std::lround(seconds * 60.0f));
        for (int f = 0; f < frames; ++f)
        {
            x += speed / 60.0f;
            std::vector<DdTransform> targets;
            for (const DdVec3& o : offsets) targets.push_back(DdTransform{{x + o.x, y + o.y, o.z}, {0, 0, 0, 1}});
            REQUIRE(dragdoll_world_set_kinematic_targets(world, ids.data(), targets.data(), static_cast<uint32_t>(ids.size())) == DD_OK);
            REQUIRE(dragdoll_world_step(world, 1.0f / 60.0f, nullptr) == DD_OK);
        }
    }
};

// A vehicle pack as one kinematic mesh, origin on the ground under its centre, driving along +X.
DdColliderId addVehicle(Scene& scene, const VehicleProfile& profile, const VehicleMesh& mesh, float x, float y, uint32_t group)
{
    DdColliderDesc car;
    dragdoll_collider_desc_default(&car);
    car.shape = DD_SHAPE_MESH;
    car.motion = DD_MOTION_KINEMATIC;
    car.vertices = mesh.vertices.data();
    car.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
    car.indices = mesh.indices.data();
    car.index_count = static_cast<uint32_t>(mesh.indices.size());
    car.transform.position = DdVec3{x, y, 0.0f};
    car.group = group;
    DdColliderId id = DD_INVALID_COLLIDER;
    REQUIRE(dragdoll_world_add_colliders(scene.world, &car, 1, &id) == DD_OK);
    (void)profile;
    return id;
}
} // namespace

TEST_CASE("perception: time to collision, closing speed and impact line of an approaching vehicle")
{
    Library lib;
    Scene scene;
    const VehicleProfile sedan = parseVehicle(builtinVehicle("sedan"));
    const VehicleMesh mesh = buildVehicleMesh(sedan, DdVec3{1, 0, 0}, DdVec3{0, 1, 0}, DdVec3{0, 0, 1});
    float x = -40.0f;
    const DdColliderId car = addVehicle(scene, sedan, mesh, x, 0.4f, 0); // Its centre line passes 0.4 m to the human's left
    CHECK(scene.threat().collider == DD_INVALID_COLLIDER); // Not moving yet

    scene.drive({car}, {DdVec3{0, 0, 0}}, x, 0.4f, 10.0f, 1.0f); // 10 m/s: still more than 2 s away
    CHECK(scene.threat().collider == DD_INVALID_COLLIDER);

    scene.drive({car}, {DdVec3{0, 0, 0}}, x, 0.4f, 10.0f, 1.0f);
    const DdThreat t = scene.threat();
    REQUIRE(t.collider == car);
    CHECK(t.group == 0);
    DdTransform now{};
    REQUIRE(dragdoll_world_get_collider_transform(scene.world, car, 0, &now) == DD_OK);
    const float front = now.position.x + sedan.length * 0.5f;
    const float expected = (0.0f - 0.2f - front) / 10.0f; // Front face to the near side of a body 0.2 m deep
    CHECK(std::fabs(t.time_to_collision - expected) < 0.05f);
    CHECK(t.closing_speed == doctest::Approx(10.0f).epsilon(0.02));
    CHECK(t.miss_distance == 0.0f);
    CHECK(std::fabs(t.lateral_offset - (-0.4f)) < 0.06f); // The human is 0.4 m to the right of the centre line
    CHECK(t.direction.x == doctest::Approx(1.0f).epsilon(0.001));
}

TEST_CASE("perception: a vehicle that passes beside the character is not a threat; a slow one neither")
{
    Library lib;
    Scene scene;
    const VehicleProfile sedan = parseVehicle(builtinVehicle("sedan"));
    const VehicleMesh mesh = buildVehicleMesh(sedan, DdVec3{1, 0, 0}, DdVec3{0, 1, 0}, DdVec3{0, 0, 1});
    float x = -12.0f;
    const DdColliderId passing = addVehicle(scene, sedan, mesh, x, 3.0f, 0); // 3 m to the side: clears the body by > 0.3 m
    scene.drive({passing}, {DdVec3{0, 0, 0}}, x, 3.0f, 10.0f, 0.5f);
    CHECK(scene.threat().collider == DD_INVALID_COLLIDER);

    float creeping = -4.0f;
    const DdColliderId slow = addVehicle(scene, sedan, mesh, creeping, 0.0f, 0);
    scene.drive({slow}, {DdVec3{0, 0, 0}}, creeping, 0.0f, 0.5f, 0.5f); // 0.5 m/s: below the 1 m/s threshold
    CHECK(scene.threat().collider == DD_INVALID_COLLIDER);
}

TEST_CASE("perception: the front profile probe recovers each vehicle pack's own profile, and predicts the trajectory class")
{
    Library lib;
    for (const std::string& name : vehicleNames())
    {
        CAPTURE(name);
        Scene scene;
        const VehicleProfile p = parseVehicle(builtinVehicle(name));
        const VehicleMesh mesh = buildVehicleMesh(p, DdVec3{1, 0, 0}, DdVec3{0, 1, 0}, DdVec3{0, 0, 1});
        float x = -12.0f - p.length * 0.5f;
        const DdColliderId car = addVehicle(scene, p, mesh, x, 0.0f, 3);
        scene.drive({car}, {DdVec3{0, 0, 0}}, x, 0.0f, 10.0f, 0.3f);
        const DdThreat t = scene.threat();
        REQUIRE(t.collider == car);
        CHECK(t.group == 3);
        REQUIRE(t.profile.valid == 1);
        MESSAGE(name << ": front top " << t.profile.front_top_height << " (" << p.bumperHeight << "), leading edge "
                     << t.profile.leading_edge_height << " @ " << t.profile.leading_edge_setback << ", bonnet " << t.profile.bonnet_length
                     << " m at " << t.profile.bonnet_angle << " deg (" << p.hoodLength << ", " << p.hoodAngle << "), windscreen "
                     << t.profile.windscreen_angle << " (" << p.windshieldAngle << "), roof " << t.profile.roof_height << " ("
                     << p.roofHeight << "), predicted " << static_cast<int>(t.predicted));
        const bool flatFront = p.hoodLength < 0.05f; // No bonnet: the front runs up to the roof
        if (flatFront) CHECK(t.profile.front_top_height >= p.bonnetEdgeHeight - 0.11f); // The face simply carries on up
        else CHECK(std::fabs(t.profile.front_top_height - p.bumperHeight) <= 0.11f);
        CHECK(std::fabs(t.profile.leading_edge_height - (flatFront ? p.roofHeight : p.bonnetEdgeHeight)) <= 0.08f);
        CHECK(std::fabs(t.profile.roof_height - p.roofHeight) <= 0.03f);
        if (!flatFront)
        {
            CHECK(std::fabs(t.profile.bonnet_length - p.hoodLength) <= 0.2f);
            CHECK(std::fabs(t.profile.bonnet_angle - p.hoodAngle) <= (p.hoodLength < 0.8f ? 6.0f : 4.0f)); // Short bonnets: few rays
            const float screenRise = p.roofHeight - (p.bonnetEdgeHeight + p.hoodLength * std::tan(p.hoodAngle * 3.14159265f / 180.0f));
            if (screenRise > 0.4f) CHECK(std::fabs(t.profile.windscreen_angle - p.windshieldAngle) <= 6.0f); // Else too short for 10 cm rays
        }
        else
        {
            CHECK(t.profile.bonnet_length == 0.0f);
        }
        // Low bonnets wrap an adult; fronts as high as the centre of mass project forward (Wood, Simms & Walsh 2005).
        if (name == "sedan" || name == "sports") CHECK(t.predicted == DD_TRAJECTORY_WRAP);
        if (name == "suv" || name == "van" || name == "bus") CHECK(t.predicted == DD_TRAJECTORY_FORWARD_PROJECTION);

        // The silhouette for tools follows the vehicle: its first point lies on the front face, near the ground.
        std::vector<DdVec3> points(128);
        const uint32_t count = dragdoll_character_get_threat_silhouette(scene.human, points.data(), 128);
        REQUIRE(count > 8);
        DdTransform now{};
        REQUIRE(dragdoll_world_get_collider_transform(scene.world, car, 0, &now) == DD_OK);
        CHECK(std::fabs(points[0].x - (now.position.x + p.length * 0.5f)) < 0.1f); // Sensed before the step: one step (8 cm) behind
        CHECK(points[0].z < p.bumperHeight + 0.11f);
    }
}

TEST_CASE("perception: a car built from spheres (one group) is one threat, and its bumpy front still reads as a low bonnet")
{
    Library lib;
    Scene scene;
    // A saloon as San Andreas would hand it over: three rows of body spheres with their tops at 0.72 m, and a cabin.
    std::vector<DdVec3> offsets;
    std::vector<float> radii;
    for (float back = 0.35f; back <= 4.3f; back += 0.3f)
    {
        for (float side : {-0.5f, 0.0f, 0.5f})
        {
            offsets.push_back(DdVec3{-back, side, 0.37f});
            radii.push_back(0.35f);
        }
    }
    for (float back = 2.0f; back <= 3.4f; back += 0.35f)
    {
        offsets.push_back(DdVec3{-back, 0.0f, 1.05f});
        radii.push_back(0.4f);
    }
    float x = -10.0f;
    std::vector<DdColliderDesc> parts(offsets.size());
    for (size_t i = 0; i < parts.size(); ++i)
    {
        dragdoll_collider_desc_default(&parts[i]);
        parts[i].shape = DD_SHAPE_SPHERE;
        parts[i].motion = DD_MOTION_KINEMATIC;
        parts[i].radius = radii[i];
        parts[i].transform.position = DdVec3{x + offsets[i].x, offsets[i].y, offsets[i].z};
        parts[i].group = 7;
    }
    std::vector<DdColliderId> ids(parts.size());
    REQUIRE(dragdoll_world_add_colliders(scene.world, parts.data(), static_cast<uint32_t>(parts.size()), ids.data()) == DD_OK);
    scene.drive(ids, offsets, x, 0.0f, 10.0f, 0.3f);

    const DdThreat t = scene.threat();
    REQUIRE(t.collider != DD_INVALID_COLLIDER);
    CHECK(t.group == 7);
    CHECK(std::fabs(t.time_to_collision - (0.0f - 0.2f - x) / 10.0f) < 0.06f); // The foremost sphere surface is at x
    REQUIRE(t.profile.valid == 1);
    MESSAGE("sphere car: leading edge " << t.profile.leading_edge_height << " @ " << t.profile.leading_edge_setback << ", bonnet "
                                        << t.profile.bonnet_length << " m, roof " << t.profile.roof_height);
    CHECK(std::fabs(t.profile.leading_edge_height - 0.72f) <= 0.1f);
    CHECK(t.profile.bonnet_length > 0.8f);
    CHECK(std::fabs(t.profile.roof_height - 1.45f) <= 0.05f);
    CHECK(t.predicted == DD_TRAJECTORY_WRAP);

    // Removing the parts removes the threat.
    REQUIRE(dragdoll_world_remove_colliders(scene.world, ids.data(), static_cast<uint32_t>(ids.size())) == DD_OK);
    REQUIRE(dragdoll_world_step(scene.world, 1.0f / 60.0f, nullptr) == DD_OK);
    CHECK(scene.threat().collider == DD_INVALID_COLLIDER);
}
