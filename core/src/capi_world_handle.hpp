// SPDX-License-Identifier: MIT
// The opaque DdWorld handle: one definition shared by every C ABI translation unit.
#pragma once

#include "recording.hpp"
#include "world.hpp"

#include <memory>

struct DdWorld
{
    explicit DdWorld(const DdWorldDesc& desc) : world(desc)
    {
        world.setOwner(this);
        if (desc.record_path != nullptr)
        {
            recorder = std::make_unique<dragdoll::rec::Recorder>(desc.record_path, desc);
        }
    }
    dragdoll::World world;
    std::unique_ptr<dragdoll::rec::Recorder> recorder; // Records every changing call at the C ABI boundary
};

