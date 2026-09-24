#include "melee_animation.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string_view>

#include <apps/openmw/mwmechanics/meleestate.hpp>
#include <components/sceneutil/animationkeys.hpp>

namespace TES3MP::Native
{
    MeleeAnimation::MeleeAnimation(const SceneUtil::TextKeyMap& keys, std::string group,
        std::string attack, float speed)
        : mGroup(std::move(group)), mSpeed(speed)
    {
        if (mGroup.empty() || mGroup.size() > 64 || !std::isfinite(speed) || speed <= 0 || speed > 100
            || (attack != "chop" && attack != "slash" && attack != "thrust"))
            throw std::invalid_argument("Invalid native melee animation input");
        // Validate before copying external resource data. This bounded slice
        // admits complete directional clips only, not random attacks or loops.
        size_t count = 0;
        for (const auto& [time, key] : keys)
        {
            if (++count > 4096 || !std::isfinite(time) || time < 0 || time > 3600 || key.size() > 256)
                throw std::invalid_argument("Native melee text-key bounds exceeded");
        }
        auto range = [&](const std::string& start, const std::string& stop) {
            SceneUtil::AnimationKeys found;
            if (!SceneUtil::findAnimationKeys(keys, mGroup, attack + ' ' + start, attack + ' ' + stop, found))
                throw std::invalid_argument("Incomplete native directional melee clip");
            return Range{found.mStart->first, found.mStop->first};
        };
        mWindUp = range("start", "max attack");
        mRelease = range("max attack", "hit");
        for (size_t i = 0; i < mFollow.size(); ++i)
        {
            const std::string strength(MWMechanics::attackFollowStrength(static_cast<float>(i) / 2));
            mFollow[i] = range(strength + " follow start", strength + " follow stop");
        }
        // Use the engine's first prefix match for getTextKeyTime. Reject
        // duplicate/inconsistent timing rather than silently mix two clips.
        auto keyTime = [&](std::string_view suffix) {
            const std::string name = mGroup + ": " + attack + ' ' + std::string(suffix);
            for (const auto& [time, key] : keys)
                if (key.starts_with(name)) return time;
            return -1.f;
        };
        mMinimumAttack = keyTime("min attack");
        mMinimumHit = keyTime("min hit");
        if (mMinimumAttack < mWindUp.mStart || mMinimumAttack >= mWindUp.mStop
            || mWindUp.mStop != mRelease.mStart || keyTime("max attack") != mRelease.mStart
            || keyTime("hit") != mRelease.mStop || mRelease.mStart >= mRelease.mStop)
            throw std::invalid_argument("Unsupported native melee timing layout");
        const std::string hitKey = mGroup + ": " + attack + " hit";
        bool hasHit = false;
        for (auto key = keys.lowerBound(mRelease.mStop); key != keys.end() && key->first == mRelease.mStop; ++key)
            hasHit |= key->second == hitKey;
        if (!hasHit) throw std::invalid_argument("Native melee release has no actionable hit key");
        mKeys = std::make_shared<const SceneUtil::TextKeyMap>(keys);
        mState.mTime = mWindUp.mStart;
    }

    float MeleeAnimation::windUp() const
    {
        return MWMechanics::attackWindUp(mState.mTime, mMinimumAttack, mWindUp.mStop);
    }

    void MeleeAnimation::restore(const Snapshot& state)
    {
        if (state.mPhase < Phase::WindUp || state.mPhase > Phase::Complete
            || !std::isfinite(state.mTime) || !std::isfinite(state.mStrength)
            || state.mStrength < 0 || state.mStrength > 1
            || (state.mReleased && state.mPhase == Phase::WindUp && state.mTime > mWindUp.mStop)
            || (!state.mReleased && (state.mPhase != Phase::WindUp || state.mHit || state.mStrength != 0))
            || (state.mHit && state.mPhase == Phase::WindUp)
            || (state.mPhase == Phase::WindUp && (state.mTime < mWindUp.mStart || state.mTime > mWindUp.mStop))
            || (state.mPhase == Phase::Release && (state.mTime < mRelease.mStart || state.mTime > mRelease.mStop))
            || (state.mPhase == Phase::Follow &&
                (state.mTime < mFollow[MWMechanics::attackFollowStrength(state.mStrength) == "small" ? 0
                    : MWMechanics::attackFollowStrength(state.mStrength) == "medium" ? 1 : 2].mStart
                 || state.mTime > mFollow[MWMechanics::attackFollowStrength(state.mStrength) == "small" ? 0
                    : MWMechanics::attackFollowStrength(state.mStrength) == "medium" ? 1 : 2].mStop))
            || ((state.mPhase == Phase::Follow || state.mPhase == Phase::Complete) && !state.mHit)
            || (state.mPhase != Phase::WindUp && !state.mReleased))
            throw std::invalid_argument("Invalid persisted native melee state");
        mState = state;
    }

