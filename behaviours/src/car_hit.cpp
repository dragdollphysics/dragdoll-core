// SPDX-License-Identifier: MIT
// "car_hit": the reaction chain for a pedestrian struck by a vehicle (ADR-0025). Stages so far:
//   Watch      — nothing; the body keeps whatever the host set.
//   Anticipate — Startle: once the character has been aware of the threat for the reflex latency, the whole body
//                tenses. Shield (fend): after the voluntary latency, both hands go towards the vehicle, elbows
//                slightly flexed (the "hands raised towards the vehicle" posture of the volunteer studies).
//                The stages after the impact come with tasks 4.6-4.9.
//   Recover    — the threat went by without contact: tension eases back to what it was.
// Includes only the behaviour SDK (ADR-0013).
#include <dragdoll/behaviour_sdk.h>

namespace
{
using namespace dragdoll::sdk;

class CarHit final : public Behaviour
{
public:
    void configure(const Params& p) override
    {
        mAnticipateTime = p.number("anticipate.time", 1.2f);
        mAwarenessNeeded = p.number("anticipate.awareness", 0.5f);
        mClock.reflexLatency = p.number("reaction.reflex", 0.1f);
        mClock.voluntaryLatency = p.number("reaction.voluntary", 0.5f);
        mStartleBlend = p.number("startle.blend", 0.08f);
        mShieldReach = p.number("shield.reach", 0.55f);
        mShieldRaise = p.number("shield.raise", 0.15f);
        mShieldExtension = p.number("shield.extension", 0.85f);
        mRecoverDelay = p.number("recover.delay", 0.6f);
        mRecoverBlend = p.number("recover.blend", 0.5f);
    }

    void update(const Percept& s, Command& out) override
    {
        const bool threat = s.threat.collider != DD_INVALID_COLLIDER;
        const bool alert = s.vitality > 0.0f && s.awareness >= mAwarenessNeeded;
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
                    out.usePose("rest", DD_REGION_BIT(DD_REGION_ARM_LEFT) | DD_REGION_BIT(DD_REGION_ARM_RIGHT)); // Arms down
                    mShielding = false;
                }
                mStage = Stage::Watch;
            }
            break;
        }
    }

    const char* status() const override
    {
        switch (mStage)
        {
        case Stage::Watch: return "watch";
        case Stage::Anticipate: return mShielding ? "anticipate: shield" : mStartled ? "anticipate: startle" : "anticipate";
        case Stage::Recover: return "recover";
        }
        return "";
    }

private:
    enum class Stage { Watch, Anticipate, Recover };
    Stage mStage = Stage::Watch;
    ReactionClock mClock;
    bool mStartled = false, mShielding = false;
    float mQuiet = 0.0f;
    float mShieldReach = 0.55f, mShieldRaise = 0.15f, mShieldExtension = 0.85f;
    DdTension mBefore[DD_REGION_COUNT] = {};
    float mAnticipateTime = 1.2f, mAwarenessNeeded = 0.5f, mStartleBlend = 0.08f, mRecoverDelay = 0.6f, mRecoverBlend = 0.5f;
};
} // namespace

namespace dragdoll::sdk
{
void registerBuiltinBehaviours()
{
    registerBehaviour("car_hit", []() -> std::unique_ptr<Behaviour> { return std::make_unique<CarHit>(); });
}
} // namespace dragdoll::sdk
