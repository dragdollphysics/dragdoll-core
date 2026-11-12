// SPDX-License-Identifier: MIT
// Tests on the shared scenario library: the same scenarios the live viewer plays are checked here by numbers.

#include <dragdoll_scenarios/scenario.hpp>

#include <doctest/doctest.h>

#include <cmath>
#include <random>
#include <string>
#include <vector>

using dragdoll::scenarios::Instance;

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

Instance make(const std::string& name)
{
    Instance instance;
    std::string error;
    REQUIRE_MESSAGE(Instance::make(name, instance, error), error);
    return instance;
}

uint64_t hashOf(DdWorld* world)
{
    uint64_t h = 0;
    REQUIRE(dragdoll_world_compute_state_hash(world, &h) == DD_OK);
    return h;
}

DdTransform transformOf(DdWorld* world, DdColliderId id)
{
    DdTransform t{};
    REQUIRE(dragdoll_world_get_collider_transform(world, id, 0, &t) == DD_OK);
    return t;
}

struct GeometryCount
{
    int triangles = 0;
    int lines = 0;
};
void DD_CALL countTriangle(void* user, DdVec3, DdVec3, DdVec3, uint32_t)
{
    ++static_cast<GeometryCount*>(user)->triangles;
}
void DD_CALL countLine(void* user, DdVec3, DdVec3, uint32_t)
{
    ++static_cast<GeometryCount*>(user)->lines;
}
} // namespace

TEST_CASE("scenarios: every registered scenario builds and runs 5 s without errors")
{
    Library lib;
    const auto names = dragdoll::scenarios::list();
    CHECK(names.size() >= 6);
    for (const std::string& name : names)
    {
        CAPTURE(name);
        Instance instance = make(name);
        CHECK(std::string(instance.scenario->description()).size() > 20);
        for (int frame = 0; frame < 300; ++frame) // 5 s at 60 fps
        {
            instance.hostFrame(1.0f / 60.0f);
        }
        CHECK(dragdoll_world_get_step_count(instance.world) >= 590);
        CHECK(hashOf(instance.world) != 0);
        std::vector<std::string> hud;
        instance.scenario->hud(instance.world, hud); // Must not crash.
    }
    CHECK(dragdoll::scenarios::create("no_such_scenario") == nullptr);
}

TEST_CASE("scenarios: rate-independent scenarios are identical at 25-200 fps hosts (SA, 60 fps mods, 144 Hz)")
{
    Library lib;
    struct Profile
    {
        const char* name;
        float minFps, maxFps;
    };
    const Profile profiles[] = {
        {"SA 25-30", 25.0f, 30.0f}, {"60 fps mod", 55.0f, 65.0f}, {"144 Hz", 138.0f, 150.0f}, {"unlocked 30-200", 30.0f, 200.0f}};

    int checkedScenarios = 0;
    for (const std::string& name : dragdoll::scenarios::list())
    {
        Instance probe = make(name);
        if (!probe.scenario->rateIndependent())
        {
            continue;
        }
        ++checkedScenarios;
        constexpr uint32_t kSteps = 480; // 4 s

        // Reference: steady fixed steps.
        Instance reference = make(name);
        std::vector<uint64_t> expected(kSteps + 1);
        expected[0] = hashOf(reference.world);
        for (uint32_t i = 1; i <= kSteps; ++i)
        {
            REQUIRE(dragdoll_world_step_fixed(reference.world, 1) == DD_OK);
            expected[i] = hashOf(reference.world);
        }
        CHECK(expected[0] != expected[kSteps]);

        for (const Profile& profile : profiles)
        {
            CAPTURE(name);
            CAPTURE(profile.name);
            Instance host = make(name);
            std::mt19937 rng(99);
            std::uniform_real_distribution<float> dtDist(1.0f / profile.maxFps, 1.0f / profile.minFps);
            while (dragdoll_world_get_step_count(host.world) < kSteps)
            {
                host.hostFrame(dtDist(rng));
                const uint64_t step = dragdoll_world_get_step_count(host.world);
                if (step <= kSteps)
                {
                    REQUIRE(hashOf(host.world) == expected[static_cast<size_t>(step)]);
                }
            }
        }
    }
    CHECK(checkedScenarios >= 3);
}

TEST_CASE("scenarios: ball drop rests at the right height for +Y and +Z up")
{
    Library lib;
    for (const char* name : {"ball_drop_yup", "ball_drop_zup"})
    {
        CAPTURE(name);
        Instance instance = make(name);
        for (int i = 0; i < 240; ++i)
        {
            instance.hostFrame(1.0f / 60.0f);
        }
        std::vector<std::string> hud;
        instance.scenario->hud(instance.world, hud);
        REQUIRE(!hud.empty());
        // "ball height above ground: 0.300 m (rest = 0.300)"
        const double height = std::stod(hud[0].substr(hud[0].find(':') + 1));
        CHECK(height == doctest::Approx(0.3).epsilon(0.05));
    }
}

TEST_CASE("scenarios: car hits a thin pole at 50 and 120 km/h - hit, not tunnelled, same outcome at any host rate")
{
    Library lib;
    for (const char* name : {"car_vs_pole_50kmh", "car_vs_pole_120kmh"})
    {
        CAPTURE(name);
        std::vector<float> poleX;
        for (float fps : {30.0f, 60.0f, 144.0f})
        {
            CAPTURE(fps);
            Instance instance = make(name);
            // The car reaches the pole at ~0.82-0.86 s; look 0.45 s after the impact.
            while (instance.hostTime < 1.3)
            {
                instance.hostFrame(1.0f / fps);
            }
            const DdTransform pole = transformOf(instance.world, 3); // ids: ground = 1, car = 2, pole = 3
            CHECK(pole.position.x > 2.0f); // Knocked away hard: a tunnelled pole would still stand near x = 0.
            poleX.push_back(pole.position.x);
        }
        // The frame-rate fix in action: the outcome must not depend on the host's frame rate.
        for (float x : poleX)
        {
            CHECK(std::fabs(x - poleX[0]) < 0.75f);
        }
    }
}

TEST_CASE("debug draw: dev builds produce geometry, shipping builds report UNSUPPORTED")
{
    Library lib;
    Instance instance = make("pile_zup");
    GeometryCount count;
    DdDebugDrawCallbacks callbacks{sizeof(DdDebugDrawCallbacks), &count, &countLine, &countTriangle};
    const DdResult result = dragdoll_world_debug_draw(instance.world, &callbacks);
    if (result == DD_OK)
    {
        CHECK(count.triangles > 100); // Ground box + 12 props.
    }
    else
    {
        CHECK(result == DD_ERROR_UNSUPPORTED);
        CHECK(count.triangles == 0);
    }

    // Argument validation.
    CHECK(dragdoll_world_debug_draw(nullptr, &callbacks) == DD_ERROR_INVALID_ARGUMENT);
    callbacks.struct_size = 4;
    CHECK(dragdoll_world_debug_draw(instance.world, &callbacks) == DD_ERROR_INVALID_ARGUMENT);
}
