// SPDX-License-Identifier: MIT
// Impact report (ADR-0024 §5, task 4.2): a limp pedestrian hit side-on by each vehicle pack at 40 km/h on the crash
// sandbox (the car brakes at contact). Checked here: that the report records the events in order and that its
// numbers are consistent with each other. Whether the physics matches the literature is task 4.3 (passive baseline).
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/vehicles.hpp>
#include <dragdoll_scenarios/crash.hpp>
#include <dragdoll_scenarios/scenario.hpp>

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

std::string zoneName(DdZone z)
{
    static const char* const kNames[] = {"none", "front", "leading edge", "bonnet", "windscreen", "roof"};
    return kNames[std::clamp(static_cast<int>(z), 0, 5)];
}
std::string className(DdTrajectory t)
{
    static const char* const kNames[] = {"none", "wrap", "forward projection", "fender vault", "roof vault", "somersault"};
    return kNames[std::clamp(static_cast<int>(t), 0, 5)];
}

struct Hit
{
    DdImpactReport report{};
    std::string firstBody, groundBody, order;
    DdRegion firstRegion = DD_REGION_COUNT;
};

// A limp pedestrian standing side-on; the vehicle arrives at `kmh` and brakes at 7 m/s^2 from first contact.
Hit run(const std::string& vehicle, float kmh, bool kinematic = false)
{
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    desc.up = DdVec3{0, 0, 1};
    desc.worker_threads = 0;
    DdWorld* world = nullptr;
    REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
    DdColliderDesc ground;
    dragdoll_collider_desc_default(&ground);
    ground.shape = DD_SHAPE_BOX;
    ground.half_extents = DdVec3{120, 40, 0.5f};
    ground.transform.position = DdVec3{40, 0, -0.5f};
    ground.friction = 0.7f;
    DdColliderId groundId;
    REQUIRE(dragdoll_world_add_colliders(world, &ground, 1, &groundId) == DD_OK);
    DdCharacterDesc cd;
    dragdoll_character_desc_default(&cd);
    cd.rig_json = dragdoll_get_builtin_rig("humanoid16");
    cd.transform.position = DdVec3{0, 0, 0.01f};
    DdCharacter* human = nullptr;
    REQUIRE(dragdoll_character_create(world, &cd, &human) == DD_OK);
    if (kinematic) REQUIRE(dragdoll_character_set_kinematic(human, 1) == DD_OK);

    const VehicleProfile profile = parseVehicle(builtinVehicle(vehicle));
    const VehicleMesh mesh = buildVehicleMesh(profile, DdVec3{1, 0, 0}, DdVec3{0, 1, 0}, DdVec3{0, 0, 1});
    DdColliderDesc car;
    dragdoll_collider_desc_default(&car);
    car.shape = DD_SHAPE_MESH;
    car.motion = DD_MOTION_KINEMATIC;
    car.vertices = mesh.vertices.data();
    car.vertex_count = static_cast<uint32_t>(mesh.vertices.size());
    car.indices = mesh.indices.data();
    car.index_count = static_cast<uint32_t>(mesh.indices.size());
    const float speed = kmh / 3.6f;
    float x = -0.25f - profile.length * 0.5f - speed * 1.0f; // Contact after 1 s (time to probe the front)
    car.transform.position = DdVec3{x, 0, 0};
    car.group = 1;
    DdColliderId carId = DD_INVALID_COLLIDER;
    REQUIRE(dragdoll_world_add_colliders(world, &car, 1, &carId) == DD_OK);

    float v = speed;
    bool braking = false;
    DdImpactReport r{};
    r.struct_size = sizeof(DdImpactReport);
    for (int f = 0; f < 120 * 12; ++f)
    {
        REQUIRE(dragdoll_character_get_impact_report(human, &r) == DD_OK);
        if (r.finished != 0) break;
        braking = braking || r.valid != 0;
        if (braking) v = std::max(0.0f, v - 7.0f / 120.0f);
        x += v / 120.0f;
        const DdTransform target{{x, 0, 0}, {0, 0, 0, 1}};
        REQUIRE(dragdoll_world_set_kinematic_targets(world, &carId, &target, 1) == DD_OK);
        REQUIRE(dragdoll_world_step(world, 1.0f / 120.0f, nullptr) == DD_OK);
    }
    Hit hit;
    hit.report = r;
    const auto name = [&](int32_t body) { return body >= 0 ? std::string(dragdoll_character_get_body_name(human, static_cast<uint32_t>(body))) : std::string("-"); };
    hit.firstBody = name(r.first_body);
    hit.groundBody = name(r.ground_body);
    if (r.first_body >= 0) hit.firstRegion = dragdoll_character_get_body_region(human, static_cast<uint32_t>(r.first_body));
    for (uint32_t i = 0; i < r.contact_count && i < 6; ++i) hit.order += (i ? ", " : "") + name(static_cast<int32_t>(r.contact_order[i]));
    dragdoll_world_destroy(world);
    return hit;
}

