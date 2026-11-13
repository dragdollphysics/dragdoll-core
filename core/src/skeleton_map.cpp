// SPDX-License-Identifier: MIT
#include "skeleton_map.hpp"

#include "character.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>
#include <string>

namespace dragdoll
{
namespace
{
[[noreturn]] void fail(const std::string& what)
{
    throw std::invalid_argument("skeleton map: " + what);
}

JPH::Quat quat(const DdQuat& q, const char* what)
{
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!std::isfinite(n) || n < 1e-6f)
    {
        fail(std::string(what) + " must be a finite, non-zero quaternion");
    }
    return JPH::Quat(q.x / n, q.y / n, q.z / n, q.w / n);
}

DdQuat toDd(JPH::Quat q)
{
    q = q.Normalized();
    if (q.GetW() < 0.0f) q = -q;
    return DdQuat{q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

JPH::Vec3 vec(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key) || !j[key].is_array() || j[key].size() != 3)
    {
        fail(std::string("\"") + key + "\" must be an array of 3 numbers");
    }
    const JPH::Vec3 v(j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>());
    if (!(v.Length() > 1e-6f))
    {
        fail(std::string("\"") + key + "\" must not be zero");
    }
    return v.Normalized();
}

// Shortest rotation taking direction a onto direction b.
JPH::Quat fromTo(JPH::Vec3 a, JPH::Vec3 b)
{
    a = a.Normalized();
    b = b.Normalized();
    const float d = a.Dot(b);
    if (d > 0.999999f) return JPH::Quat::sIdentity();
    if (d < -0.999999f)
    {
        const JPH::Vec3 axis = a.GetNormalizedPerpendicular();
        return JPH::Quat::sRotation(axis, 3.14159265f);
    }
    return JPH::Quat::sRotation(a.Cross(b).Normalized(), std::acos(d));
}
} // namespace

DdSkeletonMap buildSkeletonMap(std::string_view text, const DdCharacter& c, const DdHostBone* bones, uint32_t count)
{
    const nlohmann::json root = nlohmann::json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object() || root.value("format", "") != "dragdoll-skeleton-map" || root.value("version", 0) != 1)
    {
        fail("expected a JSON object with \"format\": \"dragdoll-skeleton-map\", \"version\": 1");
    }
    if (bones == nullptr || count == 0)
    {
        fail("host bones are required");
    }
    DdSkeletonMap map;
    // A: host model axes -> rig axes (+X left = up x forward, +Y up, +Z forward).
    const JPH::Vec3 up = vec(root, "model_up"), forward = vec(root, "model_forward");
    if (std::fabs(up.Dot(forward)) > 1e-3f)
    {
        fail("model_up and model_forward must be perpendicular");
    }
    const JPH::Vec3 left = up.Cross(forward);
    const JPH::Mat44 rigFromBasis = JPH::Mat44::sIdentity(); // Columns X, Y, Z
    const JPH::Mat44 modelFromBasis(JPH::Vec4(left, 0), JPH::Vec4(up, 0), JPH::Vec4(forward, 0), JPH::Vec4(0, 0, 0, 1));
    map.modelToRig = (rigFromBasis * modelFromBasis.Transposed()).GetQuaternion().Normalized();

    // Host bones
    map.boneParent.resize(count);
    map.boneBody.assign(count, -1);
    map.bindRotation.resize(count);
    map.bindPosition.resize(count);
    for (uint32_t k = 0; k < count; ++k)
    {
        if (bones[k].parent >= static_cast<int32_t>(k) || bones[k].parent < -1)
        {
            fail("bone " + std::to_string(bones[k].id) + ": parent must come earlier in the array (or be -1)");
        }
        map.boneParent[k] = bones[k].parent;
        map.bindRotation[k] = quat(bones[k].bind.rotation, "bind rotation");
        const DdVec3& p = bones[k].bind.position;
        map.bindPosition[k] = JPH::Vec3(p.x, p.y, p.z);
    }

    // Bodies -> bones
    if (!root.contains("bodies") || !root["bodies"].is_object())
    {
        fail("missing \"bodies\" object (rig body name -> host bone id)");
    }
    const size_t bodies = c.parent.size();
    map.bodyParent = c.parent;
    map.restRig = c.restRig;
    map.bodyBone.assign(bodies, -1);
    for (const auto& [name, id] : root["bodies"].items())
    {
        size_t body = bodies;
        for (size_t i = 0; i < bodies; ++i)
        {
            if (c.names[i] == name) body = i;
        }
        if (body == bodies)
        {
            fail("the rig has no body \"" + name + "\"");
        }
        if (!id.is_number_integer())
        {
            fail("body \"" + name + "\": the bone id must be an integer");
        }
        for (uint32_t k = 0; k < count; ++k)
        {
            if (bones[k].id == id.get<int>()) map.bodyBone[body] = static_cast<int>(k);
        }
        if (map.bodyBone[body] < 0)
        {
            fail("body \"" + name + "\": the host has no bone " + std::to_string(id.get<int>()));
        }
        map.boneBody[static_cast<size_t>(map.bodyBone[body])] = static_cast<int>(body);
    }
    for (size_t i = 0; i < bodies; ++i)
    {
        if (map.bodyBone[i] < 0)
        {
            fail("body \"" + c.names[i] + "\" has no bone (every rig body must be mapped)");
        }
    }

    // Bind correction D_i: rotate the body's rest direction onto its bone's bind direction (both in rig axes).
    map.correction.assign(bodies, JPH::Quat::sIdentity());
    for (size_t i = 1; i < bodies; ++i)
    {
        size_t child = bodies;
        for (size_t j = i + 1; j < bodies && child == bodies; ++j)
        {
            if (c.parent[j] == static_cast<int>(i)) child = j;
        }
        const JPH::Vec3 rest = child < bodies ? c.pivotRig[child] - c.pivotRig[i] : c.centreRig[i] - c.pivotRig[i];
        const size_t k = static_cast<size_t>(map.bodyBone[i]);
        int towards = child < bodies ? map.bodyBone[child] : -1;
        for (uint32_t b = 0; b < count && towards < 0; ++b) // End bodies: the bone's first child (finger, toe, head...)
        {
            if (map.boneParent[b] == static_cast<int>(k)) towards = static_cast<int>(b);
        }
        if (towards < 0)
        {
            continue; // No direction to compare with: assume the bind matches the rest pose
        }
        const JPH::Vec3 bind = map.modelToRig * (map.bindPosition[static_cast<size_t>(towards)] - map.bindPosition[k]);
        if (rest.Length() > 1e-4f && bind.Length() > 1e-4f)
        {
            map.correction[i] = fromTo(rest, bind);
        }
    }
    return map;
}

