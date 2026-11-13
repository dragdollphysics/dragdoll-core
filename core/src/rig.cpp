// SPDX-License-Identifier: MIT
#include "rig.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <stdexcept>
#include <unordered_map>

namespace dragdoll
{
namespace
{
using Json = nlohmann::json;

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

[[noreturn]] void error(const std::string& where, const std::string& what)
{
    throw std::invalid_argument("rig: " + where + ": " + what);
}

const Json& field(const Json& object, const char* key, const std::string& where)
{
    auto it = object.find(key);
    if (it == object.end())
    {
        error(where, std::string("missing \"") + key + "\"");
    }
    return *it;
}

float number(const Json& object, const char* key, const std::string& where)
{
    const Json& value = field(object, key, where);
    if (!value.is_number())
    {
        error(where, std::string("\"") + key + "\" must be a number");
    }
    const float f = value.get<float>();
    if (!std::isfinite(f))
    {
        error(where, std::string("\"") + key + "\" must be finite");
    }
    return f;
}

JPH::Vec3 vec3(const Json& object, const char* key, const std::string& where)
{
    const Json& value = field(object, key, where);
    if (!value.is_array() || value.size() != 3 || !value[0].is_number() || !value[1].is_number() ||
        !value[2].is_number())
    {
        error(where, std::string("\"") + key + "\" must be an array of 3 numbers");
    }
    const JPH::Vec3 v(value[0].get<float>(), value[1].get<float>(), value[2].get<float>());
    if (!std::isfinite(v.GetX()) || !std::isfinite(v.GetY()) || !std::isfinite(v.GetZ()))
    {
        error(where, std::string("\"") + key + "\" must be finite");
    }
    return v;
}

JPH::Vec3 direction(const Json& object, const char* key, const std::string& where)
{
    const JPH::Vec3 v = vec3(object, key, where);
    if (v.Length() < 1e-6f)
    {
        error(where, std::string("\"") + key + "\" must not be zero");
    }
    return v.Normalized();
}

float angle(const Json& object, const char* key, const std::string& where, float minDeg, float maxDeg)
{
    const float deg = number(object, key, where);
    if (deg < minDeg || deg > maxDeg)
    {
        error(where, std::string("\"") + key + "\" must be within [" + std::to_string(static_cast<int>(minDeg)) + ", " +
                         std::to_string(static_cast<int>(maxDeg)) + "] degrees");
    }
    return deg * kDegToRad;
}
} // namespace

Rig Rig::parse(std::string_view text)
{
    Json root;
    try
    {
        root = Json::parse(text.begin(), text.end());
    }
    catch (const Json::parse_error& e)
    {
        error("json", e.what());
    }
    if (!root.is_object())
    {
        error("json", "top level must be an object");
    }
    if (field(root, "format", "rig") != "dragdoll-rig")
    {
        error("rig", "\"format\" must be \"dragdoll-rig\"");
    }
    if (number(root, "version", "rig") != 1.0f)
    {
        error("rig", "unsupported \"version\" (this build reads version 1)");
    }

    Rig rig;
    rig.name = field(root, "name", "rig").is_string() ? root["name"].get<std::string>() : "";
    rig.mass = number(root, "mass", "rig");
    if (!(rig.mass > 0.0f))
    {
        error("rig", "\"mass\" must be > 0");
    }

    const Json& bodies = field(root, "bodies", "rig");
    if (!bodies.is_array() || bodies.empty())
    {
        error("rig", "\"bodies\" must be a non-empty array");
    }

    std::unordered_map<std::string, int> indexByName;
    float massPercentTotal = 0.0f;
    for (size_t i = 0; i < bodies.size(); ++i)
    {
        const Json& b = bodies[i];
        std::string where = "bodies[" + std::to_string(i) + "]";
        if (!b.is_object())
        {
            error(where, "must be an object");
        }
        RigBody body;
        const Json& name = field(b, "name", where);
        if (!name.is_string() || name.get<std::string>().empty())
        {
            error(where, "\"name\" must be a non-empty string");
        }
        body.name = name.get<std::string>();
        where = "body \"" + body.name + "\"";
        if (indexByName.count(body.name) != 0)
        {
            error(where, "duplicate name");
        }

        const Json& parent = field(b, "parent", where);
        if (!parent.is_string())
        {
            error(where, "\"parent\" must be a string (\"\" for the root)");
        }
        const std::string parentName = parent.get<std::string>();
        if (parentName.empty())
        {
            if (i != 0)
            {
                error(where, "only the first body may be the root (parent \"\")");
            }
        }
        else
        {
            auto it = indexByName.find(parentName);
            if (it == indexByName.end())
            {
                error(where, "parent \"" + parentName + "\" must be defined before its children");
            }
            body.parent = it->second;
        }

        body.massPercent = number(b, "mass_percent", where);
        if (!(body.massPercent > 0.0f))
        {
            error(where, "\"mass_percent\" must be > 0");
        }
        massPercentTotal += body.massPercent;

        if (b.contains("collide_with_parent"))
        {
            if (!b["collide_with_parent"].is_boolean())
            {
                error(where, "\"collide_with_parent\" must be true or false");
            }
            body.collideWithParent = b["collide_with_parent"].get<bool>();
            if (body.collideWithParent && body.parent < 0)
            {
                error(where, "the root has no parent to collide with");
            }
        }

        // Shape
        const Json& shape = field(b, "shape", where);
        const Json& type = field(shape, "type", where + " shape");
        if (type == "box")
        {
            body.shape.type = RigShape::Type::Box;
            body.shape.center = vec3(shape, "center", where + " shape");
            body.shape.halfExtents = vec3(shape, "half_extents", where + " shape");
            if (body.shape.halfExtents.ReduceMin() <= 0.0f)
            {
                error(where + " shape", "\"half_extents\" must all be > 0");
            }
        }
        else if (type == "capsule")
        {
            body.shape.type = RigShape::Type::Capsule;
            body.shape.from = vec3(shape, "from", where + " shape");
            body.shape.to = vec3(shape, "to", where + " shape");
            body.shape.radius = number(shape, "radius", where + " shape");
            if (!(body.shape.radius > 0.0f))
            {
                error(where + " shape", "\"radius\" must be > 0");
            }
            if ((body.shape.to - body.shape.from).Length() < 1e-4f)
            {
                error(where + " shape", "\"from\" and \"to\" must differ");
            }
        }
        else
        {
            error(where + " shape", "\"type\" must be \"box\" or \"capsule\"");
        }

        // Joint (all bodies but the root)
        if (body.parent >= 0)
        {
            const Json& joint = field(b, "joint", where);
            const std::string jw = where + " joint";
            const Json& jtype = field(joint, "type", jw);
            body.joint.pivot = vec3(joint, "pivot", jw);
            if (joint.contains("friction"))
            {
                body.joint.friction = number(joint, "friction", jw);
                if (body.joint.friction < 0.0f)
                {
                    error(jw, "\"friction\" must be >= 0 (N*m)");
                }
            }
            if (jtype == "swing_twist")
            {
                body.joint.type = RigJoint::Type::SwingTwist;
                body.joint.twistAxis = direction(joint, "twist_axis", jw);
                body.joint.coneAxis =
                    joint.contains("cone_axis") ? direction(joint, "cone_axis", jw) : body.joint.twistAxis;
                body.joint.swingFwd = angle(joint, "swing_fwd", jw, 0.0f, 179.0f);
                body.joint.swingSide = angle(joint, "swing_side", jw, 0.0f, 179.0f);
                body.joint.twistMin = angle(joint, "twist_min", jw, -180.0f, 180.0f);
                body.joint.twistMax = angle(joint, "twist_max", jw, -180.0f, 180.0f);
                if (body.joint.twistMin > body.joint.twistMax)
                {
                    error(jw, "\"twist_min\" must be <= \"twist_max\"");
                }
                if (body.joint.twistAxis.Dot(body.joint.coneAxis) < std::cos(std::max(body.joint.swingFwd, body.joint.swingSide)))
                {
                    error(jw, "the rest pose (twist_axis) lies outside the swing cone around cone_axis");
                }
            }
            else if (jtype == "hinge")
            {
                body.joint.type = RigJoint::Type::Hinge;
                body.joint.axis = direction(joint, "axis", jw);
                body.joint.min = angle(joint, "min", jw, -180.0f, 0.0f);
                body.joint.max = angle(joint, "max", jw, 0.0f, 180.0f);
            }
            else
            {
                error(jw, "\"type\" must be \"swing_twist\" or \"hinge\"");
            }
        }

        indexByName.emplace(body.name, static_cast<int>(i));
        rig.bodies.push_back(std::move(body));
    }

    if (std::fabs(massPercentTotal - 100.0f) > 1.0f)
    {
        error("rig", "\"mass_percent\" values must add up to 100 (got " + std::to_string(massPercentTotal) + ")");
    }
    return rig;
}

} // namespace dragdoll
