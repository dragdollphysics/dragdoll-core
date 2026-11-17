// SPDX-License-Identifier: MIT
// "car_hit": the reaction chain for a pedestrian struck by a vehicle (ADR-0025). Stages so far:
//   Watch      — nothing; the body keeps whatever the host set.
//   Anticipate — Startle: once the character has been aware of the threat for the reflex latency, the whole body
//                tenses. Shield (fend): after the voluntary latency, both hands go towards the vehicle, elbows
//                slightly flexed (the "hands raised towards the vehicle" posture of the volunteer studies).
//   Recover    — the threat went by without contact: tension eases back to what it was.
//   Impact     — first contact with the vehicle (task 4.6). At once: a tensed struck leg yields (its muscles are
//                overpowered, nothing is decided). After the reflex latency: trunk and neck co-contract.
//   Carried    — lying on the vehicle, not on the ground: Guard (chin tucked, forearms between the head and the
//                vehicle) and a mild Curl; the trunk eases from tense to relaxed. A character that saw the car coming
//                (startled before the contact) guards after the reflex latency; one taken by surprise after
//                guard.unaware_latency (equal for now: a later guard started in the air and looked worse, founder's
//                review 2026-11-17). The arms take guard.blend to reach full tension: they move in, not snap.
//   After      — left the vehicle (airborne or on the ground): relaxed, Guard kept. Airborne / Landing / Tumble
//                replace it in task 4.7.
//   Limp       — vitality 0: nothing left to react with.
// Includes only the behaviour SDK (ADR-0013).
#include <dragdoll/behaviour_sdk.h>

#include <cmath>

namespace
{
using namespace dragdoll::sdk;

constexpr uint32_t kArms = DD_REGION_BIT(DD_REGION_ARM_LEFT) | DD_REGION_BIT(DD_REGION_ARM_RIGHT);
constexpr uint32_t kCurlRegions = DD_REGION_BIT(DD_REGION_TRUNK) | DD_REGION_BIT(DD_REGION_LEG_LEFT) | DD_REGION_BIT(DD_REGION_LEG_RIGHT);

class CarHit final : public Behaviour
{
public:
    void configure(const Params& p) override
    {
        mAnticipateTime = p.number("anticipate.time", 1.2f);
        mAwarenessNeeded = p.number("anticipate.awareness", 0.5f);
        mClock.reflexLatency = mImpactClock.reflexLatency = p.number("reaction.reflex", 0.1f);
        mClock.voluntaryLatency = mImpactClock.voluntaryLatency = p.number("reaction.voluntary", 0.5f);
        mStartleBlend = p.number("startle.blend", 0.08f);
        mShieldReach = p.number("shield.reach", 0.55f);
        mShieldRaise = p.number("shield.raise", 0.15f);
        mShieldExtension = p.number("shield.extension", 0.85f);
        mRecoverDelay = p.number("recover.delay", 0.6f);
        mRecoverBlend = p.number("recover.blend", 0.5f);
        mYieldBlend = p.number("impact.yield_blend", 0.02f);
        mBraceBlend = p.number("impact.brace_blend", 0.05f);
        mCarriedRelax = p.number("carried.relax", 0.5f);
        mReleaseSeconds = p.number("carried.release", 0.1f);
        mGuardOffset = p.number("guard.offset", 0.12f);
        mGuardExtension = p.number("guard.extension", 0.5f);
        mGuardUnaware = p.number("guard.unaware_latency", 0.1f);
        mGuardBlend = p.number("guard.blend", 0.3f);
        mAfterBlend = p.number("after.blend", 0.3f);
    }

