// SPDX-License-Identifier: MIT
// Standing balance (Phase 3, task 3.6, ADR-0023). Acceptance: hold_stance upright 60 s under 5 N*s random pokes;
// bump_5kmh absorbed without falling. Internal torques only.
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/scenario.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using dragdoll::scenarios::Instance;

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

float pelvisHeight(DdCharacter* c)
{
    DdTransform root{};
    REQUIRE(dragdoll_character_get_pose(c, 0, nullptr, 0, &root) == DD_OK);
    return root.position.z;
}

// Runs a scenario and returns the lowest pelvis height seen (sampled every host frame).
float lowestPelvis(const char* name, int fps, double seconds, float* worstError = nullptr)
{
    Instance run;
    std::string error;
    REQUIRE(Instance::make(name, run, error));
    DdCharacter* c = run.scenario->character();
    REQUIRE(c != nullptr);
    float lowest = 10.0f, worst = 0.0f;
    for (int f = 0; f < static_cast<int>(seconds * fps); ++f)
    {
        run.hostFrame(1.0f / static_cast<float>(fps));
        lowest = std::min(lowest, pelvisHeight(c));
        DdCharacterInfo info{};
        info.struct_size = sizeof(DdCharacterInfo);
        REQUIRE(dragdoll_character_get_info(c, &info) == DD_OK);
        REQUIRE(static_cast<int>(info.state) != DD_STATE_FAILED);
        worst = std::max(worst, info.balance_error);
    }
    if (worstError != nullptr) *worstError = worst;
    return lowest;
}
} // namespace

TEST_CASE("balance: quiet standing — upright and calm for 10 s")
{
    Library lib;
    float worst = 0.0f;
    const float lowest = lowestPelvis("stand_balance", 60, 10.0, &worst);
    MESSAGE("stand_balance: lowest pelvis " << lowest << " m, largest capture-point error " << worst * 100.0f << " cm");
    CHECK(lowest > 0.9f);
    CHECK(worst < 0.02f); // The 30 ms error smoothing lags a little while settling
}

TEST_CASE("balance: hold_stance — upright 60 s under 5 N*s random pokes every 2 s (acceptance)")
{
    Library lib;
    for (int fps : {30, 60})
    {
        CAPTURE(fps);
        float worst = 0.0f;
        const float lowest = lowestPelvis("hold_stance", fps, 60.0, &worst);
        MESSAGE(fps << " fps, 30 pokes: lowest pelvis " << lowest << " m, largest capture-point error " << worst * 100.0f << " cm");
        CHECK(lowest > 0.85f);
    }
}

TEST_CASE("balance: bump_5kmh — a car nudging the standing pedestrian at 5 km/h is absorbed (acceptance)")
{
    Library lib;
    for (int fps : {30, 60, 90, 120, 144, 200})
    {
        CAPTURE(fps);
        const float lowest = lowestPelvis("bump_5kmh", fps, 4.0);
        MESSAGE(fps << " fps: lowest pelvis " << lowest << " m");
        CHECK(lowest > 0.85f);
    }
}

TEST_CASE("balance: needs muscles — balance on a limp character changes nothing; API on/off")
{
    Library lib;
    Instance run;
    std::string error;
    REQUIRE(Instance::make("drop_from_1m", run, error));
    DdCharacter* c = run.scenario->character();
    CHECK(dragdoll_character_get_balance(c) == 0);
    REQUIRE(dragdoll_character_set_balance(c, 1) == DD_OK); // Still limp
    CHECK(dragdoll_character_get_balance(c) == 1);
    for (int f = 0; f < 240; ++f) run.hostFrame(1.0f / 60.0f);
    CHECK(pelvisHeight(c) < 0.4f); // Crumpled like any limp body
    CHECK(dragdoll_character_set_balance(nullptr, 1) == DD_ERROR_INVALID_ARGUMENT);
}

