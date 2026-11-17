// SPDX-License-Identifier: MIT
#include "live_tap.hpp"

#include "capi_world_handle.hpp"
#include "character.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdexcept>
#include <vector>

namespace dragdoll
{
namespace
{
using Json = nlohmann::json;

SOCKET sock(uintptr_t s)
{
    return static_cast<SOCKET>(s);
}
constexpr uintptr_t kNone = ~uintptr_t{0};

Json vec(const DdVec3& v)
{
    return Json::array({v.x, v.y, v.z});
}
Json quat(const DdQuat& q)
{
    return Json::array({q.x, q.y, q.z, q.w});
}
} // namespace

LiveTap::LiveTap(uint16_t port, const DdWorldDesc& desc)
{
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        throw std::invalid_argument("live tap: WSAStartup failed");
    }
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // This PC only
    BOOL exclusive = TRUE;
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    u_long nonBlocking = 1;
    if (listener == INVALID_SOCKET || bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener, 1) != 0 || ioctlsocket(listener, FIONBIO, &nonBlocking) != 0)
    {
        if (listener != INVALID_SOCKET) closesocket(listener);
        WSACleanup();
        throw std::invalid_argument("live tap: port " + std::to_string(port) + " is not available");
    }
    mListener = static_cast<uintptr_t>(listener);
    mWorldId = GetTickCount64() * 1000 + static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this) & 0x3FF);
    mWorld = {{"up", vec(desc.up)}, {"step_hz", desc.step_hz}, {"id", mWorldId}};
}

LiveTap::~LiveTap()
{
    closeClient();
    if (mListener != kNone) closesocket(sock(mListener));
    WSACleanup();
}

void LiveTap::closeClient()
{
    if (mClient != kNone) closesocket(sock(mClient));
    mClient = kNone;
    mPending.clear();
}

void LiveTap::collidersAdded(const DdColliderDesc* descs, uint32_t count, const DdColliderId* ids)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        const DdColliderDesc& d = descs[i];
        Json c = {{"id", ids[i]}, {"shape", static_cast<int>(d.shape)}, {"motion", static_cast<int>(d.motion)},
                  {"p", vec(d.transform.position)}, {"q", quat(d.transform.rotation)}, {"radius", d.radius},
                  {"half_height", d.half_height}, {"half", vec(d.half_extents)}, {"group", d.group}};
        if (d.shape == DD_SHAPE_MESH && d.vertices != nullptr)
        {
            std::vector<float> v;
            v.reserve(d.vertex_count * 3);
            for (uint32_t k = 0; k < d.vertex_count; ++k)
            {
                v.insert(v.end(), {d.vertices[k].x, d.vertices[k].y, d.vertices[k].z});
            }
            c["vertices"] = v;
            if (d.indices != nullptr) c["indices"] = std::vector<uint32_t>(d.indices, d.indices + d.index_count);
        }
        mColliders[ids[i]] = std::move(c);
        if (d.motion != DD_MOTION_STATIC) mMoving[ids[i]] = true;
        mColliderAdds.insert(ids[i]);
    }
}

void LiveTap::collidersRemoved(const DdColliderId* ids, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        mColliders.erase(ids[i]);
        mMoving.erase(ids[i]);
        mColliderAdds.erase(ids[i]);
        mColliderRemoves.insert(ids[i]); // Harmless if the viewer never had it
    }
}

void LiveTap::characterCreated(uint32_t id, const DdCharacterDesc& desc)
{
    mCharacters[id] = {{"id", id}, {"name", ""}, {"rig", desc.rig_json != nullptr ? desc.rig_json : ""}, {"p", vec(desc.transform.position)},
                       {"q", quat(desc.transform.rotation)}, {"mass_scale", desc.mass_scale}};
    mCharacterAdds.insert(id);
}

void LiveTap::characterRenamed(uint32_t id, const std::string& name)
{
    auto it = mCharacters.find(id);
    if (it != mCharacters.end())
    {
        it->second["name"] = name;
        mCharacterAdds.insert(id); // Sent again: the viewer updates what it knows
    }
}

