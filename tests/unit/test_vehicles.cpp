// SPDX-License-Identifier: MIT
// Vehicle shape pack (task 2.6): every pack parses into a closed, outward-facing mesh of the right size, and each
// hit scenario throws the pedestrian ahead of (or onto) the vehicle without passing through it.
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/scenario.hpp>
#include <dragdoll_scenarios/vehicles.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace dragdoll::scenarios;

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};
} // namespace

TEST_CASE("vehicles: the six built-in classes parse into closed outward meshes of the stated size")
{
    const std::vector<std::string> names = vehicleNames();
    CHECK(names == std::vector<std::string>{"bus", "motorbike", "sedan", "sports", "suv", "van"});
    for (const std::string& name : names)
    {
        CAPTURE(name);
        const VehicleProfile v = parseVehicle(builtinVehicle(name));
        const VehicleMesh mesh = buildVehicleMesh(v, DdVec3{1, 0, 0}, DdVec3{0, 1, 0}, DdVec3{0, 0, 1});
        REQUIRE(mesh.indices.size() % 3 == 0);

        // Closed and consistently wound: every directed edge appears exactly once, and its reverse exactly once.
        std::map<std::pair<uint32_t, uint32_t>, int> edges;
        for (size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            for (int k = 0; k < 3; ++k)
            {
                ++edges[{mesh.indices[i + k], mesh.indices[i + (k + 1) % 3]}];
            }
        }
        for (const auto& [edge, count] : edges)
        {
            CHECK(count == 1);
            CHECK(edges.count({edge.second, edge.first}) == 1);
        }

        // Outward-facing: positive enclosed volume (divergence theorem), roughly a box of the vehicle's size.
        double volume = 0.0;
        for (size_t i = 0; i < mesh.indices.size(); i += 3)
        {
            const DdVec3 &a = mesh.vertices[mesh.indices[i]], &b = mesh.vertices[mesh.indices[i + 1]], &c = mesh.vertices[mesh.indices[i + 2]];
            volume += (a.x * (b.y * c.z - b.z * c.y) - a.y * (b.x * c.z - b.z * c.x) + a.z * (b.x * c.y - b.y * c.x)) / 6.0;
        }
        const double box = static_cast<double>(v.length) * v.width * (v.roofHeight - v.clearance);
        CHECK(volume > 0.4 * box);
        CHECK(volume < box);

        float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
        for (const DdVec3& p : mesh.vertices)
        {
            minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
            minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
            minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
        }
        CHECK(maxX - minX == doctest::Approx(v.length).epsilon(0.001));
        CHECK(maxY - minY == doctest::Approx(v.width).epsilon(0.001));
        CHECK(minZ == doctest::Approx(v.clearance).epsilon(0.001));
        CHECK(maxZ == doctest::Approx(v.roofHeight).epsilon(0.001));
    }
    // Front-height classes as in the impact literature: low sports car < saloon < SUV < bus.
    auto edge = [](const char* n) { return parseVehicle(builtinVehicle(n)).bonnetEdgeHeight; };
    CHECK(edge("sports") < edge("sedan"));
    CHECK(edge("sedan") < edge("suv"));
    CHECK(edge("suv") < edge("bus"));
}

TEST_CASE("vehicles: invalid profiles are rejected with a precise message")
{
    CHECK_THROWS_WITH_AS(parseVehicle("[]"), doctest::Contains("not a JSON object"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(parseVehicle(R"({"format":"dragdoll-vehicle","version":1,"profile":{}})"),
                         doctest::Contains("length"), std::invalid_argument);
    std::string tooLong = builtinVehicle("sedan");
    const size_t at = tooLong.find("\"roof_length\": 1.4");
    REQUIRE(at != std::string::npos);
    tooLong.replace(at, 18, "\"roof_length\": 9.0");
    CHECK_THROWS_WITH_AS(parseVehicle(tooLong), doctest::Contains("longer than the vehicle"), std::invalid_argument);
}

TEST_CASE("vehicles: braking drive covers v*t, then stops after v^2/(2a)")
{
    const Drive d{10.0f, 1.0f, 5.0f};
    CHECK(d.distance(0.5) == doctest::Approx(5.0));
    CHECK(d.distance(1.0) == doctest::Approx(10.0));
    CHECK(d.distance(3.0) == doctest::Approx(20.0)); // Stopped at t = 3 s: 10 + 100/10
    CHECK(d.distance(10.0) == doctest::Approx(20.0));
}

TEST_CASE("vehicles: every hit scenario throws the pedestrian ahead of or onto the vehicle, never through it")
{
    Library lib;
    for (const std::string& vehicle : vehicleNames())
    {
        const std::string name = "hit_" + vehicle + "_40kmh";
        CAPTURE(name);
        Instance run;
        std::string error;
        REQUIRE(Instance::make(name, run, error));
        DdCharacter* c = run.scenario->character();
        REQUIRE(c != nullptr);
        for (int frame = 0; frame < 180; ++frame) // 3 s at 60 fps: the vehicle has stopped (40 km/h, 7 m/s^2)
        {
            run.hostFrame(1.0f / 60.0f);
        }
        std::vector<DdTransform> bodies(dragdoll_character_get_body_count(c));
        REQUIRE(dragdoll_character_get_body_transforms(c, 0, bodies.data(), static_cast<uint32_t>(bodies.size())) == DD_OK);
        const VehicleProfile v = parseVehicle(builtinVehicle(vehicle));
        const float speed = 40.0f / 3.6f;
        // Front face position once stopped: starts 0.25 m before the pedestrian, 0.1 s of travel, then braking.
        const float front = -0.25f + speed * speed / (2.0f * 7.0f);
        const DdTransform& pelvis = bodies[static_cast<size_t>(dragdoll_character_find_body(c, "pelvis"))];
        float lowest = 1e9f;
        for (const DdTransform& t : bodies)
        {
            REQUIRE(std::isfinite(t.position.x));
            REQUIRE(std::isfinite(t.position.z));
            lowest = std::min(lowest, t.position.z);
        }
        CAPTURE(pelvis.position.x);
        CAPTURE(pelvis.position.z);
        CAPTURE(front);
        CHECK(lowest > -0.02f);                                                      // Not sunk into the ground
        CHECK((pelvis.position.x > front - 0.3f || pelvis.position.z > v.bonnetEdgeHeight)); // Ahead of or on the vehicle
        CHECK(pelvis.position.x > 1.0f);                                             // Actually moved by the hit
    }
}
