// SPDX-License-Identifier: MIT
// The opaque DdWorld handle: one definition shared by every C ABI translation unit.
#pragma once

#include "live_tap.hpp"
#include "recording.hpp"
#include "world.hpp"

#include <memory>

struct DdWorld
{
    explicit DdWorld(const DdWorldDesc& worldDesc) : world(worldDesc), desc(worldDesc)
    {
        world.setOwner(this);
        if (worldDesc.record_path != nullptr)
        {
            recorder = std::make_unique<dragdoll::rec::Recorder>(worldDesc.record_path, worldDesc);
        }
        desc.record_path = nullptr; // Not kept: the caller's string may not outlive this call
    }
    dragdoll::World world;
    DdWorldDesc desc;
    struct HostCamera
    {
        bool set = false;
        DdVec3 position{}, forward{}, up{};
        float fov = 60.0f;
    } hostCamera; // For tools (live tap), never simulated
    std::unique_ptr<dragdoll::rec::Recorder> recorder; // Records every changing call at the C ABI boundary
    std::unique_ptr<dragdoll::LiveTap> tap;            // Streams the world to a viewer (dev builds)
};

