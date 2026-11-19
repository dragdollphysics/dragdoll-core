// SPDX-License-Identifier: MIT
// "car_hit": the reaction chain for a pedestrian struck by a vehicle (ADR-0025). Stages so far:
//   Watch      — nothing; the body keeps whatever the host set.
//   Anticipate — Startle: once the character has been aware of the threat for the reflex latency, the whole body
//                tenses in a short burst (startle.burst) with a slight crouch, then eases into a braced tone
//                (startle.ease): on guard, not locked rigid (task 4.9; founder's reviews 2026-11-17 and 2026-11-19:
//                "too stiff", "falls over like a block"). Shield (fend): after the voluntary latency, both hands go towards the vehicle, elbows
//                slightly flexed (the "hands raised towards the vehicle" posture of the volunteer studies).
//   Recover    — the threat went by without contact: tension eases back to what it was.
//   Impact     — first contact with the vehicle (task 4.6). At once: a tensed struck leg yields (its muscles are
//                overpowered, nothing is decided). After the reflex latency: trunk and neck co-contract.
//   Carried    — lying on the vehicle, not on the ground: Guard (chin tucked, forearms between the head and the
//                vehicle) and a mild Curl; the trunk eases from tense to relaxed. A character that saw the car coming
//                (startled before the contact) guards after the reflex latency; one taken by surprise after
//                guard.unaware_latency (equal for now: a later guard started in the air and looked worse, founder's
//                review 2026-11-17). The arms take guard.blend to reach full tension: they move in, not snap.
//   Airborne   — no contact for 25 ms (task 4.7). With time before landing (reach.min_time): Reach - both hands go
//                to where the head will meet the ground, elbows a little bent, chin tucked (unexpected falls: the hands
//                arrive with the pelvis, Hsiao & Robinovitch). Without: Guard if it was guarding.
//   Landing    — ground due within landing.lead or touched: keep reaching until a body touches; then the arms yield
//                (soft landing, DeGoede & Ashton-Miller) and the body stays relaxed so it rolls on.
//   Tumble     — on the ground: relaxed, forearms loosely near the head while it still slides, never fighting the
//                slide; once the body has stopped (every body still) it goes to Hurt; a bounce into the
//                air goes back to Airborne.
//   Any fall (not only after a vehicle): a body that is physics-driven, off any vehicle and either airborne or
//                tipped over (upright < fall.upright) with the head dropping starts the reflex clock and goes to
//                Airborne: after the reflex latency the hands reach for the ground (founder, 2026-11-17: "instinct"
//                reactions - landing on the hands, covering the face - are what reads as natural).
//   Hurt       — alive, on the ground and stopped: a person in pain (founder, 2026-11-19, with reference photos): knees
//                and hips drawn up, trunk curled, head tucked (pose "hurt"); struck from behind on the trunk or head it
//                arches back instead (pose "hurt_back"). Both hands go to where it hurts most (founder, 2026-11-19:
//                "focus on where the hands end, the arms follow"): the non-arm body with the largest velocity change
//                since the hit, on the side it was struck from (the contact normal then, kept in that body's frame),
//                side by side. Arms tense to hold, the rest braced so the curl / arch really happens. (Replaces a version where the arms just let go:
//                "looks bad"; before that the guard stayed on: hands folded at the face, "praying".)
//   Limp       — vitality 0: nothing left to react with.
// Includes only the behaviour SDK (ADR-0013).
#include <dragdoll/behaviour_sdk.h>

