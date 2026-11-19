// SPDX-License-Identifier: MIT
// The crash sandbox and its analyzer (crash.hpp): the same runs the viewer's "Crash sandbox" panel plays.
#include <dragdoll_scenarios/crash.hpp>

#include <doctest/doctest.h>

#include <string>

using namespace dragdoll::scenarios;

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

std::string summary(const CrashAnalysis& a)
{
    std::string s = std::string(verdictName(a.overall)) + " (" + a.outcome + ")";
    for (const RuleResult& r : a.results)
    {
        if (r.verdict != Verdict::Pass) s += "\n  " + std::string(verdictName(r.verdict)) + " " + r.id + ": " + r.measured + " (expected " + r.expected + ")";
    }
    return s;
}
} // namespace

TEST_CASE("crash: settings round-trip through the scenario name; bad names are refused")
{
    CrashParams p;
    p.vehicle = "pickup";
    p.kmh = 55.0f;
    p.aware = true;
    p.brake = true;
    p.brakeDelayMs = 250.0f;
    p.facing = 270.0f;
    p.size = 1.0f;
    p.offset = -0.4f;
    CrashParams q;
    std::string error;
    REQUIRE(parseCrashName(crashName(p), q, error));
    CHECK(crashName(q) == crashName(p));
    REQUIRE(parseCrashName("crash", q, error));
    CHECK(crashName(q) == crashName(CrashParams{}));
    CHECK_FALSE(parseCrashName("crash:vehicle=spaceship", q, error));
    CHECK_FALSE(parseCrashName("crash:kmh=fast", q, error));
    CHECK_FALSE(parseCrashName("crash:colour=red", q, error));
}

TEST_CASE("crash: a sample of settings - no sanity or contract rule broken")
{
    Library lib;
    const char* names[] = {
        "crash:vehicle=sedan,kmh=40,brake=1",
        "crash:vehicle=sedan,kmh=40,brake=0",
        "crash:vehicle=sedan,kmh=40,brake=0,aware=1,facing=0",
        "crash:vehicle=van,kmh=40,brake=1",
        "crash:vehicle=sports,kmh=30,brake=1,facing=270",
        "crash:vehicle=sedan,kmh=40,brake=1,limp=1",
    };
    for (const char* name : names)
    {
        CAPTURE(name);
        CrashParams p;
        std::string error;
        REQUIRE(parseCrashName(name, p, error));
        CrashObservations o;
        CrashAnalysis a;
        REQUIRE_MESSAGE(runCrash(p, o, a, error), error);
        MESSAGE(std::string(name) << ": " << summary(a));
        CHECK(a.overall != Verdict::Fail);
    }
}
