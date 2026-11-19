// SPDX-License-Identifier: MIT
// The crash sandbox: one parametrised vehicle-hits-pedestrian scenario, and the analyzer that judges a run against
// the published pedestrian-impact knowledge (docs/reference/pedestrian-impact.md) and DragDoll's own contracts.
// The viewer plays any setting (its "Crash sandbox" panel), the tests and the crash lab sweep thousands of them -
// the same code, so what is seen is what is tested.
#pragma once

#include <dragdoll_scenarios/scenario.hpp>

#include <optional>
#include <string>
#include <vector>

namespace dragdoll::scenarios
{
// The settings of one crash. Every combination is a scenario named crashName(params) ("crash:vehicle=sedan,...").
struct CrashParams
{
    std::string vehicle = "sedan"; // A vehicle pack (packs/vehicles)
    float kmh = 40.0f;             // Speed at the contact
    bool limp = false;             // false: as in a game - host-animated, car_hit behaviour, handed over at contact
    bool aware = false;            // Game mode only: DragDoll's own anticipation (general profile; SA takes `start`)
    // What the game had the pedestrian doing as the vehicle arrived (SA, measured over 1 133 hits: none 52 %, evasive
    // step 25 %, dive 18 %, running 2 %, hands up 2 %). Any reaction = aware (the game decided it noticed).
    std::string start = "none";    // none / step / dive / handsup / run (step and dive turn to their own direction across the vehicle's path)
    std::string side = "left";     // Which way a dodge / dive goes: to the vehicle's "left" or "right" - independent of
                                   // where the pedestrian stands, so it can also dive across the path, into the bonnet
    float leadMs = 0.0f;           // With a reaction: how long before the vehicle reaches the pedestrian's starting spot
                                   // the reaction begins (0 = as the game: 1150 ms dodge, 1000 dive, 1200 hands up; not used by the run: it is running from the start, the way it faces). Long enough
                                   // and a step or dive gets clear: the vehicle passes (evaded).
    bool brake = false;            // false: the vehicle keeps going (what game drivers mostly do)
    float brakeDelayMs = 0.0f;     // Braking starts this long after the first contact
    float decel = 0.0f;            // m/s^2 while braking; 0 = the vehicle pack's own brake_decel
    float facing = 90.0f;          // Degrees: 0 = facing the vehicle, 90 = struck on its right side, 180 = from behind,
                                   // 270 = struck on its left side
    float size = 1.10f;            // The rig's size: 1 = 1.75 m, 1.10 = about 1.93 m (a game-scale pedestrian)
    float offset = 0.0f;           // m sideways from the vehicle's centre line (+ = the vehicle's left)
};

// "crash:vehicle=sedan,kmh=40,limp=0,aware=0,start=none,side=left,lead_ms=0,brake=0,delay_ms=0,decel=0,facing=90,size=1.1,offset=0".
std::string crashName(const CrashParams& params);
// Accepts any subset of the keys after "crash" (the rest keep their defaults). False + error on a bad name.
bool parseCrashName(const std::string& name, CrashParams& params, std::string& error);
// The vehicles the sandbox offers (all vehicle packs).
std::vector<std::string> crashVehicles();
// The starting states (what the game had the pedestrian doing), and how often each occurred in game (0..1).
std::vector<std::string> crashStarts();
float crashStartShare(const std::string& start);

// What a crash run recorded, filled every host frame by the scenario (times in s from the start of the run).
struct CrashObservations
{
    // Setting
    std::string vehicle;
    float kmh = 0.0f;
    bool limp = false, aware = false, brake = false;
    std::string start;
    float facing = 0.0f, offset = 0.0f;
    float stature = 1.75f;         // m (1.75 x size)
    float bonnetEdgeHeight = 0.0f; // m, from the vehicle pack
    float vehicleHalfWidth = 0.0f;
    bool contactExpected = true;   // The pedestrian stands within the vehicle's width
    // Sanity
    bool nanSeen = false;
    float lowestBody = 1e9f;       // m: lowest body centre above the ground over the run
    float fastestBody = 0.0f;      // m/s: fastest body over the run
    bool failed = false;           // The character reached its FAILED state
    // Contract (game mode)
    double handover = -1.0, contact = -1.0, brace = -1.0, carried = -1.0, guard = -1.0, offVehicle = -1.0;
    int legYielded = -1;           // At the impact: 1 = every leg touching the vehicle has given, 0 = one stayed tense,
                                   // -1 = no leg touched it then
    // The end of the run
    double duration = 0.0;
    bool onVehicleAtEnd = false;   // Still lying on / carried by the vehicle when the run ended
    float pelvisBehindFront = 0.0f; // m: how far the pelvis lies behind the vehicle's front at the end (< 0 = ahead of it)
    float pelvisHeight = 0.0f;     // m above the ground at the end
    float pelvisMoved = 0.0f;      // m: horizontal distance the pelvis moved over the run
    bool pelvisInsideVehicle = false; // At the end the pelvis lies inside the vehicle's solid shape (went through it)
    std::string endStatus;         // The behaviour's stage at the end ("hurt (head)", "hurt (belly)", ...)
    float gestureError = -1.0f;    // m: in the Hurt stage, the farther hand's distance from the surface of the part that
                                   // hurts most (both hands go there); -1 = not hurt
    std::string hurtPart;          // ...and that part's body name
    DdVec3 hands[2]{}, handTargets[2]{}; // At the end: left / right hand, and where the gesture puts them
    double hurtSince = -1.0;       // When the behaviour entered its Hurt stage (s from the start; -1 = never) // At the end: how much the belly / the left side faces up (1 = straight up)
    bool handsFolded = false;      // At the end both hands touch (< 0.12 m apart) within 0.45 m of the head (x size):
                                   // folded at the face / chest ("praying"); holding the head from both sides is not
    // DragDoll's impact report at the end (valid = touched)
    DdImpactReport report{};
};

enum class Verdict
{
    Pass,        // Inside every reference that applies
    Fail,        // A sanity or contract rule is broken: a real defect
    OutOfRange,  // Outside a published corridor that applies to this setting
    NoReference, // Nothing published covers this setting (or the measure was not available): for the founder to judge
};
const char* verdictName(Verdict v);

struct RuleResult
{
    std::string id;
    std::string kind;     // sanity / contract / corridor / reference
    std::string text;     // What the rule says
    std::string source;   // Where it comes from (with its verification tag)
    std::string measured; // The measured value, as text
    std::string expected; // The expected range, as text
    Verdict verdict = Verdict::Pass;
};

struct CrashAnalysis
{
    Verdict overall = Verdict::Pass;
    std::string outcome;              // DragDoll's measured class (wrap, forward projection ...)
    std::vector<RuleResult> results;  // Every rule that applied (and the "no reference" notes)
    int corridorRulesApplied = 0;
};

// Judges a finished run with the built-in rules (packs/analyzer/crash_rules.json).
CrashAnalysis analyzeCrash(const CrashObservations& observations);
// The observations of a crash scenario instance (nullptr if the scenario is not a crash).
const CrashObservations* crashObservations(const Scenario& scenario);
// Runs one crash headless to its end (60 host frames a second, as most games) and analyses it.
// False + error if the setting is invalid.
bool runCrash(const CrashParams& params, CrashObservations& observations, CrashAnalysis& analysis, std::string& error);
} // namespace dragdoll::scenarios
