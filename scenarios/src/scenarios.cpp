// SPDX-License-Identifier: MIT
#include <dragdoll_scenarios/scenario.hpp>

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>

namespace dragdoll::scenarios
{
namespace
{
DdVec3 scaled(DdVec3 v, float s)
{
    return DdVec3{v.x * s, v.y * s, v.z * s};
}

float dot(DdVec3 a, DdVec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

DdColliderId addOne(DdWorld* world, const DdColliderDesc& desc)
{
    DdColliderId id = DD_INVALID_COLLIDER;
    dragdoll_world_add_colliders(world, &desc, 1, &id);
    return id;
}

// The testing ground is identical in every scenario (same size, same place) so that switching scenarios in the
// viewer never makes the terrain jump. Its top surface is the plane through the origin with normal `up`.
constexpr float kGroundHalfSize = 150.0f;

DdColliderId addGround(DdWorld* world, DdVec3 up, float halfSize = kGroundHalfSize)
{
    DdColliderDesc ground;
    dragdoll_collider_desc_default(&ground);
    ground.shape = DD_SHAPE_BOX;
    ground.half_extents = DdVec3{halfSize, halfSize, halfSize};
    ground.transform.position = scaled(up, -halfSize);
    return addOne(world, ground);
}

// ------------------------------------------------------------------------------------------------
// pile_zup: a tumbling pile of mixed props in a Z-up world (San Andreas convention).
// ------------------------------------------------------------------------------------------------
class PileScenario final : public Scenario
{
public:
    explicit PileScenario(DdVec3 up, const char* name, const char* description)
        : mUp(up), mName(name), mDescription(description)
    {
    }
    const char* name() const override { return mName; }
    const char* description() const override { return mDescription; }
    void configure(DdWorldDesc& desc) const override { desc.up = mUp; }
    bool rateIndependent() const override { return true; }
    double duration() const override { return 8.0; }
    DdVec3 cameraTarget() const override { return scaled(mUp, 3.0f); }
    float cameraDistance() const override { return 14.0f; }

    void build(DdWorld* world) override
    {
        addGround(world, mUp);
        std::vector<DdColliderDesc> props;
        for (int i = 0; i < 12; ++i)
        {
            DdColliderDesc d;
            dragdoll_collider_desc_default(&d);
            d.motion = DD_MOTION_DYNAMIC;
            d.shape = (i % 3 == 0) ? DD_SHAPE_SPHERE : (i % 3 == 1) ? DD_SHAPE_BOX : DD_SHAPE_CAPSULE;
            d.radius = 0.25f;
            d.half_height = 0.3f;
            d.half_extents = DdVec3{0.3f, 0.2f, 0.25f};
            const float lateral = 0.15f * static_cast<float>(i % 4) - 0.2f;
            DdVec3 p = scaled(mUp, 1.0f + 0.7f * static_cast<float>(i));
            // Offset sideways along axes perpendicular to up so the pile topples.
            p.x += (std::fabs(mUp.x) > 0.5f) ? 0.0f : lateral;
            p.z += (std::fabs(mUp.z) > 0.5f) ? 0.0f : lateral * 0.5f;
            if (std::fabs(mUp.z) > 0.5f)
            {
                p.y += lateral * 0.5f;
            }
            d.transform.position = p;
            d.transform.rotation = DdQuat{0.0f, std::sin(0.2f * static_cast<float>(i)), 0.0f,
                                          std::cos(0.2f * static_cast<float>(i))};
            props.push_back(d);
        }
        std::vector<DdColliderId> ids(props.size());
        dragdoll_world_add_colliders(world, props.data(), static_cast<uint32_t>(props.size()), ids.data());
    }

private:
    DdVec3 mUp;
    const char* mName;
    const char* mDescription;
};

// ------------------------------------------------------------------------------------------------
// ball_drop_*: a sphere falling onto the ground along the chosen up axis.
// ------------------------------------------------------------------------------------------------
class BallDropScenario final : public Scenario
{
public:
    BallDropScenario(DdVec3 up, const char* name, const char* description)
        : mUp(up), mName(name), mDescription(description)
    {
    }
    const char* name() const override { return mName; }
    const char* description() const override { return mDescription; }
    void configure(DdWorldDesc& desc) const override { desc.up = mUp; }
    bool rateIndependent() const override { return true; }
    double duration() const override { return 4.0; }
    DdVec3 cameraTarget() const override { return scaled(mUp, 1.0f); }
    float cameraDistance() const override { return 8.0f; }

    void build(DdWorld* world) override
    {
        addGround(world, mUp);
        DdColliderDesc ball;
        dragdoll_collider_desc_default(&ball);
        ball.motion = DD_MOTION_DYNAMIC;
        ball.radius = 0.3f;
        ball.transform.position = scaled(mUp, 2.0f);
        mBall = addOne(world, ball);
    }

    void hud(DdWorld* world, std::vector<std::string>& lines) override
    {
        DdTransform t{};
        if (dragdoll_world_get_collider_transform(world, mBall, 0, &t) == DD_OK)
        {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), "ball height above ground: %.3f m (rest = 0.300)", dot(t.position, mUp));
            lines.emplace_back(buffer);
        }
    }

private:
    DdVec3 mUp;
    const char* mName;
    const char* mDescription;
    DdColliderId mBall = DD_INVALID_COLLIDER;
};

// ------------------------------------------------------------------------------------------------
// car_vs_pole: a kinematic "car" drives into a thin pole (stand-in for a shin) at a chosen speed.
// Z-up like San Andreas. The car's path is exact; the host reports its pose once per host frame.
// ------------------------------------------------------------------------------------------------
class CarVsPoleScenario final : public Scenario
{
public:
    CarVsPoleScenario(float kmh, const char* name, const char* description)
        : mSpeed(kmh / 3.6f), mName(name), mDescription(description)
    {
    }
    const char* name() const override { return mName; }
    const char* description() const override { return mDescription; }
    void configure(DdWorldDesc& desc) const override { desc.up = DdVec3{0.0f, 0.0f, 1.0f}; }
    double duration() const override { return 5.0; }
    DdVec3 cameraTarget() const override { return DdVec3{0.0f, 0.0f, 0.8f}; }
    float cameraDistance() const override { return 16.0f; }

