// SPDX-License-Identifier: MIT
#include <dragdoll_scenarios/scenario.hpp>
#include <dragdoll_scenarios/vehicles.hpp>
#include <dragdoll_scenarios/crash.hpp>

#include <cmath>
#include <iterator>
#include <random>
#include <cstdio>
#include <cstdlib>
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
DdQuat mul(DdQuat a, DdQuat b)
{
    return DdQuat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                  a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
DdQuat conj(DdQuat q)
{
    return DdQuat{-q.x, -q.y, -q.z, q.w};
}

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
        desc.rig_json = rigJson();
        desc.transform.position = mOrigin;
        desc.transform.rotation = mRotation;
        desc.size_scale = mSizeScale;
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
    float mSizeScale = 1.0f; // The rig's size (1 = the 1.75 m rig)
    DdCharacter* mCharacter = nullptr;
    std::vector<DdTransform> mBodies;
};

// Pedestrians in the vehicle scenarios stand as tall as the people of the game-scale world the vehicle packs are
// measured in (about 1.93 m; ADR-0014, founder decision 2026-11-18).
constexpr float kPedestrianSize = 1.10f;

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

// Muscles (task 3.1): four humanoids dropped side by side from 1 m, each with its own tension profile (set before the
// first step, so the scenario is identical at any host frame rate). Judge: limp crumples; stunned folds slowly and
// wobbles; relaxed sags but keeps some shape; tense lands like a stiff mannequin and topples in one piece (there is no
// balance yet).
class TensionLineupScenario final : public Scenario
{
public:
    const char* name() const override { return "tension_lineup"; }
    const char* description() const override
    {
        return "Muscles: four humanoids dropped from 1 m, left to right LIMP, STUNNED, RELAXED, TENSE. Judge: limp "
               "crumples; stunned folds slowly and wobbles; relaxed sags but keeps some shape; tense lands stiff and "
               "topples in one piece (no balance yet).";
    }
    void configure(DdWorldDesc& desc) const override { desc.up = DdVec3{0.0f, 0.0f, 1.0f}; }
    bool rateIndependent() const override { return true; }
    double duration() const override { return 5.0; }
    DdCharacter* character() const override { return mCharacters[3]; } // The tense one
    DdVec3 cameraTarget() const override { return DdVec3{0.0f, 0.0f, 0.7f}; }
    float cameraDistance() const override { return 7.5f; }

    void build(DdWorld* world) override
    {
        addGround(world, DdVec3{0.0f, 0.0f, 1.0f});
        for (int i = 0; i < 4; ++i)
        {
            DdCharacterDesc desc;
            dragdoll_character_desc_default(&desc);
            desc.rig_json = rigJson();
            desc.transform.position = DdVec3{-2.4f + 1.6f * static_cast<float>(i), 0.0f, 1.0f};
            if (dragdoll_character_create(world, &desc, &mCharacters[i]) != DD_OK)
            {
                continue;
            }
            mRest[i].resize(dragdoll_character_get_body_count(mCharacters[i]));
            dragdoll_character_get_body_transforms(mCharacters[i], 0, mRest[i].data(), static_cast<uint32_t>(mRest[i].size()));
            dragdoll_character_set_tension(mCharacters[i], kOrder[i], 0.0f);
        }
    }

    void hud(DdWorld*, std::vector<std::string>& lines) override
    {
        static const char* const kNames[] = {"limp", "relaxed", "tense", "stunned"};
        for (int i = 0; i < 4; ++i)
        {
            if (mCharacters[i] == nullptr)
            {
                lines.emplace_back("character could not be created");
                continue;
            }
            std::vector<DdTransform> now(mRest[i].size());
            dragdoll_character_get_body_transforms(mCharacters[i], 0, now.data(), static_cast<uint32_t>(now.size()));
            const metrics::PoseError e = metrics::poseError(mCharacters[i], mRest[i], now);
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer), "%-8s pose error vs standing: RMS %5.1f deg, max %5.1f deg (%s) | pelvis height %.2f m",
                          kNames[kOrder[i]], static_cast<double>(e.rms), static_cast<double>(e.max),
                          e.worst >= 0 ? dragdoll_character_get_body_name(mCharacters[i], static_cast<uint32_t>(e.worst)) : "-",
                          static_cast<double>(now[0].position.z));
            lines.emplace_back(buffer);
        }
    }

private:
    static constexpr DdTension kOrder[4] = {DD_TENSION_LIMP, DD_TENSION_STUNNED, DD_TENSION_RELAXED, DD_TENSION_TENSE};
    DdCharacter* mCharacters[4] = {};
    std::vector<DdTransform> mRest[4];
};

// Host contract (task 3.2): a tense humanoid floating in zero gravity is given a new target pose every 2.5 s — rest,
// boxing guard, arms up, tucked — through dragdoll_character_set_target_pose. No gravity, so what you see is pure pose
// tracking. With internal torques only, the whole body may slowly turn as it changes shape (angular momentum is
// conserved, like a diver or a falling cat), which is correct.
class PoseZeroGScenario final : public Scenario
{
public:
    const char* name() const override { return "pose_zero_g"; }
    const char* description() const override
    {
        return "Host pose input: a tense humanoid floats in zero gravity and is sent a new target pose every 2.5 s "
               "(rest, guard, arms up, tuck). Judge: does it reach each pose cleanly, without jitter or overshoot? "
               "Slow whole-body turning while changing shape is correct (no external torques).";
    }
    void configure(DdWorldDesc& desc) const override
    {
        desc.up = DdVec3{0.0f, 0.0f, 1.0f};
        desc.gravity = 0.0f;
    }
    bool rateIndependent() const override { return false; } // Poses change on host frame boundaries
    double duration() const override { return 10.0; }
    DdCharacter* character() const override { return mCharacter; }
    DdVec3 cameraTarget() const override { return DdVec3{0.0f, 0.0f, 1.2f}; }
    float cameraDistance() const override { return 4.5f; }

    void build(DdWorld* world) override
    {
        DdCharacterDesc desc;
        dragdoll_character_desc_default(&desc);
        desc.rig_json = rigJson();
        desc.transform.position = DdVec3{0.0f, 0.0f, 0.3f};
        if (dragdoll_character_create(world, &desc, &mCharacter) == DD_OK)
        {
            dragdoll_character_set_tension(mCharacter, DD_TENSION_TENSE, 0.0f);
        }
    }

    void hostFrame(DdWorld*, double hostTime, float) override
    {
        const int pose = std::min(3, static_cast<int>(hostTime / 2.5));
        if (mCharacter == nullptr || pose == mPose)
        {
            return;
        }
        mPose = pose;
        mTarget.assign(dragdoll_character_get_body_count(mCharacter), DdQuat{0, 0, 0, 1});
        // Joint rotations as offsets from rest in rig axes (+X left, +Y up, +Z forward); flexion of hip/shoulder
        // forward = negative about +X, knee = positive about +X, elbow = positive about -X, spine forward = +X.
        auto set = [&](const char* body, DdVec3 axis, float degrees) {
            const int32_t i = dragdoll_character_find_body(mCharacter, body);
            if (i >= 0) mTarget[static_cast<size_t>(i)] = axisAngle(axis, degrees);
        };
        const DdVec3 x{1, 0, 0}, minusX{-1, 0, 0};
        if (pose == 1) // Boxing guard
        {
            for (const char* side : {"_l", "_r"})
            {
                set((std::string("upper_arm") + side).c_str(), x, -50.0f);
                set((std::string("forearm") + side).c_str(), minusX, 115.0f);
                set((std::string("thigh") + side).c_str(), x, -15.0f);
                set((std::string("shin") + side).c_str(), x, 25.0f);
            }
            set("head", x, 12.0f);
        }
        else if (pose == 2) // Arms up
        {
            for (const char* side : {"_l", "_r"})
            {
                set((std::string("upper_arm") + side).c_str(), x, -160.0f); // Overhead
                set((std::string("forearm") + side).c_str(), minusX, 10.0f);
            }
            set("lower_spine", x, -10.0f);
            set("head", x, -15.0f);
        }
        else if (pose == 3) // Tuck
        {
            for (const char* side : {"_l", "_r"})
            {
                set((std::string("thigh") + side).c_str(), x, -65.0f);
                set((std::string("shin") + side).c_str(), x, 120.0f);
                set((std::string("upper_arm") + side).c_str(), x, -40.0f);
                set((std::string("forearm") + side).c_str(), minusX, 100.0f);
            }
            set("lower_spine", x, 30.0f);
            set("upper_spine", x, 20.0f);
            set("head", x, 30.0f);
        }
        dragdoll_character_set_target_pose(mCharacter, mTarget.data(), static_cast<uint32_t>(mTarget.size()));
    }

