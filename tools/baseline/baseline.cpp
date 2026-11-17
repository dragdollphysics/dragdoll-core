// SPDX-License-Identifier: MIT
// dragdoll_baseline (task 4.3, ADR-0024 §6): the passive baseline. A limp pedestrian - standing, host-animated until
// the vehicle is one step away, then handed over (as cadavers are held and released in the literature's tests) - is
// hit by each vehicle pack at a range of speeds, headings and lateral offsets; the impact report of every run goes to
// a CSV, and a summary per vehicle and speed is printed against the reference numbers
// (docs/reference/pedestrian-impact.md §6).
//
//   dragdoll_baseline [--vehicles sedan,suv] [--speeds 20,40] [--headings 0,90,180,270] [--offsets 0,-0.3,0.3]
//                     [--brake contact|none] [--out baseline.csv] [--quiet]
#include <dragdoll/dragdoll.h>
#include <dragdoll_scenarios/vehicles.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using namespace dragdoll::scenarios;

constexpr float kRoadFriction = 0.7f;
constexpr float kBrake = 7.0f; // m/s^2, emergency braking on a dry road
float gCarFriction = 0.6f;     // Vehicle surface (Jolt combines it with the body's 0.8 as the geometric mean)
DdTension gTension = DD_TENSION_LIMP;
std::string gRig; // --rig file: a rig JSON instead of the built-in humanoid16
bool gTrace = false; // Print the body every 25 ms for 0.6 s after the handover

struct Run
{
    std::string vehicle;
    float kmh = 0, heading = 0, offset = 0;
    DdImpactReport r{};
    bool failed = false, handedOver = false;
    std::string firstBody, groundBody;
};

std::vector<std::string> split(const std::string& text)
{
    std::vector<std::string> out;
    std::stringstream in(text);
    for (std::string item; std::getline(in, item, ',');)
        if (!item.empty()) out.push_back(item);
    return out;
}

const char* zoneName(DdZone z)
{
    static const char* const kNames[] = {"none", "front", "leading_edge", "bonnet", "windscreen", "roof"};
    return kNames[std::clamp(static_cast<int>(z), 0, 5)];
}
const char* className(DdTrajectory t)
{
    static const char* const kNames[] = {"none", "wrap", "forward", "fender_vault", "roof_vault", "somersault"};
    return kNames[std::clamp(static_cast<int>(t), 0, 5)];
}

