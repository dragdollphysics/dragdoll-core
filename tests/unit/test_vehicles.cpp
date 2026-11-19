// SPDX-License-Identifier: MIT
// Vehicle shape pack (task 2.6): every pack parses into a closed, outward-facing mesh of the right size, and each
// hit scenario throws the pedestrian ahead of (or onto) the vehicle without passing through it.
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/scenario.hpp>
#include <dragdoll_scenarios/vehicles.hpp>
#include <dragdoll_scenarios/crash.hpp>

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
    CHECK(names == std::vector<std::string>{"box_truck", "bus", "estate", "heavy_truck", "motorbike", "pickup", "sedan", "sports", "suv",
                                            "van"});
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
    const size_t at = tooLong.find("\"roof_length\": ");
    REQUIRE(at != std::string::npos);
    tooLong.replace(at, tooLong.find_first_of(",}", at) - at, "\"roof_length\": 9.0");
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

TEST_CASE("vehicles: every vehicle, braking at 40 km/h on a limp and a game pedestrian - no sanity or contract rule broken")
{
    // Was: "every hit scenario throws the pedestrian ahead of or onto the vehicle, never through it" - now the crash
    // analyzer's sanity rules (not_through_vehicle, moved_by_hit, not_through_ground), run on the crash sandbox.
    Library lib;
    for (const std::string& vehicle : vehicleNames())
    {
        for (bool limp : {true, false})
        {
            CrashParams p;
            p.vehicle = vehicle;
            p.kmh = 40.0f;
            p.brake = true;
            p.limp = limp;
            CAPTURE(crashName(p));
            CrashObservations o;
            CrashAnalysis a;
            std::string error;
            REQUIRE_MESSAGE(runCrash(p, o, a, error), error);
            for (const RuleResult& r : a.results)
            {
                CAPTURE(r.id);
                CAPTURE(r.measured);
                CAPTURE(r.expected);
                CHECK(r.verdict != Verdict::Fail);
            }
        }
    }
}