void mapToPose(const DdSkeletonMap& map, const DdQuat* boneRotations, const DdVec3* rootPosition, const DdTransform& modelToWorld,
               DdTransform& outRoot, DdQuat* outJoints)
{
    const JPH::Quat A = map.modelToRig;
    const size_t bodies = map.bodyBone.size();
    std::vector<JPH::Quat> g(bodies); // G_k D_i per body
    for (size_t i = 0; i < bodies; ++i)
    {
        const size_t k = static_cast<size_t>(map.bodyBone[i]);
        const JPH::Quat h = quat(boneRotations[k], "bone rotation");
        g[i] = (A * h * map.bindRotation[k].Conjugated() * A.Conjugated() * map.correction[i]).Normalized();
    }
    outJoints[0] = DdQuat{0, 0, 0, 1};
    for (size_t i = 1; i < bodies; ++i)
    {
        outJoints[i] = toDd(g[static_cast<size_t>(map.bodyParent[i])].Conjugated() * g[i]);
    }
    const JPH::Quat world = quat(modelToWorld.rotation, "model_to_world rotation");
    const size_t k0 = static_cast<size_t>(map.bodyBone[0]);
    const JPH::Vec3 local = rootPosition != nullptr ? JPH::Vec3(rootPosition->x, rootPosition->y, rootPosition->z) : map.bindPosition[k0];
    const JPH::Vec3 at = world * local + JPH::Vec3(modelToWorld.position.x, modelToWorld.position.y, modelToWorld.position.z);
    outRoot.position = DdVec3{at.GetX(), at.GetY(), at.GetZ()};
    outRoot.rotation = toDd(world * A.Conjugated() * g[0] * map.restRig[0]);
}

void mapFromPose(const DdSkeletonMap& map, const DdTransform& root, const DdQuat* joints, const DdTransform& modelToWorld,
                 DdQuat* outBoneRotations)
{
    const JPH::Quat A = map.modelToRig;
    const size_t bodies = map.bodyBone.size();
    const JPH::Quat world = quat(modelToWorld.rotation, "model_to_world rotation");
    std::vector<JPH::Quat> g(bodies);
    g[0] = (A * world.Conjugated() * quat(root.rotation, "root rotation") * map.restRig[0].Conjugated()).Normalized();
    for (size_t i = 1; i < bodies; ++i)
    {
        g[i] = (g[static_cast<size_t>(map.bodyParent[i])] * quat(joints[i], "joint rotation")).Normalized();
    }
    const size_t bones = map.boneParent.size();
    std::vector<JPH::Quat> G(bones); // Rotation since bind, rig axes
    for (size_t k = 0; k < bones; ++k)
    {
        const int body = map.boneBody[k];
        if (body >= 0)
        {
            G[k] = g[static_cast<size_t>(body)] * map.correction[static_cast<size_t>(body)].Conjugated();
        }
        else
        {
            // Unmapped bones follow their parent rigidly (a root above the pelvis follows the pelvis).
            G[k] = map.boneParent[k] >= 0 ? G[static_cast<size_t>(map.boneParent[k])]
                                          : g[0] * map.correction[0].Conjugated();
        }
        outBoneRotations[k] = toDd(A.Conjugated() * G[k] * A * map.bindRotation[k]);
    }
}
} // namespace dragdoll