    void hud(DdWorld*, std::vector<std::string>& lines) override
    {
        static const char* const kPoses[] = {"rest", "boxing guard", "arms up", "tuck"};
        if (mCharacter == nullptr || mTarget.empty())
        {
            return;
        }
        std::vector<DdQuat> now(mTarget.size());
        dragdoll_character_get_pose(mCharacter, 0, now.data(), static_cast<uint32_t>(now.size()), nullptr);
        double sum = 0.0;
        float worst = 0.0f;
        for (size_t i = 1; i < now.size(); ++i)
        {
            const DdQuat &a = now[i], &b = mTarget[i];
            const float e = 2.0f * std::acos(std::fmin(1.0f, std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w))) * 57.29578f;
            sum += static_cast<double>(e) * e;
            worst = std::fmax(worst, e);
        }
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "target pose: %s | tracking error RMS %.1f deg, worst %.1f deg",
                      kPoses[std::max(0, mPose)], std::sqrt(sum / static_cast<double>(now.size() - 1)), static_cast<double>(worst));
        lines.emplace_back(buffer);
    }

private:
    DdCharacter* mCharacter = nullptr;
    std::vector<DdQuat> mTarget;
    int mPose = -1;
};

// Activation from motion (task 3.3, ADR-0017): two humanoids are "animated" by the host as a simple walk for 1 s
// (each host frame hands DragDoll the new pose plus the previous one, so physics follows like a puppet), then physics
// takes over, limp. LEFT: handed over with its momentum (activate with the previous pose). RIGHT: handed over without
// velocity, the classic freeze-then-drop.
class WalkThenActivateScenario final : public Scenario
{
public:
    const char* name() const override { return "walk_then_activate"; }
    const char* description() const override
    {
        return "Activation from motion: two walkers are animated for 1 s, then go limp. LEFT keeps its walking momentum "
               "(new way: two consecutive poses), RIGHT is handed over without velocity (classic freeze-then-drop). "
               "Judge: the left one should stumble forward naturally; the right one stops dead, then collapses.";
    }
    void configure(DdWorldDesc& desc) const override { desc.up = DdVec3{0.0f, 0.0f, 1.0f}; }
    bool rateIndependent() const override { return false; } // The host animation is sampled per host frame
    double duration() const override { return 4.0; }
    DdCharacter* character() const override { return mWalkers[0]; }
    DdVec3 cameraTarget() const override { return DdVec3{0.0f, -1.5f, 0.8f}; }
    float cameraDistance() const override { return 7.0f; }

    void build(DdWorld* world) override
    {
        addGround(world, DdVec3{0.0f, 0.0f, 1.0f});
        for (int i = 0; i < 2; ++i)
        {
            DdCharacterDesc desc;
            dragdoll_character_desc_default(&desc);
            desc.rig_json = rigJson();
            desc.transform.position = DdVec3{i == 0 ? 0.8f : -0.8f, 0.0f, 0.01f};
            if (dragdoll_character_create(world, &desc, &mWalkers[i]) != DD_OK)
            {
                return;
            }
            dragdoll_character_get_pose(mWalkers[i], 0, nullptr, 0, &mRestRoot[i]);
        }
        const DdQuat& q = mRestRoot[0].rotation; // Forward = rig +Z in world space
        mForward = DdVec3{2.0f * (q.x * q.z + q.w * q.y), 2.0f * (q.y * q.z - q.w * q.x), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)};
        mCount = dragdoll_character_get_body_count(mWalkers[0]);
    }

    void hostFrame(DdWorld*, double hostTime, float dt) override
    {
        if (mWalkers[1] == nullptr || mReleased)
        {
            return;
        }
        DdTransform before, now;
        std::vector<DdQuat> jointsBefore, jointsNow;
        for (int i = 0; i < 2; ++i)
        {
            walkPose(i, hostTime - std::max(dt, 1e-3f), before, jointsBefore);
            walkPose(i, hostTime, now, jointsNow);
            const bool release = hostTime >= kRelease;
            const bool withMomentum = !release || i == 0; // The right walker loses its velocity at the handover
            dragdoll_character_activate(mWalkers[i], withMomentum ? &before : nullptr, withMomentum ? jointsBefore.data() : nullptr,
                                        &now, jointsNow.data(), mCount, std::max(dt, 1e-3f));
        }
        mReleased = hostTime >= kRelease;
    }

    void hud(DdWorld*, std::vector<std::string>& lines) override
    {
        for (int i = 0; i < 2; ++i)
        {
            if (mWalkers[i] == nullptr)
            {
                lines.emplace_back("character could not be created");
                return;
            }
            std::vector<DdTransform> t(mCount);
            dragdoll_character_get_body_transforms(mWalkers[i], 0, t.data(), mCount);
            DdTransform pelvis = t[0];
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer), "%s: pelvis %.2f m along the walk, height %.2f m | %s",
                          i == 0 ? "LEFT  (with momentum)" : "RIGHT (no velocity) ",
                          static_cast<double>(dot(DdVec3{pelvis.position.x - mRestRoot[i].position.x, pelvis.position.y - mRestRoot[i].position.y, 0.0f}, mForward)),
                          static_cast<double>(pelvis.position.z), mReleased ? "physics" : "animated by the host");
            lines.emplace_back(buffer);
        }
    }

private:
    static constexpr double kRelease = 1.0;
    // Host-side walk: root along forward at 1.4 m/s, legs and arms swinging at 0.9 Hz.
    void walkPose(int i, double t, DdTransform& root, std::vector<DdQuat>& joints) const
    {
        constexpr double kSpeed = 1.4, kStride = 2.0 * 3.14159265 * 0.9;
        root = mRestRoot[i];
        root.position.x += mForward.x * static_cast<float>(kSpeed * t);
        root.position.y += mForward.y * static_cast<float>(kSpeed * t);
        joints.assign(mCount, DdQuat{0, 0, 0, 1});
        const char* thighs[] = {"thigh_l", "thigh_r"};
        const char* shins[] = {"shin_l", "shin_r"};
        const char* arms[] = {"upper_arm_l", "upper_arm_r"};
        for (int side = 0; side < 2; ++side)
        {
            const float phase = static_cast<float>(std::sin(kStride * t + side * 3.14159265));
            joints[static_cast<size_t>(dragdoll_character_find_body(mWalkers[i], thighs[side]))] = axisAngle(DdVec3{1, 0, 0}, -25.0f * phase);
            joints[static_cast<size_t>(dragdoll_character_find_body(mWalkers[i], shins[side]))] = axisAngle(DdVec3{1, 0, 0}, 20.0f + 20.0f * phase);
            joints[static_cast<size_t>(dragdoll_character_find_body(mWalkers[i], arms[side]))] = axisAngle(DdVec3{1, 0, 0}, 20.0f * phase);
        }
    }

    DdCharacter* mWalkers[2] = {};
    DdTransform mRestRoot[2] = {};
    DdVec3 mForward{0, 0, 1};
    uint32_t mCount = 0;
    bool mReleased = false;
};

// Standing balance (task 3.6, ADR-0023): a tense humanoid stands with balance on and is poked on the chest with
// 5 N*s in a random horizontal direction every 2 s (seeded, ADR-0007). Judge: it sways and recovers with its ankles
// (and hips for bigger pushes) without stepping; it should stay upright.
class HoldStanceScenario final : public Scenario
{
public:
    HoldStanceScenario(const char* name, float poke, double seconds) : mName(name), mPoke(poke), mSeconds(seconds) {}
    const char* name() const override { return mName; }
    const char* description() const override
    {
        return mPoke > 0.0f ? "Standing balance: a tense humanoid with balance on is poked on the chest (5 N*s, random "
                              "direction) every 2 s. Judge: it sways and recovers with ankles and hips, no stepping, stays upright."
                            : "Standing balance: a tense humanoid with balance on just stands. Judge: calm, upright, no jitter.";
    }
    void configure(DdWorldDesc& desc) const override { desc.up = DdVec3{0.0f, 0.0f, 1.0f}; }
    bool rateIndependent() const override { return mPoke == 0.0f; } // Pokes land on host frame boundaries
    double duration() const override { return mSeconds; }
    DdCharacter* character() const override { return mCharacter; }
    DdVec3 cameraTarget() const override { return DdVec3{0.0f, 0.0f, 0.9f}; }
    float cameraDistance() const override { return 4.5f; }

    void build(DdWorld* world) override
    {
        addGround(world, DdVec3{0.0f, 0.0f, 1.0f});
        DdCharacterDesc desc;
        dragdoll_character_desc_default(&desc);
        desc.rig_json = rigJson();
        desc.transform.position = DdVec3{0.0f, 0.0f, 0.005f};
        if (dragdoll_character_create(world, &desc, &mCharacter) == DD_OK)
        {
            dragdoll_character_set_tension(mCharacter, DD_TENSION_TENSE, 0.0f);
            dragdoll_character_set_balance(mCharacter, 1);
        }
    }

