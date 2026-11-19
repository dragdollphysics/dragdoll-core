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
//                overpowered, nothing is decided). After the reflex latency: trunk and neck co-contract, and the
//                startle at the hit (task 4.11b; Hsiao & Robinovitch 1998: the wrist first jerks up, then drops;
//                Brown et al. 1991: the startle pattern - head forward, arms flexed and drawn in): a fast tense jerk
//                into pose "startle_hit" (head and upper back forward, upper arms forward, elbows bent, hands up in
//                front of the chest) held hit_startle.hold, before any guard or reach; then the arms ease to braced.
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
//                slide; once it is down and slowing (centre of mass under hurt.start_speed, founder 2026-11-19: the
//                game stands a survivor up about 2 s after the hit) it goes to Hurt; a bounce into the
//                air goes back to Airborne.
//   Any fall (not only after a vehicle): a body that is physics-driven, off any vehicle and either airborne or
//                tipped over (upright < fall.upright) with the head dropping starts the reflex clock and goes to
//                Airborne: after the reflex latency the hands reach for the ground (founder, 2026-11-17: "instinct"
//                reactions - landing on the hands, covering the face - are what reads as natural).
//   Hurt       — (the hands rub the spot slowly once there, task 4.11d) alive, on the ground and stopped: a person in pain (founder, 2026-11-19, with reference photos): knees
//                and hips drawn up, trunk curled, head tucked (pose "hurt"); struck from behind on the trunk or head it
//                arches back instead (pose "hurt_back"). Both hands go to where it hurts most (founder, 2026-11-19:
//                "focus on where the hands end, the arms follow"): the non-arm body with the largest velocity change
//                since the hit, on the side it was struck from (the contact normal then, kept in that body's frame),
//                side by side. Arms tense to hold, the rest braced so the curl / arch really happens. (Replaces a version where the arms just let go:
//                "looks bad"; before that the guard stayed on: hands folded at the face, "praying".)
//   Dead       — dying (vitality below dying.vitality: the host's outcome is "killed"): the reflexes in the air stay
//                (founder, 2026-11-18: even dead, "they do their muscular things in the air"), but once stopped on the
//                ground there is no pain: the whole body relaxes, the arms settle along it (no guard, no gestures).
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
        mApproachHeight = p.number("reach.approach_height", 1.2f);
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
        mDyingVitality = p.number("dying.vitality", 0.5f);
        mHurtStartSpeed = p.number("hurt.start_speed", 2.0f);
        mPalmsDown = p.number("reach.palms_down", 1.0f) > 0.5f;
        mHitStartleOn = p.number("hit_startle.on", 1.0f) > 0.5f;
        mHitStartleHold = p.number("hit_startle.hold", 0.25f);
        mHitStartleBlend = p.number("hit_startle.blend", 0.04f);
        mHitStartleEase = p.number("hit_startle.ease", 0.3f);
        mHoverShare = p.number("reach.hover", 1.0f);
        mTurnOn = p.number("turn.on", 1.0f) > 0.5f;
        mTumbleReach = p.number("tumble.reach", 1.0f) > 0.5f;
        mSecondHand = p.number("landing.second_hand", 0.25f);
        mLandingGive = static_cast<DdTension>(static_cast<int>(p.number("landing.give_tension", 1.0f))); // 1 relaxed, 2 braced
        mBackReach = p.number("reach.back", 0.15f);
        mBackSide = p.number("reach.back_side", 0.35f);
        mBackBehind = p.number("reach.back_behind", 0.05f);
        mBackHover = p.number("reach.back_hover", 0.5f);
        mBrakeHeight = p.number("reach.brake_height", 0.4f);
        mBrakeExtension = p.number("reach.brake_extension", 0.7f);
        mBrakeSpeed = p.number("reach.brake_speed", 0.5f);
        mBackAcross = p.number("reach.back_across", 0.3f);
        mBackAcrossSide = p.number("reach.back_across_side", 0.15f);
        mBackAcrossFront = p.number("reach.back_across_front", 0.2f);
        mBackExtension = p.number("reach.back_extension", 0.9f);
        mTurnSide = p.number("turn.side", 0.7f);
        mTurnMaxRoll = p.number("turn.max_roll", 3.0f);
        mTurnLead = p.number("turn.lead", 0.35f);
        mTurnBlend = p.number("turn.blend", 0.1f);
        mRubAmplitude = p.number("rub.amplitude", 0.035f);
        mBreathRate = p.number("breath.rate", 0.45f);
        mGuardHurt = p.number("hurt.guard", 1.0f) > 0.5f;
        mHoldBand = p.number("hurt.hold_band", 0.3f);
        mHurtArmTension = static_cast<DdTension>(static_cast<int>(p.number("hurt.arm_tension", 4.0f))); // 2 tense, 4 braced
        mSupineUp = p.number("hurt.supine_up", 0.5f);
        mWinceMin = p.number("wince.min", 3.0f);
        mWinceMax = p.number("wince.max", 6.0f);
        mWinceHold = p.number("wince.hold", 0.25f);
        mBreathInShare = p.number("breath.in_share", 0.4f);
        mBreathSighEvery = static_cast<int>(p.number("breath.sigh_every", 5.0f));
        mBreathDelay = p.number("breath.delay", 0.3f);
        mRubFrequency = p.number("rub.frequency", 0.9f);
        mRubDelay = p.number("rub.delay", 0.4f);
        mRubEase = p.number("rub.ease", 0.5f);
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
        if (mHitStartleLeft > 0.0f)
        {
            mHitStartleLeft -= s.dt;
            if (mHitStartleLeft <= 0.0f && !mGuarding && !mReaching)
            {
                out.setTension(DD_REGION_ARM_LEFT, DD_TENSION_BRACED, mHitStartleEase); // The jerk is over: the arms soften
                out.setTension(DD_REGION_ARM_RIGHT, DD_TENSION_BRACED, mHitStartleEase);
            }
        }
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
                if (mHitStartleOn)
                {
                    // The startle at the hit: a fast jerk, arms drawn up and in, head and upper back forward.
                    out.usePose("startle_hit", kArms | DD_REGION_BIT(DD_REGION_HEAD_NECK) | DD_REGION_BIT(DD_REGION_TRUNK));
                    out.setTension(DD_REGION_ARM_LEFT, DD_TENSION_TENSE, mHitStartleBlend);
                    out.setTension(DD_REGION_ARM_RIGHT, DD_TENSION_TENSE, mHitStartleBlend);
                    mHitStartleLeft = mHitStartleHold;
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
                // guard's tense arms stayed locked on the ground otherwise). An arm still reaching whose hand has not
                // touched yet keeps reaching for up to landing.second_hand s (the falls study: both wrists landed in 26
                // of 28 falls, 0-233 ms apart; relaxing both at the first touch left the other hand in the air).
                landArms(s, out);
                if (mTurned)
                {
                    out.usePose("rest", DD_REGION_BIT(DD_REGION_TRUNK)); // Untwist; relaxed, it rolls on (task 4.11g)
                    out.setTension(DD_REGION_TRUNK, mAfterTension, mAfterBlend);
                    mTurned = false;
                }
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
            else if (downAndSlowing(s) && !mArmsDown && s.vitality < mDyingVitality)
            {
                // Dying: stopped on the ground - no pain, the body relaxes and the arms settle along it.
                out.setTensionAll(DD_TENSION_RELAXED, 0.3f);
                out.usePose("rest", kArms);
                mArmsDown = true;
                mReaching = mSecondHandTiming = false;
                mStage = Stage::Dead;
            }
            else if (downAndSlowing(s) && !mArmsDown)
            {
                // Stopped: nothing left to protect the head from - now it hurts. Struck from behind on the trunk or head:
                // it arches back; otherwise it curls up.
                mHurtBack = hurtFromBehind(s);
                // Lying on the back (belly up): knees bent with the feet on the ground ("_supine" poses) and the legs
                // relaxed - the knees-to-chest curl held both legs up in the air (founder, 2026-11-19: "legs always
                // tense like this, is that right?"). On the side or the front the curl rests on the ground.
                const bool supine = s.trunkForward.x * s.up.x + s.trunkForward.y * s.up.y + s.trunkForward.z * s.up.z > mSupineUp;
                out.usePose(mHurtBack ? (supine ? "hurt_back_supine" : "hurt_back") : (supine ? "hurt_supine" : "hurt"),
                            kCurlRegions | DD_REGION_BIT(DD_REGION_HEAD_NECK));
                for (const DdRegion r : {DD_REGION_TRUNK, DD_REGION_LEG_LEFT, DD_REGION_LEG_RIGHT, DD_REGION_HEAD_NECK})
                {
                    const bool leg = r == DD_REGION_LEG_LEFT || r == DD_REGION_LEG_RIGHT;
                    out.setTension(r, supine && leg ? DD_TENSION_RELAXED : DD_TENSION_BRACED, mHurtBlend);
                }
                out.setTension(DD_REGION_ARM_LEFT, mHurtArmTension, mHurtBlend);
                out.setTension(DD_REGION_ARM_RIGHT, mHurtArmTension, mHurtBlend);
                // Guarding (task 4.11f; Keefe & Block 1982; Williams 2002: resting and protecting the injured part):
                // the part that hurts is held still - its region tense - while a leg that does not hurt is let go.
                if (mGuardHurt && mHurtBody >= 0 && static_cast<uint32_t>(mHurtBody) < s.bodyCount)
                {
                    const DdRegion hurt = s.bodyRegion[mHurtBody];
                    if (hurt != DD_REGION_ARM_LEFT && hurt != DD_REGION_ARM_RIGHT) out.setTension(hurt, DD_TENSION_TENSE, mHurtBlend);
                    if (hurt == DD_REGION_LEG_LEFT) out.setTension(DD_REGION_LEG_RIGHT, DD_TENSION_RELAXED, mHurtBlend);
                    if (hurt == DD_REGION_LEG_RIGHT) out.setTension(DD_REGION_LEG_LEFT, DD_TENSION_RELAXED, mHurtBlend);
                }
                mArmsDown = true;
                mReaching = mSecondHandTiming = false;
                mHurtSeconds = 0.0f;
                mHoldTop = mLeftOnPlus = -1;
                mWinceLeft = 0.0f;
                mWinceIn = mWinceMin;
                mBreathClock = 0.0f;
                mBreaths = 0;
                mBreathState = 0;
                mStage = Stage::Hurt;
            }
            else if (mTumbleReach && !mArmsDown && !groundedAboveLegs(s) && (headFalling() || mHeadDrop > mFallSpeed))
            {
                // Still falling with nothing above the legs on the ground yet - slid or tipped off the vehicle without
                // flying: the hands go to the ground too (the falls study's protective reach; before, these bodies held
                // the loose guard and landed without their hands: both hands in 36 % of such landings, 59 % after a
                // reach).
                reach(s, out);
            }
            else if (mReaching && groundedAboveLegs(s))
            {
                // Landed from a reach: each arm gives once its hand is down; the other keeps reaching a moment.
                if (!mSecondHandTiming) landArms(s, out);
                else if ((mSecondHandLeft -= s.dt) <= 0.0f || (mArmLanded[0] && mArmLanded[1])) endReach(out);
                else
                {
                    landArms(s, out);
                    if (mReaching) reach(s, out, false); // The body is down: straight to the ground, no hover
                }
            }
            else if (!mArmsDown && !mReaching)
            {
                looseGuard(s, out); // Still sliding (once settled, a nudge does not bring the guard back)
            }
            break;
        case Stage::Dead:
            if (s.senses.airborne != 0 && s.senses.landing_seconds >= mReachMinTime)
            {
                mArmsDown = false;
                mStage = Stage::Airborne; // Thrown again (another hit): the reflexes are back
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
                mHurtSeconds += s.dt;
                breathe(s, out);
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
        case Stage::Airborne: return mReaching ? (mTurned ? "airborne: reach + turn" : "airborne: reach") : mGuarding ? "airborne: guard" : "airborne";
        case Stage::Landing: return mTurned ? "landing: turn" : "landing";
        case Stage::Tumble: return mReaching ? "tumble: reach" : "tumble";
        case Stage::Hurt: return mHurtText;
        case Stage::Limp: return "limp";
        case Stage::Dead: return "dead";
        }
        return "";
    }

