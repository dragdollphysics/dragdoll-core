// SPDX-License-Identifier: MIT
// dragdoll_bench — CPU cost of active ragdolls per fixed step, through the public C ABI.
//   dragdoll_bench [--characters N] [--threads T] [--seconds S] [--budget-us X]
// N ragdolls are dropped (staggered) onto a ground slab and kept busy with periodic shoves, so every body stays
// awake: this measures the worst case, not a settled pile. Prints one summary line; with --budget-us it returns 1
// when the mean cost per character per step exceeds the budget (ADR-0015: perf budgets are tracked per architecture).

#include <dragdoll/dragdoll.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char** argv)
{
    int characters = 8;
    int threads = 0;
    double seconds = 4.0;
    double budgetUs = 0.0;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--characters") == 0 && i + 1 < argc) characters = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) threads = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) seconds = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--budget-us") == 0 && i + 1 < argc) budgetUs = std::atof(argv[++i]);
    }
    characters = std::max(characters, 1);

    if (dragdoll_init(DD_API_VERSION) != DD_OK)
    {
        std::fprintf(stderr, "dragdoll_init failed: %s\n", dragdoll_last_error_message());
        return 2;
    }
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    desc.up = DdVec3{0, 0, 1};
    desc.worker_threads = static_cast<uint32_t>(threads);
    DdWorld* world = nullptr;
    if (dragdoll_world_create(&desc, &world) != DD_OK)
    {
        std::fprintf(stderr, "world_create failed: %s\n", dragdoll_last_error_message());
        return 2;
    }

    DdColliderDesc ground;
    dragdoll_collider_desc_default(&ground);
    ground.shape = DD_SHAPE_BOX;
    ground.half_extents = DdVec3{100.0f, 100.0f, 1.0f};
    ground.transform.position = DdVec3{0, 0, -1.0f};
    DdColliderId groundId = 0;
    dragdoll_world_add_colliders(world, &ground, 1, &groundId);

    std::vector<DdCharacter*> dolls;
    const int columns = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(characters)))));
    for (int i = 0; i < characters; ++i)
    {
        DdCharacterDesc c;
        dragdoll_character_desc_default(&c);
        c.rig_json = dragdoll_get_builtin_rig("humanoid16");
        // Close enough that neighbours collide with each other as they fall.
        c.transform.position = DdVec3{static_cast<float>(i % columns) * 0.9f, static_cast<float>(i / columns) * 0.9f, 0.6f + 0.15f * static_cast<float>(i % 5)};
        DdCharacter* handle = nullptr;
        if (dragdoll_character_create(world, &c, &handle) != DD_OK)
        {
            std::fprintf(stderr, "character_create failed: %s\n", dragdoll_last_error_message());
            return 2;
        }
        dolls.push_back(handle);
    }

    dragdoll_world_step_fixed(world, 30); // warm up caches and the first contacts
    dragdoll_reset_peak_memory();

    const int steps = static_cast<int>(seconds * 120.0);
    std::vector<double> micros;
    micros.reserve(static_cast<size_t>(steps));
    for (int s = 0; s < steps; ++s)
    {
        if (s % 60 == 0) // Keep everyone awake: a gentle shove on the chest twice a second.
        {
            for (size_t i = 0; i < dolls.size(); ++i)
            {
                const float dir = (static_cast<int>(i) + s / 60) % 2 == 0 ? 1.0f : -1.0f;
                dragdoll_character_apply_impulse(dolls[i], static_cast<uint32_t>(dragdoll_character_find_body(dolls[i], "upper_spine")),
                                                 DdVec3{25.0f * dir, 10.0f, 5.0f}, nullptr);
            }
        }
        const auto start = std::chrono::steady_clock::now();
        dragdoll_world_step_fixed(world, 1);
        const auto stop = std::chrono::steady_clock::now();
        micros.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
    }

    std::vector<double> sorted = micros;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (double m : micros) sum += m;
    const double mean = sum / static_cast<double>(micros.size());
    const double p95 = sorted[static_cast<size_t>(static_cast<double>(sorted.size()) * 0.95)];
    const double worst = sorted.back();
    const double perChar = mean / static_cast<double>(characters);

    DdMemoryStats mem{};
    mem.struct_size = sizeof(DdMemoryStats);
    dragdoll_get_memory_stats(&mem);

    std::printf("%s | characters=%d threads=%d steps=%d | step mean %.0f us, p95 %.0f us, worst %.0f us | per character %.1f us"
                " | heap peak %.1f MB\n",
                dragdoll_get_build_info(), characters, threads, steps, mean, p95, worst, perChar,
                static_cast<double>(mem.peak_bytes) / (1024.0 * 1024.0));

    dragdoll_world_destroy(world);
    dragdoll_shutdown();
    if (budgetUs > 0.0 && perChar > budgetUs)
    {
        std::printf("BUDGET EXCEEDED: %.1f us per character per step > %.1f us\n", perChar, budgetUs);
        return 1;
    }
    return 0;
}