    void build(DdWorld* world) override
    {
        const DdVec3 up{0.0f, 0.0f, 1.0f};
        addGround(world, up);

        DdColliderDesc car;
        dragdoll_collider_desc_default(&car);
        car.shape = DD_SHAPE_BOX;
        car.motion = DD_MOTION_KINEMATIC;
        car.half_extents = DdVec3{2.2f, 0.9f, 0.55f}; // x = length (driving direction), y = width, z = height (bottom at 0.2 m)
        mCarStart = -3.0f - mSpeed * 0.8f;           // arrives ~0.8 s after the start
        car.transform.position = DdVec3{mCarStart, 0.0f, 0.75f};
        mCar = addOne(world, car);

        // Pole stands upright (capsule axis = up in a Z-up world): 1 m tall, 10 cm thick, hit by the car's front.
        DdColliderDesc pole;
        dragdoll_collider_desc_default(&pole);
        pole.shape = DD_SHAPE_CAPSULE;
        pole.motion = DD_MOTION_DYNAMIC;
        pole.radius = 0.05f;
        pole.half_height = 0.45f;
        pole.mass = 4.0f;
        pole.transform.position = DdVec3{0.0f, 0.0f, 0.5f};
        mPole = addOne(world, pole);
    }

    void hostFrame(DdWorld* world, double hostTime, float) override
    {
        // After about 4 s the car has long passed: stop it (hold pose) so it doesn't leave the ground slab.
        const double t = std::fmin(hostTime, 4.0);
        const DdTransform target{{static_cast<float>(mCarStart + mSpeed * t), 0.0f, 0.75f}, {0, 0, 0, 1}};
        dragdoll_world_set_kinematic_targets(world, &mCar, &target, 1);
    }

    void hud(DdWorld* world, std::vector<std::string>& lines) override
    {
        DdTransform car{}, pole{};
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "car speed: %.1f km/h", mSpeed * 3.6f);
        lines.emplace_back(buffer);
        if (dragdoll_world_get_collider_transform(world, mCar, 0, &car) == DD_OK &&
            dragdoll_world_get_collider_transform(world, mPole, 0, &pole) == DD_OK)
        {
            std::snprintf(buffer, sizeof(buffer), "car x = %.2f m | pole x = %.2f m (starts at 0.00)", car.position.x,
                          pole.position.x);
            lines.emplace_back(buffer);
        }
    }

private:
    float mSpeed;
    const char* mName;
    const char* mDescription;
    float mCarStart = 0.0f;
    DdColliderId mCar = DD_INVALID_COLLIDER;
    DdColliderId mPole = DD_INVALID_COLLIDER;
};