    void hostFrame(DdWorld*, double hostTime, float) override
    {
        if (mCharacter == nullptr || mPoke <= 0.0f)
        {
            return;
        }
        const int poke = static_cast<int>(hostTime / 2.0);
        DdTransform root{};
        dragdoll_character_get_pose(mCharacter, 0, nullptr, 0, &root);
        if (poke > mPokes && root.position.z > 0.75f) // Only poke someone who is still standing
        {
            mPokes = poke;
            std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);
            const float a = angle(mRng);
            const int32_t chest = dragdoll_character_find_body(mCharacter, "upper_spine");
            mLastPoke = DdVec3{std::cos(a) * mPoke, std::sin(a) * mPoke, 0.0f};
            dragdoll_character_apply_impulse(mCharacter, static_cast<uint32_t>(chest), mLastPoke, nullptr);
        }
    }

    void hud(DdWorld*, std::vector<std::string>& lines) override
    {
        if (mCharacter == nullptr)
        {
            return;
        }
        DdCharacterInfo info{};
        info.struct_size = sizeof(DdCharacterInfo);
        dragdoll_character_get_info(mCharacter, &info);
        DdTransform root{};
        dragdoll_character_get_pose(mCharacter, 0, nullptr, 0, &root);
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%s | pelvis height %.2f m | capture point %.1f cm from the feet centre | pokes %d",
                      root.position.z > 0.75f ? "UPRIGHT" : "FALLEN", static_cast<double>(root.position.z),
                      static_cast<double>(info.balance_error * 100.0f), mPokes);
        lines.emplace_back(buffer);
    }

private:
    const char* mName;
    float mPoke;
    double mSeconds;
    DdCharacter* mCharacter = nullptr;
    std::mt19937 mRng{23};
    int mPokes = 0;
    DdVec3 mLastPoke{0, 0, 0};
};

// A vehicle from the shape pack hits a standing limp ragdoll side-on and brakes on contact (task 2.6). Without muscles
// the body only starts to sag in the 0.1 s before contact; what matters here is how each front shape catches it:
// low fronts scoop it onto the bonnet (wrap), high flat fronts throw it ahead (forward projection).
class VehicleHitScenario final : public RagdollScenario
{
public:
    // braced: the pedestrian stands with tense muscles and balance on (task 3.6), and the vehicle arrives after 0.5 s.
    // aware: braced, the vehicle arrives after 1.6 s, and the "car_hit" behaviour reacts to it (ADR-0025).
    // react: as in a game - the pedestrian stands host-animated (kinematic) and unaware, the "car_hit" behaviour runs,
    // and the core hands it over to physics at contact (auto handover; task 4.6).
    VehicleHitScenario(const std::string& vehicle, float kmh, bool braced = false, const char* name = nullptr, bool aware = false,
                       bool react = false)
        : RagdollScenario(nullptr, nullptr, DdVec3{0.0f, 0.0f, braced ? 0.005f : 0.01f}, DdQuat{0, 0, 0, 1}), mVehicle(vehicle),
          mBraced(braced), mAware(aware), mReact(react), mContactTime(aware ? 1.6f : react ? 1.0f : braced ? 0.5f : 0.1f)
    {
        mNameText = name != nullptr ? name : std::string(react ? "react_" : "hit_") + vehicle + "_" + std::to_string(static_cast<int>(kmh)) + "kmh";
        mProfile = parseVehicle(builtinVehicle(vehicle));
        mDescriptionText = "A " + vehicle + " at " + std::to_string(static_cast<int>(kmh)) + " km/h " +
                           (react    ? "hits a pedestrian standing host-animated who never sees it; the car_hit reactions run and the "
                                       "body is handed over to physics at contact. Judge: at contact the struck side gives, ~0.1 s later "
                                       "the trunk and neck stiffen; lying on the car the chin tucks, the forearms come between head and "
                                       "car and the body curls a little; once off the car it goes loose (landing comes in task 4.7). "
                            : aware    ? "comes at a standing pedestrian who sees it 1.6 s ahead (behaviour car_hit). Judge: the body "
                                       "tenses about 0.1 s after the car gets within 1.2 s (startle), and half a second "
                                       "later both hands go up towards the car (shield), before the impact."
                            : braced ? "nudges a standing pedestrian (tense, balance on) side-on and brakes. Judge: absorbed "
                                       "with ankles and hips, no fall."
                                   : "hits a standing limp pedestrian side-on and brakes hard on contact. ") +
                           (braced ? std::string() : mProfile.description);
        mName = mNameText.c_str();
        mDescription = mDescriptionText.c_str();
        mSizeScale = kPedestrianSize;
        mDrive.speed = kmh / 3.6f;
        mDrive.brakeAt = mContactTime;
        mDrive.decel = 7.0f; // Emergency braking on a dry road
        // The front face reaches the pedestrian's near side (~0.25 m from the centre) at the contact time.
        mStart = -0.25f * kPedestrianSize - mProfile.length * 0.5f - mDrive.speed * mContactTime;
    }
    DdVec3 cameraTarget() const override { return DdVec3{1.5f, 0.0f, 0.8f}; }
    float cameraDistance() const override { return mProfile.length > 8.0f ? 16.0f : 9.0f; }
    // Braking is a curved path, and kinematic targets are interpolated linearly between host samples, so the exact
    // path depends (by millimetres) on the host frame rate — exactly as in the game. Constant-speed driving stays
    // rate-independent (car_vs_pole, car_speed).
    bool rateIndependent() const override { return false; }

    void build(DdWorld* world) override
    {
        RagdollScenario::build(world);
        if (mBraced && mCharacter != nullptr)
        {
            dragdoll_character_set_tension(mCharacter, DD_TENSION_TENSE, 0.0f);
            dragdoll_character_set_balance(mCharacter, 1);
        }
        if (mReact && mCharacter != nullptr)
        {
            dragdoll_character_set_kinematic(mCharacter, 1); // Host-animated standing (rest pose) until the handover
            dragdoll_character_set_tension(mCharacter, DD_TENSION_RELAXED, 0.0f);
            dragdoll_character_set_behaviour(mCharacter, "car_hit");
            dragdoll_character_set_awareness(mCharacter, 0.0f); // Never sees it
            dragdoll_character_set_auto_handover(mCharacter, 0.0125f);
        }
        if (mAware && mCharacter != nullptr)
        {
            dragdoll_character_set_tension(mCharacter, DD_TENSION_RELAXED, 0.0f); // Standing at ease until it notices
            dragdoll_character_set_behaviour(mCharacter, "car_hit");
        }
        mMesh = buildVehicleMesh(mProfile, DdVec3{1, 0, 0}, DdVec3{0, 1, 0}, DdVec3{0, 0, 1}); // Z-up, drives along +X
        DdColliderDesc car;
        dragdoll_collider_desc_default(&car);
        car.shape = DD_SHAPE_MESH;
        car.motion = DD_MOTION_KINEMATIC;
        car.vertices = mMesh.vertices.data();
        car.vertex_count = static_cast<uint32_t>(mMesh.vertices.size());
        car.indices = mMesh.indices.data();
        car.index_count = static_cast<uint32_t>(mMesh.indices.size());
        car.transform.position = DdVec3{mStart, 0.0f, 0.0f};
        car.group = 1; // One vehicle = one perceived object (ADR-0024)
        mCar = addOne(world, car);
    }

    void hostFrame(DdWorld* world, double hostTime, float) override
    {
        const DdTransform target{{mStart + mDrive.distance(std::fmin(hostTime, duration())), 0.0f, 0.0f}, {0, 0, 0, 1}};
        dragdoll_world_set_kinematic_targets(world, &mCar, &target, 1);
    }

    void hud(DdWorld* world, std::vector<std::string>& lines) override
    {
        RagdollScenario::hud(world, lines);
        const double t = static_cast<double>(dragdoll_world_get_step_count(world)) / 120.0;
        const double v = t <= mDrive.brakeAt ? mDrive.speed : std::fmax(0.0, mDrive.speed - mDrive.decel * (t - mDrive.brakeAt));
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "%s: bumper %.2f m, bonnet edge %.2f m | speed now %.1f km/h (braking from %.2f s)",
                      mVehicle.c_str(), static_cast<double>(mProfile.bumperHeight), static_cast<double>(mProfile.bonnetEdgeHeight),
                      v * 3.6, static_cast<double>(mDrive.brakeAt));
        lines.emplace_back(buffer);
    }

private:
    bool mBraced = false, mAware = false, mReact = false;
    float mContactTime = 0.1f;
    std::string mVehicle, mNameText, mDescriptionText;
    VehicleProfile mProfile;
    VehicleMesh mMesh;
    Drive mDrive;
    float mStart = 0.0f;
    DdColliderId mCar = DD_INVALID_COLLIDER;
};

