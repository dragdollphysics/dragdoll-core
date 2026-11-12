// SPDX-License-Identifier: MIT
// The opaque DdWorld handle: one definition shared by every C ABI translation unit.
#pragma once

#include "world.hpp"

struct DdWorld
{
    explicit DdWorld(const DdWorldDesc& desc) : world(desc) {}
    dragdoll::World world;
};