Run simulate(const std::string& vehicle, float kmh, float headingDegrees, float offset, bool brakeAtContact)
{
    Run run;
    run.vehicle = vehicle;
    run.kmh = kmh;
    run.heading = headingDegrees;
    run.offset = offset;
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    desc.up = DdVec3{0, 0, 1};
    desc.worker_threads = 0;
    DdWorld* world = nullptr;
    if (dragdoll_world_create(&desc, &world) != DD_OK) std::abort();
    DdColliderDesc ground;
    dragdoll_collider_desc_default(&ground);
    ground.shape = DD_SHAPE_BOX;
    ground.half_extents = DdVec3{150, 40, 0.5f};
    ground.transform.position = DdVec3{60, 0, -0.5f};
    ground.friction = kRoadFriction;
    DdColliderId groundId;
    dragdoll_world_add_colliders(world, &ground, 1, &groundId);

    DdCharacterDesc cd;
    dragdoll_character_desc_default(&cd);
    cd.rig_json = gRig.empty() ? dragdoll_get_builtin_rig("humanoid16") : gRig.c_str();
    const float half = headingDegrees * 3.14159265f / 360.0f;
    cd.transform.position = DdVec3{0, offset, 0.005f};
    cd.transform.rotation = DdQuat{0, 0, std::sin(half), std::cos(half)};
    DdCharacter* human = nullptr;
    if (dragdoll_character_create(world, &cd, &human) != DD_OK) std::abort();
    dragdoll_character_set_kinematic(human, 1); // Standing (host-animated) until the handover

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
    float x = -0.35f - profile.length * 0.5f - speed * 1.0f; // About 1 s to contact: time to sense and probe it
    car.transform.position = DdVec3{x, 0, 0};
    car.group = 1;
    car.friction = gCarFriction;
    DdColliderId carId = DD_INVALID_COLLIDER;
    dragdoll_world_add_colliders(world, &car, 1, &carId);

    float v = speed;
    DdImpactReport r{};
    r.struct_size = sizeof(DdImpactReport);
    for (int step = 0; step < 120 * 15; ++step)
    {
        dragdoll_character_get_impact_report(human, &r);
        if (r.finished != 0) break;
        DdThreat threat{};
        threat.struct_size = sizeof(DdThreat);
        dragdoll_character_get_threat(human, &threat);
        // Handover one step before the front reaches the body (the host would do it on its own hit detection).
        if (!run.handedOver && threat.collider == carId && threat.time_to_collision <= 1.5f / 120.0f)
        {
            dragdoll_character_set_kinematic(human, 0);
            if (gTension != DD_TENSION_LIMP) dragdoll_character_set_tension(human, gTension, 0.0f);
            run.handedOver = true;
        }
        if (brakeAtContact && r.valid != 0) v = std::max(0.0f, v - kBrake / 120.0f);
        if (!brakeAtContact && run.handedOver && step > 0) v = std::max(0.0f, v - (r.valid != 0 && r.separation_time >= 0.0f ? kBrake / 120.0f : 0.0f));
        x += v / 120.0f;
        const DdTransform target{{x, 0, 0}, {0, 0, 0, 1}};
        dragdoll_world_set_kinematic_targets(world, &carId, &target, 1);
        if (dragdoll_world_step_fixed(world, 1) != DD_OK) break;
        static int traced = 0;
        if (!run.handedOver) traced = 0;
        if (gTrace && run.handedOver && traced < 72 && traced++ % 3 == 0)
        {
            const uint32_t n = dragdoll_character_get_body_count(human);
            std::vector<DdTransform> b(n);
            dragdoll_character_get_body_transforms(human, 0, b.data(), n);
            std::vector<DdBodyContact> contacts(n);
            dragdoll_character_get_contacts(human, contacts.data(), n);
            const auto at = [&](const char* name) { return b[static_cast<size_t>(dragdoll_character_find_body(human, name))].position; };
            const float front = x + profile.length * 0.5f;
            std::string touching;
            for (uint32_t i = 0; i < n; ++i)
                if ((contacts[i].flags & DD_CONTACT_OBJECT) != 0) touching += std::string(" ") + dragdoll_character_get_body_name(human, i);
            const DdVec3 pelvis = at("pelvis"), head = at("head"), shin = at("shin_l"), thigh = at("thigh_l");
            const DdVec3 lower = at("lower_spine"), upper = at("upper_spine");
            const auto angle = [](DdVec3 p, DdVec3 q) {
                const float d = (p.x * q.x + p.y * q.y + p.z * q.z) / std::sqrt((p.x * p.x + p.y * p.y + p.z * p.z) * (q.x * q.x + q.y * q.y + q.z * q.z));
                return std::acos(std::clamp(d, -1.0f, 1.0f)) * 57.2958f;
            };
            const DdVec3 base{lower.x - pelvis.x, lower.y - pelvis.y, lower.z - pelvis.z}, top{head.x - upper.x, head.y - upper.y, head.z - upper.z};
            const DdVec3 trunk{head.x - pelvis.x, head.y - pelvis.y, head.z - pelvis.z};
            std::printf("    trunk tilt from vertical %3.0f deg, spine bend (pelvis->lower vs upper->head) %3.0f deg\n", angle(trunk, DdVec3{0, 0, 1}), angle(base, top));
            std::fflush(stdout);
            std::printf("  t=%3.0f ms car %4.1f m/s | pelvis z %.2f x-front %+.2f | head z %.2f x-front %+.2f | thigh z %.2f shin z %.2f x-front %+.2f |%s\n",
                        (traced - 1) * 1000.0f / 120.0f, v, pelvis.z, pelvis.x - front, head.z, head.x - front, thigh.z, shin.z, shin.x - front, touching.c_str());
        }
        DdCharacterInfo info{};
        info.struct_size = sizeof(DdCharacterInfo);
        dragdoll_character_get_info(human, &info);
        if (info.state == DD_STATE_FAILED)
        {
            run.failed = true;
            break;
        }
    }
    run.r = r;
    const auto name = [&](int32_t body) { return body >= 0 ? std::string(dragdoll_character_get_body_name(human, static_cast<uint32_t>(body))) : std::string("-"); };
    run.firstBody = name(r.first_body);
    run.groundBody = name(r.ground_body);
    dragdoll_world_destroy(world);
    return run;
}

