// SPDX-License-Identifier: MIT
// dragdoll_inspect — answers precise questions about a .ddrec recording (token-efficient input for analysis).
//   dragdoll_inspect <file.ddrec>                 summary + notes
//   dragdoll_inspect <file.ddrec> --motion        per-body speed around every note and over the last second
//   dragdoll_inspect <file.ddrec> --body NAME     speed of one body over the whole recording (every 0.1 s)

#include <dragdoll/dragdoll.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
struct Sample
{
    uint64_t frame;
    double time;                       // simulated seconds
    std::vector<DdTransform> bodies;   // first character only
};

float distance(const DdVec3& a, const DdVec3& b)
{
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

// Angle (degrees) between two orientations.
float angleBetween(const DdQuat& a, const DdQuat& b)
{
    const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
    return 2.0f * std::acos(std::min(1.0f, d)) * 57.29578f;
}

// Linear (m/s) and angular (deg/s) speed of every body between two samples.
void speeds(const Sample& a, const Sample& b, std::vector<float>& lin, std::vector<float>& ang)
{
    const double dt = std::max(1e-6, b.time - a.time);
    lin.assign(a.bodies.size(), 0.0f);
    ang.assign(a.bodies.size(), 0.0f);
    for (size_t i = 0; i < a.bodies.size() && i < b.bodies.size(); ++i)
    {
        lin[i] = static_cast<float>(distance(a.bodies[i].position, b.bodies[i].position) / dt);
        ang[i] = static_cast<float>(angleBetween(a.bodies[i].rotation, b.bodies[i].rotation) / dt);
    }
}

void printWindow(const std::vector<Sample>& samples, size_t from, size_t to, DdCharacter* names, const char* title)
{
    if (to <= from || to >= samples.size())
    {
        return;
    }
    std::vector<float> lin, ang;
    speeds(samples[from], samples[to], lin, ang);
    std::vector<size_t> order(lin.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t x, size_t y) { return lin[x] + ang[x] * 0.01f > lin[y] + ang[y] * 0.01f; });
    std::printf("  %s (t %.2f..%.2f s): fastest bodies\n", title, samples[from].time, samples[to].time);
    for (size_t k = 0; k < std::min<size_t>(5, order.size()); ++k)
    {
        const size_t i = order[k];
        std::printf("    %-12s %7.3f m/s  %7.1f deg/s\n", dragdoll_character_get_body_name(names, static_cast<uint32_t>(i)), lin[i], ang[i]);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: dragdoll_inspect <file.ddrec> [--motion | --body NAME]\n");
        return 2;
    }
    const bool motion = argc > 2 && std::strcmp(argv[2], "--motion") == 0;
    const char* bodyName = (argc > 3 && std::strcmp(argv[2], "--body") == 0) ? argv[3] : nullptr;

    if (dragdoll_init(DD_API_VERSION) != DD_OK)
    {
        return 2;
    }
    DdReplay* replay = nullptr;
    if (dragdoll_replay_open(argv[1], &replay) != DD_OK)
    {
        std::fprintf(stderr, "cannot open: %s\n", dragdoll_last_error_message());
        return 2;
    }
    DdReplayInfo info{};
    info.struct_size = sizeof(DdReplayInfo);
    dragdoll_replay_get_info(replay, &info);
    std::printf("recording: %s\n  frames %llu | saved by %s%s\n", argv[1], static_cast<unsigned long long>(info.frame_count),
                info.recorded_build, info.same_build ? " (this build)" : " (another build)");

    DdWorld* world = dragdoll_replay_get_world(replay);
    std::vector<Sample> samples;
    std::vector<std::pair<size_t, std::string>> notes; // sample index, text
    DdReplayFrame frame{};
    frame.struct_size = sizeof(DdReplayFrame);
    double time = 0.0;
    do
    {
        if (dragdoll_replay_advance(replay, &frame) != DD_OK)
        {
            std::fprintf(stderr, "replay error: %s\n", dragdoll_last_error_message());
            break;
        }
        time = static_cast<double>(dragdoll_world_get_step_count(world)) / 120.0;
        Sample s{frame.frame, time, {}};
        if (DdCharacter* c = dragdoll_world_get_character(world, 0))
        {
            s.bodies.resize(dragdoll_character_get_body_count(c));
            dragdoll_character_get_body_transforms(c, 0, s.bodies.data(), static_cast<uint32_t>(s.bodies.size()));
        }
        samples.push_back(std::move(s));
        if (frame.note != nullptr)
        {
            notes.emplace_back(samples.size() - 1, frame.note);
        }
    } while (frame.at_end == 0);

    std::printf("  simulated %.2f s | %s\n", time,
                frame.first_divergence == 0 ? "identical to the original at every frame" : "DIVERGED");
    for (const auto& [index, text] : notes)
    {
        std::printf("note at frame %llu (t %.2f s): %s\n", static_cast<unsigned long long>(samples[index].frame), samples[index].time, text.c_str());
    }

    DdCharacter* character = dragdoll_world_get_character(world, 0);
    if (character == nullptr || samples.size() < 3)
    {
        dragdoll_replay_close(replay);
        dragdoll_shutdown();
        return 0;
    }

    auto indexAt = [&](double t) {
        size_t i = 0;
        while (i + 1 < samples.size() && samples[i].time < t) ++i;
        return i;
    };

    if (motion)
    {
        for (const auto& [index, text] : notes)
        {
            const double t = samples[index].time;
            printWindow(samples, indexAt(t - 0.25), indexAt(t + 0.25) == indexAt(t - 0.25) ? index : indexAt(t + 0.25), character,
                        ("around note \"" + text + "\"").c_str());
        }
        printWindow(samples, indexAt(time - 1.0), samples.size() - 1, character, "last second");
    }
    if (bodyName != nullptr)
    {
        const int32_t body = dragdoll_character_find_body(character, bodyName);
        if (body < 0)
        {
            std::fprintf(stderr, "no body named %s\n", bodyName);
        }
        else
        {
            std::printf("body %s (speeds over 0.1 s windows):\n", bodyName);
            size_t prev = 0;
            for (double t = 0.1; t <= time + 1e-6; t += 0.1)
            {
                const size_t i = indexAt(t);
                if (i == prev) continue;
                std::vector<float> lin, ang;
                speeds(samples[prev], samples[i], lin, ang);
                const DdVec3& p = samples[i].bodies[static_cast<size_t>(body)].position;
                std::printf("  t %.1f s  pos (%.3f, %.3f, %.3f)  %.3f m/s  %.1f deg/s\n", samples[i].time, p.x, p.y, p.z,
                            lin[static_cast<size_t>(body)], ang[static_cast<size_t>(body)]);
                prev = i;
            }
        }
    }
    dragdoll_replay_close(replay);
    dragdoll_shutdown();
    return 0;
}