#include <cmath>
#include <cstdio>

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
        mStartleBurst = p.number("startle.burst", 0.3f);
        mStartleEase = p.number("startle.ease", 0.3f);
        mStartleCrouch = p.number("startle.crouch", 1.0f) > 0.5f;
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
        mAfterTension = static_cast<DdTension>(static_cast<int>(p.number("after.tension", 1.0f))); // 0 limp, 1 relaxed
        mAirborneCurl = p.number("airborne.curl", 0.0f) > 0.5f;
        mApproachHeight = p.number("reach.approach_height", 0.9f);
        mApproachSpeed = p.number("reach.approach_speed", 0.8f);
        mBraceOn = p.number("impact.brace", 1.0f) > 0.5f;
        mFallUpright = p.number("fall.upright", 0.8f);
        mFallSpeed = p.number("fall.speed", 0.5f);
        mReachMinTime = p.number("reach.min_time", 0.12f);
        mReachExtension = p.number("reach.extension", 0.9f);
        mReachBlend = p.number("reach.blend", 0.15f);
        mLandingLead = p.number("landing.lead", 0.12f);
        mLandingYield = p.number("landing.yield", 0.08f);
        mTumbleExtension = p.number("tumble.extension", 0.4f);
        mHurtBlend = p.number("hurt.blend", 0.6f);
        mHurtHold = p.number("hurt.hold", 0.95f);
        mHurtSpread = p.number("hurt.spread", 0.06f);
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
        trackHead(s);
        trackHurt(s);
        const bool threat = s.threat.collider != DD_INVALID_COLLIDER;
        if (threat)
        {
            mTravel = s.threat.direction; // Remembered: the threat may vanish once the object touches the body
        }
        const bool alert = s.awareness >= mAwarenessNeeded;
        const bool struck = !s.kinematic && s.senses.object_bodies > 0;
        const bool idle = mStage == Stage::Watch || mStage == Stage::Anticipate || mStage == Stage::Recover;
        if (struck && idle)
        {
            enterImpact(s, out);
        }
        else if (idle && !s.kinematic && s.senses.object_bodies == 0 &&
                 (s.senses.airborne != 0 || (s.senses.upright < mFallUpright && mHeadDrop > mFallSpeed)))
        {
            // A fall without a vehicle: the fall itself is the stimulus. As on leaving a vehicle, the startle's whole-body
            // co-contraction lets go (a host-side hit can hand the body over before DragDoll's vehicle touches it: the
            // body otherwise fell and lay fully tense - founder, 2026-11-18).
            mImpactClock.clear();
            mImpactClock.stimulus();
            mPrimed = mStartled;
            mBraced = true;
            mGuarding = false;
            mReaching = false;
            for (const DdRegion r : {DD_REGION_HEAD_NECK, DD_REGION_TRUNK, DD_REGION_LEG_LEFT, DD_REGION_LEG_RIGHT})
            {
                out.setTension(r, mAfterTension, mAfterBlend);
            }
            mStage = Stage::Airborne;
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
                mEased = false;
                mShielding = false;
                mStage = Stage::Anticipate;
            }
            break;
        case Stage::Anticipate:
            mClock.tick(s.dt);
            if (!mStartled && mClock.reflex())
            {
                out.setTensionAll(DD_TENSION_TENSE, mStartleBlend); // Startle: whole-body co-contraction (a burst)
                if (mStartleCrouch) out.usePose("brace", kCurlRegions); // Knees and hips give a little, trunk leans in
                mStartled = true;
                mSinceStartle = 0.0f;
            }
            if (mStartled && !mEased)
            {
                mSinceStartle += s.dt;
                if (mSinceStartle >= mStartleBurst)
                {
                    out.setTensionAll(DD_TENSION_BRACED, mStartleEase); // The burst eases into a braced stance
                    mEased = true;
                }
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
                if (mBraceOn)
                {
                    out.setTension(DD_REGION_TRUNK, DD_TENSION_TENSE, mBraceBlend);
                    out.setTension(DD_REGION_HEAD_NECK, DD_TENSION_TENSE, mBraceBlend);
                }
                mBraced = true; // Reflexes available from here on
            }
            if (mBraced)
            {
                if (carried(s))
                {
                    enterCarried(out);
                }
                else if (released())
                {
                    leaveVehicle(s, out);
                }
            }
            break;
        case Stage::Carried:
            mImpactClock.tick(s.dt);
            if (mBraced && headFalling())
            {
                reach(s, out); // Sliding off head first: the hands go to the ground instead of guarding the face
            }
            else if (!mReaching)
            {
                guard(s, out);
            }
            if (released())
            {
                leaveVehicle(s, out);
            }
            break;
        case Stage::Airborne:
            mImpactClock.tick(s.dt);
            if (groundedAboveLegs(s) || (s.senses.landing_seconds >= 0.0f && s.senses.landing_seconds <= mLandingLead))
            {
                mStage = Stage::Landing;
            }
            else if (!mImpactClock.reflex())
            {
                // Not yet: a fall that has just begun
            }
            else if (s.senses.landing_seconds >= mReachMinTime || headFalling() || mHeadDrop > mFallSpeed)
            {
                reach(s, out);
            }
            else if (mGuarding)
            {
                guard(s, out); // No time to reach: keep the guard (never starts one in the air: founder, it looked odd)
            }
            break;
        case Stage::Landing:
            mImpactClock.tick(s.dt);
            if (groundedAboveLegs(s))
            {
                // Soft landing: the arms meet the ground and give instead of locking - reaching or guarding alike (a
                // guard's tense arms stayed locked on the ground otherwise).
                out.setTension(DD_REGION_ARM_LEFT, DD_TENSION_RELAXED, mLandingYield);
                out.setTension(DD_REGION_ARM_RIGHT, DD_TENSION_RELAXED, mLandingYield);
                mReaching = false;
                mGuarding = false;
                mArmsDown = false; // A new stretch on the ground: guard while it slides
                mStage = Stage::Tumble;
            }
            else if (mReaching)
            {
                reach(s, out); // Still in the air: keep aiming
            }
            break;
        case Stage::Tumble:
            mImpactClock.tick(s.dt);
            if (s.senses.airborne != 0 && s.senses.landing_seconds >= mReachMinTime)
            {
                mStage = Stage::Airborne; // Bounced up
                mArmsDown = false;
            }
            else if (s.stillSeconds > 0.0f && !mArmsDown)
            {
                // Stopped: nothing left to protect the head from - now it hurts. Struck from behind on the trunk or head:
                // it arches back; otherwise it curls up.
                mHurtBack = hurtFromBehind(s);
                out.usePose(mHurtBack ? "hurt_back" : "hurt", kCurlRegions | DD_REGION_BIT(DD_REGION_HEAD_NECK));
                for (const DdRegion r : {DD_REGION_TRUNK, DD_REGION_LEG_LEFT, DD_REGION_LEG_RIGHT, DD_REGION_HEAD_NECK})
                    out.setTension(r, DD_TENSION_BRACED, mHurtBlend);
                out.setTension(DD_REGION_ARM_LEFT, DD_TENSION_TENSE, mHurtBlend);
                out.setTension(DD_REGION_ARM_RIGHT, DD_TENSION_TENSE, mHurtBlend);
                mArmsDown = true;
                mStage = Stage::Hurt;
            }
            else if (!mArmsDown)
            {
                looseGuard(s, out); // Still sliding (once settled, a nudge does not bring the guard back)
            }
            break;
        case Stage::Hurt:
            if (s.senses.airborne != 0 && s.senses.landing_seconds >= mReachMinTime)
            {
                mArmsDown = false;
                mStage = Stage::Airborne; // Thrown again (another hit)
            }
            else
            {
                holdHurt(s, out);
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
        case Stage::Airborne: return mReaching ? "airborne: reach" : mGuarding ? "airborne: guard" : "airborne";
        case Stage::Landing: return "landing";
        case Stage::Tumble: return "tumble";
        case Stage::Hurt: return mHurtText;
        case Stage::Limp: return "limp";
        }
        return "";
    }