// A standing pedestrian is shoved backwards hard enough to fall (task 4.7, the cheap landing test from ideas.md).
// reflexes: relaxed with the "car_hit" chain, which treats any fall as a stimulus - after the reflex latency the hands
// reach for the ground and give on contact. Otherwise limp: the passive fall to compare with.
class DeathDropScenario final : public RagdollScenario
{
public:
    explicit DeathDropScenario(bool reflexes)
        : RagdollScenario(reflexes ? "death_drop" : "death_drop_limp",
                          reflexes ? "A standing, relaxed pedestrian is shoved backwards and falls (car_hit reflexes on). Judge: about "
                                     "0.1 s into the fall the arms go out towards the ground behind, the chin tucks; the hands meet "
                                     "the ground first and give, the head does not bang down."
                                   : "The same shove, limp: the passive fall to compare death_drop with.",
                          DdVec3{0.0f, 0.0f, 0.005f}, DdQuat{0, 0, 0, 1}),
          mReflexes(reflexes)
    {
    }
    void build(DdWorld* world) override
    {
        RagdollScenario::build(world);
        DdCharacter* c = character();
        if (c == nullptr) return;
        if (mReflexes)
        {
            dragdoll_character_set_tension(c, DD_TENSION_RELAXED, 0.0f);
            dragdoll_character_set_behaviour(c, "car_hit");
        }
        // Backwards = opposite to where the feet point (found from the pose, not assumed).
        std::vector<DdTransform> b(dragdoll_character_get_body_count(c));
        dragdoll_character_get_body_transforms(c, 0, b.data(), static_cast<uint32_t>(b.size()));
        const int foot = dragdoll_character_find_body(c, "foot_l"), shin = dragdoll_character_find_body(c, "shin_l");
        const int chest = dragdoll_character_find_body(c, "upper_spine");
        if (foot < 0 || shin < 0 || chest < 0) return;
        float fx = b[static_cast<size_t>(foot)].position.x - b[static_cast<size_t>(shin)].position.x;
        float fy = b[static_cast<size_t>(foot)].position.y - b[static_cast<size_t>(shin)].position.y;
        const float length = std::sqrt(fx * fx + fy * fy);
        if (length < 1e-4f) return;
        fx /= length;
        fy /= length;
        dragdoll_character_apply_impulse(c, static_cast<uint32_t>(chest), DdVec3{-fx * 30.0f, -fy * 30.0f, 0.0f}, nullptr);
    }

private:
    bool mReflexes = true;
};