    bool MeleeAnimation::release(float strength)
    {
        if (!std::isfinite(strength) || strength < 0 || strength > 1)
            throw std::invalid_argument("Invalid resolved native melee strength");
        if (mState.mReleased || mState.mPhase != Phase::WindUp) return false;
        mState.mReleased = true;
        mState.mStrength = strength;
        return true;
    }

    std::optional<int> MeleeAnimation::advance(float duration)
    {
        if (!std::isfinite(duration) || duration < 0 || duration > 1.f / 30)
            throw std::invalid_argument("Invalid native melee step duration");
        if (mState.mPhase == Phase::Complete) return {};
        const float previousTime = mState.mTime;
        bool includeStart = false;
        if (mState.mPhase == Phase::WindUp && mState.mReleased && mState.mTime >= mMinimumAttack)
        {
            const float startPoint = MWMechanics::attackReleaseStartPoint(mState.mStrength,
                mMinimumAttack, mWindUp.mStop, mMinimumHit, mRelease.mStop);
            mState.mTime = mRelease.mStart + (mRelease.mStop - mRelease.mStart) * startPoint;
            mState.mPhase = Phase::Release;
            includeStart = true; // Animation::play dispatches keys at the new start.
        }
        else if (mState.mPhase == Phase::Release && mState.mTime >= mRelease.mStop)
        {
            const auto strength = MWMechanics::attackFollowStrength(mState.mStrength);
            mState.mTime = mFollow[strength == "small" ? 0 : strength == "medium" ? 1 : 2].mStart;
            mState.mPhase = Phase::Follow;
        }
        const auto strength = MWMechanics::attackFollowStrength(mState.mStrength);
        const Range follow = mFollow[strength == "small" ? 0 : strength == "medium" ? 1 : 2];
        const bool followImmediately = includeStart && previousTime >= mWindUp.mStop
            && mState.mTime >= mRelease.mStop;
        const Range range = mState.mPhase == Phase::WindUp ? mWindUp
            : mState.mPhase == Phase::Release ? mRelease
            : follow;
        const float target = std::min(mState.mTime + duration * mSpeed, range.mStop);
        std::optional<int> hit;
        if (mState.mPhase == Phase::Release && !mState.mHit)
        {
            auto key = includeStart ? mKeys->lowerBound(mState.mTime) : mKeys->upperBound(mState.mTime);
            for (; key != mKeys->end() && key->first <= target; ++key)
            {
                const std::string_view text = key->second;
                if (!text.starts_with(mGroup) || text.substr(mGroup.size(), 2) != ": ") continue;
                const int type = MWMechanics::meleeHitType(mGroup, text.substr(mGroup.size() + 2));
                if (type == -1) continue;
                hit = type;
                mState.mHit = true;
                break;
            }
        }
        mState.mTime = target;
        // Stock play() dispatches an endpoint hit synchronously. If a held
        // release skips the entire pre-hit section, CharacterController starts
        // follow-through before runAnimation consumes this frame's duration.
        if (followImmediately)
        {
            mState.mPhase = Phase::Follow;
            mState.mTime = std::min(follow.mStart + duration * mSpeed, follow.mStop);
        }
        if (mState.mPhase == Phase::Follow && mState.mTime >= follow.mStop) mState.mPhase = Phase::Complete;
        return hit;
    }
}