    void update(const Percept& s, Command& out) override
    {
        if (s.vitality <= 0.0f)
        {
            if (mStage != Stage::Limp)
            {
                out.setTensionAll(DD_TENSION_LIMP, 0.0f);
                mStage = Stage::Limp;
            }
            return;
        }
        const bool threat = s.threat.collider != DD_INVALID_COLLIDER;
        if (threat)
        {
            mTravel = s.threat.direction; // Remembered: the threat may vanish once the object touches the body
        }
        const bool alert = s.awareness >= mAwarenessNeeded;
        const bool struck = !s.kinematic && s.senses.object_bodies > 0;
        if (struck && mStage != Stage::Impact && mStage != Stage::Carried && mStage != Stage::After)
        {
            enterImpact(s, out);
        }
        switch (mStage)
        {
        case Stage::Watch:
            if (threat && alert && s.threat.time_to_collision <= mAnticipateTime)
            {
                for (int r = 0; r < DD_REGION_COUNT; ++r) mBefore[r] = s.tension[r];
                mClock.clear();
                mClock.stimulus();
                mStartled = false;
                mShielding = false;
                mStage = Stage::Anticipate;
            }
            break;
        case Stage::Anticipate:
            mClock.tick(s.dt);
            if (!mStartled && mClock.reflex())
            {
                out.setTensionAll(DD_TENSION_TENSE, mStartleBlend); // Startle: whole-body co-contraction
                mStartled = true;
            }
            if (threat && mStartled && mClock.voluntary())
            {
                // Shield: hands towards where the vehicle comes from, a little above the centre of mass.
                const DdVec3 d = s.threat.direction;
                const DdVec3 towards{s.centreOfMass.x - d.x * mShieldReach + s.up.x * mShieldRaise,
                                     s.centreOfMass.y - d.y * mShieldReach + s.up.y * mShieldRaise,
                                     s.centreOfMass.z - d.z * mShieldReach + s.up.z * mShieldRaise};
                out.aimAt(DD_REGION_ARM_LEFT, towards, mShieldExtension);
                out.aimAt(DD_REGION_ARM_RIGHT, towards, mShieldExtension);
                mShielding = true;
            }
            if (!threat)
            {
                mQuiet = 0.0f;
                mStage = Stage::Recover;
            }
            break;
        case Stage::Recover:
            mQuiet += s.dt;
            if (threat && alert && s.threat.time_to_collision <= mAnticipateTime)
            {
                mStage = Stage::Anticipate; // It came back: stay braced
            }
            else if (mQuiet >= mRecoverDelay)
            {
                if (mStartled)
                {
                    for (int r = 0; r < DD_REGION_COUNT; ++r) out.setTension(static_cast<DdRegion>(r), mBefore[r], mRecoverBlend);
                }
                if (mShielding)
                {
                    out.usePose("rest", kArms); // Arms down
                    mShielding = false;
                }
                mStage = Stage::Watch;
            }
            break;
        case Stage::Impact:
            mImpactClock.tick(s.dt);
            if (!mBraced && mImpactClock.reflex())
            {
                // Reflex co-contraction of trunk and neck: the body stiffens around the impact.
                out.setTension(DD_REGION_TRUNK, DD_TENSION_TENSE, mBraceBlend);
                out.setTension(DD_REGION_HEAD_NECK, DD_TENSION_TENSE, mBraceBlend);
                mBraced = true;
            }
            if (mBraced)
            {
                if (carried(s))
                {
                    enterCarried(out);
                }
                else if (released())
                {
                    enterAfter(out);
                }
            }
            break;
        case Stage::Carried:
            mImpactClock.tick(s.dt);
            guard(s, out);
            if (released())
            {
                enterAfter(out);
            }
            break;
        case Stage::After:
            mImpactClock.tick(s.dt);
            if (mGuarding)
            {
                guard(s, out); // Keeps a guard begun on the car; never starts one in the air (founder: arms changing pose mid-air looked odd)
            }
            break;
        case Stage::Limp:
            break;
        }
        mApart = struck ? 0.0f : mApart + s.dt;
    }

    const char* status() const override
    {
        switch (mStage)
        {
        case Stage::Watch: return "watch";
        case Stage::Anticipate: return mShielding ? "anticipate: shield" : mStartled ? "anticipate: startle" : "anticipate";
        case Stage::Recover: return "recover";
        case Stage::Impact: return mBraced ? "impact: brace" : "impact";
        case Stage::Carried: return mGuarding ? "carried: guard + curl" : "carried";
        case Stage::After: return mGuarding ? "after impact: guard" : "after impact";
        case Stage::Limp: return "limp";
        }
        return "";
    }

private:
    enum class Stage { Watch, Anticipate, Recover, Impact, Carried, After, Limp };

    // Supported by the vehicle and not by the ground.
    static bool carried(const Percept& s) { return s.senses.carried_by != DD_INVALID_COLLIDER && s.senses.ground_bodies == 0; }
    // Off the vehicle for a moment (airborne, or on the ground).
    bool released() const { return mApart >= mReleaseSeconds; }

    void enterImpact(const Percept& s, Command& out)
    {
        mPrimed = mStartled; // It saw the car coming and was already braced: its protective reflexes are ready
        mGuarding = false;
        mImpactClock.clear();
        mImpactClock.stimulus();
        mBraced = false;
        mApart = 0.0f;
        // Struck-leg yield: a leg braced by the startle cannot hold against the bumper; it gives at once.
        for (uint32_t i = 0; i < s.bodyCount && i < kMaxBodies; ++i)
        {
            const DdRegion region = s.bodyRegion[i];
            if ((s.contacts[i].flags & DD_CONTACT_OBJECT) != 0 && (region == DD_REGION_LEG_LEFT || region == DD_REGION_LEG_RIGHT) &&
                s.tension[region] == DD_TENSION_TENSE)
            {
                out.setTension(region, DD_TENSION_RELAXED, mYieldBlend);
            }
        }
        mStage = Stage::Impact;
    }

