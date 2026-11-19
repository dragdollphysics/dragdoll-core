// SPDX-License-Identifier: MIT
#include "behaviour_host.hpp"

#include "character.hpp"
#include "world.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dragdoll
{
namespace
{
std::map<std::string, sdk::Factory>& registry()
{
    static std::map<std::string, sdk::Factory> behaviours;
    return behaviours;
}

void flatten(const nlohmann::json& node, const std::string& prefix, std::map<std::string, float, std::less<>>& out)
{
    for (auto it = node.begin(); it != node.end(); ++it)
    {
        const std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();
        if (it->is_object())
        {
            flatten(*it, key, out);
        }
        else if (it->is_number())
        {
            const float value = it->get<float>();
            if (!std::isfinite(value))
            {
                throw std::invalid_argument("behaviour params: \"" + key + "\" is not finite");
            }
            out[key] = value;
        }
        else if (it->is_boolean())
        {
            out[key] = it->get<bool>() ? 1.0f : 0.0f;
        }
        else
        {
            throw std::invalid_argument("behaviour params: \"" + key + "\" must be a number, true/false or an object");
        }
    }
}

ParamSet parse(std::string_view text)
{
    const nlohmann::json doc = nlohmann::json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object())
    {
        throw std::invalid_argument("behaviour params: not valid JSON");
    }
    if (doc.value("format", std::string()) != "dragdoll-behaviour" || doc.value("version", 0) != 1)
    {
        throw std::invalid_argument("behaviour params: expected \"format\": \"dragdoll-behaviour\", \"version\": 1");
    }
    const auto params = doc.find("params");
    if (params == doc.end() || !params->is_object())
    {
        throw std::invalid_argument("behaviour params: \"params\" object is missing");
    }
    ParamSet set;
    flatten(*params, "", set.values);
    const auto poses = doc.find("poses");
    if (poses != doc.end())
    {
        if (!poses->is_object())
        {
            throw std::invalid_argument("behaviour params: \"poses\" must be an object");
        }
        for (auto pose = poses->begin(); pose != poses->end(); ++pose)
        {
            if (!pose->is_object())
            {
                throw std::invalid_argument("behaviour params: pose \"" + pose.key() + "\" must be an object of body: [x, y, z] degrees");
            }
            std::map<std::string, std::array<float, 3>>& bodies = set.poses[pose.key()];
            for (auto body = pose->begin(); body != pose->end(); ++body)
            {
                if (!body->is_array() || body->size() != 3 || !(*body)[0].is_number() || !(*body)[1].is_number() || !(*body)[2].is_number())
                {
                    throw std::invalid_argument("behaviour params: pose \"" + pose.key() + "\", body \"" + body.key() +
                                                "\": expected [x, y, z] in degrees");
                }
                const std::array<float, 3> degrees{(*body)[0].get<float>(), (*body)[1].get<float>(), (*body)[2].get<float>()};
                if (!std::isfinite(degrees[0]) || !std::isfinite(degrees[1]) || !std::isfinite(degrees[2]))
                {
                    throw std::invalid_argument("behaviour params: pose \"" + pose.key() + "\" has a value that is not finite");
                }
                bodies[body.key()] = degrees;
            }
        }
    }
    return set;
}

// The horizontal part of v (unit length), or zero.
JPH::Vec3 flat(JPH::Vec3Arg v, JPH::Vec3Arg up)
{
    const JPH::Vec3 h = v - up * v.Dot(up);
    const float length = h.Length();
    return length > 1e-5f ? h / length : JPH::Vec3::sZero();
}

DdVec3 toDd(JPH::Vec3Arg v)
{
    return DdVec3{v.GetX(), v.GetY(), v.GetZ()};
}
} // namespace

float ParamSet::number(const char* key, float fallback) const
{
    const auto it = values.find(std::string_view(key));
    return it != values.end() ? it->second : fallback;
}

const ParamSet& BehaviourParams::of(const std::string& behaviour)
{
    auto it = mSets.find(behaviour);
    if (it == mSets.end())
    {
        ParamSet set;
        if (const char* text = builtinBehaviourParams(behaviour))
        {
            set = parse(text); // Built-in packs are validated by the tests
        }
        set.revision = ++mRevisions;
        it = mSets.emplace(behaviour, std::move(set)).first;
    }
    return it->second;
}