// Starting states' key poses: our own rounded numbers, averaged over many measurements of how pedestrians react in a
// game-scale world (ADR-0014, founder decisions 2026-11-19), in the vehicle's frame (founder's drawing): whatever way
// it faced, within 0.2 s the pedestrian turns to a facing set by the vehicle (k*Facing: degrees from the vehicle's
// travel, + = away from its path - the dodge faces the path and steps back from it, the dive faces away and dives,
// hands up faces the vehicle), then moves: `move` = away from the path, up (the root's rise), along the vehicle's
// travel, in metres (rounded to 0.05). The body: the root's rotation relative to that facing, and the joints, as
// rotation vectors in degrees about the rig's axes (rounded to 5). Bodies in rig order.
struct StartKey
{
    float t;
    float move[3]; // away from the vehicle's path, up, along its travel
    int turn[3];
    int joints[16][3];
};
// Dodge: 28 measurements; faces -80 deg from the vehicle's travel (+ = away from its path); triggered 1.14 s before the vehicle arrives (n=28)
constexpr float kStepFacing = -80.0f;
const StartKey kStepKeys[] = {
    {0.00f, {0.00f, 0.00f, 0.00f}, {0, 0, 0}, {{0, 0, 0}, {5, 0, 0}, {5, 5, 0}, {5, 0, 0}, {10, -35, 25}, {-40, 0, 0}, {15, 5, 10}, {0, 15, -20}, {-35, 0, 0}, {10, 5, 0}, {-20, 10, -5}, {25, 0, 0}, {10, 0, 0}, {-5, -5, 0}, {35, 0, 0}, {5, 0, 0}}},
    {0.20f, {0.00f, 0.05f, 0.00f}, {0, 5, -5}, {{0, 0, 0}, {0, 5, 5}, {5, 0, 5}, {10, 0, -5}, {-25, -20, 15}, {-25, 0, 0}, {5, -5, 5}, {5, 10, -25}, {-20, 0, 0}, {15, 0, 5}, {-10, 20, 10}, {30, 0, 0}, {5, 10, 0}, {-20, -5, 5}, {30, 0, 0}, {15, -5, 5}}},
    {0.40f, {0.35f, 0.10f, 0.00f}, {-5, 0, 0}, {{0, 0, 0}, {15, 20, 5}, {15, -5, 0}, {15, 0, -5}, {-75, -15, 45}, {-45, 0, 0}, {5, 10, 5}, {-25, -5, -60}, {-90, 0, 0}, {10, 20, 0}, {0, 25, 10}, {35, 0, 0}, {15, -5, 5}, {-35, -5, -15}, {40, 0, 0}, {30, -10, 5}}},
    {0.60f, {0.80f, 0.05f, -0.05f}, {5, -10, 0}, {{0, 0, 0}, {25, 10, 5}, {10, 0, 0}, {10, 0, -10}, {-40, -10, 40}, {-50, 0, 0}, {-10, 10, 10}, {-15, 20, -55}, {-65, 0, 0}, {5, 0, 5}, {-25, 30, 10}, {35, 0, 0}, {-5, -5, 0}, {-30, -5, -15}, {60, 0, 0}, {5, 0, 15}}},
    {0.75f, {1.15f, 0.05f, -0.05f}, {10, -30, -5}, {{0, 0, 0}, {10, 10, 5}, {0, 0, 0}, {10, 0, -10}, {20, -15, 25}, {-35, 0, 0}, {0, 5, 10}, {20, 40, -35}, {-45, 0, 0}, {5, -10, 10}, {-40, 20, 20}, {30, 0, 0}, {10, 5, -5}, {-20, -5, 0}, {45, 0, 0}, {-5, 0, 10}}},
    {0.90f, {1.30f, 0.00f, -0.10f}, {10, -35, -5}, {{0, 0, 0}, {5, 15, 0}, {0, 5, 0}, {10, 0, -10}, {25, -25, 30}, {-35, 0, 0}, {10, 5, 5}, {35, 40, -20}, {-40, 0, 0}, {10, -10, 10}, {-40, 20, 25}, {25, 0, 0}, {15, 10, -5}, {-25, -10, 5}, {45, 0, 0}, {-10, 5, 5}}},
};
// Dive: 8 measurements; faces +85 deg from the vehicle's travel (+ = away from its path); triggered 1.01 s before the vehicle arrives (n=8)
constexpr float kDiveFacing = 85.0f;
const StartKey kDiveKeys[] = {
    {0.00f, {0.00f, 0.00f, 0.00f}, {0, 0, 0}, {{0, 0, 0}, {5, 0, 0}, {0, 0, 0}, {5, 0, -5}, {15, -40, 20}, {-50, 0, 0}, {25, 10, 30}, {15, 5, -15}, {-35, 0, 0}, {10, 25, 0}, {0, 0, -5}, {15, 0, 0}, {5, 0, 5}, {-20, -10, 0}, {35, 0, 0}, {20, -5, 0}}},
    {0.20f, {0.00f, 0.05f, 0.00f}, {0, -10, -10}, {{0, 0, 0}, {-5, 0, 5}, {5, 0, 5}, {10, -5, 0}, {-10, -20, 10}, {-15, 0, 0}, {5, -5, 5}, {5, 15, -10}, {-5, 0, 0}, {20, -5, 5}, {-15, 15, 5}, {30, 0, 0}, {5, 10, -5}, {-15, -10, 0}, {30, 0, 0}, {15, -5, 5}}},
    {0.40f, {0.30f, -0.05f, 0.05f}, {25, -30, 0}, {{0, 0, 0}, {-15, -15, 0}, {30, -15, -10}, {-5, 5, 5}, {-55, -20, 80}, {-60, 0, 0}, {5, 10, 15}, {5, 5, -25}, {-35, 0, 0}, {15, 5, 0}, {-35, 10, 5}, {60, 0, 0}, {25, 5, -20}, {-60, -20, 0}, {70, 0, 0}, {-5, -5, 5}}},
    {0.60f, {0.80f, -0.05f, 0.05f}, {25, -5, 5}, {{0, 0, 0}, {-5, -5, 0}, {15, 10, -10}, {10, 35, 5}, {-20, -25, 45}, {-50, 0, 0}, {5, 5, 5}, {40, 10, -15}, {-60, 0, 0}, {15, 5, 0}, {-60, 15, 5}, {85, 0, 0}, {5, -5, -5}, {-55, -20, 5}, {50, 0, 0}, {-5, -20, -5}}},
    {0.80f, {1.70f, -0.05f, 0.20f}, {30, -15, 10}, {{0, 0, 0}, {5, 0, 0}, {10, 10, -10}, {5, 10, 0}, {5, -10, 50}, {-75, 0, 0}, {0, 5, 5}, {5, -5, -30}, {-100, 0, 0}, {15, 5, -5}, {-35, 30, 5}, {65, 0, 0}, {20, -5, 0}, {-35, -15, -10}, {55, 0, 0}, {20, 5, -5}}},
    {1.00f, {2.70f, 0.15f, 0.30f}, {45, -20, 20}, {{0, 0, 0}, {10, 0, 0}, {10, 0, 0}, {10, 10, 0}, {-40, 20, 70}, {-60, 0, 0}, {5, 10, 10}, {-55, -5, -50}, {-70, 0, 0}, {5, 5, -20}, {-35, 20, 5}, {70, 0, 0}, {25, -10, 0}, {-20, -15, -25}, {45, 0, 0}, {25, 0, 5}}},
    {1.30f, {4.40f, -0.10f, 0.75f}, {65, -20, 15}, {{0, 0, 0}, {5, -5, 0}, {20, -10, -5}, {10, 15, 0}, {-55, 0, 75}, {-60, 0, 0}, {5, 0, 0}, {-30, -5, -55}, {-65, 0, 0}, {0, 5, -5}, {-55, 20, 10}, {80, 0, 0}, {15, -5, 0}, {-45, -10, -10}, {60, 0, 0}, {0, 0, 0}}},
    {1.60f, {5.00f, -0.75f, 0.85f}, {90, 0, 0}, {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {-10, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}, // landing: our own key - face down along the dive, lying straight
};
// Hands up: 1 measurement; faces -180 deg from the vehicle's travel (+ = away from its path); triggered 1.18 s before the vehicle arrives (n=1)
constexpr float kHandsUpFacing = -180.0f;
const StartKey kHandsUpKeys[] = {
    {0.00f, {0.00f, 0.00f, 0.00f}, {0, 0, 0}, {{0, 0, 0}, {5, 5, -5}, {5, 5, -5}, {0, -5, 5}, {15, -20, 20}, {-20, 0, 0}, {5, -20, -5}, {-20, 35, -20}, {-25, 0, 0}, {30, 5, 5}, {-20, 20, 0}, {20, 0, 0}, {5, 0, 5}, {0, -5, 0}, {55, 0, 0}, {10, 0, 15}}},
    {0.20f, {0.00f, 0.00f, -0.05f}, {0, -15, -5}, {{0, 0, 0}, {5, 10, 5}, {0, 0, 0}, {5, 0, -10}, {-30, 20, 40}, {-70, 0, 0}, {5, -25, -15}, {-20, 5, -40}, {-60, 0, 0}, {10, 30, 30}, {-25, 30, 10}, {30, 0, 0}, {-5, -5, 5}, {0, -10, -5}, {35, 0, 0}, {5, 0, 10}}},
    {0.40f, {0.00f, 0.00f, 0.35f}, {5, 5, 0}, {{0, 0, 0}, {0, 0, -5}, {-10, -5, 5}, {10, 0, 0}, {-40, 70, 60}, {-115, 0, 0}, {10, 15, 5}, {-40, -20, -60}, {-90, 0, 0}, {10, 15, -10}, {-35, 25, 5}, {40, 0, 0}, {10, -5, 0}, {-10, -15, 5}, {15, 0, 0}, {-5, 5, -5}}},
    {0.60f, {0.00f, 0.00f, 0.35f}, {5, 5, 0}, {{0, 0, 0}, {0, 0, 0}, {-10, -5, 5}, {10, 0, 0}, {-40, 65, 60}, {-110, 0, 0}, {10, 10, 0}, {-45, -30, -65}, {-95, 0, 0}, {5, 5, -25}, {-15, 15, 0}, {20, 0, 0}, {-5, 0, 0}, {-10, -10, 0}, {15, 0, 0}, {-5, 5, -5}}},
    {0.90f, {0.00f, 0.00f, 0.35f}, {5, 5, 0}, {{0, 0, 0}, {0, 0, 0}, {-10, -5, 5}, {10, 0, 0}, {-40, 65, 60}, {-110, 0, 0}, {10, 10, 0}, {-45, -30, -65}, {-95, 0, 0}, {5, 5, -25}, {-15, 15, 0}, {20, 0, 0}, {-5, 0, 0}, {-10, -10, 0}, {15, 0, 0}, {-5, 5, -5}}},
};

DdQuat fromRotationVector(const int v[3])
{
    const float x = static_cast<float>(v[0]), y = static_cast<float>(v[1]), z = static_cast<float>(v[2]);
    const float degrees = std::sqrt(x * x + y * y + z * z);
    if (degrees < 1e-4f) return DdQuat{0, 0, 0, 1};
    return axisAngle(DdVec3{x / degrees, y / degrees, z / degrees}, degrees);
}

DdQuat nlerp(DdQuat a, DdQuat b, float k)
{
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) b = DdQuat{-b.x, -b.y, -b.z, -b.w};
    DdQuat q{a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, a.w + (b.w - a.w) * k};
    const float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return DdQuat{q.x / n, q.y / n, q.z / n, q.w / n};
}

// The crash sandbox (crash.hpp): one vehicle hits one pedestrian, every input a setting. Game mode is what a game
// host does: the pedestrian stands host-animated (kinematic) with the car_hit behaviour and is handed over to physics
// at the contact; limp mode is the passive reference. The vehicle keeps going, or brakes a set time after the first
// contact. Everything the analyzer needs is recorded every host frame.
class CrashScenario final : public Scenario
{
public:
    explicit CrashScenario(const CrashParams& p) : mParams(p)
    {
        mProfile = parseVehicle(builtinVehicle(p.vehicle)); // The name was checked by parseCrashName
        mNameText = crashName(p);
        char text[512];
        std::snprintf(text, sizeof(text),
                      "Crash sandbox: a %s at %.0f km/h %s a %.2f m pedestrian (%s%s), facing %.0f deg from the vehicle "
                      "(0 = facing it, 90 = struck on the right side), %.2f m off its centre line. %s",
                      p.vehicle.c_str(), static_cast<double>(p.kmh),
                      p.brake ? "brakes after the contact and hits" : "keeps going and hits", 1.75 * static_cast<double>(p.size),
                      p.limp ? "limp" : "game: host-animated, car_hit", p.limp ? "" : (p.aware ? ", sees it coming" : ", never sees it"),
                      static_cast<double>(p.facing), static_cast<double>(p.offset), mProfile.description.c_str());
        mDescriptionText = text;
        mSpeed = p.kmh / 3.6f;
        mDecel = p.decel > 0.0f ? p.decel : mProfile.brakeDecel;
        // Limp bodies cannot stand long; DragDoll's own anticipation needs > 1.2 s. With a game reaction the pedestrian
        // first stands normally while the vehicle approaches (1.5 s), then reacts when the vehicle is the reaction's lead
        // away - as the game decides to react to an approaching vehicle (founder, 2026-11-19).
        mContactTime = p.limp ? 0.1f : p.start == "run" ? 1.5f : p.start != "none" ? 1.5f + startLead(p) : p.aware ? 1.6f : 1.0f;
        // The front reaches the pedestrian's near side (~0.25 m x size from its centre) at the contact time.
        mX = -0.25f * p.size - mProfile.length * 0.5f - mSpeed * mContactTime;
        mObs.vehicle = p.vehicle;
        mObs.kmh = p.kmh;
        mObs.limp = p.limp;
        mObs.aware = p.aware;
        mObs.start = p.start;
        mObs.brake = p.brake;
        mObs.facing = p.facing;
        mObs.offset = p.offset;
        mObs.stature = 1.75f * p.size;
        mObs.bonnetEdgeHeight = mProfile.bonnetEdgeHeight;
        mObs.vehicleHalfWidth = mProfile.width * 0.5f;
        // Where the pedestrian is sideways when the vehicle arrives: a step or a dive can carry it clear of a narrow
        // vehicle (a motorbike) - an escape, not a missed contact.
        // Where a step / dive has taken it (across the path, away from the centre) when the vehicle arrives.
        // (A runner passes the impact point as the vehicle arrives.)
        const float fled = p.start == "step" || p.start == "dive" ? keyAside(p.start, startLead(p)) : 0.0f;
        const float aside = p.limp ? 0.0f : (p.side == "right" ? -1.0f : 1.0f) * fled;
        mObs.contactExpected = std::fabs(p.offset + aside) < mProfile.width * 0.5f + 0.1f;
        mObs.report.struct_size = sizeof(DdImpactReport);
    }
    const char* name() const override { return mNameText.c_str(); }
    const char* description() const override { return mDescriptionText.c_str(); }
    void configure(DdWorldDesc& desc) const override { desc.up = DdVec3{0.0f, 0.0f, 1.0f}; } // Z-up, as SA
    double duration() const override { return mContactTime + (mParams.brake ? 5.0 : 6.0); }
    DdCharacter* character() const override { return mCharacter; }
    DdVec3 cameraTarget() const override { return DdVec3{1.5f, 0.0f, 0.8f}; }
    float cameraDistance() const override { return mProfile.length > 8.0f ? 16.0f : 9.0f; }
    const CrashObservations& observations() const { return mObs; }

    void build(DdWorld* world) override
    {
        addGround(world, DdVec3{0.0f, 0.0f, 1.0f});
        // Facing: the rig's own forward is found from its pose (the feet point forward), then the rig is turned so
        // that it looks the chosen way. Forward = the direction towards the vehicle (-X) turned by `facing`
        // counter-clockwise: 90 turns the right side towards the vehicle.
        DdCharacterDesc desc;
        dragdoll_character_desc_default(&desc);
        desc.rig_json = rigJson();
        desc.size_scale = mParams.size;
        desc.transform.position = DdVec3{0.0f, mParams.offset, 0.01f};
        desc.transform.rotation = DdQuat{0, 0, 0, 1};
        if (dragdoll_character_create(world, &desc, &mCharacter) != DD_OK || mCharacter == nullptr) return;
        mBodies.resize(dragdoll_character_get_body_count(mCharacter));
        dragdoll_character_get_body_transforms(mCharacter, 0, mBodies.data(), static_cast<uint32_t>(mBodies.size()));
        const int foot = dragdoll_character_find_body(mCharacter, "foot_l"), shin = dragdoll_character_find_body(mCharacter, "shin_l");
        if (foot >= 0 && shin >= 0)
        {
            const float fx = mBodies[static_cast<size_t>(foot)].position.x - mBodies[static_cast<size_t>(shin)].position.x;
            const float fy = mBodies[static_cast<size_t>(foot)].position.y - mBodies[static_cast<size_t>(shin)].position.y;
            const float wanted = (180.0f + mParams.facing) * 3.14159265f / 180.0f;
            const float turn = (wanted - std::atan2(fy, fx)) * 180.0f / 3.14159265f;
            dragdoll_character_destroy(mCharacter);
            mCharacter = nullptr;
            desc.transform.rotation = axisAngle(DdVec3{0, 0, 1}, turn);
            if (dragdoll_character_create(world, &desc, &mCharacter) != DD_OK || mCharacter == nullptr) return;
        }
        if (!mParams.limp)
        {
            dragdoll_character_set_kinematic(mCharacter, 1); // Host-animated standing until the handover
            dragdoll_character_set_tension(mCharacter, DD_TENSION_RELAXED, 0.0f);
            dragdoll_character_set_behaviour(mCharacter, "car_hit");
            // Aware: DragDoll's own anticipation (general profile), or the game made the pedestrian react (SA: the
            // starting state - the game decided it noticed the vehicle).
            dragdoll_character_set_awareness(mCharacter, mParams.aware || mParams.start != "none" ? 1.0f : 0.0f);
            dragdoll_character_get_pose(mCharacter, 0, nullptr, 0, &mRestRoot);
            mCount = dragdoll_character_get_body_count(mCharacter);
            // No predictive handover: as in the game, the pedestrian becomes a ragdoll when the vehicle's body actually
            // reaches it (the game's own hit), so a near miss - a step or dive that gets clear - stays the host's.
            dragdoll_character_set_vehicle_pass_through(mCharacter, 1); // As the game integration (ADR-0026)
        }
        mMesh = buildVehicleMesh(mProfile, DdVec3{1, 0, 0}, DdVec3{0, 1, 0}, DdVec3{0, 0, 1}); // Drives along +X
        DdColliderDesc car;
        dragdoll_collider_desc_default(&car);
        car.shape = DD_SHAPE_MESH;
        car.motion = DD_MOTION_KINEMATIC;
        car.vertices = mMesh.vertices.data();
        car.vertex_count = static_cast<uint32_t>(mMesh.vertices.size());
        car.indices = mMesh.indices.data();
        car.index_count = static_cast<uint32_t>(mMesh.indices.size());
        car.transform.position = DdVec3{mX, 0.0f, 0.0f};
        car.group = 1; // One vehicle = one perceived object (ADR-0024)
        mCar = addOne(world, car);
    }

    void hostFrame(DdWorld* world, double hostTime, float dt) override
    {
        observe(world, hostTime - dt);
        // The game animates the pedestrian's reaction until the handover (activation from motion carries it on).
        if (!mParams.limp && mParams.start != "none" && mCharacter != nullptr && dragdoll_character_get_kinematic(mCharacter) != 0)
        {
            DdTransform before, now;
            std::vector<DdQuat> jointsBefore, jointsNow;
            startPose(hostTime - std::max(dt, 1e-3f), before, jointsBefore);
            startPose(hostTime, now, jointsNow);
            dragdoll_character_activate(mCharacter, &before, jointsBefore.data(), &now, jointsNow.data(), mCount, std::max(dt, 1e-3f));
        }
        // The vehicle: constant speed; braking from the first contact + the delay (if it brakes).
        if (mParams.brake && mObs.contact >= 0.0 && hostTime > mObs.contact + mParams.brakeDelayMs / 1000.0)
        {
            mSpeed = std::max(0.0f, mSpeed - mDecel * dt);
        }
        mX += mSpeed * dt;
        // The game's hit: the vehicle's body reaches a body of the pedestrian (inside its length and width, below its
        // roof) - the host hands it over to physics then (ADR-0027), a frame ahead of DragDoll's own contact.
        if (!mParams.limp && mCharacter != nullptr && dragdoll_character_get_kinematic(mCharacter) != 0)
        {
            const float front = mX + mProfile.length * 0.5f, rear = mX - mProfile.length * 0.5f;
            for (const DdTransform& b : mBodies)
            {
                if (b.position.x <= front + 0.1f && b.position.x >= rear && std::fabs(b.position.y) <= mProfile.width * 0.5f + 0.1f &&
                    b.position.z <= mProfile.roofHeight)
                {
                    dragdoll_character_set_kinematic(mCharacter, 0);
                    break;
                }
            }
        }
        const DdTransform target{{mX, 0.0f, 0.0f}, {0, 0, 0, 1}};
        dragdoll_world_set_kinematic_targets(world, &mCar, &target, 1);
    }

    void hud(DdWorld*, std::vector<std::string>& lines) override
    {
        char b[200];
        std::snprintf(b, sizeof(b), "%s: bumper %.2f m, bonnet edge %.2f m | speed %.1f km/h%s", mParams.vehicle.c_str(),
                      static_cast<double>(mProfile.bumperHeight), static_cast<double>(mProfile.bonnetEdgeHeight),
                      static_cast<double>(mSpeed * 3.6f), mParams.brake ? (mSpeed < mParams.kmh / 3.6f ? " (braking)" : " (will brake)") : " (keeps going)");
        lines.emplace_back(b);
        std::snprintf(b, sizeof(b), "status: %s", mStatus.c_str());
        lines.emplace_back(b);
    }

private:
    // How long before the contact the game's reaction began (how far into it the pedestrian is struck).
    static float startLead(const CrashParams& p)
    {
        if (p.leadMs > 0.0f) return p.leadMs / 1000.0f;
        // Measured: the game starts a dodge about 1.15 s, a dive about 1.0 s, hands up about 1.2 s before the vehicle
        // arrives (the run: our own choice).
        return p.start == "step" ? 1.15f : p.start == "dive" ? 1.0f : p.start == "handsup" ? 1.2f : 0.0f; // (The run: none)
    }
    // Travel away from the vehicle's path (m) of a measured reaction this far into it.
    static float keyAside(const std::string& s, float u)
    {
        const StartKey* keys = s == "step" ? kStepKeys : kDiveKeys;
        const size_t n = s == "step" ? std::size(kStepKeys) : std::size(kDiveKeys);
        float best = 0.0f;
        for (size_t i = 0; i < n; ++i)
        {
            if (keys[i].t <= u) best = std::max(best, keys[i].move[0]);
            else if (i > 0) // Part way to the next key
            {
                const float k = (u - keys[i - 1].t) / (keys[i].t - keys[i - 1].t);
                best = std::max(best, keys[i - 1].move[0] + (keys[i].move[0] - keys[i - 1].move[0]) * k);
                break;
            }
        }
        return best;
    }

    // The starting state at host time t: a stand-in for the game's reaction animation (its timing and motion, our own
    // shapes): it starts shortly before the contact, early enough to be under way, late enough that the pedestrian is
    // still struck (these were all struck in game). Sideways = towards the vehicle's left (+Y).
    void startPose(double t, DdTransform& root, std::vector<DdQuat>& joints) const
    {
        root = mRestRoot;
        joints.assign(mCount, DdQuat{0, 0, 0, 1});
        const auto set = [&](const char* body, float degrees) {
            const int i = dragdoll_character_find_body(mCharacter, body);
            if (i >= 0) joints[static_cast<size_t>(i)] = axisAngle(DdVec3{1, 0, 0}, degrees);
        };
        const std::string& s = mParams.start;
        const float lead = startLead(mParams);
        const float u = std::max(0.0f, static_cast<float>(t) - (mContactTime - lead)); // Seconds into the reaction
        const auto ramp = [&](float seconds) { return std::min(1.0f, u / seconds); };
        if (s == "step" || s == "dive" || s == "handsup")
        {
            const StartKey* keys = s == "step" ? kStepKeys : s == "dive" ? kDiveKeys : kHandsUpKeys;
            const size_t n = s == "step" ? std::size(kStepKeys) : s == "dive" ? std::size(kDiveKeys) : std::size(kHandsUpKeys);
            size_t i = 0;
            while (i + 2 < n && keys[i + 1].t <= u) ++i;
            const StartKey &k0 = keys[i], &k1 = keys[std::min(i + 1, n - 1)];
            const float k = k1.t > k0.t ? std::clamp((u - k0.t) / (k1.t - k0.t), 0.0f, 1.0f) : 0.0f;
            float away = k0.move[0] + (k1.move[0] - k0.move[0]) * k, up = k0.move[1] + (k1.move[1] - k0.move[1]) * k,
                  along = k0.move[2] + (k1.move[2] - k0.move[2]) * k;
            // A pedestrian that has got away stays away: the travel never shrinks back (late keys average fewer, often
            // shorter recordings) - it holds the farthest point reached so far.
            for (size_t m = 0; m < n && keys[m].t <= u; ++m) away = std::max(away, keys[m].move[0]);
            // The vehicle drives along +X; the side the reaction goes to (+Y = the vehicle's left).
            const float side = mParams.side == "right" ? -1.0f : 1.0f; // The setting, not where it stands
            root.position = DdVec3{mRestRoot.position.x + along, mRestRoot.position.y + side * away, mRestRoot.position.z + up};
            // Facing: degrees from the vehicle's travel, + = away from its path (mirrored to the pedestrian's side).
            const float facingDeg = (s == "step" ? kStepFacing : s == "dive" ? kDiveFacing : kHandsUpFacing) * side;
            const DdVec3 f0 = metrics::rotate(mRestRoot.rotation, DdVec3{0.0f, 0.0f, 1.0f});
            const float turnTo = facingDeg - std::atan2(f0.y, f0.x) * 57.2958f;
            const DdQuat facing = mul(axisAngle(DdVec3{0, 0, 1}, turnTo), mRestRoot.rotation);
            const DdQuat react = mul(facing, nlerp(fromRotationVector(k0.turn), fromRotationVector(k1.turn), k));
            root.rotation = u < 0.2f ? nlerp(mRestRoot.rotation, react, u / 0.2f) : react; // The game turns it within 0.2 s
            for (size_t j = 1; j < mCount && j < 16; ++j)
                joints[j] = nlerp(fromRotationVector(k0.joints[j]), fromRotationVector(k1.joints[j]), k);
            // After the dodge the pedestrian stands again where it got to (0.4 s back to standing), facing as it was set.
            if (s == "step" && u > keys[n - 1].t)
            {
                const float settle = std::min(1.0f, (u - keys[n - 1].t) / 0.4f);
                root.position.z = mRestRoot.position.z + up * (1.0f - settle);
                root.rotation = nlerp(react, facing, settle);
                for (size_t j = 1; j < mCount && j < 16; ++j) joints[j] = nlerp(joints[j], DdQuat{0, 0, 0, 1}, settle);
            }
        }
        else if (s == "run")
        {
            // Running in panic - from this vehicle or from something else (founder, 2026-11-19): already running from the
            // first frame, straight the way it faces (the facing sets the crash angle: 180 = running away down the
            // vehicle's path, caught from behind; 90 / 270 = crossing; 0 = towards it), timed to pass the impact point
            // as the vehicle arrives (our stand-in shape; 3.5 m/s).
            const DdVec3 f0 = metrics::rotate(mRestRoot.rotation, DdVec3{0.0f, 0.0f, 1.0f});
            const float fl = std::sqrt(f0.x * f0.x + f0.y * f0.y);
            const float run = 3.5f * (static_cast<float>(t) - mContactTime);
            if (fl > 1e-4f)
            {
                root.position.x += f0.x / fl * run;
                root.position.y += f0.y / fl * run;
            }
            const float stride = 2.0f * 3.14159265f * 1.4f * static_cast<float>(t);
            for (int leg = 0; leg < 2; ++leg)
            {
                const float phase = std::sin(stride + static_cast<float>(leg) * 3.14159265f);
                set(leg == 0 ? "thigh_l" : "thigh_r", -40.0f * phase);
                set(leg == 0 ? "shin_l" : "shin_r", 35.0f + 35.0f * phase);
                set(leg == 0 ? "upper_arm_l" : "upper_arm_r", 35.0f * phase);
            }
            set("lower_spine", 10.0f);
        }
    }

    // Records what the analyzer needs, at the start of each host frame (the state the last step left).
    void observe(DdWorld* world, double t)
    {
        mObs.duration = t;
        if (mCharacter == nullptr) return;
        DdCharacterInfo info{};
        info.struct_size = sizeof(DdCharacterInfo);
        if (dragdoll_character_get_info(mCharacter, &info) == DD_OK && info.state == DD_STATE_FAILED)
        {
            mObs.failed = true;
            return;
        }
        if (dragdoll_character_get_body_transforms(mCharacter, 0, mBodies.data(), static_cast<uint32_t>(mBodies.size())) == DD_OK)
        {
            const double dt = t - mLastT;
            for (size_t i = 0; i < mBodies.size(); ++i)
            {
                const DdVec3 p = mBodies[i].position;
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) mObs.nanSeen = true;
                mObs.lowestBody = std::min(mObs.lowestBody, p.z);
                // Speeds only once physics drives the body: the host's animation before the hit is not ours to judge.
                if (mLast.size() == mBodies.size() && dt > 1e-6 && dragdoll_character_get_kinematic(mCharacter) == 0)
                {
                    const DdVec3 q = mLast[i].position;
                    const float v = std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z)) /
                                    static_cast<float>(dt);
                    mObs.fastestBody = std::max(mObs.fastestBody, v);
                }
            }
            mLast = mBodies;
            mLastT = t;
        }
        dragdoll_character_get_impact_report(mCharacter, &mObs.report);
        if (mObs.contact < 0.0 && mObs.report.valid) mObs.contact = t;
        if (!mParams.limp)
        {
            if (mObs.handover < 0.0 && dragdoll_character_get_kinematic(mCharacter) == 0) mObs.handover = t;
            mStatus = dragdoll_character_get_behaviour_status(mCharacter);
            if (!mImpactSeen && mStatus.rfind("impact", 0) == 0) // The behaviour's own impact stage: the struck leg gives
            {
                mImpactSeen = true;
                // When a leg was struck first, a leg has given at the impact (car_hit: a startle-braced leg cannot hold
                // against the bumper). -1 = something else met the vehicle first (side-on: often the hanging hand).
                DdImpactReport now{};
                now.struct_size = sizeof(DdImpactReport);
                mObs.legYielded = -1;
                if (dragdoll_character_get_impact_report(mCharacter, &now) == DD_OK && now.valid && now.first_body >= 0)
                {
                    const DdRegion first = dragdoll_character_get_body_region(mCharacter, static_cast<uint32_t>(now.first_body));
                    if (first == DD_REGION_LEG_LEFT || first == DD_REGION_LEG_RIGHT)
                        mObs.legYielded = dragdoll_character_get_region_tension(mCharacter, DD_REGION_LEG_LEFT) == DD_TENSION_RELAXED ||
                                                  dragdoll_character_get_region_tension(mCharacter, DD_REGION_LEG_RIGHT) == DD_TENSION_RELAXED
                                              ? 1
                                              : 0;
                }
            }
            if (mObs.contact >= 0.0 && mObs.brace < 0.0 &&
                dragdoll_character_get_region_tension(mCharacter, DD_REGION_HEAD_NECK) == DD_TENSION_TENSE)
                mObs.brace = t;
            if (mObs.carried < 0.0 && mStatus.rfind("carried", 0) == 0) mObs.carried = t;
            if (mStatus.rfind("hurt", 0) == 0) { if (mObs.hurtSince < 0.0) mObs.hurtSince = t; }
            else mObs.hurtSince = -1.0;
            if (mObs.guard < 0.0 && mStatus.find("guard") != std::string::npos) mObs.guard = t;
            if (mObs.offVehicle < 0.0 && (mStatus.rfind("airborne", 0) == 0 || mStatus.rfind("landing", 0) == 0 ||
                                          mStatus.rfind("tumble", 0) == 0))
                mObs.offVehicle = t;
        }
        DdSenses senses{};
        senses.struct_size = sizeof(DdSenses);
        if (dragdoll_character_get_senses(mCharacter, &senses) == DD_OK)
            mObs.onVehicleAtEnd = senses.object_bodies > 0 && senses.carried_by_group == 1;
        const int head = dragdoll_character_find_body(mCharacter, "head");
        const int handL = dragdoll_character_find_body(mCharacter, "hand_l"), handR = dragdoll_character_find_body(mCharacter, "hand_r");
        if (!mParams.limp) mObs.endStatus = mStatus;
        mObs.gestureError = -1.0f;
        const int hurtBody = mStatus.rfind("hurt: body ", 0) == 0 ? std::atoi(mStatus.c_str() + 11) : -1;
        if (hurtBody >= 0 && static_cast<size_t>(hurtBody) < mBodies.size() &&
            handL >= 0 && handR >= 0)
        {
            // Both hands on the part that hurts: each hand's distance to that body's centre, less its half-thickness.
            const DdRegion region = dragdoll_character_get_body_region(mCharacter, static_cast<uint32_t>(hurtBody));
            const float half = (region == DD_REGION_HEAD_NECK ? 0.10f : region == DD_REGION_TRUNK ? 0.13f : 0.08f) * mParams.size;
            const auto gap = [](DdVec3 a, DdVec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); };
            DdVec3 centre = mBodies[static_cast<size_t>(hurtBody)].position;
            if ((region == DD_REGION_LEG_LEFT || region == DD_REGION_LEG_RIGHT) && static_cast<size_t>(hurtBody) + 1 < mBodies.size() &&
                dragdoll_character_get_body_region(mCharacter, static_cast<uint32_t>(hurtBody + 1)) == region)
            {
                const DdVec3 shin = mBodies[static_cast<size_t>(hurtBody) + 1].position; // A leg is held at the knee
                centre = DdVec3{(centre.x + shin.x) * 0.5f, (centre.y + shin.y) * 0.5f, (centre.z + shin.z) * 0.5f};
            }
            mObs.hands[0] = mBodies[static_cast<size_t>(handL)].position;
            mObs.hands[1] = mBodies[static_cast<size_t>(handR)].position;
            mObs.handTargets[0] = mObs.handTargets[1] = centre;
            mObs.gestureError = std::max(0.0f, std::max(gap(mObs.hands[0], centre), gap(mObs.hands[1], centre)) - half);
            mObs.hurtPart = dragdoll_character_get_body_name(mCharacter, static_cast<uint32_t>(hurtBody));
        }
        if (head >= 0 && handL >= 0 && handR >= 0)
        {
            const auto distance = [&](int a, int b) {
                const DdVec3 p = mBodies[static_cast<size_t>(a)].position, q = mBodies[static_cast<size_t>(b)].position;
                return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) + (p.z - q.z) * (p.z - q.z));
            };
            const DdVec3 l = mBodies[static_cast<size_t>(handL)].position, r = mBodies[static_cast<size_t>(handR)].position,
                         h = mBodies[static_cast<size_t>(head)].position;
            const DdVec3 mid{(l.x + r.x) * 0.5f, (l.y + r.y) * 0.5f, (l.z + r.z) * 0.5f};
            const float toHead = std::sqrt((mid.x - h.x) * (mid.x - h.x) + (mid.y - h.y) * (mid.y - h.y) + (mid.z - h.z) * (mid.z - h.z));
            // Hands held together on a hurt part are the pain gesture, not "praying".
            mObs.handsFolded = mStatus.rfind("hurt", 0) != 0 && distance(handL, handR) < 0.12f * mParams.size && toHead < 0.45f * mParams.size;
        }
        const int pelvis = dragdoll_character_find_body(mCharacter, "pelvis");
        if (pelvis >= 0 && static_cast<size_t>(pelvis) < mBodies.size())
        {
            const DdVec3 p = mBodies[static_cast<size_t>(pelvis)].position;
            if (!mHasStart)
            {
                mStartPelvis = p;
                mHasStart = true;
            }
            mObs.pelvisBehindFront = (mX + mProfile.length * 0.5f) - p.x;
            const float local = p.x - mX; // Vehicle space: forward from its centre
            float top = -1.0f;
            const auto outline = mProfile.outline(); // front-bottom -> over the top -> rear-bottom
            for (size_t i = 1; i + 2 < outline.size(); ++i)
            {
                const float x0 = outline[i][0], x1 = outline[i + 1][0];
                if (local <= x0 && local >= x1 && x0 - x1 > 1e-4f)
                    top = std::max(top, outline[i][1] + (outline[i + 1][1] - outline[i][1]) * (x0 - local) / (x0 - x1));
            }
            mObs.pelvisInsideVehicle = std::fabs(p.y) < mProfile.width * 0.5f - 0.1f && top > 0.0f &&
                                       p.z > mProfile.clearance + 0.1f && p.z < top - 0.05f;
            mObs.pelvisHeight = p.z;
            mObs.pelvisMoved = std::hypot(p.x - mStartPelvis.x, p.y - mStartPelvis.y);
        }
        (void)world;
    }

    CrashParams mParams;
    VehicleProfile mProfile;
    VehicleMesh mMesh;
    std::string mNameText, mDescriptionText, mStatus;
    float mSpeed = 0.0f, mDecel = 0.0f, mContactTime = 1.0f, mX = 0.0f;
    DdColliderId mCar = DD_INVALID_COLLIDER;
    DdCharacter* mCharacter = nullptr;
    std::vector<DdTransform> mBodies, mLast;
    double mLastT = 0.0;
    DdVec3 mStartPelvis{};
    DdTransform mRestRoot{};
    uint32_t mCount = 0;
    bool mHasStart = false, mImpactSeen = false;
    CrashObservations mObs;
};