    void enterCarried(Command& out)
    {
        out.setTension(DD_REGION_TRUNK, DD_TENSION_RELAXED, mCarriedRelax);
        mStage = Stage::Carried;
    }

    void enterAfter(Command& out)
    {
        for (const DdRegion r : {DD_REGION_HEAD_NECK, DD_REGION_TRUNK, DD_REGION_LEG_LEFT, DD_REGION_LEG_RIGHT})
        {
            out.setTension(r, DD_TENSION_RELAXED, mAfterBlend);
        }
        mStage = Stage::After;
    }

    // Guard: chin tucked (the "guard" pose on the neck), forearms between the head and the nearest part of the vehicle
    // touching the body (else the side the vehicle came from).
    void guard(const Percept& s, Command& out)
    {
        if (!mGuarding)
        {
            if (mImpactClock.elapsed < (mPrimed ? mImpactClock.reflexLatency : mGuardUnaware))
            {
                return; // Taken by surprise: not yet
            }
            out.usePose("guard", DD_REGION_BIT(DD_REGION_HEAD_NECK)); // Chin tucked; the curl follows next step (one named pose per step)
            out.setTension(DD_REGION_ARM_LEFT, DD_TENSION_TENSE, mGuardBlend); // Gradual: the arms move in, not snap
            out.setTension(DD_REGION_ARM_RIGHT, DD_TENSION_TENSE, mGuardBlend);
            mPendingCurl = true;
            mGuarding = true;
            return;
        }
        if (mPendingCurl)
        {
            out.usePose("curl", kCurlRegions); // One named pose per step: the curl goes in the step after the guard
            mPendingCurl = false;
        }
        if (s.bodies == nullptr || s.headBody < 0 || static_cast<uint32_t>(s.headBody) >= s.bodyCount)
        {
            return;
        }
        const DdVec3 head = s.bodies[s.headBody].position;
        DdVec3 toward{-mTravel.x, -mTravel.y, -mTravel.z};
        float best = 1e9f;
        for (uint32_t i = 0; i < s.bodyCount; ++i)
        {
            if ((s.contacts[i].flags & DD_CONTACT_OBJECT) == 0) continue;
            const DdVec3 p = s.contacts[i].point;
            const DdVec3 d{p.x - head.x, p.y - head.y, p.z - head.z};
            const float distance = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (distance > 1e-3f && distance < best)
            {
                best = distance;
                toward = DdVec3{d.x / distance, d.y / distance, d.z / distance};
            }
        }
        const DdVec3 target{head.x + toward.x * mGuardOffset, head.y + toward.y * mGuardOffset, head.z + toward.z * mGuardOffset};
        out.aimAt(DD_REGION_ARM_LEFT, target, mGuardExtension);
        out.aimAt(DD_REGION_ARM_RIGHT, target, mGuardExtension);
    }

    Stage mStage = Stage::Watch;
    ReactionClock mClock, mImpactClock;
    bool mStartled = false, mShielding = false, mBraced = false, mPendingCurl = false, mPrimed = false, mGuarding = false;
    float mQuiet = 0.0f, mApart = 0.0f;
    DdVec3 mTravel{1, 0, 0};
    float mShieldReach = 0.55f, mShieldRaise = 0.15f, mShieldExtension = 0.85f;
    DdTension mBefore[DD_REGION_COUNT] = {};
    float mAnticipateTime = 1.2f, mAwarenessNeeded = 0.5f, mStartleBlend = 0.08f, mRecoverDelay = 0.6f, mRecoverBlend = 0.5f;
    float mYieldBlend = 0.02f, mBraceBlend = 0.05f, mCarriedRelax = 0.5f, mReleaseSeconds = 0.1f;
    float mGuardOffset = 0.12f, mGuardExtension = 0.5f, mGuardUnaware = 0.1f, mGuardBlend = 0.3f, mAfterBlend = 0.3f;
};
} // namespace

namespace dragdoll::sdk
{
void registerBuiltinBehaviours()
{
    registerBehaviour("car_hit", []() -> std::unique_ptr<Behaviour> { return std::make_unique<CarHit>(); });
}
} // namespace dragdoll::sdk