float median(std::vector<float> v)
{
    if (v.empty()) return -1.0f;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
} // namespace

int main(int argc, char** argv)
{
    std::vector<std::string> vehicles = vehicleNames();
    std::vector<float> speeds = {10, 20, 30, 40, 50, 60, 80};
    std::vector<float> headings = {0, 90, 180, 270};
    std::vector<float> offsets = {0.0f};
    std::string out = "baseline.csv";
    bool brakeAtContact = true, quiet = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        const auto numbers = [&](std::vector<float>& into) {
            into.clear();
            for (const std::string& s : split(next())) into.push_back(std::strtof(s.c_str(), nullptr));
        };
        if (a == "--vehicles") vehicles = split(next());
        else if (a == "--speeds") numbers(speeds);
        else if (a == "--headings") numbers(headings);
        else if (a == "--offsets") numbers(offsets);
        else if (a == "--brake") brakeAtContact = next() != "none";
        else if (a == "--out") out = next();
        else if (a == "--quiet") quiet = true;
        else if (a == "--trace") gTrace = true;
        else if (a == "--rig")
        {
            FILE* f = nullptr;
            if (fopen_s(&f, next().c_str(), "rb") != 0 || f == nullptr) return 2;
            char chunk[4096];
            for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) gRig.append(chunk, n);
            std::fclose(f);
        }
        else if (a == "--car-friction") gCarFriction = std::strtof(next().c_str(), nullptr);
        else if (a == "--tension")
        {
            const std::string t = next();
            gTension = t == "relaxed" ? DD_TENSION_RELAXED : t == "tense" ? DD_TENSION_TENSE : t == "stunned" ? DD_TENSION_STUNNED : DD_TENSION_LIMP;
        }
        else
        {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (dragdoll_init(DD_API_VERSION) != DD_OK) return 1;
    FILE* csv = nullptr;
    if (fopen_s(&csv, out.c_str(), "w") != 0 || csv == nullptr) return 1;
    std::fprintf(csv, "vehicle,kmh,heading,offset,handed_over,failed,finished,class,first_body,first_zone,first_height,head_time_ms,head_zone,"
                      "wad,head_speed_ratio,separation_s,launch_ratio,launch_angle,launch_height,apex,ground_s,ground_body,rest_s,carry,"
                      "flight_slide,throw,corridor_min,corridor_max,in_corridor,pitch,peak_head_dv\n");
    const auto start = std::chrono::steady_clock::now();
    std::vector<Run> runs;
    for (const std::string& vehicle : vehicles)
        for (float kmh : speeds)
            for (float heading : headings)
                for (float offset : offsets)
                {
                    const Run run = simulate(vehicle, kmh, heading, offset, brakeAtContact);
                    const DdImpactReport& r = run.r;
                    const float v = kmh / 3.6f;
                    std::fprintf(csv, "%s,%.0f,%.0f,%.2f,%d,%d,%d,%s,%s,%s,%.3f,%.0f,%s,%.3f,%.3f,%.3f,%.3f,%.1f,%.3f,%.3f,%.3f,%s,%.3f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%.0f,%.2f\n",
                                 vehicle.c_str(), kmh, heading, offset, run.handedOver ? 1 : 0, run.failed ? 1 : 0, r.finished, className(r.measured),
                                 run.firstBody.c_str(), zoneName(r.first_zone), r.first_height, r.head_strike_time >= 0 ? r.head_strike_time * 1000.0f : -1.0f,
                                 zoneName(r.head_strike_zone), r.wrap_around_distance, r.head_strike_time >= 0 ? r.head_strike_speed / v : -1.0f,
                                 r.separation_time, r.launch_speed / v, r.launch_angle, r.launch_height, r.apex_height, r.ground_time,
                                 run.groundBody.c_str(), r.rest_time, r.carry_distance, r.flight_slide_distance, r.throw_distance, r.corridor_min,
                                 r.corridor_max, r.in_corridor, r.pitch_degrees, r.peak_head_delta_v);
                    runs.push_back(run);
                }
    std::fclose(csv);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    // Summary per vehicle and speed.
    std::printf("%zu runs in %.1f s -> %s\n\n", runs.size(), seconds, out.c_str());
    std::printf("%-9s %4s | %-30s | %-12s | %-9s | %-10s | %-8s | %-9s | %s\n", "vehicle", "km/h", "classes (of runs)", "head ms", "WAD m",
                "head/v", "launch/v", "throw m", "flight+slide in corridor");
    for (const std::string& vehicle : vehicles)
        for (float kmh : speeds)
        {
            std::map<std::string, int> classes;
            std::vector<float> head, wad, ratio, launch, thrown;
            int inside = 0, finished = 0, failed = 0;
            for (const Run& run : runs)
            {
                if (run.vehicle != vehicle || run.kmh != kmh) continue;
                const DdImpactReport& r = run.r;
                failed += run.failed ? 1 : 0;
                if (r.valid == 0) continue;
                ++classes[className(r.measured)];
                if (r.head_strike_time >= 0)
                {
                    head.push_back(r.head_strike_time * 1000.0f);
                    wad.push_back(r.wrap_around_distance);
                    ratio.push_back(r.head_strike_speed / (kmh / 3.6f));
                }
                launch.push_back(r.launch_speed / (kmh / 3.6f));
                if (r.finished)
                {
                    ++finished;
                    thrown.push_back(r.throw_distance);
                    inside += r.in_corridor;
                }
            }
            std::string cls;
            for (const auto& [name, n] : classes) cls += name + " " + std::to_string(n) + "  ";
            if (failed) cls += "FAILED " + std::to_string(failed);
            char headText[16] = "-", wadText[16] = "-", ratioText[16] = "-";
            if (!head.empty())
            {
                std::snprintf(headText, sizeof(headText), "%.0f (%zu)", median(head), head.size());
                std::snprintf(wadText, sizeof(wadText), "%.2f", median(wad));
                std::snprintf(ratioText, sizeof(ratioText), "%.2f", median(ratio));
            }
            std::printf("%-9s %4.0f | %-30s | %-12s | %-9s | %-10s | %-8.2f | %-9.1f | %d of %d\n", vehicle.c_str(), kmh, cls.c_str(), headText, wadText,
                        ratioText, median(launch), median(thrown), inside, finished);
        }
    if (!quiet)
    {
        std::printf("\nReference (docs/reference/pedestrian-impact.md §6): sedan, adult, 30-50 km/h -> wrap; head strike 50-140 ms;\n"
                    "WAD 1.5-2.3 m (1.6-1.75 m body); head speed 0.6-1.06 x vehicle speed; high fronts -> forward projection;\n"
                    "flight + slide inside the throw corridor for the measured launch.\n");
    }
    dragdoll_shutdown();
    return 0;
}
