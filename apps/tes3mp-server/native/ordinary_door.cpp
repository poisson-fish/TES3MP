#include "ordinary_door.hpp"

#include <apps/openmw/mwworld/doormotion.hpp>

#include <cmath>
#include <stdexcept>
#include <tuple>

namespace TES3MP::Native
{
    namespace
    {
        void require(bool condition, const char* message)
        {
            if (!condition)
                throw std::invalid_argument(message);
        }

        auto fields(const ESM::CellRef& ref)
        {
            return std::tie(ref.mRefNum, ref.mRefID, ref.mScale, ref.mOwner, ref.mGlobalVariable, ref.mSoul,
                ref.mFaction, ref.mFactionRank, ref.mChargeInt, ref.mChargeIntRemainder, ref.mEnchantmentCharge,
                ref.mCount, ref.mTeleport, ref.mDoorDest, ref.mDestCell, ref.mLockLevel, ref.mIsLocked, ref.mKey,
                ref.mTrap, ref.mReferenceBlocked, ref.mPos);
        }

        void validText(std::string_view value)
        {
            require(value.size() <= 256 && value.find('\0') == std::string_view::npos,
                "Native door text outside bounds");
        }
    }

    OrdinaryDoor::OrdinaryDoor(const ESM::Door& base, const ESM::CellRef& placement)
    {
        require(base.mId == placement.mRefID && !base.mId.empty()
                && placement.mRefNum.isSet() && placement.mRefNum.hasContentFile(),
            "Native door requires a bound content placement");
        require(base.mScript.empty() && !placement.mTeleport && !placement.mIsLocked
                && placement.mLockLevel == 0 && placement.mKey.empty() && placement.mTrap.empty(),
            "Native door script, teleport, lock/key or trap services unavailable");
        require(placement.mCount == 1 && std::isfinite(placement.mScale)
                && placement.mScale >= .5f && placement.mScale <= 2.f
                && placement.mSoul.empty() && placement.mChargeInt == -1 && placement.mChargeIntRemainder == 0
                && placement.mEnchantmentCharge == -1 && placement.mDestCell.empty()
                && placement.mDoorDest == ESM::Position{},
            "Unsupported native ordinary door placement fields");
        for (const auto& id : {base.mId, base.mOpenSound, base.mCloseSound, placement.mOwner, placement.mFaction})
        {
            require(id.empty() || id.is<ESM::StringRefId>(), "Native door requires TES3 record IDs");
            if (!id.empty()) validText(id.getRefIdString());
        }
        validText(placement.mGlobalVariable);
        for (int i = 0; i < 3; ++i)
            require(std::isfinite(placement.mPos.pos[i])
                    && std::abs(double(placement.mPos.pos[i])) < double(INT64_MAX) / 1024
                    && std::isfinite(placement.mPos.rot[i]),
                "Native door transform outside bounds");
        const float end = MWWorld::doorMotion(MWWorld::DoorState::Opening,
            placement.mPos.rot[2], placement.mPos.rot[2], 1).mTargetAngle;
        require(std::isfinite(end) && end > placement.mPos.rot[2], "Native door rotation has insufficient precision");
        mPlacement = placement;
        mOpenSound = base.mOpenSound;
        mCloseSound = base.mCloseSound;
    }

    ESM::DoorState OrdinaryDoor::initialState() const
    {
        ESM::DoorState result;
        result.blank();
        result.mRef = mPlacement;
        result.mPosition = mPlacement.mPos;
        result.mEnabled = 1;
        result.mHasCustomState = false;
        return result;
    }

    void OrdinaryDoor::validate(const ESM::DoorState& state) const
    {
        require(fields(state.mRef) == fields(mPlacement), "Native door saved placement differs from bound content");
        require((state.mVersion == ESM::DefaultFormatVersion || state.mVersion == ESM::CurrentSaveGameFormatVersion)
                && !state.mActorIdConverter && state.mEnabled == 1 && state.mFlags == 0
                && !state.mHasLocals && state.mLocals.mVariables.empty() && state.mLuaScripts.mScripts.empty()
                && state.mAnimationState.empty() && state.mDoorState >= 0 && state.mDoorState <= 2
                && (state.mHasCustomState || state.mDoorState == 0),
            "Unsupported native door saved state");
        validatePosition(state.mPosition);
    }

    void OrdinaryDoor::validatePosition(const ESM::Position& position) const
    {
        const float closed = mPlacement.mPos.rot[2];
        const float opened = MWWorld::doorMotion(MWWorld::DoorState::Opening, closed, closed, 1).mTargetAngle;
        require(position.asVec3() == mPlacement.mPos.asVec3()
                && position.rot[0] == mPlacement.mPos.rot[0]
                && position.rot[1] == mPlacement.mPos.rot[1]
                && std::isfinite(position.rot[2]) && position.rot[2] >= closed
                && position.rot[2] <= opened,
            "Native door saved transform outside its authored swing");
    }

    PreparedDoorChange OrdinaryDoor::activate(const ESM::DoorState& state) const
    {
        validate(state);
        const auto movement = MWWorld::activatedDoorState(static_cast<MWWorld::DoorState>(state.mDoorState),
            mPlacement.mPos.rot[2], state.mPosition.rot[2]);
        PreparedDoorChange result{state};
        result.mState.mHasCustomState = true;
        result.mState.mDoorState = static_cast<int32_t>(movement);
        const bool opening = movement == MWWorld::DoorState::Opening;
        result.mPlaySound = opening ? mOpenSound : mCloseSound;
        result.mFadeSound = opening ? mCloseSound : mOpenSound;
        result.mSoundOffset = MWWorld::doorSoundOffset(movement, mPlacement.mPos.rot[2], state.mPosition.rot[2]);
        return result;
    }

    PreparedDoorChange OrdinaryDoor::advance(
        const ESM::DoorState& state, float seconds, const CollisionQuery& blocked) const
    {
        validate(state);
        require(std::isfinite(seconds) && seconds >= 0 && seconds <= MaxStepSeconds,
            "Native door step duration outside bounds");
        const auto movement = static_cast<MWWorld::DoorState>(state.mDoorState);
        if (movement == MWWorld::DoorState::Idle)
            return {state};
        require(bool(blocked), "Native door motion requires an actor collision query");
        const auto motion = MWWorld::doorMotion(movement, mPlacement.mPos.rot[2], state.mPosition.rot[2], seconds);
        auto position = state.mPosition;
        position.rot[2] = motion.mTargetAngle;
        const bool collision = blocked(position, motion.mDelta);
        PreparedDoorChange result{state};
        if (collision)
            result.mStopSound = movement == MWWorld::DoorState::Opening ? mOpenSound : mCloseSound;
        else
        {
            result.mState.mPosition = position;
            if (motion.mReached)
                result.mState.mDoorState = static_cast<int32_t>(MWWorld::DoorState::Idle);
        }
        return result;
    }
}