void LiveTap::readCommands(DdWorld* world)
{
    char chunk[4096];
    for (;;)
    {
        const int n = recv(sock(mClient), chunk, sizeof(chunk), 0);
        if (n > 0)
        {
            mIncoming.append(chunk, static_cast<size_t>(n));
            if (mIncoming.size() > 65536) mIncoming.clear(); // Not a well-behaved viewer
            continue;
        }
        if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) closeClient();
        break;
    }
    size_t end;
    while (mClient != kNone && (end = mIncoming.find('\n')) != std::string::npos)
    {
        const Json m = Json::parse(mIncoming.substr(0, end), nullptr, false);
        mIncoming.erase(0, end + 1);
        if (m.is_discarded()) continue;
        const std::string type = m.value("t", "");
        if (type == "host")
        {
            mHostMessages.push_back(m.value("data", ""));
            if (mHostMessages.size() > 64) mHostMessages.pop_front(); // The host stopped polling: keep the newest
            continue;
        }
        const uint32_t id = m.value("id", 0u);
        for (uint32_t i = 0; i < world->world.characterCount(); ++i)
        {
            DdCharacter* c = world->world.characterAt(i);
            if (c->id != id) continue;
            if (type == "show") c->debugVisible = m.value("visible", 1) != 0;
            // Simulation changes go through the C API: recorded and validated like any host call.
            if (type == "tension") dragdoll_character_set_tension(c, static_cast<DdTension>(m.value("tension", 0)), m.value("blend", 0.3f));
            if (type == "balance") dragdoll_character_set_balance(c, m.value("on", 0));
        }
    }
}

bool LiveTap::popHostMessage(std::string& out)
{
    if (mHostMessages.empty()) return false;
    out = std::move(mHostMessages.front());
    mHostMessages.pop_front();
    return true;
}

void LiveTap::characterDestroyed(uint32_t id)
{
    mCharacters.erase(id);
    mCharacterAdds.erase(id);
    mCharacterRemoves.insert(id); // Even if only a rename was pending: the viewer has it
}

bool LiveTap::flush()
{
    while (!mPending.empty())
    {
        const int n = send(sock(mClient), mPending.data(), static_cast<int>(mPending.size()), 0);
        if (n == SOCKET_ERROR)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK) closeClient(); // Viewer gone: wait for the next one
            return false;
        }
        mPending.erase(0, static_cast<size_t>(n));
    }
    return true;
}