private:
    enum class Stage { Watch, Anticipate, Recover, Impact, Carried, Airborne, Landing, Tumble, Hurt, Dead, Limp };

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
        mHitStartleLeft = 0.0f;
        mTurned = false;
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
    void reach(const Percept& s, Command& out, bool hover = true)
    {
        if (mHitStartleLeft > 0.0f) return; // Startled: the arms are still in the jerk
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
            mHandBraking[0] = mHandBraking[1] = false;
        }
        const DdVec3 head = s.bodies[s.headBody].position, up = s.up, v = s.velocity, com = s.centreOfMass;
        const float vUp = v.x * up.x + v.y * up.y + v.z * up.z;
        const DdVec3 vh{v.x - up.x * vUp, v.y - up.y * vUp, v.z - up.z * vUp};
        const float headAboveGround = (head.x - com.x) * up.x + (head.y - com.y) * up.y + (head.z - com.z) * up.z + s.senses.ground_height;
        const float t = std::fmax(0.0f, s.senses.landing_seconds);
        const DdVec3 target{head.x + vh.x * t - up.x * headAboveGround, head.y + vh.y * t - up.y * headAboveGround,
                            head.z + vh.z * t - up.z * headAboveGround};
        // Palms down: the hands meet the ground palm first, then the arms give (founder, 2026-11-19).
        const DdVec3 down = mPalmsDown ? DdVec3{-up.x, -up.y, -up.z} : DdVec3{};
        // Hold back (task 4.11c; Hsiao & Robinovitch 1998, Fig. 6): people swing the hands down, then brake them so
        // they land together with the pelvis (within 50 ms in 72 % of falls) at about 2/3 of their top speed. The
        // reach swung the hands at the ground point itself - up to 10 m/s while the body fell at 2-3 m/s, landing
        // 100 ms ahead of it. Now the hands aim above the ground by a share (reach.hover) of how high the lowest part
        // of the trunk or the head still is: they wait there and come down with the body.
        // Turn to face the ground (task 4.11g; Hsiao & Robinovitch 1998, Fig. 7: in sideways falls the trunk turned
        // 37-62 deg about its long axis late in the descent, so that both hands landed and the hip was spared): falling
        // on a side (the trunk's left axis within turn.side of vertical) and the ground due within turn.lead, the spine
        // and the neck twist the chest towards the ground (poses "turn_left" / "turn_right"), the trunk tensed for it.
        // Once per fall; on landing the trunk lets go again.
        // Not while the body is already rolling about its long axis (turn.max_roll, kg m^2/s): the twist would fight or
        // follow the roll and change nothing.
        const float leftUp = s.trunkLeft.x * up.x + s.trunkLeft.y * up.y + s.trunkLeft.z * up.z;
        const DdVec3 f = s.trunkForward, l = s.trunkLeft, L = s.senses.angular_momentum;
        const DdVec3 along{f.y * l.z - f.z * l.y, f.z * l.x - f.x * l.z, f.x * l.y - f.y * l.x}; // The trunk's long axis
        const float roll = std::fabs(L.x * along.x + L.y * along.y + L.z * along.z);
        if (mTurnOn && !mTurned && std::fabs(leftUp) > mTurnSide && roll < mTurnMaxRoll && s.senses.landing_seconds >= 0.0f &&
            s.senses.landing_seconds <= mTurnLead)
        {
            out.usePose(leftUp < 0.0f ? "turn_left" : "turn_right", DD_REGION_BIT(DD_REGION_TRUNK) | DD_REGION_BIT(DD_REGION_HEAD_NECK));
            out.setTension(DD_REGION_TRUNK, DD_TENSION_TENSE, mTurnBlend);
            mTurned = true;
        }
        DdVec3 aim = target;
        if (hover && mHoverShare > 0.0f)
        {
            float lowest = 1e9f;
            for (uint32_t i = 0; i < s.bodyCount && i < kMaxBodies; ++i)
            {
                if (s.bodyRegion[i] != DD_REGION_TRUNK && s.bodyRegion[i] != DD_REGION_HEAD_NECK) continue;
                const DdVec3 b = s.bodies[i].position;
                lowest = std::fmin(lowest, (b.x - com.x) * up.x + (b.y - com.y) * up.y + (b.z - com.z) * up.z + s.senses.ground_height);
            }
            const float lift = lowest < 1e8f ? mHoverShare * std::fmax(0.0f, lowest) : 0.0f;
            aim = DdVec3{target.x + up.x * lift, target.y + up.y * lift, target.z + up.z * lift};
        }
        // Falling backwards (the head will land behind the trunk): the hands go down beside and a little behind the
        // hips instead (Hsiao & Robinovitch 1998, Fig. 4: in backward falls the wrists land next to the pelvis, within
        // 50 ms of it) - the shoulders cannot reach back to where the head will land, and those hands never landed.
        // (f, l: the trunk's forward and left, from the turn above)
        const DdVec3 hips = s.root.position;
        const float behind = (target.x - hips.x) * f.x + (target.y - hips.y) * f.y + (target.z - hips.z) * f.z;
        if (mBackReach > 0.0f && behind < -mBackReach)
        {
            const float hipsUp = (hips.x - com.x) * up.x + (hips.y - com.y) * up.y + (hips.z - com.z) * up.z + s.senses.ground_height;
            const float lift = (aim.x - target.x) * up.x + (aim.y - target.y) * up.y + (aim.z - target.z) * up.z;
            const float drop = hipsUp - mBackHover * lift;
            for (int k = 0; k < 2; ++k)
            {
                // Falling back and to one side: the hand on the upper side cannot reach the ground beside its own hip
                // (in the air) - it reaches across, to the ground in front of the lower hip.
                const float sideUp = (k == 0 ? 1.0f : -1.0f) * (l.x * up.x + l.y * up.y + l.z * up.z);
                const bool across = sideUp > mBackAcross;
                const float side = (k == 0 ? 1.0f : -1.0f) * (across ? -mBackAcrossSide : mBackSide);
                const float back = across ? -mBackAcrossFront : mBackBehind;
                const DdVec3 p{hips.x + l.x * side - f.x * back - up.x * drop, hips.y + l.y * side - f.y * back - up.y * drop,
                               hips.z + l.z * side - f.z * back - up.z * drop};
                out.aimAt(k == 0 ? DD_REGION_ARM_LEFT : DD_REGION_ARM_RIGHT, p, armExtension(s, k, mBackExtension), down);
            }
            return;
        }
        out.aimAt(DD_REGION_ARM_LEFT, aim, armExtension(s, 0, mReachExtension), down);
        out.aimAt(DD_REGION_ARM_RIGHT, aim, armExtension(s, 1, mReachExtension), down);
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

    // Down on the ground and slowing (the pain may start while the last of the slide runs out): the game gives a
    // survivor about 2 s from the hit to its get-up (measured), and a body takes 2-3 s to come to a full stop.
    bool downAndSlowing(const Percept& s) const
    {
        // Down = something above the legs lies on the ground: a pedestrian knocked over slowly still stands (or kneels)
        // on its feet at first - the pain waited for nothing then, and it fell over curled, without its hands.
        if (s.senses.airborne != 0 || s.senses.ground_bodies == 0 || !groundedAboveLegs(s)) return false;
        const float v = std::sqrt(s.velocity.x * s.velocity.x + s.velocity.y * s.velocity.y + s.velocity.z * s.velocity.z);
        return s.stillSeconds > 0.0f || v < mHurtStartSpeed;
    }

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
        // Which side to hold, and which hand goes where, stick once chosen until the body clearly turns (a band of
        // hurt.hold_band): lying on a side both choices sat on their tipping point and flipped step to step - the hands
        // swapped places and the arm bounced (founder, 2026-11-19: "right arm looks stiff, bouncy").
        const float facing = -(n.x * s.up.x + n.y * s.up.y + n.z * s.up.z);
        if (mHoldTop < 0) mHoldTop = facing < 0.0f ? 1 : 0;
        else if (mHoldTop == 1 && facing > mHoldBand) mHoldTop = 0;
        else if (mHoldTop == 0 && facing < -mHoldBand) mHoldTop = 1;
        if (mHoldTop == 1) n = DdVec3{-s.up.x, -s.up.y, -s.up.z};
        const DdVec3 spot{c.x - n.x * depth, c.y - n.y * depth, c.z - n.z * depth};
        // Side by side across the struck surface.
        DdVec3 t{n.y * s.up.z - n.z * s.up.y, n.z * s.up.x - n.x * s.up.z, n.x * s.up.y - n.y * s.up.x};
        float tl = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        if (tl < 0.3f)
        {
            // Holding the top: side by side along the ground - across the body when it lies on its back or front,
            // along it (head to feet) when it lies on its side. The trunk's left axis alone pointed straight up on a
            // side: one hand above the spot and one in the road, the arm pushing into it and springing back (founder,
            // 2026-11-19: "right arm looks stiff, bouncy").
            const auto level = [&](DdVec3 v) {
                const float k = v.x * s.up.x + v.y * s.up.y + v.z * s.up.z;
                return DdVec3{v.x - s.up.x * k, v.y - s.up.y * k, v.z - s.up.z * k};
            };
            const DdVec3 across = level(s.trunkLeft);
            const DdVec3 f = s.trunkForward, l = s.trunkLeft;
            const DdVec3 along = level(DdVec3{f.y * l.z - f.z * l.y, f.z * l.x - f.x * l.z, f.x * l.y - f.y * l.x});
            const float a1 = std::sqrt(across.x * across.x + across.y * across.y + across.z * across.z);
            const float a2 = std::sqrt(along.x * along.x + along.y * along.y + along.z * along.z);
            t = a1 >= 0.5f || a1 >= a2 ? across : along;
            tl = std::fmax(1e-4f, a1 >= 0.5f || a1 >= a2 ? a1 : a2);
        }
        t = DdVec3{t.x / tl * mHurtSpread, t.y / tl * mHurtSpread, t.z / tl * mHurtSpread};
        const float leftward = t.x * s.trunkLeft.x + t.y * s.trunkLeft.y + t.z * s.trunkLeft.z;
        if (mLeftOnPlus < 0) mLeftOnPlus = leftward >= 0.0f ? 1 : 0;
        else if (mLeftOnPlus == 1 && leftward < -mHoldBand * mHurtSpread) mLeftOnPlus = 0;
        else if (mLeftOnPlus == 0 && leftward > mHoldBand * mHurtSpread) mLeftOnPlus = 1;
        const bool leftOnPlus = mLeftOnPlus == 1;
        DdVec3 a{spot.x + t.x, spot.y + t.y, spot.z + t.z}, b{spot.x - t.x, spot.y - t.y, spot.z - t.z};
        // Rubbing (task 4.11d; Keefe & Block 1982: guarding, bracing, rubbing, grimacing, sighing): once the hands are
        // there (rub.delay), each one circles slowly over the spot, the two half a turn apart - small (rub.amplitude)
        // and unhurried (rub.frequency), easing in over rub.ease.
        if (mRubAmplitude > 0.0f && mHurtSeconds > mRubDelay && mWinceLeft <= 0.0f)
        {
            const float tl2 = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
            const DdVec3 u = tl2 > 1e-5f ? DdVec3{t.x / tl2, t.y / tl2, t.z / tl2} : s.trunkLeft;
            const DdVec3 w{n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x}; // Along the surface
            const float ease = std::fmin(1.0f, (mHurtSeconds - mRubDelay) / std::fmax(mRubEase, 1e-3f));
            const float phase = 6.2831853f * mRubFrequency * (mHurtSeconds - mRubDelay);
            const auto circle = [&](DdVec3 p, float offset) {
                const float cu = std::cos(phase + offset) * mRubAmplitude * ease, cw = std::sin(phase + offset) * mRubAmplitude * ease;
                return DdVec3{p.x + u.x * cu + w.x * cw, p.y + u.y * cu + w.y * cw, p.z + u.z * cu + w.z * cw};
            };
            a = circle(a, 0.0f);
            b = circle(b, 3.14159265f);
        }
        out.aimAt(DD_REGION_ARM_LEFT, leftOnPlus ? a : b, mHurtHold);
        out.aimAt(DD_REGION_ARM_RIGHT, leftOnPlus ? b : a, mHurtHold);
        std::snprintf(mHurtText, sizeof(mHurtText), "hurt: body %d, %s", body, mHurtBack ? "back" : "front");
    }

    // Breathing in pain (task 4.11e; Keefe & Block 1982: sighing is one of the five observed pain behaviours): quick,
    // shallow breaths (breath.rate) - the chest opens on each inhale (the spine eases out of the curl a little, the
    // shoulders lift: pose "<hurt pose>_in"), back to the curl on the exhale ("_out") - and every breath.sigh_every-th
    // breath a sigh: a deeper, slower inhale ("_sigh") and a long exhale. Trunk and shoulder girdles only (the arms'
    // aims are set again right after); the braced trunk's springs smooth the switch between the two poses.
    void breathe(const Percept& s, Command& out)
    {
        if (mBreathRate <= 0.0f || mHurtSeconds < mBreathDelay) return;
        // Holding back (task 4.11f; Williams 2002: held-back pain reads as more real than shown pain): now and then
        // (every wince.min..wince.max s, deterministic) a short, small wince - the curl tightens a few degrees and the
        // shoulders hunch for wince.hold s, the breath stops and the hands stop rubbing - then the breathing goes on.
        if (mWinceMax > 0.0f)
        {
            if (mWinceLeft > 0.0f)
            {
                mWinceLeft -= s.dt;
                if (mWinceLeft > 0.0f) return;
                mBreathState = 0; // Back to the breath: its pose is set again below
                const uint64_t h = (s.step * 2654435761u) ^ (static_cast<uint64_t>(mBreaths) * 40503u);
                mWinceIn = mWinceMin + (mWinceMax - mWinceMin) * static_cast<float>(h % 1000u) / 1000.0f;
            }
            else if ((mWinceIn -= s.dt) <= 0.0f)
            {
                mWinceLeft = mWinceHold;
                out.usePose(mHurtBack ? "hurt_back_wince" : "hurt_wince",
                            DD_REGION_BIT(DD_REGION_TRUNK) | DD_REGION_BIT(DD_REGION_HEAD_NECK) | kArms);
                return;
            }
        }
        const bool sigh = mBreathSighEvery > 0 && mBreaths % mBreathSighEvery == mBreathSighEvery - 1;
        const float period = (sigh ? 2.2f : 1.0f) / mBreathRate;
        const float inShare = sigh ? 0.45f : mBreathInShare;
        mBreathClock += s.dt;
        int state = mBreathClock < period * inShare ? (sigh ? 2 : 1) : 3;
        if (mBreathClock >= period)
        {
            mBreathClock = 0.0f;
            ++mBreaths;
            state = mBreathSighEvery > 0 && mBreaths % mBreathSighEvery == mBreathSighEvery - 1 ? 2 : 1;
        }
        if (state == mBreathState) return;
        mBreathState = state;
        static const char* const kFront[4] = {"", "hurt_in", "hurt_sigh", "hurt_out"};
        static const char* const kBack[4] = {"", "hurt_back_in", "hurt_back_sigh", "hurt_back_out"};
        out.usePose((mHurtBack ? kBack : kFront)[state], DD_REGION_BIT(DD_REGION_TRUNK) | DD_REGION_BIT(DD_REGION_HEAD_NECK) | kArms);
    }

    // The last-moment brake (Hsiao & Robinovitch 1998, Fig. 6: just before contact the wrist accelerates upward again,
    // landing at about 2/3 of its top falling speed): once a hand still falling is within reach.brake_height of the
    // ground, its elbow bends (extension reach.brake_extension) - the hand is pulled back towards the shoulder while the
    // body keeps falling, so it slows. Per hand; its own height and falling speed (tracked step to step).
    float armExtension(const Percept& s, int k, float extension)
    {
        const DdRegion arm = k == 0 ? DD_REGION_ARM_LEFT : DD_REGION_ARM_RIGHT;
        int32_t hand = -1;
        for (uint32_t i = 0; i < s.bodyCount && i < kMaxBodies; ++i)
            if (s.bodyRegion[i] == arm) hand = static_cast<int32_t>(i);
        if (mBrakeHeight <= 0.0f || hand < 0 || s.bodies == nullptr) return extension;
        const DdVec3 h = s.bodies[hand].position, com = s.centreOfMass, up = s.up;
        const float height = (h.x - com.x) * up.x + (h.y - com.y) * up.y + (h.z - com.z) * up.z + s.senses.ground_height;
        const float level = h.x * up.x + h.y * up.y + h.z * up.z;
        const bool fresh = mHandStep[k] + 1 == s.step && s.dt > 0.0f;
        const float fall = fresh ? (mHandLevel[k] - level) / s.dt : 0.0f;
        mHandLevel[k] = level;
        mHandStep[k] = s.step;
        if (fall > mBrakeSpeed && height < mBrakeHeight) mHandBraking[k] = true;
        return mHandBraking[k] ? std::fmin(extension, mBrakeExtension) : extension;
    }

    // An arm touches the ground (any of its bodies).
    static bool armOnGround(const Percept& s, DdRegion arm)
    {
        for (uint32_t i = 0; i < s.bodyCount && i < kMaxBodies; ++i)
            if (s.bodyRegion[i] == arm && (s.contacts[i].flags & DD_CONTACT_GROUND) != 0) return true;
        return false;
    }

    // Landing: each arm whose hand is down gives; while reaching, one still in the air keeps reaching for up to
    // landing.second_hand s (Tumble carries it on). Not reaching (a guard): both give at once.
    void landArms(const Percept& s, Command& out)
    {
        const DdRegion arms[2] = {DD_REGION_ARM_LEFT, DD_REGION_ARM_RIGHT};
        if (!mReaching || mSecondHand <= 0.0f)
        {
            endReach(out);
            return;
        }
        if (!mSecondHandTiming)
        {
            mSecondHandTiming = true;
            mSecondHandLeft = mSecondHand;
            mArmLanded[0] = mArmLanded[1] = false;
        }
        for (int k = 0; k < 2; ++k)
        {
            if (mArmLanded[k] || !armOnGround(s, arms[k])) continue;
            out.setTension(arms[k], DD_TENSION_RELAXED, mLandingYield);
            mArmLanded[k] = true;
        }
        if (mArmLanded[0] && mArmLanded[1]) endReach(out);
    }

    void endReach(Command& out)
    {
        out.setTension(DD_REGION_ARM_LEFT, mLandingGive, mLandingYield);
        out.setTension(DD_REGION_ARM_RIGHT, mLandingGive, mLandingYield);
        mReaching = false;
        mSecondHandTiming = false;
    }

    // On the ground: the forearms loosely between the head and the ground (relaxed arms: it does not fight the slide).
    void looseGuard(const Percept& s, Command& out)
    {
        if (mHitStartleLeft > 0.0f) return; // Startled: the arms are still in the jerk
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
        if (mHitStartleLeft > 0.0f) return; // Startled: the arms are still in the jerk
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
    float mHurtBlend = 0.6f, mHurtHold = 0.95f, mHurtSpread = 0.06f, mHurtDv = 0.0f, mDyingVitality = 0.5f, mHurtStartSpeed = 2.0f;
    int32_t mHurtBody = -1;
    DdVec3 mHurtNormalLocal{};
    bool mHurtBack = false;
    bool mPalmsDown = true, mHitStartleOn = true;
    float mHoverShare = 1.0f;
    bool mTurnOn = true, mTurned = false;
    bool mTumbleReach = true, mSecondHandTiming = false, mArmLanded[2] = {false, false};
    float mSecondHand = 0.25f, mSecondHandLeft = 0.0f;
    DdTension mLandingGive = DD_TENSION_RELAXED;
    float mBackReach = 0.15f, mBackSide = 0.35f, mBackBehind = 0.05f, mBackHover = 0.5f, mBackExtension = 0.9f;
    float mBrakeHeight = 0.4f, mBrakeExtension = 0.7f, mBrakeSpeed = 0.5f, mHandLevel[2] = {0.0f, 0.0f};
    bool mHandBraking[2] = {false, false};
    uint64_t mHandStep[2] = {0, 0};
    float mBackAcross = 0.3f, mBackAcrossSide = 0.15f, mBackAcrossFront = 0.2f;
    float mTurnSide = 0.7f, mTurnMaxRoll = 3.0f, mTurnLead = 0.35f, mTurnBlend = 0.1f;
    bool mGuardHurt = true;
    int mHoldTop = -1, mLeftOnPlus = -1; // Hurt: hold the top of the part (1) / which hand on the + side (1); -1 = not chosen
    float mHoldBand = 0.3f;
    DdTension mHurtArmTension = DD_TENSION_BRACED;
    float mSupineUp = 0.5f; // Belly-up share of the trunk's forward above which the body lies on its back
    float mWinceMin = 3.0f, mWinceMax = 6.0f, mWinceHold = 0.25f, mWinceLeft = 0.0f, mWinceIn = 3.0f;
    float mBreathRate = 0.45f, mBreathInShare = 0.4f, mBreathDelay = 0.3f, mBreathClock = 0.0f;
    int mBreathSighEvery = 5, mBreaths = 0, mBreathState = 0;
    float mRubAmplitude = 0.035f, mRubFrequency = 0.9f, mRubDelay = 0.4f, mRubEase = 0.5f, mHurtSeconds = 0.0f;
    float mHitStartleHold = 0.25f, mHitStartleBlend = 0.04f, mHitStartleEase = 0.3f, mHitStartleLeft = 0.0f;
    char mHurtText[48] = "hurt";
    float mYieldBlend = 0.02f, mBraceBlend = 0.05f, mCarriedRelax = 0.5f, mReleaseSeconds = 0.1f;
    float mGuardOffset = 0.12f, mGuardExtension = 0.5f, mGuardUnaware = 0.1f, mGuardBlend = 0.3f, mAfterBlend = 0.3f;
    DdTension mAfterTension = DD_TENSION_RELAXED;
    float mApproachHeight = 1.2f, mApproachSpeed = 0.8f, mHeadHeight = 1e9f, mHeadDrop = 0.0f, mLastHeadUp = 0.0f;
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
