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

float gRoadFriction = 0.7f;    // --road-friction
float gRoadRestitution = 0.0f; // --road-restitution (Jolt takes the larger of the two: the body's is 0)
constexpr float kBrake = 7.0f; // m/s^2, emergency braking on a dry road
float gCarFriction = 0.6f;     // Vehicle surface (Jolt combines it with the body's 0.8 as the geometric mean)
float gCarRestitution = 0.0f; // --car-restitution: spring-back of the vehicle surface (Jolt takes the larger of the two)
DdTension gTension = DD_TENSION_LIMP;
std::string gRig; // --rig file: a rig JSON instead of the built-in humanoid16
std::string gParams; // --params file: a behaviour pack JSON replacing the built-in one for --behaviour
bool gTrace = false;
bool gMomentum = false; // --momentum: centre-of-mass and rotation trace for the first 0.8 s
std::string gBehaviour; // --behaviour car_hit: the reaction chain runs (relaxed standing, core auto handover)
bool gAware = false;     // --aware: the character knows about the car (awareness 1), else 0
bool gGait = false; // --gait: walking posture (as the literature's cadavers): arms swung, elbows and knees a little bent
float gSize = 1.0f;
bool gArmsForward = false; // --arms-forward: both arms raised forward (out of a side-on car's path)
 // --size: the character's size_scale
 // Print the body every 25 ms for 0.6 s after the handover

