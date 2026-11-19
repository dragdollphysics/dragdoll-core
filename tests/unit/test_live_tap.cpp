// SPDX-License-Identifier: MIT
// Live tap (bridge task 3.7): a world streams a scene + frames to a viewer on 127.0.0.1, and never blocks the host,
// even when the viewer stops reading.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <dragdoll/dragdoll.h>

#include <doctest/doctest.h>

#include <chrono>
#include <string>

namespace
{
struct Library
{
    Library() { REQUIRE(dragdoll_init(DD_API_VERSION) == DD_OK); }
    ~Library() { dragdoll_shutdown(); }
};

struct Client
{
    SOCKET s = INVALID_SOCKET;
    std::string buffer;
    explicit Client(uint16_t port)
    {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
        s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0);
        DWORD timeout = 2000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    }
    ~Client()
    {
        closesocket(s);
        WSACleanup();
    }
    // Next complete line ("" on timeout).
    std::string line()
    {
        char chunk[65536];
        while (buffer.find('\n') == std::string::npos)
        {
            const int n = recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0) return "";
            buffer.append(chunk, static_cast<size_t>(n));
        }
        const size_t end = buffer.find('\n');
        std::string l = buffer.substr(0, end);
        buffer.erase(0, end + 1);
        return l;
    }
};

DdWorld* makeWorld(uint16_t port)
{
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    desc.up = DdVec3{0, 0, 1};
    DdWorld* w = nullptr;
    REQUIRE(dragdoll_world_create(&desc, &w) == DD_OK);
    REQUIRE(dragdoll_world_start_live_tap(w, port) == DD_OK);
    DdColliderDesc ground;
    dragdoll_collider_desc_default(&ground);
    ground.shape = DD_SHAPE_BOX;
    ground.half_extents = DdVec3{20, 20, 0.5f};
    ground.transform.position = DdVec3{0, 0, -0.5f};
    DdColliderId id;
    REQUIRE(dragdoll_world_add_colliders(w, &ground, 1, &id) == DD_OK);
    DdCharacterDesc cd;
    dragdoll_character_desc_default(&cd);
    cd.rig_json = dragdoll_get_builtin_rig("humanoid18");
    cd.transform.position = DdVec3{0, 0, 1};
    DdCharacter* c = nullptr;
    REQUIRE(dragdoll_character_create(w, &cd, &c) == DD_OK);
    return w;
}
} // namespace

TEST_CASE("live tap: a viewer receives the scene, then frames with every character's pose")
{
    Library lib;
    DdWorld* w = makeWorld(47891);
    Client viewer(47891);
    dragdoll_world_step_fixed(w, 1); // Accepts the viewer and sends the scene
    const std::string scene = viewer.line();
    CHECK(scene.find("\"t\":\"scene\"") != std::string::npos);
    CHECK(scene.find("\"colliders\":[{") != std::string::npos);
    CHECK(scene.find("dragdoll-rig") != std::string::npos); // The rig travels with the character
    for (int i = 0; i < 40; ++i) dragdoll_world_step_fixed(w, 1);
    const std::string frame = viewer.line();
    CHECK(frame.find("\"t\":\"frame\"") != std::string::npos);
    CHECK(frame.find("\"joints\":[[") != std::string::npos);
    dragdoll_world_destroy(w);
}

TEST_CASE("live tap: a viewer that stops reading never blocks the host; frames are dropped instead")
{
    Library lib;
    DdWorld* w = makeWorld(47892);
    Client viewer(47892); // Connects, then never reads again
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 120 * 60; ++i) dragdoll_world_step_fixed(w, 1); // A minute of game time
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    MESSAGE("60 s of simulation with a stalled viewer took " << seconds << " s of real time");
    CHECK(seconds < 30.0); // Would hang forever if sends blocked
    dragdoll_world_destroy(w);
}