void BehaviourParams::set(const std::string& behaviour, std::string_view json)
{
    ParamSet set = parse(json); // Throws before anything changes
    set.revision = ++mRevisions;
    mSets[behaviour] = std::move(set);
}

void sdk::registerBehaviour(const char* name, Factory factory)
{
    if (name != nullptr && factory != nullptr)
    {
        registry()[name] = factory;
    }
}

void initBehaviours()
{
    if (registry().empty())
    {
        sdk::registerBuiltinBehaviours();
    }
}

void shutdownBehaviours()
{
    registry().clear();
}

bool behaviourExists(const std::string& name)
{
    return registry().count(name) != 0;
}

void attachBehaviour(DdCharacter& c, const std::string& name)
{
    if (name.empty())
    {
        c.behaviour.reset();
        c.behaviourName.clear();
        c.behaviourStatus.clear();
        return;
    }
    const auto it = registry().find(name);
    if (it == registry().end())
    {
        throw std::invalid_argument("unknown behaviour \"" + name + "\"");
    }
    c.behaviour = it->second();
    c.behaviourName = name;
    c.behaviourParamsRevision = 0; // Configured on the next step
    c.behaviourStatus.clear();
    c.threatFrom = DD_INVALID_COLLIDER;
    c.threatSeconds = c.threatSeenSeconds = 0.0f;
}