// ------------------------------------------------------------------------------------------------
// car_drive_by: a car passes a row of dynamic crates; shows the speed seen per fixed step.
// ------------------------------------------------------------------------------------------------
class CarSpeedScenario final : public Scenario
{
public:
    const char* name() const override { return "car_speed_50kmh"; }
    const char* description() const override
    {
        return "A kinematic car drives at a constant 50 km/h. Switch the host frame rate (F) and watch the "
               "per-step speed stay at 50 km/h: vehicle speed must not depend on the game's frame rate.";
    }
    void configure(DdWorldDesc& desc) const override { desc.up = DdVec3{0.0f, 0.0f, 1.0f}; }
    DdVec3 cameraTarget() const override { return DdVec3{0.0f, 0.0f, 0.8f}; }
    float cameraDistance() const override { return 18.0f; }

    void build(DdWorld* world) override
    {
        addGround(world, DdVec3{0.0f, 0.0f, 1.0f});
        DdColliderDesc car;
        dragdoll_collider_desc_default(&car);
        car.shape = DD_SHAPE_BOX;
        car.motion = DD_MOTION_KINEMATIC;
        car.half_extents = DdVec3{2.2f, 0.9f, 0.7f};
        car.transform.position = DdVec3{-15.0f, 0.0f, 0.75f};
        mCar = addOne(world, car);
        mLastX = -15.0f;
    }

    void hostFrame(DdWorld* world, double hostTime, float) override
    {
        const double wrapped = std::fmod(hostTime, 2.0 * 30.0 / kSpeed); // drives back and forth along 30 m
        const double half = 30.0 / kSpeed;
        const double dist = wrapped < half ? wrapped * kSpeed : (2.0 * half - wrapped) * kSpeed;
        const DdTransform target{{static_cast<float>(-15.0 + dist), 0.0f, 0.75f}, {0, 0, 0, 1}};
        dragdoll_world_set_kinematic_targets(world, &mCar, &target, 1);
    }

    void hud(DdWorld* world, std::vector<std::string>& lines) override
    {
        DdTransform t{};
        if (dragdoll_world_get_collider_transform(world, mCar, 0, &t) != DD_OK)
        {
            return;
        }
        const uint64_t step = dragdoll_world_get_step_count(world);
        if (step > mLastStep)
        {
            mSpeedKmh = std::fabs(static_cast<double>(t.position.x - mLastX)) / (static_cast<double>(step - mLastStep) / 120.0) * 3.6;
            mLastX = t.position.x;
            mLastStep = step;
        }
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "speed seen by physics: %.1f km/h (target 50.0)", mSpeedKmh);
        lines.emplace_back(buffer);
    }

private:
    static constexpr double kSpeed = 50.0 / 3.6;
    DdColliderId mCar = DD_INVALID_COLLIDER;
    float mLastX = 0.0f;
    uint64_t mLastStep = 0;
    double mSpeedKmh = 0.0;
};

// ------------------------------------------------------------------------------------------------
// Characters: the humanoid16 rig as a limp ragdoll (no muscles yet).
// ------------------------------------------------------------------------------------------------
DdQuat axisAngle(DdVec3 axis, float degrees)
{
    const float half = degrees * 3.14159265f / 360.0f;
    const float s = std::sin(half);
    return DdQuat{axis.x * s, axis.y * s, axis.z * s, std::cos(half)};
}

class RagdollScenario : public Scenario
{
public:
    RagdollScenario(const char* name, const char* description, DdVec3 origin, DdQuat rotation)
        : mName(name), mDescription(description), mOrigin(origin), mRotation(rotation)
    {
    }
    const char* name() const override { return mName; }
    const char* description() const override { return mDescription; }
    void configure(DdWorldDesc& desc) const override { desc.up = DdVec3{0.0f, 0.0f, 1.0f}; } // SA: Z-up
    bool rateIndependent() const override { return true; }
    double duration() const override { return 4.0; }
    DdCharacter* character() const override { return mCharacter; }
    DdVec3 cameraTarget() const override { return DdVec3{0.0f, 0.0f, 0.7f}; }
    float cameraDistance() const override { return 5.5f; }

