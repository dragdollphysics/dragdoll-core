// SPDX-License-Identifier: MIT
// The core's side of the behaviour SDK (ADR-0025): the registry of behaviours, their parameters (per world, so a
// change is recorded and replayed), and the per-step run: percept in, command out, command applied to the muscles.
#pragma once

#include <dragdoll/behaviour_sdk.h>

#include <array>
#include <map>
#include <string>
#include <string_view>

struct DdCharacter;

namespace dragdoll
{
class World;

// One behaviour's numbers, flattened ("anticipate.time").
class ParamSet final : public sdk::Params
{
public:
    float number(const char* key, float fallback) const override;
    std::map<std::string, float, std::less<>> values;
    // Named poses: pose -> body name -> degrees about the rig's X, Y, Z axes (offsets from rest).
    std::map<std::string, std::map<std::string, std::array<float, 3>>> poses;
    uint32_t revision = 0; // Bumped on every change: characters re-configure their behaviour
};

// Parameters of every behaviour in one world. Starts from the built-in packs (packs/behaviours/*.json).
class BehaviourParams
{
public:
    const ParamSet& of(const std::string& behaviour); // Never fails: unknown names have no numbers
    // Replaces a behaviour's numbers from pack JSON. Throws std::invalid_argument on malformed input.
    void set(const std::string& behaviour, std::string_view json);

private:
    std::map<std::string, ParamSet> mSets;
    uint32_t mRevisions = 0;
};

// Built-in pack text (generated from packs/behaviours), or nullptr.
const char* builtinBehaviourParams(std::string_view name);

// Registers the built-in behaviours (once); clears the registry at shutdown.
void initBehaviours();
void shutdownBehaviours();
bool behaviourExists(const std::string& name);

// Attaches the named behaviour to the character ("" detaches). Throws std::invalid_argument for unknown names.
void attachBehaviour(DdCharacter& character, const std::string& name);
// Sets the target pose of the bodies in `regionMask` (bit per DdRegion) from a pose of the pack; "rest" = the rest
// pose. Throws std::invalid_argument for unknown poses.
void applyNamedPose(DdCharacter& character, const ParamSet& pack, const std::string& pose, uint32_t regionMask);
// One fixed step: builds the percept, runs the behaviour, applies its command. No-op without a behaviour.
void updateBehaviour(DdCharacter& character, World& world, float dt);
} // namespace dragdoll