void updateBehaviour(DdCharacter& c, World& world, float dt)
{
    if (c.behaviour == nullptr)
    {
        return;
    }
    const ParamSet& params = world.behaviourParams().of(c.behaviourName);
    if (params.revision != c.behaviourParamsRevision)
    {
        c.behaviour->configure(params);
        c.behaviourParamsRevision = params.revision;
    }

    const JPH::Vec3 up = world.up();
    sdk::Percept percept;
    percept.dt = dt;
    percept.step = world.stepCount();
    percept.up = toDd(up);
    percept.root = DdTransform{toDd(JPH::Vec3(c.currPosition[0])),
                               DdQuat{c.currRotation[0].GetX(), c.currRotation[0].GetY(), c.currRotation[0].GetZ(), c.currRotation[0].GetW()}};
    JPH::Vec3 com = JPH::Vec3::sZero(), before = JPH::Vec3::sZero();
    float mass = 0.0f;
    for (size_t i = 0; i < c.currPosition.size(); ++i)
    {
        com += JPH::Vec3(c.currPosition[i]) * c.bodyMass[i];
        before += JPH::Vec3(c.prevPosition[i]) * c.bodyMass[i];
        mass += c.bodyMass[i];
    }
    if (mass > 0.0f)
    {
        com /= mass;
        before /= mass;
    }
    percept.centreOfMass = toDd(com);
    percept.velocity = toDd((com - before) / dt);
    percept.kinematic = c.kinematic;
    percept.stillSeconds = c.stillSeconds;
    for (int r = 0; r < DD_REGION_COUNT; ++r)
    {
        percept.tension[r] = c.regionMuscles[static_cast<size_t>(r)].tension;
    }
    percept.vitality = c.vitality;
    percept.senses = c.senses;
    percept.contacts = c.contacts.data();
    percept.bodyCount = static_cast<uint32_t>(c.contacts.size());
    c.perceptBodies.resize(c.currPosition.size());
    for (size_t i = 0; i < c.currPosition.size(); ++i)
    {
        c.perceptBodies[i] = DdTransform{toDd(JPH::Vec3(c.currPosition[i])),
                                         DdQuat{c.currRotation[i].GetX(), c.currRotation[i].GetY(), c.currRotation[i].GetZ(), c.currRotation[i].GetW()}};
        if (i < sdk::kMaxBodies && i < c.region.size())
        {
            percept.bodyRegion[i] = static_cast<DdRegion>(c.region[i]);
        }
    }
    percept.bodies = c.perceptBodies.data();
    percept.headBody = c.headBody;
    {
        const size_t head = c.headBody >= 0 ? static_cast<size_t>(c.headBody) : 0;
        const JPH::Vec3 hf = c.currRotation[head] * (c.restRig[head].Conjugated() * JPH::Vec3::sAxisZ());
        const JPH::Vec3 tf = c.currRotation[0] * (c.restRig[0].Conjugated() * JPH::Vec3::sAxisZ());
        const JPH::Vec3 tl = c.currRotation[0] * (c.restRig[0].Conjugated() * JPH::Vec3::sAxisX());
        percept.headForward = toDd(hf);
        percept.trunkForward = toDd(tf);
        percept.trunkLeft = toDd(tl);
    }

    // Threat, how long it has been one, and whether the character knows: told by the host, or seen — the object lies
    // within the head's field of view (about 100 degrees to each side of where the head faces).
    percept.threat = c.threat;
    const bool threat = c.threat.collider != DD_INVALID_COLLIDER;
    bool seen = false;
    if (threat)
    {
        const size_t head = c.headBody >= 0 ? static_cast<size_t>(c.headBody) : 0;
        const JPH::Vec3 facing = flat(c.currRotation[head] * (c.restRig[head].Conjugated() * JPH::Vec3(0.0f, 0.0f, 1.0f)), up);
        const JPH::Vec3 travel(c.threat.direction.x, c.threat.direction.y, c.threat.direction.z);
        seen = facing.Dot(-travel) > -0.17f; // The object comes from the direction opposite to its travel
    }
    percept.awareness = !threat ? 0.0f : c.awarenessOverride >= 0.0f ? c.awarenessOverride : seen ? 1.0f : 0.0f;
    if (!threat || c.threat.collider != c.threatFrom)
    {
        c.threatFrom = threat ? c.threat.collider : DD_INVALID_COLLIDER;
        c.threatSeconds = c.threatSeenSeconds = 0.0f;
    }
    else
    {
        c.threatSeconds += dt;
        c.threatSeenSeconds = percept.awareness > 0.0f ? c.threatSeenSeconds + dt : 0.0f;
    }
    percept.threatSeconds = c.threatSeconds;
    percept.threatSeenSeconds = c.threatSeenSeconds;

    sdk::Command command;
    c.behaviour->update(percept, command);
    c.behaviourStatus = c.behaviour->status();

    // Apply: muscles only (ADR-0003).
    for (int r = 0; r < DD_REGION_COUNT; ++r)
    {
        const DdTension wanted = command.tension[r];
        if (command.tense[r] && static_cast<int>(wanted) >= 0 && wanted < DD_TENSION_COUNT &&
            wanted != c.regionMuscles[static_cast<size_t>(r)].tension)
        {
            const float blend = std::isfinite(command.blendSeconds[r]) ? std::max(0.0f, command.blendSeconds[r]) : 0.0f;
            setRegionTension(c, static_cast<DdRegion>(r), wanted, blend);
        }
    }
    if (command.poseName != nullptr && command.poseRegions != 0)
    {
        try
        {
            applyNamedPose(c, params, command.poseName, command.poseRegions);
        }
        catch (const std::invalid_argument&)
        {
            c.behaviourStatus += " (unknown pose)"; // A pack without that pose: visible in tools, harmless otherwise
        }
    }
    const uint32_t joints = std::min<uint32_t>(command.poseCount, static_cast<uint32_t>(c.targetPose.size()));
    for (uint32_t i = 0; i < joints && i < sdk::kMaxBodies; ++i)
    {
        const DdQuat& q = command.pose[i];
        const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (std::isfinite(length) && length > 1e-6f) // A bad joint keeps its previous target
        {
            c.targetPose[i] = JPH::Quat(q.x / length, q.y / length, q.z / length, q.w / length);
            c.musclesDirty = true;
            c.ragdoll->Activate(); // A body at rest is asleep and would ignore the new targets
        }
    }
    for (int r = 0; r < DD_REGION_COUNT; ++r)
    {
        const sdk::Command::Aim& aim = command.aim[r];
        if (aim.active && std::isfinite(aim.target.x) && std::isfinite(aim.target.y) && std::isfinite(aim.target.z) &&
            std::isfinite(aim.extension))
        {
            aimLimb(c, static_cast<DdRegion>(r), JPH::RVec3(aim.target.x, aim.target.y, aim.target.z), aim.extension);
        }
    }
    if (command.balance >= 0)
    {
        c.balance = command.balance != 0;
    }
}
} // namespace dragdoll