struct Run
{
    std::string vehicle;
    float kmh = 0, heading = 0, offset = 0;
    DdImpactReport r{};
    bool failed = false, handedOver = false;
    float groundTurn = 0.0f; // deg the pelvis turned from the first ground contact to rest
    float lead = -1.0f, setback = -1.0f, bonnet = -1.0f, com = -1.0f; // Probed front; centre of mass above the ground
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

// Real-world throw distance (impact to rest) for an impact speed, Wood, Simms & Walsh 2005 (Proc. IMechE D 219,
// Table 1; 185 wrap and 21 forward-projection accidents): the 0.1 and 99.9 percentile speed-distance curves.
// Wrap: v = C (S - So)^D; forward projection: v = A sqrt(S) + B (v in m/s, S in m).
void woodBand(bool forward, float v, float& shortest, float& longest)
{
    if (forward)
    {
        shortest = std::pow((v + 1.0f) / 4.6f, 2.0f);
        longest = std::pow((v + 1.2f) / 2.5f, 2.0f);
    }
    else
    {
        shortest = 1.2f + std::pow(v / 5.3f, 1.0f / 0.46f);
        longest = 1.9f + std::pow(v / 2.3f, 1.0f / 0.51f);
    }
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
    if (!gParams.empty() && dragdoll_world_set_behaviour_params(world, gBehaviour.c_str(), gParams.c_str()) != DD_OK) std::abort();
    DdColliderDesc ground;
    dragdoll_collider_desc_default(&ground);
    ground.shape = DD_SHAPE_BOX;
    ground.half_extents = DdVec3{150, 40, 0.5f};
    ground.transform.position = DdVec3{60, 0, -0.5f};
    ground.friction = gRoadFriction;
    ground.restitution = gRoadRestitution;
    DdColliderId groundId;
    dragdoll_world_add_colliders(world, &ground, 1, &groundId);

    DdCharacterDesc cd;
    dragdoll_character_desc_default(&cd);
    cd.rig_json = gRig.empty() ? dragdoll_get_builtin_rig("humanoid16") : gRig.c_str();
    const float half = headingDegrees * 3.14159265f / 360.0f;
    cd.transform.position = DdVec3{0, offset, 0.005f};
    cd.transform.rotation = DdQuat{0, 0, std::sin(half), std::cos(half)};
    cd.size_scale = gSize;
    DdCharacter* human = nullptr;
    if (dragdoll_character_create(world, &cd, &human) != DD_OK) std::abort();
    dragdoll_character_set_kinematic(human, 1); // Standing (host-animated) until the handover
    if (!gBehaviour.empty())
    {
        if (dragdoll_character_set_behaviour(human, gBehaviour.c_str()) != DD_OK) std::abort();
        dragdoll_character_set_tension(human, gTension != DD_TENSION_LIMP ? gTension : DD_TENSION_RELAXED, 0.0f); // Standing at ease
        dragdoll_character_set_awareness(human, gAware ? 1.0f : 0.0f);
        dragdoll_character_set_auto_handover(human, 1.5f / 120.0f); // The core hands over (same lead as below)
    }
    if (gGait || gArmsForward)
    {
        // Mid-stance of a walk: right leg forward, left back; left arm forward, right back (arms swing against the
        // legs); elbows and the forward knee a little bent. Joint offsets from rest, rig axes (+X left): a rotation
        // about -X swings a hanging limb forwards.
        const uint32_t n = dragdoll_character_get_body_count(human);
        std::vector<DdQuat> joints(n);
        DdTransform root{};
        dragdoll_character_get_pose(human, 0, joints.data(), n, &root);
        const auto aboutX = [](float degrees) {
            const float h = degrees * 3.14159265f / 360.0f;
            return DdQuat{std::sin(h), 0.0f, 0.0f, std::cos(h)};
        };
        const auto set = [&](const char* body, float degrees) {
            const int i = dragdoll_character_find_body(human, body);
            if (i >= 0) joints[static_cast<size_t>(i)] = aboutX(degrees);
        };
        if (gGait)
        {
            set("upper_arm_l", -18.0f);
            set("upper_arm_r", 18.0f);
            set("forearm_l", -20.0f);
            set("forearm_r", -12.0f);
            set("thigh_r", -15.0f);
            set("thigh_l", 10.0f);
            set("shin_r", 8.0f);
        }
        if (gArmsForward)
        {
            set("upper_arm_l", -90.0f);
            set("upper_arm_r", -90.0f);
        }
        dragdoll_character_activate(human, nullptr, nullptr, &root, joints.data(), n, 0.0f);
    }

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
    car.restitution = gCarRestitution;
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
            // With a behaviour the core hands over by itself inside the next step (auto handover).
            run.lead = threat.profile.leading_edge_height;
            run.setback = threat.profile.leading_edge_setback;
            run.bonnet = threat.profile.bonnet_length;
            DdSenses senses{};
            senses.struct_size = sizeof(DdSenses);
            dragdoll_character_get_senses(human, &senses);
            run.com = senses.ground_height;
            if (gBehaviour.empty())
            {
                dragdoll_character_set_kinematic(human, 0);
                if (gTension != DD_TENSION_LIMP) dragdoll_character_set_tension(human, gTension, 0.0f);
            }
            run.handedOver = true;
        }
        if (brakeAtContact && r.valid != 0) v = std::max(0.0f, v - kBrake / 120.0f);
        if (!brakeAtContact && run.handedOver && step > 0) v = std::max(0.0f, v - (r.valid != 0 && r.separation_time >= 0.0f ? kBrake / 120.0f : 0.0f));
        x += v / 120.0f;
        const DdTransform target{{x, 0, 0}, {0, 0, 0, 1}};
        dragdoll_world_set_kinematic_targets(world, &carId, &target, 1);
        if (dragdoll_world_step_fixed(world, 1) != DD_OK) break;
        {
            static DdQuat lastPelvis{0, 0, 0, 1};
            std::vector<DdTransform> all(dragdoll_character_get_body_count(human));
            if (dragdoll_character_get_body_transforms(human, 0, all.data(), static_cast<uint32_t>(all.size())) != DD_OK) std::abort();
            const DdTransform pelvis = all[0];
            DdImpactReport now{};
            now.struct_size = sizeof(DdImpactReport);
            dragdoll_character_get_impact_report(human, &now);
            if (now.ground_time >= 0.0f)
            {
                const DdQuat &a = lastPelvis, &b = pelvis.rotation;
                const float dot = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
                run.groundTurn += 2.0f * std::acos(std::fmin(1.0f, dot)) * 57.29578f;
            }
            lastPelvis = pelvis.rotation;
        }
        static int traced = 0;
        if (!run.handedOver) traced = 0;
        static DdVec3 lastCom{0, 0, 0};
        if (gMomentum && run.handedOver && traced < 96)
        {
            // --momentum: centre of mass height and velocity (relative to the car), angular momentum about the car's
            // left axis (+Y; positive = top of the body turning toward the car's front... sign shown as measured), and
            // the bodies hit hard by the car this step.
            const uint32_t n = dragdoll_character_get_body_count(human);
            std::vector<DdTransform> b(n);
            dragdoll_character_get_body_transforms(human, 0, b.data(), n);
            std::vector<DdBodyContact> contacts(n);
            dragdoll_character_get_contacts(human, contacts.data(), n);
            DdVec3 com{0, 0, 0};
            float mass = 0.0f;
            for (uint32_t i = 0; i < n; ++i)
            {
                const float m = dragdoll_character_get_body_mass(human, i);
                com.x += m * b[i].position.x;
                com.y += m * b[i].position.y;
                com.z += m * b[i].position.z;
                mass += m;
            }
            com = DdVec3{com.x / mass, com.y / mass, com.z / mass};
            DdSenses senses{};
            senses.struct_size = sizeof(DdSenses);
            dragdoll_character_get_senses(human, &senses);
            if (traced > 0 && traced % 3 == 0)
            {
                std::string hits;
                for (uint32_t i = 0; i < n; ++i)
                {
                    if ((contacts[i].flags & DD_CONTACT_OBJECT) != 0)
                    {
                        char text[64];
                        std::snprintf(text, sizeof(text), " %s(%.1f)", dragdoll_character_get_body_name(human, i), contacts[i].delta_v);
                        hits += text;
                    }
                }
                std::printf("  t=%3.0f ms com z %.2f vz %+5.2f vx-car %+5.2f | L_left %+6.1f kg m2/s |%s\n", traced * 1000.0f / 120.0f, com.z,
                            (com.z - lastCom.z) * 120.0f, (com.x - lastCom.x) * 120.0f - v, senses.angular_momentum.y, hits.c_str());
            }
            lastCom = com;
            ++traced;
        }
        if (gTrace && run.handedOver && traced < 204 && traced++ % 8 == 0)
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
                else if ((contacts[i].flags & DD_CONTACT_GROUND) != 0) touching += std::string(" road:") + dragdoll_character_get_body_name(human, i);
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

// --kick: a floating limp body (no gravity, nothing to touch) whose pelvis alone is given 11 m/s sideways. With limp
// joints the upper body should barely follow at first; how fast the head starts moving shows how much the joints
// transmit.
void kick()
{
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    desc.up = DdVec3{0, 0, 1};
    desc.gravity = 0.0f;
    desc.worker_threads = 0;
    DdWorld* world = nullptr;
    dragdoll_world_create(&desc, &world);
    DdCharacterDesc cd;
    dragdoll_character_desc_default(&cd);
    cd.rig_json = gRig.empty() ? dragdoll_get_builtin_rig("humanoid16") : gRig.c_str();
    cd.transform.position = DdVec3{0, 0, 5};
    DdCharacter* c = nullptr;
    dragdoll_character_create(world, &cd, &c);
    dragdoll_world_step_fixed(world, 1);
    const uint32_t n = dragdoll_character_get_body_count(c);
    const float pelvisMass = dragdoll_character_get_body_mass(c, 0);
    dragdoll_character_apply_impulse(c, 0, DdVec3{pelvisMass * 11.0f, 0, 0}, nullptr);
    std::vector<DdTransform> before(n), now(n);
    dragdoll_character_get_body_transforms(c, 0, before.data(), n);
    const int head = dragdoll_character_find_body(c, "head"), upper = dragdoll_character_find_body(c, "upper_spine"), thigh = dragdoll_character_find_body(c, "thigh_l");
    std::printf("simulated mass per body (kg):");
    for (uint32_t i = 0; i < n; ++i) std::printf(" %s %.2f", dragdoll_character_get_body_name(c, i), dragdoll_character_get_body_mass(c, i));
    std::printf("\n");
    std::printf("kick: pelvis %.1f kg given 11 m/s along X; body speeds along X (m/s):\n", pelvisMass);
    for (int step = 1; step <= 24; ++step)
    {
        dragdoll_world_step_fixed(world, 1);
        dragdoll_character_get_body_transforms(c, 0, now.data(), n);
        if (step % 3 == 0)
        {
            const auto speed = [&](int i) { return (now[static_cast<size_t>(i)].position.x - before[static_cast<size_t>(i)].position.x) * 120.0f; };
            std::printf("  t=%3.0f ms  pelvis %5.2f  thigh %5.2f  upper spine %5.2f  head %5.2f\n", step * 1000.0f / 120.0f, speed(0), speed(thigh), speed(upper), speed(head));
        }
        before = now;
    }
    dragdoll_world_destroy(world);
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
        else if (a == "--momentum") gMomentum = true;
        else if (a == "--gait") gGait = true;
        else if (a == "--behaviour") gBehaviour = next();
        else if (a == "--aware") gAware = true;
        else if (a == "--kick") {}
        else if (a == "--arms-forward") gArmsForward = true;
        else if (a == "--size") gSize = std::strtof(next().c_str(), nullptr);
        else if (a == "--params")
        {
            FILE* f = nullptr;
            if (fopen_s(&f, next().c_str(), "rb") != 0 || f == nullptr) return 2;
            char chunk[4096];
            for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) gParams.append(chunk, n);
            std::fclose(f);
        }
        else if (a == "--rig")
        {
            FILE* f = nullptr;
            if (fopen_s(&f, next().c_str(), "rb") != 0 || f == nullptr) return 2;
            char chunk[4096];
            for (size_t n; (n = std::fread(chunk, 1, sizeof(chunk), f)) > 0;) gRig.append(chunk, n);
            std::fclose(f);
        }
        else if (a == "--car-friction") gCarFriction = std::strtof(next().c_str(), nullptr);
        else if (a == "--road-friction") gRoadFriction = std::strtof(next().c_str(), nullptr);
        else if (a == "--road-restitution") gRoadRestitution = std::strtof(next().c_str(), nullptr);
        else if (a == "--car-restitution") gCarRestitution = std::strtof(next().c_str(), nullptr);
        else if (a == "--tension")
        {
            const std::string t = next();
            gTension = t == "relaxed" ? DD_TENSION_RELAXED : t == "tense" ? DD_TENSION_TENSE : t == "stunned" ? DD_TENSION_STUNNED : t == "braced" ? DD_TENSION_BRACED : DD_TENSION_LIMP;
        }
        else
        {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (dragdoll_init(DD_API_VERSION) != DD_OK) return 1;
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--kick")
        {
            kick();
            dragdoll_shutdown();
            return 0;
        }
    }
    FILE* csv = nullptr;
    if (fopen_s(&csv, out.c_str(), "w") != 0 || csv == nullptr) return 1;
    std::fprintf(csv, "vehicle,kmh,heading,offset,handed_over,failed,finished,class,first_body,first_zone,first_height,head_time_ms,head_zone,"
                      "wad,head_speed_ratio,separation_s,launch_ratio,launch_angle,launch_height,apex,ground_s,ground_body,rest_s,carry,"
                      "flight_slide,throw,corridor_min,corridor_max,in_corridor,pitch,peak_head_dv,com_over_front,contact_s,lead_height,com_height,lead_setback,bonnet_length,ground_turn\n");
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
                    std::fprintf(csv, "%s,%.0f,%.0f,%.2f,%d,%d,%d,%s,%s,%s,%.3f,%.0f,%s,%.3f,%.3f,%.3f,%.3f,%.1f,%.3f,%.3f,%.3f,%s,%.3f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%.0f,%.2f,%.2f,%.3f,%.2f,%.2f,%.2f,%.2f,%.0f\n",
                                 vehicle.c_str(), kmh, heading, offset, run.handedOver ? 1 : 0, run.failed ? 1 : 0, r.finished, className(r.measured),
                                 run.firstBody.c_str(), zoneName(r.first_zone), r.first_height, r.head_strike_time >= 0 ? r.head_strike_time * 1000.0f : -1.0f,
                                 zoneName(r.head_strike_zone), r.wrap_around_distance, r.head_strike_time >= 0 ? r.head_strike_speed / v : -1.0f,
                                 r.separation_time, r.launch_speed / v, r.launch_angle, r.launch_height, r.apex_height, r.ground_time,
                                 run.groundBody.c_str(), r.rest_time, r.carry_distance, r.flight_slide_distance, r.throw_distance, r.corridor_min,
                                 r.corridor_max, r.in_corridor, r.pitch_degrees, r.peak_head_delta_v, r.com_over_front, r.contact_seconds, run.lead, run.com, run.setback, run.bonnet, run.groundTurn);
                    runs.push_back(run);
                }
    std::fclose(csv);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    // Summary per vehicle and speed.
    std::printf("%zu runs in %.1f s -> %s\n\n", runs.size(), seconds, out.c_str());
    std::printf("%-9s %4s | %-30s | %-12s | %-9s | %-10s | %-8s | %-9s | %s\n", "vehicle", "km/h", "classes (of runs)", "head ms", "WAD m",
                "head/v", "launch/v", "throw m", "in corridor (of flights) | in real-world band (Wood 2005)");
    for (const std::string& vehicle : vehicles)
        for (float kmh : speeds)
        {
            std::map<std::string, int> classes;
            std::vector<float> head, wad, ratio, launch, thrown;
            int inside = 0, flights = 0, finished = 0, failed = 0, real = 0;
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
                    inside += r.in_corridor == 1 ? 1 : 0;
                    flights += r.in_corridor >= 0 ? 1 : 0;
                    float shortest = 0.0f, longest = 0.0f;
                    woodBand(r.measured == DD_TRAJECTORY_FORWARD_PROJECTION, kmh / 3.6f, shortest, longest);
                    real += r.throw_distance >= shortest && r.throw_distance <= longest ? 1 : 0;
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
            std::printf("%-9s %4.0f | %-30s | %-12s | %-9s | %-10s | %-8.2f | %-9.1f | %2d of %-21d | %d of %d\n", vehicle.c_str(), kmh, cls.c_str(), headText, wadText,
                        ratioText, median(launch), median(thrown), inside, flights, real, finished);
        }
    if (!quiet)
    {
        std::printf("\nReference (docs/reference/pedestrian-impact.md §6): sedan, adult, 30-50 km/h -> wrap; head strike 50-140 ms;\n"
                    "WAD 1.5-2.3 m (1.6-1.75 m body); head speed 0.6-1.06 x vehicle speed; high fronts -> forward projection;\n"
                    "flight + slide inside the throw corridor for the measured launch; throw distance inside the real-world band\n"
                    "(Wood, Simms & Walsh 2005, 0.1-99.9 percentile; roof vaults fall outside its wrap model).\n");
    }
    dragdoll_shutdown();
    return 0;
}