using Factory = std::function<std::unique_ptr<Scenario>()>;

const std::vector<std::pair<std::string, Factory>>& registry()
{
    static const std::vector<std::pair<std::string, Factory>> sRegistry = [] {
    std::vector<std::pair<std::string, Factory>> all = {
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
    all.emplace_back("tension_lineup", [] { return std::make_unique<TensionLineupScenario>(); });
    all.emplace_back("pose_zero_g", [] { return std::make_unique<PoseZeroGScenario>(); });
    all.emplace_back("walk_then_activate", [] { return std::make_unique<WalkThenActivateScenario>(); });
    all.emplace_back("stand_balance", [] { return std::make_unique<HoldStanceScenario>("stand_balance", 0.0f, 10.0); });
    all.emplace_back("hold_stance", [] { return std::make_unique<HoldStanceScenario>("hold_stance", 5.0f, 20.0); });
    all.emplace_back("bump_5kmh", [] { return std::make_unique<VehicleHitScenario>("sedan", 5.0f, true, "bump_5kmh"); });
    all.emplace_back("death_drop", [] { return std::make_unique<DeathDropScenario>(true); });
    all.emplace_back("death_drop_limp", [] { return std::make_unique<DeathDropScenario>(false); });
    return all;
    }();
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
PoseError poseError(const DdCharacter* character, const std::vector<DdTransform>& reference, const std::vector<DdTransform>& now)
{
    auto conj = [](const DdQuat& q) { return DdQuat{-q.x, -q.y, -q.z, q.w}; };
    auto mul = [](const DdQuat& a, const DdQuat& b) {
        return DdQuat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
    };
    PoseError e;
    double sum = 0.0;
    int joints = 0;
    for (uint32_t i = 0; i < now.size() && i < reference.size(); ++i)
    {
        const int32_t p = dragdoll_character_get_body_parent(character, i);
        if (p < 0)
        {
            continue;
        }
        const DdQuat relRef = mul(conj(reference[static_cast<size_t>(p)].rotation), reference[i].rotation);
        const DdQuat relNow = mul(conj(now[static_cast<size_t>(p)].rotation), now[i].rotation);
        const DdQuat d = mul(conj(relRef), relNow);
        const float angle = 2.0f * std::acos(std::fmin(1.0f, std::fabs(d.w))) * 57.29578f;
        sum += static_cast<double>(angle) * angle;
        ++joints;
        if (angle > e.max)
        {
            e.max = angle;
            e.worst = static_cast<int>(i);
        }
    }
    e.rms = joints > 0 ? static_cast<float>(std::sqrt(sum / joints)) : 0.0f;
    return e;
}
} // namespace metrics

const CrashObservations* crashObservations(const Scenario& scenario)
{
    const auto* crash = dynamic_cast<const CrashScenario*>(&scenario);
    return crash != nullptr ? &crash->observations() : nullptr;
}

std::vector<std::string> list()
{
    std::vector<std::string> names{"crash"};
    for (const auto& entry : registry())
    {
        names.push_back(entry.first);
    }
    return names;
}

std::unique_ptr<Scenario> create(const std::string& name)
{
    if (name.rfind("crash", 0) == 0)
    {
        CrashParams params;
        std::string error;
        if (!parseCrashName(name, params, error)) return nullptr;
        return std::make_unique<CrashScenario>(params);
    }
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

namespace
{
std::string& rigOverride()
{
    static std::string json;
    return json;
}
} // namespace

void setRigOverride(std::string json)
{
    rigOverride() = std::move(json);
}

const char* rigJson()
{
    return rigOverride().empty() ? dragdoll_get_builtin_rig("humanoid16") : rigOverride().c_str();
}

} // namespace dragdoll::scenarios
