// SPDX-License-Identifier: MIT
// dragdoll_crashlab: sweeps the crash sandbox (scenarios/crash.hpp) over a grid of settings (10 vehicles x 8 speeds x
// 4 drive / brake modes x 8 facings x 2 sizes x 5 starting states = 25 600; start_share weights them as in game), judges every run with the
// analyzer and writes one CSV row per run - the table the viewer's "Crash results" window reads (any row opens there
// with exactly its settings).
//
//   dragdoll_crashlab                    full grid, all CPU cores, -> crashlab/results.csv next to the exe
//   dragdoll_crashlab --sample 500       a seeded random sample of the grid (--seed N)
//   dragdoll_crashlab --jobs 4           worker processes (default: the core count)
//   dragdoll_crashlab --only "crash:..." one setting, its full analysis printed
#include <dragdoll_scenarios/crash.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace dragdoll::scenarios;

namespace
{
std::vector<CrashParams> grid()
{
    std::vector<CrashParams> all;
    for (const std::string& vehicle : crashVehicles())
        for (float kmh : {10.0f, 20.0f, 30.0f, 40.0f, 50.0f, 60.0f, 80.0f, 100.0f})
            for (int drive = 0; drive < 4; ++drive) // Keeps going; brakes at once, after 300 ms, after 800 ms
                for (float facing : {0.0f, 45.0f, 90.0f, 135.0f, 180.0f, 225.0f, 270.0f, 315.0f})
                    for (float size : {1.0f, 1.10f})
                        for (const std::string& start : crashStarts())
                          for (float lead : {0.0f, 400.0f, 700.0f, 1500.0f})
                          for (const char* side : {"left", "right"})
                          {
                            // Dodge / dive go either way; the others have no side.
                            if (std::string(side) == "right" && start != "step" && start != "dive") continue;
                            // Step / dive: struck early, mid-way, late, or got clear; the others at their own timing (the run: any facing).
                            if (lead > 0.0f && start != "step" && start != "dive") continue;
                            CrashParams p;
                            p.vehicle = vehicle;
                            p.kmh = kmh;
                            p.brake = drive > 0;
                            p.brakeDelayMs = drive == 2 ? 300.0f : drive == 3 ? 800.0f : 0.0f;
                            p.facing = facing;
                            p.size = size;
                            p.start = start;
                            p.leadMs = lead;
                            p.side = side;
                            // Only outcomes the game produces at this speed (crashOutcomePlausible: alive up to 40, dead from 30 km/h).
                            for (const char* outcome : {"alive", "dead"})
                            {
                                if (!crashOutcomePlausible(outcome, kmh)) continue;
                                p.outcome = outcome;
                                all.push_back(p);
                            }
                            continue;
                            all.push_back(p);
                          }
    return all;
}

std::string number(float v)
{
    char b[32];
    std::snprintf(b, sizeof(b), "%.3f", static_cast<double>(v));
    return b;
}

std::string csvField(std::string s)
{
    std::replace(s.begin(), s.end(), '"', '\'');
    return "\"" + s + "\"";
}

std::string row(const CrashParams& p, const CrashObservations& o, const CrashAnalysis& a)
{
    std::string issues;
    for (const RuleResult& r : a.results)
    {
        if (r.verdict == Verdict::Pass) continue;
        issues += (issues.empty() ? "" : "; ") + std::string(verdictName(r.verdict)) + " " + r.id;
        if (!r.measured.empty()) issues += " " + r.measured + " (" + r.expected + ")";
    }
    const DdImpactReport& r = o.report;
    char numbers[256];
    std::snprintf(numbers, sizeof(numbers), "%.2f,%.0f,%.2f,%.2f,%.1f,%.1f,%d", r.valid ? r.throw_distance : -1.0f,
                  r.valid && r.head_strike_zone != DD_ZONE_NONE ? r.head_strike_time * 1000.0f : -1.0f,
                  r.valid ? r.wrap_around_distance : -1.0f, r.valid ? r.apex_height : -1.0f, r.valid ? r.launch_speed : -1.0f,
                  r.valid ? r.launch_angle : 0.0f, o.onVehicleAtEnd ? 1 : 0);
    char settings[160];
    std::snprintf(settings, sizeof(settings), "%s,%g,%d,%g,%g,%g,%d,%d", p.vehicle.c_str(), static_cast<double>(p.kmh), p.brake ? 1 : 0,
                  static_cast<double>(p.brakeDelayMs), static_cast<double>(p.facing), static_cast<double>(p.size), p.aware ? 1 : 0,
                  p.limp ? 1 : 0);
    char extra[32], lying[48];
    std::snprintf(extra, sizeof(extra), "%.2f", r.valid ? r.peak_head_delta_v : 0.0f);
    std::snprintf(lying, sizeof(lying), "%.2f", o.hurtSince >= 0.0 ? o.duration - o.hurtSince : -1.0);
    return csvField(crashName(p)) + "," + verdictName(a.overall) + "," + csvField(a.outcome) + "," + settings + "," + numbers + "," +
           csvField(issues) + "," + extra + "," + csvField(o.endStatus) + "," + csvField(o.hurtPart) + "," + lying + "," + p.start + "," +
           number(crashStartShare(p.start)) + "," + number(o.hurtSince >= 0.0 && o.contact >= 0.0 ? static_cast<float>(o.hurtSince - o.contact) : -1.0f) +
           "," + number(r.valid && r.ground_time > 0.0f ? r.ground_time : -1.0f);
}

const char* kHeader = "name,verdict,outcome,vehicle,kmh,brake,brake_delay_ms,facing,size,aware,limp,throw_m,head_strike_ms,"
                      "wrap_around_m,apex_m,launch_mps,launch_deg,on_vehicle_at_end,issues,peak_head_dv,end_stage,hurt_part,hurt_seconds,start,start_share,hurt_after_hit,ground_after_hit";

int runShard(const std::vector<CrashParams>& all, int shard, int shards, const fs::path& out)
{
    std::ofstream file(out, std::ios::trunc);
    // Population rules (a published share of people, e.g. Hsiao 1998's falls): per rule, runs it applied to and runs inside.
    std::map<std::string, std::array<int, 2>> population;
    std::map<std::string, double> shares;
    for (size_t i = static_cast<size_t>(shard); i < all.size(); i += static_cast<size_t>(shards))
    {
        CrashObservations o;
        CrashAnalysis a;
        std::string error;
        if (!runCrash(all[i], o, a, error))
        {
            std::fprintf(stderr, "%s: %s\n", crashName(all[i]).c_str(), error.c_str());
            continue;
        }
        file << row(all[i], o, a) << "\n";
        for (const RuleResult& r : a.results)
        {
            if (r.kind != "population") continue;
            ++population[r.id][0];
            if (r.verdict == Verdict::Pass) ++population[r.id][1];
            shares[r.id] = r.share;
        }
    }
    std::ofstream pop(fs::path(out).concat(".population"), std::ios::trunc);
    for (const auto& [id, n] : population) pop << id << " " << n[0] << " " << n[1] << " " << shares[id] << "\n";
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    int sample = 0, jobs = static_cast<int>(std::max(1u, std::thread::hardware_concurrency())), shard = -1, shards = 0;
    unsigned seed = 1;
    std::string only, shardOut, poseTrace, bodyTrace;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--sample" && more) sample = std::atoi(argv[++i]);
        else if (a == "--seed" && more) seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (a == "--jobs" && more) jobs = std::max(1, std::atoi(argv[++i]));
        else if (a == "--only" && more) only = argv[++i];
        else if (a == "--pose-trace" && more) poseTrace = argv[++i];
        else if (a == "--body-trace" && more) bodyTrace = argv[++i];
        else if (a == "--shard" && i + 3 < argc)
        {
            shard = std::atoi(argv[++i]);
            shards = std::atoi(argv[++i]);
            shardOut = argv[++i];
        }
        else
        {
            std::fprintf(stderr, "usage: dragdoll_crashlab [--sample N] [--seed S] [--jobs J] [--only crash:...]\n");
            return 2;
        }
    }
    if (dragdoll_init(DD_API_VERSION) != DD_OK)
    {
        std::fprintf(stderr, "dragdoll_init failed: %s\n", dragdoll_last_error_message());
        return 1;
    }
    std::vector<CrashParams> all = grid();
    if (sample > 0 && sample < static_cast<int>(all.size()))
    {
        std::mt19937 rng(seed);
        std::shuffle(all.begin(), all.end(), rng);
        all.resize(static_cast<size_t>(sample));
    }
    int code = 0;
    if (!bodyTrace.empty())
    {
        // Every body's world position each host frame for the whole run, plus whether the host still animates it: for
        // judging what happens around the handover (e.g. arms jumping to a pose the host never showed).
        Instance instance;
        std::string error;
        if (!Instance::make(bodyTrace, instance, error))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            code = 1;
        }
        else
        {
            DdCharacter* c = instance.scenario->character();
            std::vector<DdTransform> bodies(c != nullptr ? dragdoll_character_get_body_count(c) : 0);
            const double end = instance.scenario->duration();
            while (c != nullptr && instance.hostTime < end)
            {
                instance.hostFrame(1.0f / 60.0f);
                if (dragdoll_character_get_body_transforms(c, 1, bodies.data(), static_cast<uint32_t>(bodies.size())) != DD_OK) break;
                std::printf("t %.4f kin %d status %s bodies", instance.hostTime, dragdoll_character_get_kinematic(c),
                            dragdoll_character_get_behaviour_status(c));
                for (const DdTransform& b : bodies) std::printf(" %.4f %.4f %.4f", b.position.x, b.position.y, b.position.z);
                std::vector<DdBodyContact> contacts(bodies.size());
                dragdoll_character_get_contacts(c, contacts.data(), static_cast<uint32_t>(contacts.size()));
                std::printf(" rot"); // Every body's rotation (x y z w)
                for (const DdTransform& b : bodies) std::printf(" %.4f %.4f %.4f %.4f", b.rotation.x, b.rotation.y, b.rotation.z, b.rotation.w);
                std::printf(" ground"); // Rig indices of the bodies touching the ground
                for (size_t i = 0; i < contacts.size(); ++i)
                    if ((contacts[i].flags & DD_CONTACT_GROUND) != 0) std::printf(" %zu", i);
                std::printf("\n");
            }
        }
    }
    else if (!poseTrace.empty())
    {
        // The pedestrian's pose each host frame while the host animates it (its starting state): root move and turn
        // relative to the first frame's root (in its own frame) and the 16 joints - the private reaction recordings'
        // format, for scoring against them.
        Instance instance;
        std::string error;
        if (!Instance::make(poseTrace, instance, error))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            code = 1;
        }
        else
        {
            DdCharacter* c = instance.scenario->character();
            DdTransform root0{};
            std::vector<DdQuat> joints(c != nullptr ? dragdoll_character_get_body_count(c) : 0);
            // The recordings' 16 bodies (the rig before its clavicles), by name.
            const char* const recorded[16] = {"pelvis", "lower_spine", "upper_spine", "head", "upper_arm_l", "forearm_l",
                                              "hand_l", "upper_arm_r", "forearm_r", "hand_r", "thigh_l", "shin_l",
                                              "foot_l", "thigh_r", "shin_r", "foot_r"};
            bool first = true;
            const double end = instance.scenario->duration();
            while (instance.hostTime < end && c != nullptr && dragdoll_character_get_kinematic(c) != 0)
            {
                instance.hostFrame(1.0f / 120.0f);
                DdTransform root{};
                if (dragdoll_character_get_pose(c, 0, joints.data(), static_cast<uint32_t>(joints.size()), &root) != DD_OK) break;
                if (first)
                {
                    root0 = root;
                    first = false;
                }
                const DdQuat inv{-root0.rotation.x, -root0.rotation.y, -root0.rotation.z, root0.rotation.w};
                const DdQuat& q = root.rotation;
                const DdQuat turn{inv.w * q.x + inv.x * q.w + inv.y * q.z - inv.z * q.y, inv.w * q.y - inv.x * q.z + inv.y * q.w + inv.z * q.x,
                                  inv.w * q.z + inv.x * q.y - inv.y * q.x + inv.z * q.w, inv.w * q.w - inv.x * q.x - inv.y * q.y - inv.z * q.z};
                // The move in the start root's own frame (rig axes: x left, y up, z forward) - the recorder's format.
                const float dx = root.position.x - root0.position.x, dy = root.position.y - root0.position.y, dz = root.position.z - root0.position.z;
                const auto rotate = [](DdQuat r, float x, float y, float z, float out[3]) {
                    const float tx = 2.0f * (r.y * z - r.z * y), ty = 2.0f * (r.z * x - r.x * z), tz = 2.0f * (r.x * y - r.y * x);
                    out[0] = x + r.w * tx + (r.y * tz - r.z * ty);
                    out[1] = y + r.w * ty + (r.z * tx - r.x * tz);
                    out[2] = z + r.w * tz + (r.x * ty - r.y * tx);
                };
                float local[3];
                rotate(inv, dx, dy, dz, local);
                std::printf("t %.4f move %.4f %.4f %.4f turn %.5f %.5f %.5f %.5f joints", instance.hostTime, local[0], local[1], local[2], turn.x,
                            turn.y, turn.z, turn.w);
                for (const char* name : recorded)
                {
                    const int b = dragdoll_character_find_body(c, name);
                    const DdQuat j = b >= 0 ? joints[static_cast<size_t>(b)] : DdQuat{0, 0, 0, 1};
                    std::printf(" %.5f %.5f %.5f %.5f", j.x, j.y, j.z, j.w);
                }
                std::printf(" world %.4f %.4f %.4f rot %.5f %.5f %.5f %.5f\n", root.position.x, root.position.y, root.position.z, root.rotation.x,
                            root.rotation.y, root.rotation.z, root.rotation.w);
            }
        }
    }
    else if (!only.empty())
    {
        CrashParams p;
        std::string error;
        CrashObservations o;
        CrashAnalysis a;
        if (!parseCrashName(only, p, error) || !runCrash(p, o, a, error))
        {
            std::fprintf(stderr, "%s\n", error.c_str());
            code = 1;
        }
        else
        {
            std::printf("%s: %s (%s)\n", crashName(p).c_str(), verdictName(a.overall), a.outcome.c_str());
            const auto v = [](DdVec3 p) { char b[64]; std::snprintf(b, sizeof(b), "(%.2f %.2f %.2f)", p.x, p.y, p.z); return std::string(b); };
            std::printf("  end: stage \"%s\", run %.2f s, still since rest %d | hands L %s R %s | gesture targets L %s R %s\n",
                        o.endStatus.c_str(), o.duration, o.report.finished, v(o.hands[0]).c_str(), v(o.hands[1]).c_str(),
                        v(o.handTargets[0]).c_str(), v(o.handTargets[1]).c_str());
            for (const RuleResult& r : a.results)
                std::printf("  %-13s %-26s %s | measured %s, expected %s | %s\n", verdictName(r.verdict), r.id.c_str(), r.text.c_str(),
                            r.measured.c_str(), r.expected.c_str(), r.source.c_str());
        }
    }
    else if (shard >= 0)
    {
        code = runShard(all, shard, shards, shardOut);
    }
    else
    {
        // Worker processes (each its own library instance), then one merged table.
        const fs::path exe = fs::absolute(argv[0]);
        const fs::path dir = exe.parent_path() / "crashlab";
        fs::create_directories(dir);
        const auto start = std::chrono::steady_clock::now();
        std::printf("%zu runs on %d workers...\n", all.size(), jobs);
        std::vector<std::thread> workers;
        std::atomic<int> failures{0};
        for (int j = 0; j < jobs; ++j)
        {
            workers.emplace_back([&, j] {
                std::string cmd = "\"\"" + exe.string() + "\" --shard " + std::to_string(j) + " " + std::to_string(jobs) + " \"" +
                                  (dir / ("part_" + std::to_string(j) + ".csv")).string() + "\" --seed " + std::to_string(seed) +
                                  (sample > 0 ? " --sample " + std::to_string(sample) : "") + "\"";
                if (std::system(cmd.c_str()) != 0) ++failures;
            });
        }
        for (std::thread& t : workers) t.join();
        std::ofstream results(dir / "results.csv", std::ios::trunc);
        results << kHeader << "\n";
        std::map<std::string, int> counts;
        std::map<std::string, std::array<int, 2>> population;
        std::map<std::string, double> shares;
        for (int j = 0; j < jobs; ++j)
        {
            const fs::path part = dir / ("part_" + std::to_string(j) + ".csv");
            std::ifstream in(part);
            std::string line;
            while (std::getline(in, line))
            {
                results << line << "\n";
                const size_t a = line.find("\",") + 2, b = line.find(',', a);
                ++counts[line.substr(a, b - a)];
            }
            in.close();
            fs::remove(part);
            fs::path popPart = part;
            popPart.concat(".population");
            std::ifstream pin(popPart);
            std::string id;
            int applied = 0, inside = 0;
            double share = 0.0;
            while (pin >> id >> applied >> inside >> share)
            {
                population[id][0] += applied;
                population[id][1] += inside;
                shares[id] = share;
            }
            pin.close();
            fs::remove(popPart);
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("done in %.0f s -> %s\n", seconds, (dir / "results.csv").string().c_str());
        for (const auto& [verdict, n] : counts) std::printf("  %-13s %d\n", verdict.c_str(), n);
        if (!population.empty()) std::printf("population rules (runs inside, vs people):\n");
        for (const auto& [id, n] : population)
            std::printf("  %-24s %5.1f %% of %d runs   people %3.0f %%\n", id.c_str(), n[0] > 0 ? 100.0 * n[1] / n[0] : 0.0, n[0],
                        100.0 * shares[id]);
        if (failures > 0) code = 1;
    }
    dragdoll_shutdown();
    return code;
}
