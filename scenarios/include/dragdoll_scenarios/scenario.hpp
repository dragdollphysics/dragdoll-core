// SPDX-License-Identifier: MIT
// Scenarios: reusable, deterministic situations built ONLY on the public C API.
// The unit tests use them to check numbers; the live viewer plays the very same ones so a human can
// judge how they look. One definition, two consumers.
#pragma once

#include <dragdoll/dragdoll.h>

#include <memory>
#include <string>
#include <vector>

namespace dragdoll::scenarios
{

class Scenario
{
public:
    virtual ~Scenario() = default;

    virtual const char* name() const = 0;
    virtual const char* description() const = 0;

    // World settings (up axis etc.). Defaults are applied before this is called.
    virtual void configure(DdWorldDesc& /*desc*/) const {}

    // Populate a freshly created world.
    virtual void build(DdWorld* world) = 0;

    // Host-side logic for one host frame, called BEFORE dragdoll_world_step(dt): move vehicles, apply
    // inputs. hostTime is the host clock at the END of the frame (what kinematic targets refer to).
    virtual void hostFrame(DdWorld* /*world*/, double /*hostTime*/, float /*dt*/) {}

    // Live readouts for the viewer HUD (speeds, distances ...).
    virtual void hud(DdWorld* /*world*/, std::vector<std::string>& /*lines*/) {}

    // True if the final state must match a steady 120 Hz run bit for bit at any host rate
    // (scenarios without host-driven motion).
    virtual bool rateIndependent() const { return false; }

    // The scenario's main character, if any (tests inspect it).
    virtual DdCharacter* character() const { return nullptr; }

    // Simulated seconds after which the scenario has played out (the viewer then restarts it).
    // 0 = never ends (continuous scenarios).
    virtual double duration() const { return 0.0; }

    // Suggested camera: look-at point and distance.
    virtual DdVec3 cameraTarget() const { return DdVec3{0.0f, 1.0f, 0.0f}; }
    virtual float cameraDistance() const { return 12.0f; }
};

// Measurements shared by tests and the viewer HUD.
namespace metrics
{
// Rotates v by quaternion q.
DdVec3 rotate(const DdQuat& q, const DdVec3& v);

// Signed bend (degrees) of a child capsule bone relative to its parent capsule bone, about the parent's local +X axis
// (the character's left), right-handed. Knees flex with a POSITIVE bend, elbows with a NEGATIVE bend; the opposite sign
// means hyperextension.
float hingeBendDegrees(const DdTransform& parent, const DdTransform& child);
} // namespace metrics

// Names of all registered scenarios, in a stable order.
std::vector<std::string> list();

// Creates a scenario by name; nullptr if unknown.
std::unique_ptr<Scenario> create(const std::string& name);

// A scenario instance together with its world.
struct Instance
{
    std::unique_ptr<Scenario> scenario;
    DdWorld* world = nullptr;
    double hostTime = 0.0;

    Instance() = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    Instance(Instance&& other) noexcept;
    Instance& operator=(Instance&& other) noexcept;
    ~Instance();

    // Creates the world (applying the scenario's configuration) and builds the scenario.
    // Returns false and fills `error` on failure. dragdoll_init must have been called.
    static bool make(const std::string& name, Instance& out, std::string& error);

    // One host frame: scenario host logic, then dragdoll_world_step(dt). Returns steps taken.
    uint32_t hostFrame(float dt);
};

} // namespace dragdoll::scenarios
