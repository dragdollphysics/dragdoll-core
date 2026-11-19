// SPDX-License-Identifier: MIT
// Crash sandbox settings (names), the analyzer and the headless runner. The scenario itself lives in scenarios.cpp.
#include <dragdoll_scenarios/crash.hpp>
#include <dragdoll_scenarios/vehicles.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace dragdoll::scenarios
{
const char* builtinCrashRules(); // Generated from packs/analyzer/crash_rules.json

namespace
{
std::string number(float v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%g", static_cast<double>(v));
    return b;
}

bool parseFloat(const std::string& text, float& out)
{
    char* end = nullptr;
    const float v = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    out = v;
    return true;
}

const char* trajectoryName(DdTrajectory t)
{
    switch (t)
    {
    case DD_TRAJECTORY_WRAP: return "wrap";
    case DD_TRAJECTORY_FORWARD_PROJECTION: return "forward projection";
    case DD_TRAJECTORY_FENDER_VAULT: return "fender vault";
    case DD_TRAJECTORY_ROOF_VAULT: return "roof vault";
    case DD_TRAJECTORY_SOMERSAULT: return "somersault";
    default: return "none";
    }
}

const char* zoneName(DdZone z)
{
    switch (z)
    {
    case DD_ZONE_FRONT: return "front";
    case DD_ZONE_LEADING_EDGE: return "leading edge";
    case DD_ZONE_BONNET: return "bonnet";
    case DD_ZONE_WINDSCREEN: return "windscreen";
    case DD_ZONE_ROOF: return "roof";
    default: return "none";
    }
}

// A measured value: a number or a word; absent = not measured in this run.
struct Value
{
    bool present = false;
    bool isText = false;
    double number = 0.0;
    std::string text;
    static Value of(double v) { return Value{true, false, v, {}}; }
    static Value word(std::string w) { return Value{true, true, 0.0, std::move(w)}; }
    std::string show() const
    {
        if (!present) return "not measured";
        if (isText) return text;
        char b[32];
        std::snprintf(b, sizeof(b), "%.3g", number);
        return b;
    }
};

Value measure(const std::string& name, const CrashObservations& o)
{
    const DdImpactReport& r = o.report;
    if (name == "nan") return Value::of(o.nanSeen ? 1 : 0);
    if (name == "lowest_body_m") return Value::of(o.lowestBody);
    if (name == "body_speed_excess") return Value::of(o.fastestBody - (2.0 * o.kmh / 3.6 + 5.0));
    if (name == "failed") return Value::of(o.failed ? 1 : 0);
    if (name == "contact") return Value::of(r.valid ? 1 : 0);
    if (name == "handover_lead_m")
        return o.handover >= 0.0 && o.contact >= 0.0 ? Value::of((o.contact - o.handover) * o.kmh / 3.6) : Value{};
    if (name == "leg_yielded") return o.contact >= 0.0 ? Value::of(o.legYielded) : Value{};
    if (name == "reflex_ms") return o.brace >= 0.0 && o.contact >= 0.0 ? Value::of((o.brace - o.contact) * 1000.0) : Value{};
    if (name == "guard_on_vehicle")
    {
        if (o.guard < 0.0) return Value::of(-1); // Never guarded (e.g. never lay on the vehicle)
        // The stage it began in (the startle at the hit holds the arms for a moment, so the guard may start a little
        // after the body landed on the vehicle - but never in the air).
        const bool onVehicle = o.guardStartedCarried && (o.offVehicle < 0.0 || o.guard < o.offVehicle);
        return Value::of(onVehicle ? 1 : 0);
    }
    if (name == "on_vehicle_at_end") return Value::of(o.onVehicleAtEnd ? 1 : 0);
    if (name == "inside_vehicle") return Value::of(o.pelvisInsideVehicle ? 1 : 0);
    if (name == "hands_folded") return Value::of(o.handsFolded ? 1 : 0);
    if (name == "in_pain") return o.report.valid ? Value::of(o.endStatus.rfind("hurt", 0) == 0 ? 1 : 0) : Value{};
    if (name == "gesture_m") return o.gestureError >= 0.0f ? Value::of(o.gestureError) : Value{};
    if (name == "pelvis_moved_m") return Value::of(o.pelvisMoved);
    // The first landing (task 4.11a). The first hand: the earlier of the two that landed.
    const int firstHand = o.landHand[0] < 0.0 ? (o.landHand[1] < 0.0 ? -1 : 1) : (o.landHand[1] < 0.0 || o.landHand[0] <= o.landHand[1] ? 0 : 1);
    if (name == "hand_trunk_ms")
        return firstHand >= 0 && o.landTrunk >= 0.0 ? Value::of((o.landHand[firstHand] - o.landTrunk) * 1000.0) : Value{};
    if (name == "both_hands")
        return o.landFirst >= 0.0 ? Value::of(o.landHand[0] >= 0.0 && o.landHand[1] >= 0.0 && std::fabs(o.landHand[0] - o.landHand[1]) <= 0.25 ? 1 : 0)
                                  : Value{};
    if (name == "hand_contact_ratio")
        return firstHand >= 0 && o.landHandRatio[firstHand] >= 0.0f ? Value::of(o.landHandRatio[firstHand]) : Value{};
    if (name == "head_first") return o.landFirst >= 0.0 ? Value::of(o.landHeadFirst ? 1 : 0) : Value{};
    if (!r.valid) return Value{};
    if (name == "class") return Value::word(trajectoryName(r.measured));
    if (name == "head_zone") return r.head_strike_zone != DD_ZONE_NONE ? Value::word(zoneName(r.head_strike_zone)) : Value{};
    if (name == "head_strike_ms") return r.head_strike_zone != DD_ZONE_NONE ? Value::of(r.head_strike_time * 1000.0) : Value{};
    if (name == "wad_m") return r.wrap_around_distance > 0.0f ? Value::of(r.wrap_around_distance) : Value{};
    if (name == "head_speed_ratio")
        return r.head_strike_zone != DD_ZONE_NONE && r.vehicle_speed > 0.1f ? Value::of(r.head_strike_speed / r.vehicle_speed) : Value{};
    if (name == "flight_s")
        return r.finished && r.in_corridor != -1 && r.ground_time > r.separation_time ? Value::of(r.ground_time - r.separation_time) : Value{};
    if (name == "throw_m") return r.finished ? Value::of(r.throw_distance) : Value{};
    if (name == "searle") return r.finished ? Value::of(r.in_corridor) : Value{};
    return Value{};
}

bool sideOn(float facing)
{
    float f = std::fmod(facing, 360.0f);
    if (f < 0.0f) f += 360.0f;
    return std::fabs(f - 90.0f) <= 45.0f || std::fabs(f - 270.0f) <= 45.0f;
}

// Do all of a rule's conditions hold for this run?
bool applies(const nlohmann::json& when, const CrashObservations& o)
{
    if (!when.is_object()) return true;
    const DdImpactReport& r = o.report;
    for (const auto& [key, v] : when.items())
    {
        if (key == "mode" && (v.get<std::string>() == "game") == o.limp) return false;
        if (key == "outcome" && (o.limp || v.get<std::string>() != o.outcome)) return false;
        if (key == "aware" && v.get<bool>() != o.aware) return false;
        if (key == "drive" && (v.get<std::string>() == "brake") != o.brake) return false;
        if (key == "kmh" && (o.kmh < v[0].get<float>() || o.kmh > v[1].get<float>())) return false;
        if (key == "front" && (v.get<std::string>() == "low") != (o.bonnetEdgeHeight < 0.57f * o.stature)) return false;
        if (key == "side_on" && v.get<bool>() != sideOn(o.facing)) return false;
        if (key == "contact_expected" && v.get<bool>() != o.contactExpected) return false;
        if (key == "hurt")
        {
            const bool hurt = o.endStatus.rfind("hurt", 0) == 0;
            if (v.is_boolean() ? v.get<bool>() != hurt : o.endStatus != "hurt (" + v.get<std::string>() + ")") return false;
        }
        if (key == "finished" && v.get<bool>() != (r.valid && r.finished)) return false;
        if (key == "flew" && v.get<bool>() != (r.valid && r.finished && r.in_corridor != -1)) return false;
        if (key == "class")
        {
            if (!r.valid) return false;
            const std::string c = trajectoryName(r.measured);
            if (std::none_of(v.begin(), v.end(), [&](const nlohmann::json& x) { return x.get<std::string>() == c; })) return false;
        }
    }
    return true;
}

// A published throw band (first contact to rest) at the impact speed: Wood, Simms & Walsh 2005, Table 1 (the 99.9 and
// 0.1 percentile curves of the predicted speed bound the throw from below and above).
bool band(const std::string& name, float kmh, double& lo, double& hi)
{
    const double v = kmh / 3.6;
    if (name == "wood2005_wrap")
    {
        const auto s = [&](double so, double c, double d) { return so + std::pow(v / c, 1.0 / d); };
        lo = s(1.2, 5.3, 0.46);
        hi = s(1.9, 2.3, 0.51);
        return true;
    }
    if (name == "wood2005_forward")
    {
        const auto s = [&](double a, double b) { return v > b ? ((v - b) / a) * ((v - b) / a) : 0.0; };
        lo = s(4.6, -1.0);
        hi = s(2.5, -1.2);
        return true;
    }
    return false;
}

const nlohmann::json& rules()
{
    static const nlohmann::json kRules = nlohmann::json::parse(builtinCrashRules());
    return kRules;
}
} // namespace

std::string crashName(const CrashParams& p)
{
    return "crash:vehicle=" + p.vehicle + ",kmh=" + number(p.kmh) + ",limp=" + (p.limp ? "1" : "0") + ",outcome=" + p.outcome + ",aware=" +
           (p.aware ? "1" : "0") + ",start=" + p.start + ",side=" + p.side + ",lead_ms=" + number(p.leadMs) + ",brake=" + (p.brake ? "1" : "0") + ",delay_ms=" + number(p.brakeDelayMs) +
           ",decel=" + number(p.decel) + ",facing=" + number(p.facing) + ",size=" + number(p.size) + ",offset=" + number(p.offset);
}

bool parseCrashName(const std::string& name, CrashParams& p, std::string& error)
{
    p = CrashParams{};
    if (name == "crash") return true;
    if (name.rfind("crash:", 0) != 0)
    {
        error = "a crash name starts with \"crash:\"";
        return false;
    }
    std::stringstream items(name.substr(6));
    std::string item;
    while (std::getline(items, item, ','))
    {
        const size_t eq = item.find('=');
        if (eq == std::string::npos)
        {
            error = "\"" + item + "\" is not key=value";
            return false;
        }
        const std::string key = item.substr(0, eq), value = item.substr(eq + 1);
        float f = 0.0f;
        const bool isNumber = parseFloat(value, f);
        if (key == "vehicle")
        {
            if (builtinVehicle(value) == nullptr)
            {
                error = "unknown vehicle \"" + value + "\"";
                return false;
            }
            p.vehicle = value;
            continue;
        }
        if (key == "outcome")
        {
            if (value != "alive" && value != "dead")
            {
                error = "outcome must be alive or dead";
                return false;
            }
            p.outcome = value;
            continue;
        }
        if (key == "side")
        {
            if (value != "left" && value != "right")
            {
                error = "side must be left or right";
                return false;
            }
            p.side = value;
            continue;
        }
        if (key == "start")
        {
            const std::vector<std::string> starts = crashStarts();
            if (std::find(starts.begin(), starts.end(), value) == starts.end())
            {
                error = "unknown start \"" + value + "\" (none / step / dive / handsup / run)";
                return false;
            }
            p.start = value;
            continue;
        }
        if (!isNumber)
        {
            error = key + ": \"" + value + "\" is not a number";
            return false;
        }
        if (key == "kmh" && f >= 1.0f && f <= 200.0f) p.kmh = f;
        else if (key == "limp") p.limp = f != 0.0f;
        else if (key == "aware") p.aware = f != 0.0f;
        else if (key == "prehit") continue; // Retired setting (old feedback names)
        else if (key == "brake") p.brake = f != 0.0f;
        else if (key == "delay_ms" && f >= 0.0f && f <= 5000.0f) p.brakeDelayMs = f;
        else if (key == "lead_ms" && f >= 0.0f && f <= 3000.0f) p.leadMs = f;
        else if (key == "decel" && f >= 0.0f && f <= 12.0f) p.decel = f;
        else if (key == "facing") p.facing = std::fmod(std::fmod(f, 360.0f) + 360.0f, 360.0f);
        else if (key == "size" && f >= 0.5f && f <= 1.5f) p.size = f;
        else if (key == "offset" && std::fabs(f) <= 5.0f) p.offset = f;
        else
        {
            error = "unknown key or value out of range: \"" + item + "\"";
            return false;
        }
    }
    return true;
}

std::vector<std::string> crashVehicles()
{
    return vehicleNames();
}

bool crashOutcomePlausible(const std::string& outcome, float kmh)
{
    return outcome == "dead" ? kmh >= 30.0f : kmh <= 40.0f;
}

std::vector<std::string> crashStarts()
{
    return {"none", "step", "dive", "handsup", "run"};
}

float crashStartShare(const std::string& start)
{
    // SA, 1 133 peds hit (research notes, 2026-11-19): 590 none, 284 step, 203 dive, 23 running, 19 hands up (and 14
    // rude gestures, counted with none: standing).
    if (start == "none") return 604.0f / 1133.0f;
    if (start == "step") return 284.0f / 1133.0f;
    if (start == "dive") return 203.0f / 1133.0f;
    if (start == "run") return 23.0f / 1133.0f;
    if (start == "handsup") return 19.0f / 1133.0f;
    return 0.0f;
}

const char* verdictName(Verdict v)
{
    switch (v)
    {
    case Verdict::Pass: return "PASS";
    case Verdict::Fail: return "FAIL";
    case Verdict::OutOfRange: return "OUT OF RANGE";
    default: return "NO REFERENCE";
    }
}

CrashAnalysis analyzeCrash(const CrashObservations& o)
{
    CrashAnalysis a;
    a.outcome = o.report.valid ? trajectoryName(o.report.measured) : o.contactExpected ? "no contact" : "evaded";
    for (const nlohmann::json& rule : rules()["rules"])
    {
        if (!applies(rule.value("when", nlohmann::json::object()), o)) continue;
        RuleResult res;
        res.id = rule.value("id", "");
        res.kind = rule.value("kind", "");
        res.text = rule.value("text", "");
        res.source = rule.value("source", "");
        res.share = rule.value("share", -1.0);
        // population: a published share of people (e.g. 72 % of falls); a single run outside it is no defect, so it never
        // changes the run's verdict - the crash lab compares the share of runs inside it with the published one.
        const Verdict miss = res.kind == "corridor" || res.kind == "population" ? Verdict::OutOfRange
                             : res.kind == "reference"                          ? Verdict::NoReference
                                                                                : Verdict::Fail;
        if (res.kind == "corridor") ++a.corridorRulesApplied;
        if (!rule.contains("measure"))
        {
            res.verdict = Verdict::NoReference; // A note: nothing published covers this setting
            a.results.push_back(res);
            continue;
        }
        const Value v = measure(rule["measure"].get<std::string>(), o);
        if (!v.present && (res.kind == "contract" || res.kind == "sanity" || res.kind == "population")) continue; // Nothing to judge (e.g. never touched)
        res.measured = v.show();
        bool ok = false;
        if (rule.contains("one_of"))
        {
            std::string list;
            for (const nlohmann::json& x : rule["one_of"])
            {
                list += (list.empty() ? "" : " / ") + (x.is_string() ? x.get<std::string>() : x.dump());
                if (v.present && (x.is_string() ? v.isText && v.text == x.get<std::string>() : !v.isText && v.number == x.get<double>())) ok = true;
            }
            res.expected = "one of " + list;
        }
        else
        {
            double lo = -1e300, hi = 1e300;
            if (rule.contains("range"))
            {
                if (!rule["range"][0].is_null()) lo = rule["range"][0].get<double>();
                if (!rule["range"][1].is_null()) hi = rule["range"][1].get<double>();
            }
            else if (rule.contains("band") && !band(rule["band"].get<std::string>(), o.kmh, lo, hi))
            {
                lo = hi = 0.0;
            }
            if (rule.value("scale", "") == "stature")
            {
                const double k = o.stature / 1.70;
                lo = lo > -1e299 ? lo * k : lo;
                hi = hi < 1e299 ? hi * k : hi;
            }
            char b[96];
            if (lo <= -1e299) std::snprintf(b, sizeof(b), "<= %.3g", hi);
            else if (hi >= 1e299) std::snprintf(b, sizeof(b), ">= %.3g", lo);
            else std::snprintf(b, sizeof(b), "%.3g .. %.3g", lo, hi);
            res.expected = b;
            ok = v.present && !v.isText && v.number >= lo && v.number <= hi;
        }
        if (!v.present && res.kind == "corridor") res.verdict = Verdict::NoReference; // Applies, but nothing to measure
        else res.verdict = ok ? Verdict::Pass : miss;
        a.results.push_back(res);
    }
    if (a.corridorRulesApplied == 0)
    {
        RuleResult note;
        note.id = "no_corridor";
        note.kind = "reference";
        note.text = "no published corridor applies to this setting";
        note.source = "docs/reference/pedestrian-impact.md";
        note.verdict = Verdict::NoReference;
        a.results.push_back(note);
    }
    const auto any = [&](Verdict v) {
        return std::any_of(a.results.begin(), a.results.end(), [&](const RuleResult& r) { return r.kind != "population" && r.verdict == v; });
    };
    a.overall = any(Verdict::Fail) ? Verdict::Fail : any(Verdict::OutOfRange) ? Verdict::OutOfRange
                                                   : any(Verdict::NoReference) ? Verdict::NoReference : Verdict::Pass;
    return a;
}

bool runCrash(const CrashParams& params, CrashObservations& observations, CrashAnalysis& analysis, std::string& error)
{
    Instance instance;
    if (!Instance::make(crashName(params), instance, error)) return false;
    const double end = instance.scenario->duration();
    while (instance.hostTime < end) instance.hostFrame(1.0f / 120.0f);
    const CrashObservations* o = crashObservations(*instance.scenario);
    if (o == nullptr)
    {
        error = "not a crash scenario";
        return false;
    }
    observations = *o;
    analysis = analyzeCrash(observations);
    return true;
}
} // namespace dragdoll::scenarios
