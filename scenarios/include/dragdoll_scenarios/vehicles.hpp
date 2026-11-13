// SPDX-License-Identifier: MIT
// Vehicle shape pack (packs/vehicles/*.json): parameterised side profiles turned into closed kinematic meshes, plus a
// simple braking drive. Own generic geometry for the hit scenarios and the lab; in the game, vehicles come from the
// game's own collision data instead (Phase 6).
#pragma once

#include <dragdoll/dragdoll.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace dragdoll::scenarios
{
struct VehicleProfile
{
    std::string name;
    std::string description;
    float length = 0, width = 0, clearance = 0;
    float bumperHeight = 0;      // Top of the vertical front face
    float bonnetEdgeHeight = 0;  // Front edge of the bonnet (hood)
    float bonnetEdgeSetback = 0; // How far the bonnet edge sits behind the front face
    float hoodLength = 0;        // Horizontal length of the bonnet
    float hoodAngle = 0;         // Degrees: rise of the bonnet towards the windscreen
    float windshieldAngle = 0;   // Degrees from horizontal
    float roofHeight = 0, roofLength = 0;
    float rearHeight = 0;        // Top of the vertical rear face

    // Side outline as (forward, up) points, counter-clockwise, origin = ground under the vehicle's centre.
    std::vector<std::array<float, 2>> outline() const;
};

// Names of the built-in vehicle packs (compiled in from packs/vehicles), sorted.
std::vector<std::string> vehicleNames();
// JSON text of a built-in vehicle, or nullptr.
const char* builtinVehicle(std::string_view name);
// Throws std::invalid_argument with a precise message on any problem.
VehicleProfile parseVehicle(std::string_view json);

struct VehicleMesh
{
    std::vector<DdVec3> vertices;
    std::vector<uint32_t> indices; // 3 per triangle, outward-facing, closed surface
};
// forward / left / up: the world directions of the vehicle's axes (right-handed or not; windings are fixed up).
VehicleMesh buildVehicleMesh(const VehicleProfile& profile, DdVec3 forward, DdVec3 left, DdVec3 up);

// Constant speed, then braking to a stop.
struct Drive
{
    float speed = 0.0f;   // m/s
    float brakeAt = 1e9f; // s
    float decel = 0.0f;   // m/s^2 while braking
    float distance(double t) const;
};
} // namespace dragdoll::scenarios