    void build(DdWorld* world) override
    {
        addGround(world, DdVec3{0.0f, 0.0f, 1.0f});
        DdCharacterDesc desc;
        dragdoll_character_desc_default(&desc);
        desc.rig_json = dragdoll_get_builtin_rig("humanoid16");
        desc.transform.position = mOrigin;
        desc.transform.rotation = mRotation;
        dragdoll_character_create(world, &desc, &mCharacter);
        if (mCharacter != nullptr)
        {
            mBodies.resize(dragdoll_character_get_body_count(mCharacter));
        }
    }

    void hud(DdWorld*, std::vector<std::string>& lines) override
    {
        if (mCharacter == nullptr ||
            dragdoll_character_get_body_transforms(mCharacter, 0, mBodies.data(), static_cast<uint32_t>(mBodies.size())) != DD_OK)
        {
            lines.emplace_back("character could not be created");
            return;
        }
        auto body = [&](const char* name) { return mBodies[static_cast<size_t>(dragdoll_character_find_body(mCharacter, name))]; };
        float lowest = 1e9f;
        for (const DdTransform& t : mBodies)
        {
            lowest = std::fmin(lowest, t.position.z);
        }
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "character: %.1f kg | pelvis height %.2f m | lowest body centre %.2f m",
                      static_cast<double>(dragdoll_character_get_mass(mCharacter)), static_cast<double>(body("pelvis").position.z),
                      static_cast<double>(lowest));
        lines.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer), "knee flex  L %5.1f  R %5.1f deg  (must stay >= -3)",
                      static_cast<double>(metrics::hingeBendDegrees(body("thigh_l"), body("shin_l"))),
                      static_cast<double>(metrics::hingeBendDegrees(body("thigh_r"), body("shin_r"))));
        lines.emplace_back(buffer);
        std::snprintf(buffer, sizeof(buffer), "elbow flex L %5.1f  R %5.1f deg  (must stay >= -3)",
                      static_cast<double>(-metrics::hingeBendDegrees(body("upper_arm_l"), body("forearm_l"))),
                      static_cast<double>(-metrics::hingeBendDegrees(body("upper_arm_r"), body("forearm_r"))));
        lines.emplace_back(buffer);
    }

protected:
    const char* mName;
    const char* mDescription;
    DdVec3 mOrigin;
    DdQuat mRotation;
    DdCharacter* mCharacter = nullptr;
    std::vector<DdTransform> mBodies;
};

// Same as a drop, plus a sideways shove on the chest while falling.
class ShoveScenario final : public RagdollScenario
{
public:
    using RagdollScenario::RagdollScenario;
    bool rateIndependent() const override { return false; } // The shove lands on a host frame boundary.

    void hostFrame(DdWorld*, double hostTime, float) override
    {
        if (!mShoved && hostTime >= 0.25 && mCharacter != nullptr)
        {
            const int32_t chest = dragdoll_character_find_body(mCharacter, "upper_spine");
            dragdoll_character_apply_impulse(mCharacter, static_cast<uint32_t>(chest), DdVec3{70.0f, 0.0f, 0.0f}, nullptr);
            mShoved = true;
        }
    }

private:
    bool mShoved = false;
};

using Factory = std::function<std::unique_ptr<Scenario>()>;