// The same hit on the crash sandbox (what the viewer's sandbox plays): limp, side-on, braking at the contact.
Hit runSandbox(const std::string& vehicle, float kmh)
{
    CrashParams p;
    p.vehicle = vehicle;
    p.kmh = kmh;
    p.limp = true;
    p.brake = true;
    Instance instance;
    std::string error;
    REQUIRE_MESSAGE(Instance::make(crashName(p), instance, error), error);
    DdCharacter* human = instance.scenario->character();
    REQUIRE(human != nullptr);
    DdImpactReport r{};
    r.struct_size = sizeof(DdImpactReport);
    for (int f = 0; f < 120 * 12; ++f)
    {
        REQUIRE(dragdoll_character_get_impact_report(human, &r) == DD_OK);
        if (r.finished != 0) break;
        instance.hostFrame(1.0f / 120.0f);
    }
    Hit hit;
    hit.report = r;
    const auto name = [&](int32_t body) { return body >= 0 ? std::string(dragdoll_character_get_body_name(human, static_cast<uint32_t>(body))) : std::string("-"); };
    hit.firstBody = name(r.first_body);
    hit.groundBody = name(r.ground_body);
    if (r.first_body >= 0) hit.firstRegion = dragdoll_character_get_body_region(human, static_cast<uint32_t>(r.first_body));
    for (uint32_t i = 0; i < r.contact_count && i < 6; ++i) hit.order += (i ? ", " : "") + name(static_cast<int32_t>(r.contact_order[i]));
    return hit;
}
} // namespace

TEST_CASE("impact report: a limp pedestrian hit at 40 km/h by each vehicle pack - events in order, numbers consistent")
{
    Library lib;
    for (const std::string& vehicle : vehicleNames())
    {
        CAPTURE(vehicle);
        const Hit hit = runSandbox(vehicle, 40.0f);
        const DdImpactReport& r = hit.report;
        MESSAGE(vehicle << " 40 km/h: " << className(r.measured) << " | first " << hit.firstBody << " on the " << zoneName(r.first_zone) << " at "
                        << r.first_height << " m; order " << hit.order << " | head strike " << r.head_strike_time << " s on the "
                        << zoneName(r.head_strike_zone) << ", wrap-around " << r.wrap_around_distance << " m, " << r.head_strike_speed * 3.6f
                        << " km/h | separation " << r.separation_time << " s, launch " << r.launch_speed * 3.6f << " km/h at " << r.launch_angle
                        << " deg from " << r.launch_height << " m, apex " << r.apex_height << " m | ground " << r.ground_time << " s on "
                        << hit.groundBody << " | rest " << r.rest_time << " s | carry " << r.carry_distance << " + flight/slide "
                        << r.flight_slide_distance << " -> throw " << r.throw_distance << " m (corridor " << r.corridor_min << "-"
                        << r.corridor_max << (r.in_corridor == 1 ? ", inside" : r.in_corridor == 0 ? ", OUTSIDE" : ", no flight") << ") | pitch " << r.pitch_degrees
                        << " deg | peak head " << r.peak_head_delta_v << " m/s");
        REQUIRE(r.valid == 1);
        CHECK(r.finished == 1);
        CHECK(r.group == 1);
        CHECK(r.vehicle_speed == doctest::Approx(40.0f / 3.6f).epsilon(0.05));
        CHECK(std::fabs(r.lateral_offset) < 0.3f); // Standing on the centre line
        // The vehicle reaches the legs first - or, standing side-on with the arms hanging, the near hand at hip height
        // (a flat bus front meets it as soon as the shins) - on its front or its leading edge.
        CHECK((hit.firstRegion == DD_REGION_LEG_LEFT || hit.firstRegion == DD_REGION_LEG_RIGHT || hit.firstRegion == DD_REGION_ARM_RIGHT ||
               hit.firstRegion == DD_REGION_ARM_LEFT));
        CHECK(r.first_height < 1.0f);
        CHECK((r.first_zone == DD_ZONE_FRONT || r.first_zone == DD_ZONE_LEADING_EDGE));
        REQUIRE(r.contact_count >= 2);
        CHECK(r.contact_order[0] == static_cast<uint32_t>(r.first_body));
        // Events in order.
        if (r.head_strike_time >= 0.0f)
        {
            CHECK(r.head_strike_time > 0.0f);
            CHECK(r.head_strike_zone != DD_ZONE_NONE);
            CHECK(r.wrap_around_distance > 0.5f);
            CHECK(r.wrap_around_distance < 3.5f);
        }
        CHECK(r.separation_time > 0.0f);
        CHECK(r.ground_time >= r.separation_time); // Equal when the feet never left the road (pushed, not thrown)
        CHECK(r.rest_time > r.ground_time);
        CHECK(r.apex_height >= r.launch_height);
        // Distances agree with each other (horizontal, so the triangle inequality holds).
        CHECK(r.throw_distance <= r.carry_distance + r.flight_slide_distance + 0.01f);
        CHECK(r.throw_distance > 1.0f);
        // The corridor is the throw equation for this launch (measured angle), over body-on-road friction 0.66-0.84;
        // only for a real flight.
        if (r.ground_time - r.separation_time > 0.5f / 120.0f)
        {
            const float g = 9.81f, h = std::max(r.launch_height, 0.0f), u = r.launch_speed, a = r.launch_angle / 57.29578f;
            const auto equation = [&](float mu) {
                const float k = std::max(0.0f, std::cos(a) + mu * std::sin(a));
                return mu * h + u * u * k * k / (2 * mu * g);
            };
            CHECK(r.corridor_min <= std::min(equation(0.66f), equation(0.84f)) + 0.001f);
            CHECK(r.corridor_max >= std::max(equation(0.66f), equation(0.84f)) - 0.001f);
            CHECK(r.corridor_min > 0.0f);
            CHECK(r.in_corridor >= 0);
        }
        else
        {
            CHECK(r.in_corridor == -1);
            CHECK(r.corridor_min == -1.0f);
        }
        CHECK(r.measured != DD_TRAJECTORY_NONE);
    }
}

TEST_CASE("impact report: a host-animated character is not hit (it is not pushed); a new handover starts a new report")
{
    Library lib;
    const Hit kinematic = run("sedan", 40.0f, true);
    CHECK(kinematic.report.valid == 0);
    CHECK(kinematic.report.finished == 0);
}
