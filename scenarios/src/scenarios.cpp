// SPDX-License-Identifier: MIT
#include <dragdoll_scenarios/scenario.hpp>
#include <dragdoll_scenarios/vehicles.hpp>

#include <cmath>
#include <random>
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
        desc.rig_json = rigJson();
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
    VehicleHitScenario(const std::string& vehicle, float kmh, bool braced = false, const char* name = nullptr)
        : RagdollScenario(nullptr, nullptr, DdVec3{0.0f, 0.0f, braced ? 0.005f : 0.01f}, DdQuat{0, 0, 0, 1}), mVehicle(vehicle),
          mBraced(braced), mContactTime(braced ? 0.5f : 0.1f)
    {
        mNameText = name != nullptr ? name : "hit_" + vehicle + "_" + std::to_string(static_cast<int>(kmh)) + "kmh";
        mProfile = parseVehicle(builtinVehicle(vehicle));
        mDescriptionText = "A " + vehicle + " at " + std::to_string(static_cast<int>(kmh)) + " km/h " +
                           (braced ? "nudges a standing pedestrian (tense, balance on) side-on and brakes. Judge: absorbed "
                                     "with ankles and hips, no fall."
                                   : "hits a standing limp pedestrian side-on and brakes hard on contact. ") +
                           (braced ? std::string() : mProfile.description);
        mName = mNameText.c_str();
        mDescription = mDescriptionText.c_str();
        mDrive.speed = kmh / 3.6f;
        mDrive.brakeAt = mContactTime;
        mDrive.decel = 7.0f; // Emergency braking on a dry road
        // The front face reaches the pedestrian's near side (~0.25 m from the centre) at the contact time.
        mStart = -0.25f - mProfile.length * 0.5f - mDrive.speed * mContactTime;
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
    bool mBraced = false;
    float mContactTime = 0.1f;
    std::string mVehicle, mNameText, mDescriptionText;
    VehicleProfile mProfile;
    VehicleMesh mMesh;
    Drive mDrive;
    float mStart = 0.0f;
    DdColliderId mCar = DD_INVALID_COLLIDER;
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
    for (const std::string& vehicle : vehicleNames())
    {
        all.emplace_back("hit_" + vehicle + "_40kmh", [vehicle] { return std::make_unique<VehicleHitScenario>(vehicle, 40.0f); });
    }
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
