// SPDX-License-Identifier: MIT
// Live tap (bridge task 3.7): a world streams what DragDoll itself sees — its proxy colliders and ragdolls — to a
// viewer over TCP on 127.0.0.1, as JSON lines. Dev builds only. No game data beyond what the host handed DragDoll.
//
// Messages: {"t":"scene", ...} on connect and whenever colliders/characters change; {"t":"frame", ...} after every host
// frame that advanced the simulation (at most 60 a second). A frame is dropped (never queued) while the previous message is still being sent, so a slow viewer can
// never stall the game.
#pragma once

#include <dragdoll/dragdoll.h>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>

struct DdWorld;

namespace dragdoll
{
class LiveTap
{
public:
    // Throws std::invalid_argument if the port cannot be opened.
    LiveTap(uint16_t port, const DdWorldDesc& desc);
    ~LiveTap();
    LiveTap(const LiveTap&) = delete;
    LiveTap& operator=(const LiveTap&) = delete;

    void collidersAdded(const DdColliderDesc* descs, uint32_t count, const DdColliderId* ids);
    void collidersRemoved(const DdColliderId* ids, uint32_t count);
    void characterCreated(uint32_t id, const DdCharacterDesc& desc);
    void characterDestroyed(uint32_t id);
    void characterRenamed(uint32_t id, const std::string& name);
    // After every step call: accepts a viewer, sends the whole scene to a new viewer, then only what changed
    // ("delta": added/removed colliders and characters), and a frame at up to 60 Hz.
    void afterStep(DdWorld* world);
    // Next viewer -> host message ({"t":"host","data":"..."}), oldest first; false when none.
    bool popHostMessage(std::string& out);

private:
    bool flush(); // true when nothing is left to send
    // Viewer -> host: {"t":"show","id":N,"visible":0|1}, {"t":"tension","id":N,"tension":0-3,"blend":s},
    // {"t":"balance","id":N,"on":0|1}, {"t":"host","data":"..."} (queued for the host, see popHostMessage).
    void readCommands(DdWorld* world);
    void closeClient();

    uintptr_t mListener = ~uintptr_t{0};
    uintptr_t mClient = ~uintptr_t{0};
    std::string mPending;
    nlohmann::json mWorld;
    std::map<DdColliderId, nlohmann::json> mColliders;
    std::map<uint32_t, nlohmann::json> mCharacters;
    std::map<DdColliderId, bool> mMoving; // Non-static colliders: their poses go into frames
    bool mSceneDirty = true; // A new viewer needs the whole scene
    // Changes since the last scene/delta sent (ids); removes are applied first.
    std::set<DdColliderId> mColliderAdds, mColliderRemoves;
    std::set<uint32_t> mCharacterAdds, mCharacterRemoves;
    uint64_t mLastFrameStep = 0;
    std::string mIncoming;
    std::deque<std::string> mHostMessages;
    uint64_t mWorldId = 0; // Distinguishes successive worlds on the same port (e.g. one per F7 drop)
};
} // namespace dragdoll