void LiveTap::afterStep(DdWorld* world)
{
    if (mClient == kNone)
    {
        const SOCKET client = accept(sock(mListener), nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        u_long nonBlocking = 1;
        ioctlsocket(client, FIONBIO, &nonBlocking);
        mClient = static_cast<uintptr_t>(client);
        mSceneDirty = true; // A new viewer needs the whole scene first
        mIncoming.clear();
    }
    readCommands(world);
    if (mClient == kNone) return;
    if (!flush()) return; // Still sending (or the viewer left): drop this frame
    if (mClient == kNone) return;

    if (mSceneDirty)
    {
        Json colliders = Json::array(), characters = Json::array();
        for (const auto& [id, c] : mColliders) colliders.push_back(c);
        for (const auto& [id, c] : mCharacters) characters.push_back(c);
        mPending = Json{{"t", "scene"}, {"world", mWorld}, {"colliders", colliders}, {"characters", characters}}.dump() + "\n";
        mSceneDirty = false;
        mColliderAdds.clear();
        mColliderRemoves.clear();
        mCharacterAdds.clear();
        mCharacterRemoves.clear();
        flush();
        return;
    }
    if (!mColliderAdds.empty() || !mColliderRemoves.empty() || !mCharacterAdds.empty() || !mCharacterRemoves.empty())
    {
        // Only what changed: streaming level geometry in and out never resends what the viewer already has.
        Json colliders = Json::array(), characters = Json::array();
        for (DdColliderId id : mColliderAdds) colliders.push_back(mColliders.at(id));
        for (uint32_t id : mCharacterAdds) characters.push_back(mCharacters.at(id));
        mPending = Json{{"t", "delta"}, {"world", mWorld["id"]}, {"colliders_add", colliders},
                        {"colliders_remove", mColliderRemoves}, {"characters_add", characters},
                        {"characters_remove", mCharacterRemoves}}.dump() + "\n";
        mColliderAdds.clear();
        mColliderRemoves.clear();
        mCharacterAdds.clear();
        mCharacterRemoves.clear();
        flush();
        return;
    }

    const uint64_t step = dragdoll_world_get_step_count(world);
    // After every host frame that advanced, capped at 60 Hz (2 steps): a 30 fps game sends every frame, so poses do
    // not skip frames when the game's frame time wobbles.
    if (step < mLastFrameStep + 2 && step >= mLastFrameStep) return;
    mLastFrameStep = step;
    Json colliders = Json::array(), characters = Json::array();
    for (const auto& [id, moving] : mMoving)
    {
        DdTransform t{};
        if (dragdoll_world_get_collider_transform(world, id, 0, &t) == DD_OK)
            colliders.push_back({{"id", id}, {"p", vec(t.position)}, {"q", quat(t.rotation)}});
    }
    for (uint32_t i = 0; i < dragdoll_world_get_character_count(world); ++i)
    {
        DdCharacter* c = dragdoll_world_get_character(world, i);
        const uint32_t count = dragdoll_character_get_body_count(c);
        std::vector<DdQuat> joints(count);
        DdTransform root{};
        dragdoll_character_get_pose(c, 0, joints.data(), count, &root);
        DdCharacterInfo info{};
        info.struct_size = sizeof(DdCharacterInfo);
        dragdoll_character_get_info(c, &info);
        Json j = Json::array();
        for (const DdQuat& q : joints) j.push_back(quat(q));
        characters.push_back({{"index", i}, {"id", c->id}, {"visible", c->debugVisible ? 1 : 0}, {"p", vec(root.position)}, {"q", quat(root.rotation)}, {"joints", j},
                              {"state", static_cast<int>(info.state)}, {"tension", static_cast<int>(dragdoll_character_get_tension(c))},
                              {"balance", dragdoll_character_get_balance(c)}, {"balance_error", info.balance_error},
                              // What it senses and makes of it (ADR-0024 / 0025); ttc < 0 = no threat.
                              {"ttc", c->threat.collider != DD_INVALID_COLLIDER ? c->threat.time_to_collision : -1.0f},
                              {"expects", static_cast<int>(c->threat.predicted)}, {"behaviour", c->behaviourName},
                              {"riding", c->ridingGroup},
                              // Impact report (ADR-0024 §5): what a hit did, while it unfolds and once it is over.
                              {"impact", c->impact.valid == 0 ? Json(nullptr)
                                                              : Json{{"class", static_cast<int>(c->impact.measured)}, {"finished", c->impact.finished},
                                                                     {"first_body", c->impact.first_body}, {"first_zone", static_cast<int>(c->impact.first_zone)},
                                                                     {"head_time", c->impact.head_strike_time}, {"head_zone", static_cast<int>(c->impact.head_strike_zone)},
                                                                     {"wad", c->impact.wrap_around_distance}, {"launch", c->impact.launch_speed},
                                                                     {"throw", c->impact.throw_distance}, {"carry", c->impact.carry_distance},
                                                                     {"corridor", {c->impact.corridor_min, c->impact.corridor_max}},
                                                                     {"in_corridor", c->impact.in_corridor}, {"peak_head", c->impact.peak_head_delta_v}}},
                              // Senses (ADR-0024 §3): airborne seconds (-1 = in contact), landing prediction, bodies on
                              // the level / on moving objects, ground distance, peak hits (head, whole body).
                              {"senses", {{"air", c->senses.airborne != 0 ? c->senses.airborne_seconds : -1.0f},
                                          {"land", c->senses.landing_seconds}, {"land_body", c->senses.landing_body},
                                          {"ground", c->senses.ground_bodies}, {"object", c->senses.object_bodies},
                                          {"carried", c->senses.carried_by}, {"height", c->senses.ground_height},
                                          {"upright", c->senses.upright}, {"head_hit", c->senses.peak_head_delta_v},
                                          {"body_hit", c->senses.peak_impact_delta_v}}},
                              // The measurements behind "expects": which object, its leading edge and bumper line, the
                              // character's centre of mass above the same ground, where across the front it will hit.
                              {"threat", {{"group", c->threat.group}, {"lead", c->threat.profile.leading_edge_height},
                                          {"bumper", c->threat.profile.front_top_height}, {"roof", c->threat.profile.roof_height},
                                          {"bonnet", c->threat.profile.bonnet_length}, {"com", c->sensedComHeight},
                                          {"lateral", c->threat.lateral_offset}, {"speed", c->threat.closing_speed}}},
                              {"stage", c->behaviourStatus}});
    }
    Json frame = {{"t", "frame"}, {"step", step}, {"colliders", colliders}, {"characters", characters}};
    if (world->hostCamera.set)
    {
        const DdWorld::HostCamera& cam = world->hostCamera;
        frame["camera"] = {{"p", vec(cam.position)}, {"f", vec(cam.forward)}, {"u", vec(cam.up)}, {"fov", cam.fov}};
    }
    mPending = frame.dump() + "\n";
    flush();
}
} // namespace dragdoll