private:
    enum class Stage { Watch, Anticipate, Recover, Impact, Carried, Airborne, Landing, Tumble, Hurt, Limp };

    // Supported by the vehicle and not by the ground.
    static bool carried(const Percept& s) { return s.senses.carried_by != DD_INVALID_COLLIDER && s.senses.ground_bodies == 0; }
    // Off the vehicle for a moment (airborne, or on the ground).
    bool released() const { return mApart >= mReleaseSeconds; }

    void enterImpact(const Percept& s, Command& out)
    {
        mHurtBody = -1;
        mHurtDv = 0.0f;
        mReaching = false;
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
                (s.tension[region] == DD_TENSION_TENSE || s.tension[region] == DD_TENSION_BRACED))
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

    void leaveVehicle(const Percept& s, Command& out)
    {
        for (const DdRegion r : {DD_REGION_HEAD_NECK, DD_REGION_TRUNK, DD_REGION_LEG_LEFT, DD_REGION_LEG_RIGHT})
        {
            out.setTension(r, mAfterTension, mAfterBlend);
        }
        if (mAirborneCurl)
        {
            out.usePose("curl", kCurlRegions); // Tucked in the air
        }
        mArmsDown = false;
        mStage = s.senses.airborne != 0 ? Stage::Airborne : Stage::Tumble;
    }

    // Reach: both hands towards where the head will meet the ground (ballistic: the head carried by the body's
    // horizontal velocity for the time to landing, at ground level), elbows a little bent; chin tucked.
    void reach(const Percept& s, Command& out)
    {
        if (s.bodies == nullptr || s.headBody < 0 || static_cast<uint32_t>(s.headBody) >= s.bodyCount || s.senses.ground_height < 0.0f)
        {
            return;
        }
        if (!mReaching)
        {
            out.setTension(DD_REGION_ARM_LEFT, DD_TENSION_TENSE, mReachBlend); // Arms move in, not snap
            out.setTension(DD_REGION_ARM_RIGHT, DD_TENSION_TENSE, mReachBlend);
            if (!mGuarding)
            {
                out.usePose("guard", DD_REGION_BIT(DD_REGION_HEAD_NECK)); // Chin tucked
            }
            mReaching = true;
        }
        const DdVec3 head = s.bodies[s.headBody].position, up = s.up, v = s.velocity, com = s.centreOfMass;
        const float vUp = v.x * up.x + v.y * up.y + v.z * up.z;
        const DdVec3 vh{v.x - up.x * vUp, v.y - up.y * vUp, v.z - up.z * vUp};
        const float headAboveGround = (head.x - com.x) * up.x + (head.y - com.y) * up.y + (head.z - com.z) * up.z + s.senses.ground_height;
        const float t = std::fmax(0.0f, s.senses.landing_seconds);
        const DdVec3 target{head.x + vh.x * t - up.x * headAboveGround, head.y + vh.y * t - up.y * headAboveGround,
                            head.z + vh.z * t - up.z * headAboveGround};
        out.aimAt(DD_REGION_ARM_LEFT, target, mReachExtension);
        out.aimAt(DD_REGION_ARM_RIGHT, target, mReachExtension);
    }

    // Head height above the ground and how fast it drops (from its last position).
    void trackHead(const Percept& s)
    {
        mHeadDrop = 0.0f;
        mHeadHeight = 1e9f;
        if (s.bodies == nullptr || s.headBody < 0 || static_cast<uint32_t>(s.headBody) >= s.bodyCount || s.senses.ground_height < 0.0f)
        {
            mHaveHead = false;
            return;
        }
        const DdVec3 head = s.bodies[s.headBody].position, com = s.centreOfMass, up = s.up;
        const float headUp = head.x * up.x + head.y * up.y + head.z * up.z;
        mHeadHeight = headUp - (com.x * up.x + com.y * up.y + com.z * up.z) + s.senses.ground_height;
        if (mHaveHead && s.dt > 0.0f)
        {
            mHeadDrop = (mLastHeadUp - headUp) / s.dt;
        }
        mLastHeadUp = headUp;
        mHaveHead = true;
    }
    // Something other than the legs touches the ground: the body has landed (standing on the feet is not landing).
    static bool groundedAboveLegs(const Percept& s)
    {
        for (uint32_t i = 0; i < s.bodyCount && i < kMaxBodies; ++i)
        {
            const DdRegion r = s.bodyRegion[i];
            if ((s.contacts[i].flags & DD_CONTACT_GROUND) != 0 && r != DD_REGION_LEG_LEFT && r != DD_REGION_LEG_RIGHT)
            {
                return true;
            }
        }
        return false;
    }
    // The head is coming down to the ground: close to it and dropping.
    bool headFalling() const { return mHeadHeight < mApproachHeight && mHeadDrop > mApproachSpeed; }

    // The first body of a region in rig order (an arm's upper arm, the trunk's pelvis).
    int32_t firstBody(const Percept& s, DdRegion region) const
    {
        for (uint32_t i = 0; i < s.bodyCount && i < kMaxBodies; ++i)
            if (s.bodyRegion[i] == region) return static_cast<int32_t>(i);
        return -1;
    }

    static DdVec3 rotate(DdQuat q, DdVec3 v)
    {
        // v' = v + 2w (q x v) + 2 q x (q x v)
        const DdVec3 u{q.x, q.y, q.z};
        const auto cross = [](DdVec3 a, DdVec3 b) { return DdVec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; };
        const DdVec3 t = cross(u, v), tt = cross(u, t);
        return DdVec3{v.x + 2.0f * (q.w * t.x + tt.x), v.y + 2.0f * (q.w * t.y + tt.y), v.z + 2.0f * (q.w * t.z + tt.z)};
    }

    // Where it hurts: the non-arm body with the largest velocity change since the hit (the arms do the holding), and the
    // side it was struck from (the contact normal points from the other thing into the body), kept in that body's
    // frame so it turns with it.
    void trackHurt(const Percept& s)
    {
        if (s.kinematic || s.contacts == nullptr || s.bodies == nullptr) return;
        for (uint32_t i = 0; i < s.bodyCount && i < kMaxBodies; ++i)
        {
            const DdRegion r = s.bodyRegion[i];
            if (r == DD_REGION_ARM_LEFT || r == DD_REGION_ARM_RIGHT || s.contacts[i].flags == 0) continue;
            if (s.contacts[i].delta_v > mHurtDv)
            {
                // A leg is held at the knee (both hands on the thigh): a shin or foot blow included - the hands cannot
                // reach the foot of a body curled up on the ground.
                int32_t held = static_cast<int32_t>(i);
                if (r == DD_REGION_LEG_LEFT || r == DD_REGION_LEG_RIGHT) held = firstBody(s, r);
                if (held < 0) continue;
                mHurtDv = s.contacts[i].delta_v;
                mHurtBody = held;
                const DdQuat q = s.bodies[held].rotation;
                mHurtNormalLocal = rotate(DdQuat{-q.x, -q.y, -q.z, q.w}, s.contacts[i].normal);
            }
        }
    }

    // Struck from behind: the hurt part is the trunk or the head and the blow pushed it forwards.
    bool hurtFromBehind(const Percept& s) const
    {
        if (mHurtBody < 0 || static_cast<uint32_t>(mHurtBody) >= s.bodyCount || s.bodies == nullptr) return false;
        const DdRegion r = s.bodyRegion[mHurtBody];
        if (r != DD_REGION_TRUNK && r != DD_REGION_HEAD_NECK) return false;
        const DdVec3 n = rotate(s.bodies[mHurtBody].rotation, mHurtNormalLocal);
        return n.x * s.trunkForward.x + n.y * s.trunkForward.y + n.z * s.trunkForward.z > 0.4f;
    }

    // Hurt: both hands on the struck side of the part that hurts most, side by side (aimed at one spot the hands
    // collide and push each other away).
    void holdHurt(const Percept& s, Command& out)
    {
        if (s.bodies == nullptr) return;
        int32_t body = mHurtBody;
        if (body < 0 || static_cast<uint32_t>(body) >= s.bodyCount)
        {
            body = firstBody(s, DD_REGION_TRUNK) >= 0 ? firstBody(s, DD_REGION_TRUNK) + 1 : -1; // Nothing recorded: the belly
            if (body < 0 || static_cast<uint32_t>(body) >= s.bodyCount) return;
        }
        const DdRegion region = s.bodyRegion[body];
        DdVec3 n = mHurtBody == body ? rotate(s.bodies[body].rotation, mHurtNormalLocal) : DdVec3{-s.trunkForward.x, -s.trunkForward.y, -s.trunkForward.z};
        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < 1e-4f) return;
        n = DdVec3{n.x / len, n.y / len, n.z / len};
        // The surface where it was struck (the normal points into the body), plus the hand's own thickness. A struck side
        // that faces the ground (most blows are the landing's) cannot be held - the body lies on it: the hands go to
        // the top of that part instead (the face or forehead of a head lying on its back, the top of a thigh).
        const float depth = region == DD_REGION_HEAD_NECK ? 0.13f : region == DD_REGION_TRUNK ? 0.16f : 0.11f;
        DdVec3 c = s.bodies[body].position;
        if ((region == DD_REGION_LEG_LEFT || region == DD_REGION_LEG_RIGHT) && static_cast<uint32_t>(body) + 1 < s.bodyCount &&
            s.bodyRegion[body + 1] == region)
        {
            // A leg is held at the knee (between the thigh's and the shin's centres): drawn up towards the chest, both
            // hands reach it.
            const DdVec3 shin = s.bodies[body + 1].position;
            c = DdVec3{(c.x + shin.x) * 0.5f, (c.y + shin.y) * 0.5f, (c.z + shin.z) * 0.5f};
        }
        if (-(n.x * s.up.x + n.y * s.up.y + n.z * s.up.z) < 0.0f) n = DdVec3{-s.up.x, -s.up.y, -s.up.z};
        const DdVec3 spot{c.x - n.x * depth, c.y - n.y * depth, c.z - n.z * depth};
        // Side by side across the struck surface.
        DdVec3 t{n.y * s.up.z - n.z * s.up.y, n.z * s.up.x - n.x * s.up.z, n.x * s.up.y - n.y * s.up.x};
        float tl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        if (tl < 0.3f)
        {
            t = s.trunkLeft;
            tl = 1.0f;
        }
        t = DdVec3{t.x / tl * mHurtSpread, t.y / tl * mHurtSpread, t.z / tl * mHurtSpread};
        const bool leftOnPlus = t.x * s.trunkLeft.x + t.y * s.trunkLeft.y + t.z * s.trunkLeft.z >= 0.0f;
        const DdVec3 a{spot.x + t.x, spot.y + t.y, spot.z + t.z}, b{spot.x - t.x, spot.y - t.y, spot.z - t.z};
        out.aimAt(DD_REGION_ARM_LEFT, leftOnPlus ? a : b, mHurtHold);
        out.aimAt(DD_REGION_ARM_RIGHT, leftOnPlus ? b : a, mHurtHold);
        std::snprintf(mHurtText, sizeof(mHurtText), "hurt: body %d, %s", body, mHurtBack ? "back" : "front");
    }

    // On the ground: the forearms loosely between the head and the ground (relaxed arms: it does not fight the slide).
    void looseGuard(const Percept& s, Command& out)
    {
        if (s.bodies == nullptr || s.headBody < 0 || static_cast<uint32_t>(s.headBody) >= s.bodyCount)
        {
            return;
        }
        const DdVec3 head = s.bodies[s.headBody].position;
        const DdVec3 target{head.x - s.up.x * mGuardOffset, head.y - s.up.y * mGuardOffset, head.z - s.up.z * mGuardOffset};
        out.aimAt(DD_REGION_ARM_LEFT, target, mTumbleExtension);
        out.aimAt(DD_REGION_ARM_RIGHT, target, mTumbleExtension);
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
    bool mStartled = false, mShielding = false, mBraced = false, mPendingCurl = false, mPrimed = false, mGuarding = false, mReaching = false;
    float mQuiet = 0.0f, mApart = 0.0f;
    DdVec3 mTravel{1, 0, 0};
    float mShieldReach = 0.55f, mShieldRaise = 0.15f, mShieldExtension = 0.85f;
    DdTension mBefore[DD_REGION_COUNT] = {};
    float mAnticipateTime = 1.2f, mAwarenessNeeded = 0.5f, mStartleBlend = 0.08f, mRecoverDelay = 0.6f, mRecoverBlend = 0.5f;
    float mStartleBurst = 0.3f, mStartleEase = 0.3f, mSinceStartle = 0.0f;
    bool mStartleCrouch = true, mEased = false, mArmsDown = false;
    float mHurtBlend = 0.6f, mHurtHold = 0.95f, mHurtSpread = 0.06f, mHurtDv = 0.0f;
    int32_t mHurtBody = -1;
    DdVec3 mHurtNormalLocal{};
    bool mHurtBack = false;
    char mHurtText[48] = "hurt";
    float mYieldBlend = 0.02f, mBraceBlend = 0.05f, mCarriedRelax = 0.5f, mReleaseSeconds = 0.1f;
    float mGuardOffset = 0.12f, mGuardExtension = 0.5f, mGuardUnaware = 0.1f, mGuardBlend = 0.3f, mAfterBlend = 0.3f;
    DdTension mAfterTension = DD_TENSION_RELAXED;
    float mApproachHeight = 0.9f, mApproachSpeed = 0.8f, mHeadHeight = 1e9f, mHeadDrop = 0.0f, mLastHeadUp = 0.0f;
    bool mHaveHead = false, mBraceOn = true;
    float mFallUpright = 0.8f, mFallSpeed = 0.5f;
    bool mAirborneCurl = false;
    float mReachMinTime = 0.12f, mReachExtension = 0.9f, mReachBlend = 0.15f, mLandingLead = 0.12f, mLandingYield = 0.08f, mTumbleExtension = 0.4f;
};
} // namespace

namespace dragdoll::sdk
{
void registerBuiltinBehaviours()
{
    registerBehaviour("car_hit", []() -> std::unique_ptr<Behaviour> { return std::make_unique<CarHit>(); });
}
} // namespace dragdoll::sdk