const std::vector<std::pair<std::string, Factory>>& registry()
{
    static const std::vector<std::pair<std::string, Factory>> sRegistry = {
        {"pile_zup",
         [] {
             return std::make_unique<PileScenario>(
                 DdVec3{0, 0, 1}, "pile_zup",
                 "A pile of 12 mixed props topples in a Z-up world. Identical at any host frame rate (see the match line).");
         }},
        {"ball_drop_yup",
         [] {
             return std::make_unique<BallDropScenario>(DdVec3{0, 1, 0}, "ball_drop_yup",
                                                       "A ball falls and rests on the ground; world up = +Y.");
         }},
        {"ball_drop_zup",
         [] {
             return std::make_unique<BallDropScenario>(DdVec3{0, 0, 1}, "ball_drop_zup",
                                                       "A ball falls and rests on the ground; world up = +Z (SA).");
         }},
        {"car_vs_pole_50kmh",
         [] {
             return std::make_unique<CarVsPoleScenario>(50.0f, "car_vs_pole_50kmh",
                                                        "A car at 50 km/h hits a thin 1 m pole: it should be knocked away hard, not pass through.");
         }},
        {"car_vs_pole_120kmh",
         [] {
             return std::make_unique<CarVsPoleScenario>(120.0f, "car_vs_pole_120kmh",
                                                        "A car at 120 km/h hits a thin 1 m pole: no tunnelling, it is knocked away violently.");
         }},
        {"car_speed_50kmh", [] { return std::make_unique<CarSpeedScenario>(); }},
        {"drop_from_1m",
         [] {
             return std::make_unique<RagdollScenario>(
                 "drop_from_1m", "A limp 75 kg humanoid (16 bodies) is dropped feet-first from 1 m. Judge: proportions, "
                 "joints bending only the natural way (knees back, elbows forward), a calm landing without jitter.",
                 DdVec3{0.0f, 0.0f, 1.0f}, DdQuat{0, 0, 0, 1});
         }},
        {"fall_forward",
         [] {
             return std::make_unique<RagdollScenario>(
                 "fall_forward", "The limp humanoid starts tipped 60 degrees forward and falls on its face. Judge: does it "
                 "crumple like a person, and do arms and legs stay out of each other and the floor?",
                 DdVec3{0.0f, 0.0f, 0.3f}, axisAngle(DdVec3{1, 0, 0}, 60.0f));
         }},
        {"shove_mid_fall",
         [] {
             return std::make_unique<ShoveScenario>(
                 "shove_mid_fall", "Dropped from 1 m and shoved hard on the chest (70 N*s, sideways) after 0.25 s. Judge: "
                 "the upper body should whip away first and the legs follow.",
                 DdVec3{0.0f, 0.0f, 1.0f}, DdQuat{0, 0, 0, 1});
         }},
    };
    return sRegistry;
}
} // namespace

namespace metrics
{
DdVec3 rotate(const DdQuat& q, const DdVec3& v)
{
    // v' = v + 2w(q x v) + 2 q x (q x v)
    const DdVec3 u{q.x, q.y, q.z};
    const DdVec3 t{2.0f * (u.y * v.z - u.z * v.y), 2.0f * (u.z * v.x - u.x * v.z), 2.0f * (u.x * v.y - u.y * v.x)};
    return DdVec3{v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
                  v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
}

float hingeBendDegrees(const DdTransform& parent, const DdTransform& child)
{
    const DdVec3 a = rotate(parent.rotation, DdVec3{0, 1, 0});     // parent bone direction
    const DdVec3 b = rotate(child.rotation, DdVec3{0, 1, 0});      // child bone direction
    const DdVec3 lateral = rotate(parent.rotation, DdVec3{1, 0, 0}); // parent's local +X (character's left)
    const DdVec3 c{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    return std::atan2(dot(c, lateral), dot(a, b)) * 57.29578f;
}
} // namespace metrics

std::vector<std::string> list()
{
    std::vector<std::string> names;
    for (const auto& entry : registry())
    {
        names.push_back(entry.first);
    }
    return names;
}

std::unique_ptr<Scenario> create(const std::string& name)
{
    for (const auto& entry : registry())
    {
        if (entry.first == name)
        {
            return entry.second();
        }
    }
    return nullptr;
}

Instance::Instance(Instance&& other) noexcept
    : scenario(std::move(other.scenario)), world(other.world), hostTime(other.hostTime)
{
    other.world = nullptr;
}

Instance& Instance::operator=(Instance&& other) noexcept
{
    if (this != &other)
    {
        dragdoll_world_destroy(world);
        scenario = std::move(other.scenario);
        world = other.world;
        hostTime = other.hostTime;
        other.world = nullptr;
    }
    return *this;
}

Instance::~Instance()
{
    dragdoll_world_destroy(world);
}

bool Instance::make(const std::string& name, Instance& out, std::string& error, const char* recordPath)
{
    out = Instance();
    out.scenario = create(name);
    if (!out.scenario)
    {
        error = "unknown scenario: " + name;
        return false;
    }
    DdWorldDesc desc;
    dragdoll_world_desc_default(&desc);
    out.scenario->configure(desc);
    desc.record_path = recordPath;
    if (dragdoll_world_create(&desc, &out.world) != DD_OK)
    {
        error = dragdoll_last_error_message();
        return false;
    }
    out.scenario->build(out.world);
    return true;
}

uint32_t Instance::hostFrame(float dt)
{
    hostTime += static_cast<double>(dt);
    scenario->hostFrame(world, hostTime, dt);
    uint32_t steps = 0;
    dragdoll_world_step(world, dt, &steps);
    return steps;
}

} // namespace dragdoll::scenarios
