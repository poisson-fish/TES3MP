#ifndef TES3MP_NATIVE_ORDINARY_DOOR_HPP
#define TES3MP_NATIVE_ORDINARY_DOOR_HPP

#include <components/esm3/doorstate.hpp>
#include <components/esm3/loaddoor.hpp>

#include <functional>

namespace TES3MP::Native
{
    struct PreparedDoorChange
    {
        ESM::DoorState mState;
        // Fade the opposite sound over the stock 0.5 seconds after commit.
        ESM::RefId mPlaySound, mFadeSound, mStopSound;
        float mSoundOffset = 0;
    };

    // Preparation only: no live writer, network publication, file or singleton.
    // The eventual canonical transaction must commit position + motion together
    // with the inventory image before installing state or emitting these effects.
    class OrdinaryDoor
    {
        ESM::CellRef mPlacement;
        ESM::RefId mOpenSound, mCloseSound;

    public:
        OrdinaryDoor(const ESM::Door& base, const ESM::CellRef& placement);
        ESM::DoorState initialState() const;
        void validate(const ESM::DoorState& state) const;
        PreparedDoorChange activate(const ESM::DoorState& state) const;

        // Query actor contacts at the proposed transform without mutating live
        // physics. True cancels the whole step, preserving motion for retry.
        // Use MWWorld::doorContactBlocks for stock contact direction semantics.
        // An explicit query is required for every moving step; absence is never
        // treated as proof of a clear door. Idle doors are not advanced.
        using CollisionQuery = std::function<bool(const ESM::Position&, float delta)>;
        PreparedDoorChange advance(const ESM::DoorState& state, float seconds, const CollisionQuery& blocked) const;
        static constexpr float MaxStepSeconds = 1.f;
    };
}

#endif