TEST_CASE("live tap: the port is freed on stop and on world destroy; a busy port is reported")
{
    Library lib;
    DdWorld* a = makeWorld(47893);
    DdWorld* b = nullptr;
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    REQUIRE(dragdoll_world_create(&desc, &b) == DD_OK);
    CHECK(dragdoll_world_start_live_tap(b, 47893) == DD_ERROR_INVALID_ARGUMENT); // Taken by a
    dragdoll_world_stop_live_tap(a);
    CHECK(dragdoll_world_start_live_tap(b, 47893) == DD_OK);
    dragdoll_world_destroy(b);
    dragdoll_world_destroy(a);
}

TEST_CASE("live tap: host messages from the viewer are queued for the host, oldest first, and cut to the buffer")
{
    Library lib;
    DdWorld* w = makeWorld(47894);
    char buffer[16];
    CHECK(dragdoll_world_poll_live_message(w, buffer, sizeof(buffer)) == 0); // Nothing yet
    Client viewer(47894);
    dragdoll_world_step_fixed(w, 1); // Accepts the viewer
    const std::string lines = "{\"t\":\"host\",\"data\":\"input 1 2 0 87\"}\n"
                              "{\"t\":\"show\",\"id\":1,\"visible\":0}\n"
                              "{\"t\":\"host\",\"data\":\"second\"}\n";
    REQUIRE(send(viewer.s, lines.data(), static_cast<int>(lines.size()), 0) == static_cast<int>(lines.size()));
    uint32_t length = 0;
    for (int i = 0; i < 200 && length == 0; ++i) // The tap reads after each step
    {
        dragdoll_world_step_fixed(w, 1);
        length = dragdoll_world_poll_live_message(w, buffer, sizeof(buffer));
        if (length == 0) Sleep(5);
    }
    CHECK(length == 14);
    CHECK(std::string(buffer) == "input 1 2 0 87");
    CHECK(dragdoll_world_poll_live_message(w, buffer, 4) == 6); // Full length, text cut to fit
    CHECK(std::string(buffer) == "sec");
    CHECK(dragdoll_world_poll_live_message(w, buffer, sizeof(buffer)) == 0);
    dragdoll_world_destroy(w);
}

TEST_CASE("live tap: after the scene, only changes are sent (delta), removals included")
{
    Library lib;
    DdWorld* w = makeWorld(47895);
    Client viewer(47895);
    dragdoll_world_step_fixed(w, 1);
    REQUIRE(viewer.line().find("\"t\":\"scene\"") != std::string::npos);
    DdColliderDesc box;
    dragdoll_collider_desc_default(&box);
    box.shape = DD_SHAPE_BOX;
    box.half_extents = DdVec3{1, 1, 1};
    box.transform.position = DdVec3{5, 0, 1};
    DdColliderId id = DD_INVALID_COLLIDER;
    REQUIRE(dragdoll_world_add_colliders(w, &box, 1, &id) == DD_OK);
    auto nextDelta = [&] {
        for (int i = 0; i < 50; ++i)
        {
            dragdoll_world_step_fixed(w, 1);
            const std::string l = viewer.line();
            if (l.find("\"t\":\"delta\"") != std::string::npos) return l;
            CHECK(l.find("\"t\":\"scene\"") == std::string::npos); // Never the whole scene again
        }
        return std::string();
    };
    const std::string added = nextDelta();
    CHECK(added.find("\"colliders_add\":[{") != std::string::npos);
    CHECK(added.find("\"half\":[1.0,1.0,1.0]") != std::string::npos);
    REQUIRE(dragdoll_world_remove_colliders(w, &id, 1) == DD_OK);
    const std::string removed = nextDelta();
    CHECK(removed.find("\"colliders_remove\":[" + std::to_string(id) + "]") != std::string::npos);
    CHECK(removed.find("\"colliders_add\":[]") != std::string::npos);
    dragdoll_world_destroy(w);
}
