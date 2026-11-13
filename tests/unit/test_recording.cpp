// SPDX-License-Identifier: MIT
// Recording & replay: every scenario recorded at varying host frame rates replays bit-identically.

#include <dragdoll_scenarios/scenario.hpp>

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
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

std::string tempFile(const std::string& name)
{
    return (std::filesystem::temp_directory_path() / ("dragdoll_test_" + name + ".ddrec")).string();
}

uint64_t hashOf(DdWorld* world)
{
    uint64_t h = 0;
    REQUIRE(dragdoll_world_compute_state_hash(world, &h) == DD_OK);
    return h;
}

DdReplayFrame frameStruct()
{
    DdReplayFrame f{};
    f.struct_size = sizeof(DdReplayFrame);
    return f;
}
} // namespace

TEST_CASE("recording: every scenario, recorded at a jittery 25-200 fps host, replays bit-identically")
{
    Library lib;
    for (const std::string& name : dragdoll::scenarios::list())
    {
        CAPTURE(name);
        const std::string path = tempFile(name);
        uint64_t recordedFrames = 0;
        uint64_t finalHash = 0;
        {
            Instance instance;
            std::string error;
            REQUIRE_MESSAGE(Instance::make(name, instance, error, path.c_str()), error);
            CHECK(dragdoll_world_is_recording(instance.world) == 1);
            std::mt19937 rng(5);
            std::uniform_real_distribution<float> dt(1.0f / 200.0f, 1.0f / 25.0f);
            for (int frame = 0; frame < 240; ++frame)
            {
                instance.hostFrame(dt(rng));
                ++recordedFrames;
                if (frame == 100)
                {
                    REQUIRE(dragdoll_world_record_note(instance.world, "frame 100: looks odd here") == DD_OK);
                }
            }
            finalHash = hashOf(instance.world);
        } // world destroyed -> file closed

        DdReplay* replay = nullptr;
        REQUIRE_MESSAGE(dragdoll_replay_open(path.c_str(), &replay) == DD_OK, dragdoll_last_error_message());
        DdReplayInfo info{};
        info.struct_size = sizeof(DdReplayInfo);
        REQUIRE(dragdoll_replay_get_info(replay, &info) == DD_OK);
        CHECK(info.frame_count == recordedFrames);
        CHECK(info.same_build == 1);
        CHECK(info.recorded_api_version == DD_API_VERSION);

        DdReplayFrame frame = frameStruct();
        std::string noteSeen;
        uint64_t noteFrame = 0;
        do
        {
            REQUIRE_MESSAGE(dragdoll_replay_advance(replay, &frame) == DD_OK, dragdoll_last_error_message());
            REQUIRE(frame.hash_matches == 1);
            if (frame.note != nullptr)
            {
                noteSeen = frame.note;
                noteFrame = frame.frame;
            }
        } while (frame.at_end == 0);
        CHECK(frame.frame == recordedFrames);
        CHECK(frame.first_divergence == 0);
        CHECK(noteSeen == "frame 100: looks odd here");
        CHECK(noteFrame == 102); // The note was added after host frame 101 (0-based 100) and arrives with the next frame.
        CHECK(hashOf(dragdoll_replay_get_world(replay)) == finalHash);
        dragdoll_replay_close(replay);
        std::filesystem::remove(path);
    }
}

TEST_CASE("recording: a tampered recording is detected as divergent and the replay keeps going")
{
    Library lib;
    const std::string path = tempFile("tamper");
    {
        Instance instance;
        std::string error;
        REQUIRE(Instance::make("pile_zup", instance, error, path.c_str()));
        for (int i = 0; i < 120; ++i)
        {
            instance.hostFrame(1.0f / 60.0f);
        }
    }
    // Flip one byte of the last record's hash (the final 8 bytes of the file).
    {
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        f.seekg(-1, std::ios::end);
        char c = 0;
        f.read(&c, 1);
        c = static_cast<char>(c ^ 0x5A);
        f.seekp(-1, std::ios::end);
        f.write(&c, 1);
    }
    DdReplay* replay = nullptr;
    REQUIRE(dragdoll_replay_open(path.c_str(), &replay) == DD_OK);
    DdReplayFrame frame = frameStruct();
    do
    {
        REQUIRE(dragdoll_replay_advance(replay, &frame) == DD_OK);
    } while (frame.at_end == 0);
    CHECK(frame.hash_matches == 0);
    CHECK(frame.first_divergence == 120);
    dragdoll_replay_close(replay);
    std::filesystem::remove(path);
}

TEST_CASE("recording: bad files and arguments are rejected with a message")
{
    Library lib;
    DdReplay* replay = nullptr;
    CHECK(dragdoll_replay_open("no/such/file.ddrec", &replay) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(replay == nullptr);

    const std::string junk = tempFile("junk");
    {
        std::ofstream f(junk, std::ios::binary);
        f << "this is not a recording";
    }
    CHECK(dragdoll_replay_open(junk.c_str(), &replay) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(dragdoll_last_error_message()).find("not a .ddrec") != std::string::npos);
    std::filesystem::remove(junk);

    // Truncated recording: cut the file in the middle of a record.
    const std::string path = tempFile("truncated");
    {
        Instance instance;
        std::string error;
        REQUIRE(Instance::make("ball_drop_zup", instance, error, path.c_str()));
        for (int i = 0; i < 30; ++i)
        {
            instance.hostFrame(1.0f / 30.0f);
        }
    }
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 3);
    CHECK(dragdoll_replay_open(path.c_str(), &replay) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(std::string(dragdoll_last_error_message()).find("truncated") != std::string::npos);
    std::filesystem::remove(path);

    // A recording path that can't be created fails world creation cleanly.
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    desc.record_path = "Z:/definitely/missing/folder/x.ddrec";
    DdWorld* world = nullptr;
    CHECK(dragdoll_world_create(&desc, &world) == DD_ERROR_INVALID_ARGUMENT);
    CHECK(world == nullptr);

    // Notes on a world that isn't recording are harmless.
    dragdoll_world_desc_default(&desc);
    REQUIRE(dragdoll_world_create(&desc, &world) == DD_OK);
    CHECK(dragdoll_world_is_recording(world) == 0);
    CHECK(dragdoll_world_record_note(world, "ignored") == DD_OK);
    dragdoll_world_destroy(world);
}
